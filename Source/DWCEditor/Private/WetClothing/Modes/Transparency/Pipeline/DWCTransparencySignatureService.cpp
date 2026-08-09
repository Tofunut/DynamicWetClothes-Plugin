//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencySignatureService.h"

#include "DataAssets/WetClothingAsset.h"
#include "DataAssets/DWCBakeLayer.h"
#include "WetClothing/Modes/Transparency/Providers/DWCTransparencyProjectionSourceProvider.h"
#include "DataAssets/WetClothingTransparencyData.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Misc/SecureHash.h"

namespace
{
    FString HashCanonical(const FString& Canonical)
    {
        return FMD5::HashAnsiString(*Canonical);
    }

    FString ParameterKey(const FMaterialParameterInfo& Info)
    {
        return FString::Printf(
            TEXT("%d:%d:%s"),
            static_cast<int32>(Info.Association),
            Info.Index,
            *Info.Name.ToString());
    }

    void AppendMaterialParameters(const UMaterialInterface* Material, FString& InOutCanonical)
    {
        if (Material == nullptr)
        {
            return;
        }

        InOutCanonical += FString::Printf(
            TEXT("|LightingGuid=%s|BaseState=%s"),
            *Material->GetLightingGuid().ToString(EGuidFormats::Digits),
            Material->GetMaterial() != nullptr
                ? *Material->GetMaterial()->StateId.ToString(EGuidFormats::Digits)
                : TEXT("None"));

        TArray<FMaterialParameterInfo> Infos;
        TArray<FGuid> Ids;
        Material->GetAllScalarParameterInfo(Infos, Ids);
        Infos.Sort([](const FMaterialParameterInfo& A, const FMaterialParameterInfo& B)
        {
            return ParameterKey(A) < ParameterKey(B);
        });
        for (const FMaterialParameterInfo& Info : Infos)
        {
            float Value = 0.0f;
            if (Material->GetScalarParameterValue(Info, Value))
            {
                InOutCanonical += FString::Printf(TEXT("|S=%s,%.9g"), *ParameterKey(Info), Value);
            }
        }

        Infos.Reset();
        Ids.Reset();
        Material->GetAllVectorParameterInfo(Infos, Ids);
        Infos.Sort([](const FMaterialParameterInfo& A, const FMaterialParameterInfo& B)
        {
            return ParameterKey(A) < ParameterKey(B);
        });
        for (const FMaterialParameterInfo& Info : Infos)
        {
            FLinearColor Value = FLinearColor::Black;
            if (Material->GetVectorParameterValue(Info, Value))
            {
                InOutCanonical += FString::Printf(
                    TEXT("|V=%s,%.9g,%.9g,%.9g,%.9g"),
                    *ParameterKey(Info), Value.R, Value.G, Value.B, Value.A);
            }
        }

        Infos.Reset();
        Ids.Reset();
        Material->GetAllTextureParameterInfo(Infos, Ids);
        Infos.Sort([](const FMaterialParameterInfo& A, const FMaterialParameterInfo& B)
        {
            return ParameterKey(A) < ParameterKey(B);
        });
        for (const FMaterialParameterInfo& Info : Infos)
        {
            UTexture* Value = nullptr;
            if (Material->GetTextureParameterValue(Info, Value))
            {
                InOutCanonical += FString::Printf(
                    TEXT("|T=%s,%s"), *ParameterKey(Info), *GetPathNameSafe(Value));
                if (const UTexture2D* Texture2D = Cast<UTexture2D>(Value))
                {
                    InOutCanonical += FString::Printf(
                        TEXT(",%s"), *Texture2D->Source.GetId().ToString(EGuidFormats::Digits));
                }
            }
        }

        TArray<UTexture*> UsedTextures;
        Material->GetUsedTextures(UsedTextures);
        UsedTextures.Sort([](const UTexture& A, const UTexture& B)
        {
            return A.GetPathName() < B.GetPathName();
        });
        for (const UTexture* Texture : UsedTextures)
        {
            InOutCanonical += FString::Printf(TEXT("|UsedTexture=%s"), *GetPathNameSafe(Texture));
            if (const UTexture2D* Texture2D = Cast<UTexture2D>(Texture))
            {
                InOutCanonical += FString::Printf(
                    TEXT(",%s"), *Texture2D->Source.GetId().ToString(EGuidFormats::Digits));
            }
        }
    }

