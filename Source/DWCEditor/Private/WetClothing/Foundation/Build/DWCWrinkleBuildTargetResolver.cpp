// Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Foundation/Build/DWCWrinkleBuildTargetResolver.h"

#include "DataAssets/WetClothingAsset.h"
#include "DataAssets/WetClothingWrinkleData.h"
#include "Engine/SkeletalMesh.h"
#include "WetClothing/DerivedAssets/Textures/Wrinkle/WetWrinkleBakeService.h"
#include "WetClothing/DerivedAssets/Textures/Wrinkle/WetWrinkleNormalMapBaker.h"

namespace
{
const FWetWrinkleAuthoredSlotState* FindAuthoredState(
    const TArray<FWetWrinkleAuthoredSlotState>& States,
    const int32 MaterialSlotIndex)
{
    return States.FindByPredicate(
        [MaterialSlotIndex](const FWetWrinkleAuthoredSlotState& State)
        {
            return State.MaterialSlotIndex == MaterialSlotIndex;
        });
}

int32 CountRuntimeSources(const FWetClothingWrinkleData& Data, const int32 MaterialSlotIndex)
{
    int32 Count = 0;
    for (const FWetWrinkleRuntimeNormalSource& Source : Data.RuntimeNormalSources)
    {
        if (Source.MaterialSlotIndex == MaterialSlotIndex)
        {
            ++Count;
        }
    }
    return Count;
}

int32 CountBakedOutputs(const FWetClothingWrinkleData& Data, const int32 MaterialSlotIndex)
{
    int32 Count = 0;
    for (const FWetWrinkleBakedMapSet& Output : Data.BakedWrinkleMaps)
    {
        if (Output.MaterialSlotIndex == MaterialSlotIndex)
        {
            ++Count;
        }
    }
    return Count;
}

FString BuildInvalidAuthoredDetail(const FWetWrinkleAuthoredSlotState& State)
{
    TArray<FString> Details;
    if (State.MissingPatchTextureCount > 0)
    {
        Details.Add(FString::Printf(
            TEXT("%d wrinkle patch(es) have no normal texture."),
            State.MissingPatchTextureCount));
    }
    if (State.InvalidPatchTextureCount > 0)
    {
        Details.Add(FString::Printf(
            TEXT("%d wrinkle patch source texture reference(s) are missing or invalid."),
            State.InvalidPatchTextureCount));
    }
    if (State.InvalidPatchPlacementCount > 0)
    {
        Details.Add(FString::Printf(
            TEXT("%d wrinkle patch(es) have an invalid surface anchor, frame, or footprint."),
            State.InvalidPatchPlacementCount));
    }
    if (State.InvalidRidgeStrokeCount > 0)
    {
        Details.Add(FString::Printf(
            TEXT("%d procedural ridge stroke(s) are not bakeable."),
            State.InvalidRidgeStrokeCount));
    }
    return FString::Join(Details, TEXT(" "));
}
}

bool FDWCWrinkleBuildTargetSnapshot::HasBakedAuthoringTargets() const
{
    return Targets.ContainsByPredicate(
        [](const FDWCWrinkleBuildTarget& Target)
        {
            return Target.RequiresBakedOutput();
        });
}

const FDWCWrinkleBuildTarget* FDWCWrinkleBuildTargetSnapshot::FindByMaterialSlot(
    const int32 MaterialSlotIndex) const
{
    return Targets.FindByPredicate(
        [MaterialSlotIndex](const FDWCWrinkleBuildTarget& Target)
        {
            return Target.MaterialSlotIndex == MaterialSlotIndex;
        });
}

void FDWCWrinkleBuildTargetSnapshot::CollectBuildMaterialSlots(
    TArray<int32>& OutMaterialSlots) const
{
    OutMaterialSlots.Reset();
    for (const FDWCWrinkleBuildTarget& Target : Targets)
    {
        if (Target.IsBuildable())
        {
            OutMaterialSlots.AddUnique(Target.MaterialSlotIndex);
        }
    }
    OutMaterialSlots.Sort();
}

