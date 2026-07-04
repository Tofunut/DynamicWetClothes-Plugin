#pragma once

#include "CoreMinimal.h"

class FSkeletalMeshLODRenderData;
class USkeletalMesh;
class UWetClothingAsset;
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

    static bool TryCopyBakedGraph(
        const UWetClothingAsset*     WetClothingAsset,
        const USkeletalMesh*         SkeletalMesh,
        int32                        LODIndex,
        int32                        VertexCount,
        TArray<FWetVertexNeighbors>& OutNeighborGraph,
        FString*                     OutErrorMessage = nullptr);
};
