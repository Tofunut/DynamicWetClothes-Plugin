#pragma once

#include "CoreMinimal.h"
#include "RuntimeState/WetBoneOptimizationCache.h"

class UWetClothingAsset;
class USkeletalMesh;
struct FWetVertexNeighbors;

class DWC_API FWetPrecomputedSimulationDataBridge
{
  public:
    static bool TryCopyPrecomputedBoneOptimizationCache(
        const UWetClothingAsset*   WetClothingAsset,
        USkeletalMesh*             SkeletalMesh,
        int32                      LODIndex,
        FWetBoneOptimizationCache& OutRuntimeCache,
        FString*                   OutErrorMessage = nullptr);

    static bool TryCopyPrecomputedNeighborGraph(
        const UWetClothingAsset*     WetClothingAsset,
        const USkeletalMesh*         SkeletalMesh,
        int32                        LODIndex,
        int32                        VertexCount,
        TArray<FWetVertexNeighbors>& OutNeighborGraph,
        FString*                     OutErrorMessage = nullptr);
};
