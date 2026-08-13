// Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Foundation/Build/DWCTransparencyBuildTargetResolver.h"

#include "DataAssets/WetClothingAsset.h"
#include "Engine/SkeletalMesh.h"
#include "WetClothing/DerivedAssets/Textures/Transparency/DWCTransparencyEditedMapBaker.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyAffectedStage4Rebake.h"

bool FDWCTransparencyBuildTargetSnapshot::HasEnabledLayers() const
{
    return Targets.ContainsByPredicate(
        [](const FDWCTransparencyBuildTarget& Target) { return Target.IsEnabled(); });
}

const FDWCTransparencyBuildTarget* FDWCTransparencyBuildTargetSnapshot::FindByLayerGuid(
    const FGuid& LayerGuid) const
{
    return Targets.FindByPredicate(
        [&LayerGuid](const FDWCTransparencyBuildTarget& Target)
        {
            return Target.LayerGuid == LayerGuid;
        });
}

void FDWCTransparencyBuildTargetSnapshot::CollectLayerGuids(
    const EDWCTransparencyBuildRequirement Requirement,
    TArray<FGuid>& OutLayerGuids) const
{
    OutLayerGuids.Reset();
    for (const FDWCTransparencyBuildTarget& Target : Targets)
    {
        if (Target.Requirement == Requirement && Target.IsBuildable())
        {
            OutLayerGuids.AddUnique(Target.LayerGuid);
        }
    }
}

