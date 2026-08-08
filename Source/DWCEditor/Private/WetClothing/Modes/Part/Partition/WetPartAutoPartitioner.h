// Copyright 2026 Team Tofunut. All Rights Reserved.

/*
 * Declares Wet Part Auto Partition calculations and cluster display-color helpers.
 */

#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Modes/Part/Partition/WetPartAutoPartitionTypes.h"

struct FWetClothingAssetUVIsland;
struct FWetClothingTextureReadback;

class FWetPartAutoPartitioner
{
  public:
    static bool BuildClusters(
        const TArray<TSharedPtr<FWetClothingAssetUVIsland>>& Islands,
        const FWetClothingTextureReadback&                   TextureData,
        float                                                TolerancePercent,
        EWetPartAutoPartitionColorMode                       ColorMode,
        TArray<FWetPartAutoPartitionCluster>&                OutClusters,
        FString*                                             OutErrorMessage = nullptr);
};