    void AppendRevealStrokes(
        const FWetClothingTransparencyLayerData& Layer,
        FString& InOutCanonical)
    {
        for (const FDWCTransparencyRevealColorStroke& Stroke : Layer.RevealColorPaintStrokes)
        {
            InOutCanonical += FString::Printf(
                TEXT("|RevealStroke=%s,%d,%d,%d,%d,%.9g,%.9g,%.9g,%.9g,%.9g,%d"),
                *Stroke.StrokeGuid.ToString(EGuidFormats::Digits), Stroke.bEnabled ? 1 : 0,
                Stroke.MaterialSlotIndex, static_cast<int32>(Stroke.UVAddressMode),
                static_cast<int32>(Stroke.BrushMode), Stroke.PaintColor.R,
                Stroke.PaintColor.G, Stroke.PaintColor.B, Stroke.Falloff,
                Stroke.Spacing, Stroke.Samples.Num());
            for (const FDWCTransparencyBrushSample& Sample : Stroke.Samples)
            {
                InOutCanonical += FString::Printf(
                    TEXT(";%.9g,%.9g,%.9g,%.9g,%d"), Sample.PositionUV.X,
                    Sample.PositionUV.Y, Sample.RadiusUV, Sample.Strength,
                    Sample.UVIslandID);
            }
        }
    }

    void AppendAlphaStrokes(
        const FWetClothingTransparencyLayerData& Layer,
        FString& InOutCanonical)
    {
        for (const FDWCTransparencyBrushStroke& Stroke : Layer.EditableStrokes)
        {
            InOutCanonical += FString::Printf(
                TEXT("|AlphaStroke=%s,%d,%d,%d,%d,%.9g,%.9g,%.9g,%d"),
                *Stroke.StrokeGuid.ToString(EGuidFormats::Digits), Stroke.bEnabled ? 1 : 0,
                Stroke.MaterialSlotIndex, static_cast<int32>(Stroke.UVAddressMode),
                static_cast<int32>(Stroke.BrushMode), Stroke.Falloff,
                Stroke.TargetAlpha, Stroke.Spacing, Stroke.Samples.Num());
            for (const FDWCTransparencyBrushSample& Sample : Stroke.Samples)
            {
                InOutCanonical += FString::Printf(
                    TEXT(";%.9g,%.9g,%.9g,%.9g,%d"), Sample.PositionUV.X,
                    Sample.PositionUV.Y, Sample.RadiusUV, Sample.Strength,
                    Sample.UVIslandID);
            }
        }
    }
}

FString FDWCTransparencySignatureService::BuildMaterialBakeSignature(
    const UMaterialInterface* Material,
    const int32 SourceUVChannel,
    const int32 Resolution)
{
    FString Canonical = FString::Printf(
        TEXT("DWC.Transparency.MaterialBake.v1|Material=%s|UV=%d|Resolution=%d"),
        *GetPathNameSafe(Material), SourceUVChannel, Resolution);
    AppendMaterialParameters(Material, Canonical);
    return HashCanonical(Canonical);
}

