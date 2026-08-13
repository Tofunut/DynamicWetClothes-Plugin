// Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetClothingAssetSetupData.h"
#include "WetClothing/Foundation/Build/DWCEditorBuildActionTypes.h"

class UWetClothingAsset;

enum class EDWCRenderProfileIssueResolution : uint8
{
    BakeRenderProfile,
    GenerateMaterials,
    Manual
};

struct FDWCRenderProfileValidationIssue
{
    FName Code;
    int32 MaterialSlotIndex = INDEX_NONE;
    FString ProfileStableKey;
    FString Detail;
    EDWCRenderProfileIssueResolution Resolution =
        EDWCRenderProfileIssueResolution::BakeRenderProfile;
    bool bFailed = false;
};

struct FDWCRenderProfileValidationSnapshot
{
    TArray<FDWCRenderProfileValidationIssue> Issues;
    bool bRequired = false;
    EDWCBakeStatus RecordedStatus = EDWCBakeStatus::Disabled;
    FString FailureMessage;
    bool bSavePending = false;
    EDWCEditorBuildActionState BakeState = EDWCEditorBuildActionState::Unavailable;
    FString BakeReason;

    bool HasPendingTasks() const
    {
        return BakeState == EDWCEditorBuildActionState::Required ||
               BakeState == EDWCEditorBuildActionState::Blocked ||
               BakeState == EDWCEditorBuildActionState::Failed;
    }
};

/** Side-effect-free source of truth for Render Profile validation and build admission. */
class FDWCRenderProfileBuildTargetResolver
{
  public:
    static FDWCRenderProfileValidationSnapshot Resolve(
        const UWetClothingAsset* WetClothingAsset);
};
