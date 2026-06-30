/*
 *  Wet Clothing Auto Partition 계산 함수와 클러스터 표시 색상 유틸리티를 선언합니다.
 */

#pragma once

#include "CoreMinimal.h"
#include "WetClothing/AutoPartition/WetClothingAutoPartitionTypes.h"

struct FWetClothingAssetUVIsland;
struct FWetClothingTextureReadback;

class FWetClothingAutoPartitioner
{
  public:
    static bool BuildClusters(
        const TArray<TSharedPtr<FWetClothingAssetUVIsland>>& Islands,
        const FWetClothingTextureReadback&                     TextureData,
        float                                                  TolerancePercent,
        TArray<FWetClothingAutoPartitionCluster>&              OutClusters,
        FString*                                               OutErrorMessage = nullptr);
};
