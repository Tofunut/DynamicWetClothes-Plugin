//Copyright 2026 Team Tofunut. All Rights Reserved.
/*
 *  Wet Part Auto Partition 계산 함수와 클러스터 표시 색상 유틸리티를 선언합니다.
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