FDWCWrinkleBuildTargetSnapshot FDWCWrinkleBuildTargetResolver::Resolve(
    const UWetClothingAsset& Asset,
    const EDWCEditorValidationAccess Access)
{
    const bool bExactPayload = Access == EDWCEditorValidationAccess::ExactPayload;
    FDWCWrinkleBuildTargetSnapshot Result;
    const FWetClothingWrinkleData& Data = Asset.Authored.WrinkleData;
    TArray<FWetWrinkleAuthoredSlotState> AuthoredStates;
    FWetWrinkleBakeService::CollectAuthoredSlotStates(Asset, AuthoredStates);

    TSet<int32> Slots;
    if (const USkeletalMesh* Mesh = Asset.GetDWCSkeletalMesh())
    {
        for (int32 Slot = 0; Slot < Mesh->GetMaterials().Num(); ++Slot)
        {
            if (Asset.IsMaterialSlotWettable(Slot))
            {
                Slots.Add(Slot);
            }
        }
    }
    for (const FWetWrinkleAuthoredSlotState& State : AuthoredStates)
    {
        Slots.Add(State.MaterialSlotIndex);
    }
    for (const FWetWrinkleRuntimeNormalSource& Source : Data.RuntimeNormalSources)
    {
        Slots.Add(Source.MaterialSlotIndex);
    }
    for (const FWetWrinkleBakedMapSet& Output : Data.BakedWrinkleMaps)
    {
        Slots.Add(Output.MaterialSlotIndex);
    }

    TArray<int32> SortedSlots = Slots.Array();
    SortedSlots.Sort();
    for (const int32 Slot : SortedSlots)
    {
        FDWCWrinkleBuildTarget& Target = Result.Targets.AddDefaulted_GetRef();
        Target.MaterialSlotIndex = Slot;
        Target.bWettable = Slot != INDEX_NONE && Asset.IsMaterialSlotWettable(Slot);
        Target.bSavePending = Asset.IsBakeOutputSavePending(DWCBakeOutput::WrinkleMaps);

        const FWetWrinkleAuthoredSlotState* Authored = FindAuthoredState(AuthoredStates, Slot);
        if (Authored != nullptr)
        {
            Target.bHasAuthoredContent = Authored->HasAuthoredContent();
            Target.bHasBakeableContent = Authored->HasBakeableContent();
            Target.PatchCount = Authored->PatchCount;
            Target.RidgeStrokeCount = Authored->RidgeStrokeCount;
        }

        const int32 RuntimeSourceCount = CountRuntimeSources(Data, Slot);
        const int32 BakedOutputCount = CountBakedOutputs(Data, Slot);
        const FWetWrinkleRuntimeNormalSource* RuntimeSource = Data.FindRuntimeNormalSource(Slot);
        const bool bUsesCustom = RuntimeSource != nullptr &&
            RuntimeSource->Source == EDWCWrinkleNormalSource::CustomTexture;
        Target.SourceMode = bUsesCustom
            ? EDWCWrinkleBuildSourceMode::CustomTexture
            : Target.bHasAuthoredContent
                ? EDWCWrinkleBuildSourceMode::BakedAuthoring
                : EDWCWrinkleBuildSourceMode::None;
        Target.bConfigured = Target.SourceMode != EDWCWrinkleBuildSourceMode::None;
        Target.bOrphanOutput = !Target.bConfigured && BakedOutputCount > 0;

        if ((Target.bConfigured || RuntimeSourceCount > 0 || BakedOutputCount > 0) &&
            !Target.bWettable)
        {
            Target.Requirement = EDWCWrinkleBuildRequirement::ManualRepair;
            Target.DiagnosticCode = TEXT("WrinkleTargetNotWettable");
            Target.Detail = TEXT("Wrinkle data targets a missing or non-Wettable material slot.");
            continue;
        }
        if (RuntimeSourceCount > 1)
        {
            Target.Requirement = EDWCWrinkleBuildRequirement::ManualRepair;
            Target.DiagnosticCode = TEXT("WrinkleDuplicateRuntimeSource");
            Target.Detail = TEXT("More than one runtime wrinkle source uses this material slot.");
            continue;
        }
        if (BakedOutputCount > 1)
        {
            Target.Requirement = EDWCWrinkleBuildRequirement::ManualRepair;
            Target.DiagnosticCode = TEXT("WrinkleDuplicateBakedOutput");
            Target.Detail = TEXT("More than one baked wrinkle output uses this material slot.");
            continue;
        }

        if (bUsesCustom)
        {
            Target.bInputValid = RuntimeSource->CustomWrinkleNormalMap != nullptr;
            Target.bHasRuntimeNormal = Target.bInputValid;
            Target.bOutputCurrent = Target.bInputValid;
            if (!Target.bInputValid)
            {
                Target.Requirement = EDWCWrinkleBuildRequirement::ManualRepair;
                Target.DiagnosticCode = TEXT("WrinkleCustomTextureMissing");
                Target.Detail = TEXT("Custom Wrinkle Map is selected, but no normal texture is assigned.");
            }
            else
            {
                Target.Detail = TEXT("The custom runtime wrinkle normal is ready.");
            }
            continue;
        }

        if (!Target.bHasAuthoredContent)
        {
            Target.bInputValid = true;
            Target.Detail = Target.bOrphanOutput
                ? TEXT("A baked wrinkle output remains, but this slot has no active authored wrinkles.")
                : TEXT("Wrinkle is not configured for this Wettable slot.");
            continue;
        }
        if (Authored == nullptr || Authored->HasInvalidInput() || !Target.bHasBakeableContent)
        {
            Target.Requirement = EDWCWrinkleBuildRequirement::ManualRepair;
            Target.DiagnosticCode = Authored != nullptr &&
                    (Authored->MissingPatchTextureCount > 0 ||
                     Authored->InvalidPatchTextureCount > 0)
                ? FName(TEXT("WrinklePatchMissingTexture"))
                : FName(TEXT("WrinkleSourceInvalid"));
            Target.Detail = Authored != nullptr
                ? BuildInvalidAuthoredDetail(*Authored)
                : TEXT("The authored wrinkle source is invalid.");
            continue;
        }

        Target.bInputValid = true;
        const FWetWrinkleMaterialSlotBakeState BakeState =
            FWetWrinkleNormalMapBaker::EvaluateMaterialSlotBakeState(
                &Asset, Slot, bExactPayload);
        Target.bHasBakedNormal = BakeState.bNormalExists;
        Target.bHasCoverageMask = BakeState.bCoverageMaskExists;
        Target.bHasRuntimeNormal = BakeState.bNormalExists;
        Target.bOutputCurrent = BakeState.IsCurrent();
        Target.Detail = BakeState.Detail;
        if (!bExactPayload &&
            !DWCBuildStatus::IsUsable(Asset.GetBakeState().WrinkleMaps))
        {
            Target.bOutputCurrent = false;
            Target.Detail = TEXT("The saved wrinkle bake status is missing or out of date.");
        }
        if (!Target.bOutputCurrent)
        {
            Target.Requirement = EDWCWrinkleBuildRequirement::Bake;
            Target.DiagnosticCode = BakeState.Issue == EWetWrinkleBakeCurrentnessIssue::CoverageMissing
                ? FName(TEXT("WrinkleCoverageMissing"))
                : BakeState.Issue == EWetWrinkleBakeCurrentnessIssue::NormalMissing
                    ? FName(TEXT("WrinkleNormalMissing"))
                    : FName(TEXT("WrinkleOutputStale"));
        }
    }

    const bool bHasBake = Result.Targets.ContainsByPredicate(
        [](const FDWCWrinkleBuildTarget& Target)
        {
            return Target.Requirement == EDWCWrinkleBuildRequirement::Bake;
        });
    const bool bHasBakedManualRepair = Result.Targets.ContainsByPredicate(
        [](const FDWCWrinkleBuildTarget& Target)
        {
            return Target.SourceMode == EDWCWrinkleBuildSourceMode::BakedAuthoring &&
                Target.Requirement == EDWCWrinkleBuildRequirement::ManualRepair;
        });
    if (!Result.HasBakedAuthoringTargets())
    {
        Result.BakeReason = TEXT("No authored baked wrinkle content requires an output.");
        Result.BakeState = EDWCEditorBuildActionState::Unavailable;
        return Result;
    }
    if (bHasBakedManualRepair)
    {
        Result.BakeState = EDWCEditorBuildActionState::Blocked;
        Result.BakeReason = TEXT("One or more authored wrinkle sources require manual repair.");
    }
    else if (bHasBake)
    {
        Result.BakeState = Asset.GetBakeState().WrinkleMaps == EDWCBakeStatus::Failed
            ? EDWCEditorBuildActionState::Failed
            : EDWCEditorBuildActionState::Required;
        Result.BakeReason = TEXT("One or more baked wrinkle normal or coverage outputs are missing or out of date.");
    }
    else
    {
        Result.BakeState = EDWCEditorBuildActionState::UpToDate;
        Result.BakeReason = TEXT("All authored wrinkle normal and coverage outputs are current.");
    }
    return Result;
}
