// Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Foundation/Validation/DWCTransparencyCookValidationEvaluator.h"

#include "DataAssets/WetClothingAsset.h"
#include "DataAssets/WetClothingTransparencyData.h"
#include "Engine/Texture2D.h"
#include "WetClothing/Foundation/Validation/DWCEditorValidationEvaluatorUtils.h"
#include "WetClothing/Modes/Transparency/Temp/DWCTransparencyIntermediateAssetPolicy.h"

namespace
{
void AddCookDiagnostic(
    FWCAEditorValidationSnapshot& Snapshot,
    const FDWCEditorValidationTargetKey& Key,
    const FName Code,
    const FString& Detail,
    const bool bFinalRuntimeOutput)
{
    FDWCEditorValidationNode& Node =
        DWCEditorValidation::FindOrAddNode(Snapshot, Key);
    Node.Intent = EDWCEditorValidationIntentState::Enabled;
    Node.Artifact = bFinalRuntimeOutput
        ? EDWCEditorValidationArtifactState::Invalid
        : EDWCEditorValidationArtifactState::Stale;
    DWCEditorValidation::AddDiagnostic(
        Snapshot,
        Node,
        Code,
        bFinalRuntimeOutput
            ? EDWCEditorValidationSeverity::Error
            : EDWCEditorValidationSeverity::Warning,
        bFinalRuntimeOutput
            ? NSLOCTEXT("DWCTransparencyCookValidation", "FinalTitle", "Transparency Runtime Texture")
            : NSLOCTEXT("DWCTransparencyCookValidation", "IntermediateTitle", "Transparency Intermediate Asset"),
        bFinalRuntimeOutput
            ? NSLOCTEXT("DWCTransparencyCookValidation", "Invalid", "Invalid")
            : NSLOCTEXT("DWCTransparencyCookValidation", "EditorOnlyRequired", "Editor Only Required"),
        FText::FromString(Detail),
        bFinalRuntimeOutput
            ? NSLOCTEXT("DWCTransparencyCookValidation", "FinalAction", "Bake final Transparency Textures outside the Temp directory.")
            : NSLOCTEXT("DWCTransparencyCookValidation", "IntermediateAction", "Rebuild or save the affected Transparency stage artifact as Editor Only."),
        bFinalRuntimeOutput
            ? EDWCEditorValidationRemediation::BuildAction
            : EDWCEditorValidationRemediation::Manual,
        bFinalRuntimeOutput
            ? TOptional<EDWCEditorBuildAction>(EDWCEditorBuildAction::BakeTransparencyTextures)
            : TOptional<EDWCEditorBuildAction>(),
        bFinalRuntimeOutput);
}

void CheckIntermediateReference(
    FWCAEditorValidationSnapshot& Snapshot,
    const FDWCEditorValidationTargetKey& Key,
    const FName Code,
    const FSoftObjectPath& Path,
    const FString& Label)
{
    FString Reason;
    if (!FDWCTransparencyIntermediateAssetPolicy::IsReferenceCookExcluded(Path, &Reason))
    {
        AddCookDiagnostic(
            Snapshot,
            Key,
            Code,
            FString::Printf(TEXT("%s: %s"), *Label, *Reason),
            false);
    }
}

void CheckFinalTexture(
    FWCAEditorValidationSnapshot& Snapshot,
    const FDWCEditorValidationTargetKey& Key,
    UTexture2D* Texture,
    const TCHAR* Label,
    const FName Code)
{
    if (Texture == nullptr)
    {
        return;
    }
    const FSoftObjectPath Path(Texture);
    FString Detail;
    if (FDWCTransparencyIntermediateAssetPolicy::IsIntermediatePackagePath(
            Path.GetLongPackageName()))
    {
        Detail = FString::Printf(
            TEXT("Slot %d final %s is stored under the Temp directory: %s"),
            Key.MaterialSlotIndex,
            Label,
            *Path.ToString());
    }
    else if (FDWCTransparencyIntermediateAssetPolicy::HasEditorOnlyPackageFlag(Path))
    {
        Detail = FString::Printf(
            TEXT("Slot %d final %s is marked Editor Only: %s"),
            Key.MaterialSlotIndex,
            Label,
            *Path.ToString());
    }
    if (!Detail.IsEmpty())
    {
        AddCookDiagnostic(Snapshot, Key, Code, Detail, true);
    }
}
}

