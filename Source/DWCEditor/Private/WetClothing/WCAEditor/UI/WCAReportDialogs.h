// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class USkeletalMesh;
class UWetClothingAsset;
struct FDWCDataUVBuildResult;
struct FDWCEditorBakeBatchResult;

struct FDWCLODRangeUpdateLODDetail
{
    int32   LODIndex = INDEX_NONE;
    bool    bSucceeded = false;
    bool    bHasWarnings = false;
    FString Message;
};

struct FDWCLODRangeUpdateReport
{
    int32                               PreviousFirstLODIndex = 0;
    int32                               PreviousLastLODIndex = 0;
    int32                               RequestedFirstLODIndex = 0;
    int32                               RequestedLastLODIndex = 0;
    int32                               ActiveFirstLODIndex = 0;
    int32                               ActiveLastLODIndex = 0;
    bool                                bApplied = true;
    TArray<int32>                       RetainedLODIndices;
    TArray<int32>                       ReusedLODIndices;
    TArray<int32>                       GeneratedLODIndices;
    TArray<int32>                       RemovedLODIndices;
    TArray<int32>                       PreparedLODIndices;
    TArray<int32>                       FailedLODIndices;
    TArray<FDWCLODRangeUpdateLODDetail> LODDetails;
    FString                             AdditionalSummary;
};

namespace WCAReportDialogs
{
    struct FDWCDataUVVisibleExclusionDecision
    {
        TSet<int32> AcceptedMaterialSlotIndices;
        TSet<int32> SkippedMaterialSlotIndices;
        bool        bCancelBuild = false;
        bool        bInternalError = false;
        FString     ErrorMessage;

        bool HasSlotDecisions() const
        {
            return !AcceptedMaterialSlotIndices.IsEmpty() || !SkippedMaterialSlotIndices.IsEmpty();
        }
    };

    void OpenDWCDataUVBuildResultDialog(
        const FDWCDataUVBuildResult& Result,
        const UWetClothingAsset*     Asset,
        const USkeletalMesh*         PreparedMesh,
        const TSet<int32>&           IncludedMaterialSlotIndices);

    /** Resolves one pending visible-surface warning as Accept / Skip / Cancel. */
    FDWCDataUVVisibleExclusionDecision ConfirmDWCDataUVVisibleExclusion(
        const FDWCDataUVBuildResult& Result,
        const USkeletalMesh*         SlotIdentityMesh);

    void OpenDWCDataUVBuildFailureDialog(
        const FDWCDataUVBuildResult& Result,
        const UWetClothingAsset*     Asset,
        const USkeletalMesh*         PreparedMesh,
        const TSet<int32>&           IncludedMaterialSlotIndices);

    void OpenDWCDataUVSlotDetailsDialog(
        const UWetClothingAsset& Asset,
        const USkeletalMesh*     PreparedMesh,
        int32                    MaterialSlotIndex,
        const TSet<int32>&       FailedMaterialSlotIndices,
        const FString&           LastFailureMessage);

    void OpenDWCDataUVAllSlotsDetailsDialog(
        const UWetClothingAsset& Asset,
        const USkeletalMesh*     PreparedMesh,
        const TSet<int32>&       FailedMaterialSlotIndices,
        const FString&           LastFailureMessage);

    void OpenLODRangeUpdateDialog(const FDWCLODRangeUpdateReport& Report);

    void OpenBakeResultDialog(
        const FDWCEditorBakeBatchResult& Result,
        const FText& SuccessTitle);
} // namespace WCAReportDialogs
