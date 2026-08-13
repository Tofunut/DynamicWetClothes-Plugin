// Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetClothingAssetSetupData.h"
#include "DataAssets/WetClothingTransparencyData.h"
#include "WetClothing/Foundation/Build/DWCEditorBuildActionTypes.h"
#include "WetClothing/Foundation/Validation/DWCEditorValidationTypes.h"

class UWetClothingAsset;

enum class EDWCTransparencyBuildRequirement : uint8
{
    None,
    FullBake,
    AffectedStage4,
    ManualRepair
};

struct FDWCTransparencyBuildTarget
{
    int32 MaterialSlotIndex = INDEX_NONE;
    FGuid LayerGuid;
    EDWCTransparencyLayerIntent Intent = EDWCTransparencyLayerIntent::Draft;
    EDWCTransparencyBuildRequirement Requirement = EDWCTransparencyBuildRequirement::None;
    bool bWettable = false;
    bool bInputValid = false;
    bool bHasBakedOutput = false;
    bool bOutputCurrent = false;
    FString Detail;

    bool IsEnabled() const { return Intent == EDWCTransparencyLayerIntent::Enabled; }
    bool IsBuildable() const
    {
        return IsEnabled() && bWettable && bInputValid &&
            (Requirement == EDWCTransparencyBuildRequirement::FullBake ||
             Requirement == EDWCTransparencyBuildRequirement::AffectedStage4);
    }
};

struct FDWCTransparencyBuildTargetSnapshot
{
    TArray<FDWCTransparencyBuildTarget> Targets;
    EDWCBakeStatus RecordedStatus = EDWCBakeStatus::Disabled;
    FString FailureMessage;
    bool bSavePending = false;
    EDWCEditorBuildActionState FullBakeState = EDWCEditorBuildActionState::Unavailable;
    EDWCEditorBuildActionState AffectedStage4State = EDWCEditorBuildActionState::Unavailable;
    FString FullBakeReason;
    FString AffectedStage4Reason;

    bool HasEnabledLayers() const;
    const FDWCTransparencyBuildTarget* FindByLayerGuid(const FGuid& LayerGuid) const;
    void CollectLayerGuids(
        EDWCTransparencyBuildRequirement Requirement,
        TArray<FGuid>& OutLayerGuids) const;
};

/** Side-effect-free source of truth for Transparency validation and build admission. */
class FDWCTransparencyBuildTargetResolver
{
  public:
    static FDWCTransparencyBuildTargetSnapshot Resolve(
        const UWetClothingAsset& Asset,
        EDWCEditorValidationAccess Access);
};