bool FDWCTransparencySignatureService::BuildSourceSignature(
    const UWetClothingAsset& Asset,
    const FWetClothingTransparencyLayerData& Layer,
    FString& OutSignature,
    FString& OutMaterialBakeSignature,
    FString& OutError)
{
    OutSignature.Reset();
    OutMaterialBakeSignature.Reset();
    OutError.Reset();

    const USkeletalMesh* Mesh = Asset.GetRuntimeSkeletalMesh();
    const USkeletalMesh* SourceMesh = Asset.GetSourceSkeletalMesh();
    constexpr int32 LODIndex = 0;
    const int32 DataUV = Asset.GetDWCDataUVChannelIndex();
    const FDWCDataUVLODMetadata* DataUVMetadata = Asset.FindDataUVMetadataForLOD(LODIndex);
    if (Mesh == nullptr || SourceMesh == nullptr || DataUV == INDEX_NONE || DataUVMetadata == nullptr ||
        DataUVMetadata->DataUVOutputSignature.IsEmpty())
    {
        OutError = TEXT("Transparency source signature requires a runtime mesh and valid LOD 0 DWC Data UV metadata.");
        return false;
    }
    if (!Mesh->GetMaterials().IsValidIndex(Layer.TargetSurface.OuterMaterialSlotIndex))
    {
        OutError = TEXT("Transparency source signature references an invalid target material slot.");
        return false;
    }

    const int32 Resolution = FMath::Clamp(
        Asset.Authored.TransparencyData.TransparencyBakeResolution, 16, 4096);
    FString Canonical = FString::Printf(
        TEXT("DWC.Transparency.Source.v1|Mesh=%s|Layer=%s|Type=%d|Slot=%d|UV=%d|LOD=0|Resolution=%d|Address=%d|DataUV=%s"),
        *GetPathNameSafe(Mesh), *Layer.LayerGuid.ToString(EGuidFormats::DigitsWithHyphens),
        static_cast<int32>(Layer.SourceType), Layer.TargetSurface.OuterMaterialSlotIndex,
        DataUV, Resolution, static_cast<int32>(Layer.TargetSurface.UVAddressMode),
        *DataUVMetadata->DataUVOutputSignature);

    if (Layer.SourceType == EDWCTransparencySourceType::ManualColorOrTexture)
    {
        const FLinearColor& Color = Layer.ManualColorSource.BaseRevealColor;
        Canonical += FString::Printf(
            TEXT("|Manual=%d,%.9g,%.9g,%.9g,%.9g,%s,%d,%d,%d"),
            static_cast<int32>(Layer.ManualColorSource.SourceMode),
            Color.R, Color.G, Color.B,
            Layer.ManualColorSource.InitialTransparencyAlpha,
            *Layer.ManualColorSource.SampledColorTexture.ToSoftObjectPath().ToString(),
            Layer.ManualColorSource.SampledMaterialSlotIndex,
            Layer.ManualColorSource.SampledUVChannelIndex,
            Layer.ManualColorSource.SampledUVIslandID);
        if (Layer.ManualColorSource.SourceMode ==
                EDWCTransparencyManualRevealSourceMode::UVIslandAverage)
        {
            const UTexture2D* SampledTexture =
                Layer.ManualColorSource.SampledColorTexture.LoadSynchronous();
            Canonical += FString::Printf(
                TEXT("|SampledTextureSource=%s"),
                SampledTexture != nullptr
                    ? *SampledTexture->Source.GetId().ToString()
                    : TEXT("Missing"));
        }
    }
    else if (Layer.SourceType == EDWCTransparencySourceType::SameMeshMaterialSlots)
    {
        Canonical += FString::Printf(
            TEXT("|Ray=%.9g,%.9g,%.9g,%.9g,%.9g"), Layer.RaySettings.RayStartOffset,
            Layer.RaySettings.MinHitDistance, Layer.RaySettings.FullTransparencyDistance,
            Layer.RaySettings.NoTransparencyDistance, Layer.RaySettings.MaxRayDistance);
        for (int32 Priority = 0; Priority < Layer.SameMeshSource.InnerSlotPriority.Num(); ++Priority)
        {
            const FWetClothingTransparencyInnerSlot& Inner =
                Layer.SameMeshSource.InnerSlotPriority[Priority];
            UMaterialInterface* Material = SourceMesh->GetMaterials().IsValidIndex(Inner.MaterialSlotIndex)
                ? SourceMesh->GetMaterials()[Inner.MaterialSlotIndex].MaterialInterface
                : nullptr;
            const FString MaterialSignature = BuildMaterialBakeSignature(
                Material, Inner.SourceUVChannel, Resolution);
            OutMaterialBakeSignature += MaterialSignature;
            Canonical += FString::Printf(
                TEXT("|Inner=%d,%d,%d,%s,%s,%s"), Priority, Inner.MaterialSlotIndex,
                Inner.SourceUVChannel, *Inner.MaterialSlotName.ToString(),
                *GetPathNameSafe(SourceMesh), *MaterialSignature);
        }
        OutMaterialBakeSignature = HashCanonical(OutMaterialBakeSignature);
    }
    else if (Layer.SourceType == EDWCTransparencySourceType::OtherSkeletalMeshComponents)
    {
        if (Asset.Authored.TransparencyData.SourceBlueprintClass.IsNull())
        {
            OutError = TEXT("Transparency source signature requires a Source Blueprint.");
            return false;
        }
        FDWCTransparencyProjectionSourceSet BlueprintSources;
        FString SourceError;
        if (!FDWCTransparencyProjectionSourceProvider::BuildBlueprintSources(
                Asset,
                Layer,
                BlueprintSources,
                SourceError))
        {
            OutError = MoveTemp(SourceError);
            return false;
        }
        Canonical += FString::Printf(
            TEXT("|Blueprint=%s|Snapshot=%s"),
            *Asset.Authored.TransparencyData.SourceBlueprintClass.ToSoftObjectPath().ToString(),
            *BlueprintSources.ProviderSignature);
        FString MaterialCanonical;
        for (const FDWCTransparencyProjectionSource& Source : BlueprintSources.Sources)
        {
            MaterialCanonical += BuildMaterialBakeSignature(
                Source.EffectiveMaterial,
                Source.Layer.SourceUVChannel,
                Resolution);
        }
        OutMaterialBakeSignature = HashCanonical(MaterialCanonical);
    }
    else if (Layer.SourceType == EDWCTransparencySourceType::ExternalSkeletalMesh)
    {
        const USkeletalMesh* ExternalMesh = Layer.ExternalMeshSource.SkeletalMesh;
        if (ExternalMesh == nullptr)
        {
            OutError = TEXT("Transparency source signature requires an External Skeletal Mesh.");
            return false;
        }
        Canonical += FString::Printf(
            TEXT("|ExternalMesh=%s|Transform=%s"), *GetPathNameSafe(ExternalMesh),
            *Layer.ExternalMeshSource.BakeTransform.ToHumanReadableString());
        const TArray<FWetClothingTransparencyInnerSlot>& Slots =
            Layer.ExternalMeshSource.SourceSlotPriority;
        if (Slots.IsEmpty())
        {
            for (int32 SlotIndex = 0; SlotIndex < ExternalMesh->GetMaterials().Num(); ++SlotIndex)
            {
                const UMaterialInterface* Material =
                    ExternalMesh->GetMaterials()[SlotIndex].MaterialInterface;
                const FString MaterialSignature = BuildMaterialBakeSignature(Material, 0, Resolution);
                Canonical += FString::Printf(TEXT("|ExternalSlot=%d,0,%s"), SlotIndex, *MaterialSignature);
                OutMaterialBakeSignature += MaterialSignature;
            }
        }
        else
        {
            for (int32 Priority = 0; Priority < Slots.Num(); ++Priority)
            {
                const FWetClothingTransparencyInnerSlot& Slot = Slots[Priority];
                const UMaterialInterface* Material = ExternalMesh->GetMaterials().IsValidIndex(Slot.MaterialSlotIndex)
                    ? ExternalMesh->GetMaterials()[Slot.MaterialSlotIndex].MaterialInterface
                    : nullptr;
                const FString MaterialSignature = BuildMaterialBakeSignature(
                    Material, Slot.SourceUVChannel, Resolution);
                Canonical += FString::Printf(
                    TEXT("|ExternalSlot=%d,%d,%d,%s"), Priority, Slot.MaterialSlotIndex,
                    Slot.SourceUVChannel, *MaterialSignature);
                OutMaterialBakeSignature += MaterialSignature;
            }
        }
        OutMaterialBakeSignature = HashCanonical(OutMaterialBakeSignature);
    }
    else
    {
        OutError = TEXT("Unsupported transparency source type.");
        return false;
    }

    OutSignature = HashCanonical(Canonical);
    return true;
}

