#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetnessProfile.h"
#include "RuntimeState/WetBoneOptimizationCache.h"

struct FWetnessProfileParameters;

struct FWetVertexNeighborRange
{
    int32 StartOffset = 0;
    int32 Count = 0;

    bool IsValidFor(const TArray<int32>& FlatNeighborIndices) const
    {
        return StartOffset >= 0 &&
               Count >= 0 &&
               StartOffset <= FlatNeighborIndices.Num() &&
               Count <= FlatNeighborIndices.Num() - StartOffset;
    }
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
    TArray<FWetVertexNeighborRange>   NeighborRanges;
    TArray<int32>                     FlatNeighborIndices;
    bool                              bHasNeighborGraph = false;

    FWetBoneOptimizationCache BoneOptimizationCache;
    bool                      bHasBoneOptimizationCache = false;
    FString                   BoneOptimizationCacheFallbackReason;

    bool IsVertexWettable(int32 VertexIndex) const
    {
        return VertexWettableFlags.IsValidIndex(VertexIndex) && VertexWettableFlags[VertexIndex];
    }
};
