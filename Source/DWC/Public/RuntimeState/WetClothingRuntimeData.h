#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetnessProfile.h"
#include "RuntimeState/WetBoneOptimizationCache.h"

struct FWetnessProfileParameters;

struct FWetVertexNeighbors
{
    TArray<int32> Neighbors;
};

class DWC_API FWetClothingRuntimeData
{
  public:
    void ResetWetPartData();
    void ResetNeighborGraph();
    void ResetBoneOptimizationCache();

    TArray<int32>                     VertexWetPartIDs;
    TArray<bool>                      VertexWettableFlags;
    TArray<FWetnessProfileParameters> VertexWetnessProfileParameters;
    TArray<FLinearColor>              VertexWetPartDebugColors;
    TArray<FWetVertexNeighbors>       NeighborGraph;
    bool                              bHasNeighborGraph = false;

    FWetBoneOptimizationCache BoneOptimizationCache;
    bool                      bHasBoneOptimizationCache = false;
    FString                   BoneOptimizationCacheFallbackReason;

    bool IsVertexWettable(int32 VertexIndex) const
    {
        return VertexWettableFlags.IsValidIndex(VertexIndex) && VertexWettableFlags[VertexIndex];
    }
};