FString FDWCTransparencySignatureService::BuildRevealSignature(
    const FString& SourceSignature,
    const FWetClothingTransparencyLayerData& Layer)
{
    FString Canonical = FString::Printf(
        TEXT("DWC.Transparency.Reveal.v1|Source=%s"), *SourceSignature);
    AppendRevealStrokes(Layer, Canonical);
    return HashCanonical(Canonical);
}

FString FDWCTransparencySignatureService::BuildAlphaAuthoringSignature(
    const FWetClothingTransparencyLayerData& Layer)
{
    FString Canonical(TEXT("DWC.Transparency.AlphaAuthoring.v1"));
    AppendAlphaStrokes(Layer, Canonical);
    return HashCanonical(Canonical);
}

FString FDWCTransparencySignatureService::BuildSuppressionSettingsSignature(
    const float CoverageThreshold,
    const float MaskSoftness,
    const float SuppressionStrength,
    const float TransparencyStrength)
{
    const FString Canonical = FString::Printf(
        TEXT("DWCTransparencySuppression_v2.DirectMask|threshold=%.9g|softness=%.9g|suppression=%.9g|transparency=%.9g"),
        CoverageThreshold,
        MaskSoftness,
        SuppressionStrength,
        TransparencyStrength);
    return FMD5::HashAnsiString(*Canonical);
}