FDWCTransparencyBuildTargetSnapshot FDWCTransparencyBuildTargetResolver::Resolve(
    const UWetClothingAsset& Asset,
    const EDWCEditorValidationAccess Access)
{
    const bool bExactPayload = Access == EDWCEditorValidationAccess::ExactPayload;
    FDWCTransparencyBuildTargetSnapshot Result;
    Result.RecordedStatus = Asset.GetBakeOutputStatus(DWCBakeOutput::TransparencyMaps);
    Result.FailureMessage = Asset.GetBakeOutputFailureMessage(DWCBakeOutput::TransparencyMaps);
    Result.bSavePending = Asset.IsBakeOutputSavePending(DWCBakeOutput::TransparencyMaps);
    const USkeletalMesh* Mesh = Asset.GetDWCSkeletalMesh();
    const int32 SlotCount = Mesh != nullptr ? Mesh->GetMaterials().Num() : 0;

    TMap<int32, TArray<const FWetClothingTransparencyLayerData*>> LayersBySlot;
    for (const FWetClothingTransparencyLayerData& Layer :
         Asset.Authored.TransparencyData.TransparencyLayers)
    {
        LayersBySlot.FindOrAdd(Layer.TargetSurface.OuterMaterialSlotIndex).Add(&Layer);
    }

    TMap<FGuid, FDWCTransparencyAffectedRebakeCandidate> AffectedByLayer;
    if (bExactPayload)
    {
        TArray<FDWCTransparencyAffectedRebakeCandidate> Candidates;
        FDWCTransparencyAffectedStage4Rebake::CollectCandidates(
            Asset, TConstArrayView<int32>(), Candidates);
        for (FDWCTransparencyAffectedRebakeCandidate& Candidate : Candidates)
        {
            AffectedByLayer.Add(Candidate.LayerGuid, MoveTemp(Candidate));
        }
    }

    TSet<const FWetClothingTransparencyLayerData*> Evaluated;
    for (int32 Slot = 0; Slot < SlotCount; ++Slot)
    {
        if (!Asset.IsMaterialSlotWettable(Slot))
        {
            continue;
        }

        const TArray<const FWetClothingTransparencyLayerData*>* SlotLayers = LayersBySlot.Find(Slot);
        if (SlotLayers == nullptr || SlotLayers->IsEmpty())
        {
            FDWCTransparencyBuildTarget& Target = Result.Targets.AddDefaulted_GetRef();
            Target.MaterialSlotIndex = Slot;
            Target.Detail = TEXT("Transparency is not configured for this Wettable slot.");
            continue;
        }

        if (SlotLayers->Num() > 1)
        {
            for (const FWetClothingTransparencyLayerData* Layer : *SlotLayers)
            {
                Evaluated.Add(Layer);
                FDWCTransparencyBuildTarget& Target = Result.Targets.AddDefaulted_GetRef();
                Target.MaterialSlotIndex = Slot;
                Target.LayerGuid = Layer->LayerGuid;
                Target.Intent = Layer->Intent;
                Target.bWettable = true;
                Target.Requirement = EDWCTransparencyBuildRequirement::ManualRepair;
                Target.Detail = TEXT("More than one Transparency Target Part uses this material slot.");
            }
            continue;
        }

        const FWetClothingTransparencyLayerData& Layer = *(*SlotLayers)[0];
        Evaluated.Add(&Layer);
        FDWCTransparencyBuildTarget& Target = Result.Targets.AddDefaulted_GetRef();
        Target.MaterialSlotIndex = Slot;
        Target.LayerGuid = Layer.LayerGuid;
        Target.Intent = Layer.Intent;
        Target.bWettable = true;
        if (Layer.Intent != EDWCTransparencyLayerIntent::Enabled)
        {
            Target.Detail = Layer.Intent == EDWCTransparencyLayerIntent::Draft
                ? TEXT("The Transparency Target Part is still a draft.")
                : TEXT("The Transparency Target Part is disabled.");
            continue;
        }

        TArray<FString> Errors;
        Target.bInputValid = FWetClothingTransparencyDataHelpers::ValidateTransparencyLayer(
            Mesh,
            Layer,
            Errors,
            Asset.GetDWCDataUVChannelIndex(),
            bExactPayload);
        if (!Target.bInputValid)
        {
            Target.Requirement = EDWCTransparencyBuildRequirement::ManualRepair;
            Target.Detail = FString::Join(Errors, TEXT("\n"));
            continue;
        }

        const FWetClothingBakedTransparencyMap* BakedMap =
            Asset.Authored.TransparencyData.FindBakedTransparencyMap(Slot);
        Target.bHasBakedOutput = BakedMap != nullptr;
        FString CurrentnessReason;
        const bool bFastUsable = BakedMap != nullptr &&
            BakedMap->IsRuntimeUsableForLayer(Layer.RequiresRuntimeRevealNormal());
        const FDWCTransparencyAffectedRebakeCandidate* Affected =
            AffectedByLayer.Find(Layer.LayerGuid);
        const bool bCanonicalCurrent = bExactPayload
            ? (Affected != nullptr &&
               Affected->Status == EDWCTransparencyAffectedRebakeStatus::AlreadyCurrent)
            : Result.RecordedStatus == EDWCBakeStatus::Valid;
        Target.bOutputCurrent = bFastUsable && bCanonicalCurrent;
        if (Target.bOutputCurrent && bExactPayload)
        {
            Target.bOutputCurrent = FDWCTransparencyEditedMapBaker::IsLayerBakeCurrent(
                Asset, Layer, &CurrentnessReason);
        }
        if (Target.bOutputCurrent)
        {
            Target.Detail = TEXT("The runtime Transparency output is current.");
            continue;
        }

        Target.Requirement = bExactPayload && Affected != nullptr && Affected->IsEligible()
            ? EDWCTransparencyBuildRequirement::AffectedStage4
            : EDWCTransparencyBuildRequirement::FullBake;
        Target.Detail = CurrentnessReason.IsEmpty()
            ? (Affected != nullptr && !Affected->Detail.IsEmpty()
                ? Affected->Detail
                : (BakedMap != nullptr
                    ? TEXT("The runtime Transparency output is out of date.")
                    : TEXT("The runtime Transparency output has not been baked.")))
            : MoveTemp(CurrentnessReason);
    }

    for (const FWetClothingTransparencyLayerData& Layer :
         Asset.Authored.TransparencyData.TransparencyLayers)
    {
        if (Evaluated.Contains(&Layer))
        {
            continue;
        }
        FDWCTransparencyBuildTarget& Target = Result.Targets.AddDefaulted_GetRef();
        Target.MaterialSlotIndex = Layer.TargetSurface.OuterMaterialSlotIndex;
        Target.LayerGuid = Layer.LayerGuid;
        Target.Intent = Layer.Intent;
        Target.Requirement = EDWCTransparencyBuildRequirement::ManualRepair;
        Target.Detail = TEXT("The Transparency layer targets a missing or non-Wettable material slot.");
    }

    bool bHasFullBake = false;
    bool bHasAffected = false;
    bool bHasManualRepair = false;
    for (const FDWCTransparencyBuildTarget& Target : Result.Targets)
    {
        if (!Target.IsEnabled())
        {
            continue;
        }
        bHasFullBake |= Target.Requirement == EDWCTransparencyBuildRequirement::FullBake;
        bHasAffected |= Target.Requirement == EDWCTransparencyBuildRequirement::AffectedStage4;
        bHasManualRepair |= Target.Requirement == EDWCTransparencyBuildRequirement::ManualRepair;
    }

    if (!Result.HasEnabledLayers())
    {
        Result.FullBakeReason = TEXT("No enabled Transparency Target Parts require runtime output.");
        Result.AffectedStage4Reason = Result.FullBakeReason;
        return Result;
    }
    Result.FullBakeState = bHasFullBake
        ? EDWCEditorBuildActionState::Required
        : bHasManualRepair
            ? EDWCEditorBuildActionState::Blocked
            : EDWCEditorBuildActionState::UpToDate;
    Result.FullBakeReason = bHasFullBake
            ? TEXT("One or more enabled Transparency outputs require a full bake.")
            : bHasManualRepair
                ? TEXT("Enabled Transparency Target Parts require manual input repair.")
                : TEXT("No enabled Transparency output requires a full bake.");
    Result.AffectedStage4State = bHasAffected
        ? EDWCEditorBuildActionState::Required
        : EDWCEditorBuildActionState::UpToDate;
    Result.AffectedStage4Reason = bHasAffected
        ? TEXT("Wrinkle coverage changed for one or more Transparency outputs.")
        : TEXT("No wrinkle-dependent Transparency output requires a partial rebake.");
    return Result;
}