void FDWCTransparencyCookValidationEvaluator::AppendToSnapshot(
    const UWetClothingAsset& Asset,
    FWCAEditorValidationSnapshot& InOutSnapshot)
{
#if WITH_EDITORONLY_DATA
    for (const FDWCTransparencyMaterialColorCacheReference& Reference :
         Asset.Authored.TransparencyData.MaterialColorCache)
    {
        const FDWCEditorValidationTargetKey Key{
            EDWCEditorValidationDomain::Transparency,
            Reference.MaterialSlotIndex,
            FGuid(),
            TEXT("MaterialSurfaceCache")};
        CheckIntermediateReference(
            InOutSnapshot,
            Key,
            TEXT("Transparency.Cook.MaterialColorCache"),
            Reference.Texture.ToSoftObjectPath(),
            FString::Printf(TEXT("Material surface Base Color cache slot %d"), Reference.MaterialSlotIndex));
        if (!Reference.NormalTexture.IsNull())
        {
            CheckIntermediateReference(
                InOutSnapshot,
                Key,
                TEXT("Transparency.Cook.MaterialNormalCache"),
                Reference.NormalTexture.ToSoftObjectPath(),
                FString::Printf(TEXT("Material surface Normal cache slot %d"), Reference.MaterialSlotIndex));
        }
        if (!Reference.MetallicTexture.IsNull())
        {
            CheckIntermediateReference(
                InOutSnapshot,
                Key,
                TEXT("Transparency.Cook.MaterialMetallicCache"),
                Reference.MetallicTexture.ToSoftObjectPath(),
                FString::Printf(TEXT("Material surface Metallic cache slot %d"), Reference.MaterialSlotIndex));
        }
    }

    for (const FWetClothingTransparencyLayerData& Layer :
         Asset.Authored.TransparencyData.TransparencyLayers)
    {
        const FDWCEditorValidationTargetKey LayerKey{
            EDWCEditorValidationDomain::Transparency,
            Layer.TargetSurface.OuterMaterialSlotIndex,
            Layer.LayerGuid};
        for (const FDWCTransparencyTempArtifactReference& Reference :
             Layer.EditorStageCache.Artifacts)
        {
            CheckIntermediateReference(
                InOutSnapshot,
                LayerKey,
                FName(*FString::Printf(
                    TEXT("Transparency.Cook.StageArtifact.%d"),
                    static_cast<int32>(Reference.Kind))),
                Reference.Texture.ToSoftObjectPath(),
                FString::Printf(
                    TEXT("Layer %s artifact %d"),
                    *Layer.LayerGuid.ToString(EGuidFormats::Digits).Left(8),
                    static_cast<int32>(Reference.Kind)));
        }

        for (const FWetClothingBakedTransparencyMap& BakedMap : Layer.BakedMaps)
        {
            FDWCEditorValidationTargetKey BakedKey = LayerKey;
            BakedKey.MaterialSlotIndex = BakedMap.MaterialSlotIndex;
            CheckFinalTexture(
                InOutSnapshot,
                BakedKey,
                BakedMap.TransparencyMap.Get(),
                TEXT("Transparency Map"),
                TEXT("Transparency.Cook.FinalTransparencyMap"));
            CheckFinalTexture(
                InOutSnapshot,
                BakedKey,
                BakedMap.RevealNormalMap.Get(),
                TEXT("Reveal Normal Map"),
                TEXT("Transparency.Cook.FinalRevealNormalMap"));

            if (BakedMap.HasAnyLegacyRevealSurfaceData())
            {
                AddCookDiagnostic(
                    InOutSnapshot,
                    BakedKey,
                    TEXT("Transparency.LegacyRevealSurfacePayload"),
                    FString::Printf(
                        TEXT("Slot %d contains deprecated packed Reveal Surface runtime data."),
                        BakedMap.MaterialSlotIndex),
                    true);
            }
            if (Layer.RequiresRuntimeRevealNormal() &&
                !BakedMap.HasRuntimeRevealNormalPayload())
            {
                AddCookDiagnostic(
                    InOutSnapshot,
                    BakedKey,
                    TEXT("Transparency.RuntimeRevealNormalMissing"),
                    FString::Printf(
                        TEXT("Slot %d uses a raycast Transparency source but has no coverage-weighted runtime Reveal Normal Map."),
                        BakedMap.MaterialSlotIndex),
                    true);
            }
            else if (!Layer.RequiresRevealSurface() &&
                     (BakedMap.RevealNormalMap != nullptr ||
                      !BakedMap.RevealNormalBuildSignature.IsEmpty() ||
                      BakedMap.bSourceCoverageBakedIntoRevealNormal) &&
                     !BakedMap.HasRuntimeRevealNormalPayload())
            {
                AddCookDiagnostic(
                    InOutSnapshot,
                    BakedKey,
                    TEXT("Transparency.OptionalRevealNormalMetadataInvalid"),
                    FString::Printf(
                        TEXT("Slot %d has inconsistent optional Reveal Normal metadata."),
                        BakedMap.MaterialSlotIndex),
                    true);
            }
        }
    }
#endif
}
