#pragma once

#include "CoreMinimal.h"
#include "RuntimeData/WetBoneOptimizationCache.h"

class UWetClothingAsset;
class USkeletalMesh;
struct FWetVertexNeighbors;

class DWC_API FWetBakedRuntimeDataBridge
{
  public:
    static bool TryCopyBakedBoneOptimizationCache(
        const UWetClothingAsset*   WetClothingAsset,
        USkeletalMesh*             SkeletalMesh,
        int32                      LODIndex,
        FWetBoneOptimizationCache& OutRuntimeCache,
        FString*                   OutErrorMessage = nullptr);

    static bool TryCopyBakedNeighborGraph(
        const UWetClothingAsset*     WetClothingAsset,
        const USkeletalMesh*         SkeletalMesh,
        int32                        LODIndex,
        int32                        VertexCount,
        TArray<FWetVertexNeighbors>& OutNeighborGraph,
        FString*                     OutErrorMessage = nullptr);
};
