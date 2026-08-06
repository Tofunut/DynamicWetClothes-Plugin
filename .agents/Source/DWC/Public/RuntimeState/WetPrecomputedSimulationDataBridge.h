#pragma once

#include "CoreMinimal.h"
#include "RuntimeState/WetBoneOptimizationCache.h"

class UWetClothingAsset;
class USkeletalMesh;
struct FWetVertexNeighborRange;

class DWC_API FWetPrecomputedSimulationDataBridge
{
  public:
    static bool TryCopyPrecomputedBoneOptimizationCache(
        const UWetClothingAsset*   WetClothingAsset,
        USkeletalMesh*             SkeletalMesh,
        FWetBoneOptimizationCache& OutRuntimeCache,
        FString*                   OutErrorMessage = nullptr);

    static bool TryCopyPrecomputedNeighborGraph(
        const UWetClothingAsset*         WetClothingAsset,
        const USkeletalMesh*             SkeletalMesh,
        int32                            VertexCount,
        TArray<FWetVertexNeighborRange>& OutNeighborRanges,
        TArray<int32>&                   OutFlatNeighborIndices,
        FString*                         OutErrorMessage = nullptr);
};
