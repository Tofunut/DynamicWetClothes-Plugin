//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

class USkeletalMesh;
class UWetClothingAsset;
struct FDWCDataUVBuildResult;

struct FDWCLODRangeUpdateLODDetail
{
    int32 LODIndex = INDEX_NONE;
    bool bSucceeded = false;
    bool bHasNotes = false;
    bool bHasWarnings = false;
    FString Message;
};

struct FDWCLODRangeUpdateReport
{
    int32 PreviousFirstLODIndex = 0;
    int32 PreviousLastLODIndex = 0;
    int32 RequestedFirstLODIndex = 0;
    int32 RequestedLastLODIndex = 0;
    int32 ActiveFirstLODIndex = 0;
    int32 ActiveLastLODIndex = 0;
    bool bApplied = true;
    TArray<int32> RetainedLODIndices;
    TArray<int32> ReusedLODIndices;
    TArray<int32> GeneratedLODIndices;
    TArray<int32> RemovedLODIndices;
    TArray<int32> PreparedLODIndices;
    TArray<int32> FailedLODIndices;
    TArray<FDWCLODRangeUpdateLODDetail> LODDetails;
    FString AdditionalSummary;
};

namespace WCAReportDialogs
{
    void OpenDWCDataUVBuildResultDialog(
        const FDWCDataUVBuildResult& Result,
        const UWetClothingAsset* Asset,
        const USkeletalMesh* PreparedMesh,
        const TSet<int32>& IncludedMaterialSlotIndices);

    /** Returns the affected material slots explicitly accepted as Ready with warnings. */
    TSet<int32> ConfirmDWCDataUVVisibleExclusion(
        const FDWCDataUVBuildResult& Result,
        const USkeletalMesh* PreparedMesh,
        const TSet<int32>& IncludedMaterialSlotIndices);

    void OpenDWCDataUVBuildFailureDialog(
        const FDWCDataUVBuildResult& Result,
        const UWetClothingAsset* Asset,
        const USkeletalMesh* PreparedMesh,
        const TSet<int32>& IncludedMaterialSlotIndices);

    void OpenDWCDataUVSlotDetailsDialog(
        const UWetClothingAsset& Asset,
        const USkeletalMesh* PreparedMesh,
        int32 MaterialSlotIndex,
        const TSet<int32>& FailedMaterialSlotIndices,
        const FString& LastFailureMessage);

    void OpenDWCDataUVAllSlotsDetailsDialog(
        const UWetClothingAsset& Asset,
        const USkeletalMesh* PreparedMesh,
        const TSet<int32>& FailedMaterialSlotIndices,
        const FString& LastFailureMessage);

    void OpenLODRangeUpdateDialog(const FDWCLODRangeUpdateReport& Report);
}
