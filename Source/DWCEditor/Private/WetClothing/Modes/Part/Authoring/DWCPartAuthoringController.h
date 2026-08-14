// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetClothingPartData.h"
#include "WetClothing/Foundation/Authoring/DWCEditorAuthoringTypes.h"
#include "WetClothing/Modes/Part/Partition/WetPartAutoPartitionTypes.h"

class FDWCEditorAuthoringDocument;
class UWetClothingAsset;
struct FWetnessProfileParameters;

struct FDWCPartAuthoringResult
{
    bool bChanged = false;
    int32 WetPartID = INDEX_NONE;
    FString Error;

    explicit operator bool() const { return bChanged; }
};

/**
 * Single mutation boundary for Wet Part authoring.
 *
 * UI code owns selection only. This controller owns serialized Part mutations,
 * transaction boundaries, derived-output invalidation, and authoring events.
 */
class FDWCPartAuthoringController final
{
  public:
    FDWCPartAuthoringController(
        UWetClothingAsset* InAsset,
        TSharedPtr<FDWCEditorAuthoringDocument> InAuthoringDocument);

    FDWCPartAuthoringResult SetMaterialSlotWettable(int32 MaterialSlotIndex, bool bWettable) const;
    FDWCPartAuthoringResult RenamePart(int32 MaterialSlotIndex, int32 WetPartID, const FString& DisplayName) const;
    FDWCPartAuthoringResult SetPartColor(int32 MaterialSlotIndex, int32 WetPartID, FLinearColor Color) const;
    FDWCPartAuthoringResult SetPartVisibility(int32 MaterialSlotIndex, int32 WetPartID, bool bVisible) const;
    FDWCPartAuthoringResult SetPartProfile(
        int32 MaterialSlotIndex,
        int32 WetPartID,
        const FSoftObjectPath& SourceProfilePath,
        const FWetnessProfileParameters* ProfileParameters) const;
    FDWCPartAuthoringResult AddPart(int32 MaterialSlotIndex) const;
    FDWCPartAuthoringResult RemovePart(int32 MaterialSlotIndex, int32 WetPartID) const;
    FDWCPartAuthoringResult ResetPart(int32 MaterialSlotIndex, int32 WetPartID) const;
    FDWCPartAuthoringResult ReplaceWithAutoPartition(
        int32 MaterialSlotIndex,
        const TArray<FWetPartAutoPartitionCluster>& Clusters) const;
    FDWCPartAuthoringResult AssignIslands(
        int32 MaterialSlotIndex,
        int32 WetPartID,
        const TSet<int32>& UVIslandIDs) const;
    FDWCPartAuthoringResult ApplySurfaceWaterSettings(
        int32 MaterialSlotIndex,
        int32 WetPartID,
        const FWetPartSurfaceWaterSettings& Settings) const;

  private:
    FDWCPartAuthoringResult Edit(
        const FText& TransactionText,
        int32 MaterialSlotIndex,
        int32 WetPartID,
        int32 InvalidatedBakeOutputMask,
        EDWCEditorAuthoringImpact Impact,
        TFunctionRef<bool(UWetClothingAsset&)> Mutation) const;

    TWeakObjectPtr<UWetClothingAsset> Asset;
    TSharedPtr<FDWCEditorAuthoringDocument> AuthoringDocument;
};
