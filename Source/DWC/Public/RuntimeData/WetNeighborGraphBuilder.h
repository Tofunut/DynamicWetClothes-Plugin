#pragma once

#include "CoreMinimal.h"

class FSkeletalMeshLODRenderData;
struct FWetClothingAssetBakedVertexNeighbors;
struct FWetVertexNeighbors;

class DWC_API FWetNeighborGraphBuilder
{
  public:
    static bool BuildRuntimeGraph(
        const FSkeletalMeshLODRenderData& LODData,
        float                             InCoincidentVertexNeighborTolerance,
        TArray<FWetVertexNeighbors>&      OutNeighborGraph,
        FString*                          OutErrorMessage = nullptr);

    static bool BuildBakedGraph(
        const FSkeletalMeshLODRenderData&              LODData,
        float                                          InCoincidentVertexNeighborTolerance,
        TArray<FWetClothingAssetBakedVertexNeighbors>& OutNeighborGraph,
        FString*                                       OutErrorMessage = nullptr);
};