FString FDWCTransparencySignatureService::BuildFinalSignature(
    const FDWCTransparencyFinalSignatureInputs& Inputs)
{
    const FString Canonical = FString::Printf(
        TEXT("DWC.Transparency.Final.v2|Reveal=%s|Alpha=%s|WrinkleMask=%s|Suppression=%s|Padding=%d|EdgeFeather=%.9g"),
        *Inputs.RevealSignature,
        *Inputs.AlphaAuthoringSignature,
        *Inputs.WrinkleMaskBuildSignature,
        *Inputs.SuppressionSettingsSignature,
        Inputs.PaddingPixels,
        Inputs.EdgeFeatherPixels);
    return HashCanonical(Canonical);
}

FString FDWCTransparencySignatureService::BuildFinalSignature(
    const FString& RevealSignature,
    const FWetClothingTransparencyLayerData& Layer,
    const FString& WrinkleMaskBuildSignature,
    const FString& SuppressionSettingsSignature,
    const int32 PaddingPixels,
    const float EdgeFeatherPixels)
{
    FDWCTransparencyFinalSignatureInputs Inputs;
    Inputs.RevealSignature = RevealSignature;
    Inputs.AlphaAuthoringSignature = BuildAlphaAuthoringSignature(Layer);
    Inputs.WrinkleMaskBuildSignature = WrinkleMaskBuildSignature;
    Inputs.SuppressionSettingsSignature = SuppressionSettingsSignature;
    Inputs.PaddingPixels = PaddingPixels;
    Inputs.EdgeFeatherPixels = EdgeFeatherPixels;
    return BuildFinalSignature(Inputs);
}

FDWCTransparencyStageStatus FDWCTransparencySignatureService::EvaluateEditorStageCache(
    const FWetClothingTransparencyLayerData& Layer,
    const FString& ExpectedSourceSignature,
    const FString& ExpectedRevealSignature)
{
    FDWCTransparencyStageStatus Status;
#if WITH_EDITORONLY_DATA
    const FDWCTransparencyEditorStageCacheMetadata& Cache = Layer.EditorStageCache;
    Status.Stage = EDWCTransparencyStage::Source;
    if (!Cache.bSourceGenerated || Cache.SourceSignature.IsEmpty())
    {
        Status.Reason = EDWCTransparencyStaleReason::MissingArtifact;
        Status.Detail = TEXT("The Stage 2 source checkpoint has not been generated.");
        return Status;
    }
    if (Cache.SourceSignature != ExpectedSourceSignature)
    {
        Status.Reason = EDWCTransparencyStaleReason::SourceInputsChanged;
        Status.Detail = TEXT("The Stage 2 source inputs changed.");
        return Status;
    }
    const bool bHasCurrentBaseReveal = Cache.Artifacts.ContainsByPredicate(
        [&ExpectedSourceSignature](const FDWCTransparencyTempArtifactReference& Artifact)
        {
            return Artifact.Kind == EDWCTransparencyTempArtifactKind::BaseRevealColor &&
                !Artifact.bObsolete && Artifact.BuildSignature == ExpectedSourceSignature &&
                !Artifact.Texture.IsNull();
        });
    if (!bHasCurrentBaseReveal)
    {
        Status.Reason = EDWCTransparencyStaleReason::MissingArtifact;
        Status.Detail = TEXT("The Stage 2 Base Reveal Color Temp artifact is missing.");
        return Status;
    }
    Status.Stage = EDWCTransparencyStage::Reveal;
    if (!ExpectedRevealSignature.IsEmpty() && Cache.RevealSignature != ExpectedRevealSignature)
    {
        Status.Reason = EDWCTransparencyStaleReason::RevealEditsChanged;
        Status.Detail = TEXT("The Stage 3 reveal-color edits changed.");
        return Status;
    }
#else
    Status.Reason = EDWCTransparencyStaleReason::MissingArtifact;
    Status.Detail = TEXT("Transparency stage cache metadata is editor-only.");
#endif
    return Status;
}
