#include "RuntimeState/WetPrecomputedSimulationDataBridge.h"

#include "DataAssets/WetClothingAsset.h"
#include "Engine/SkeletalMesh.h"
#include "RuntimeState/WetClothingRuntimeData.h"
#include "Utility/DWCError.h"

bool FWetPrecomputedSimulationDataBridge::TryCopyPrecomputedBoneOptimizationCache(
    const UWetClothingAsset*   WetClothingAsset,
    USkeletalMesh*             SkeletalMesh,
    const int32                LODIndex,
    FWetBoneOptimizationCache& OutRuntimeCache,
    FString*                   OutErrorMessage)
{
    OutRuntimeCache = FWetBoneOptimizationCache();

    if (WetClothingAsset == nullptr || SkeletalMesh == nullptr)
    {
        DWC::Error::SetMessage(OutErrorMessage, TEXT("No WetClothingAsset or SkeletalMesh is available."));
        return false;
    }

    if (!WetClothingAsset->IsPrecomputedSimulationDataValidForMesh(SkeletalMesh, LODIndex))
    {
        DWC::Error::SetMessage(OutErrorMessage, TEXT("Precomputed simulation data is stale or invalid for the target mesh."));
        return false;
    }

    const FWetClothingPrecomputedSimulationData& PrecomputedData = WetClothingAsset->GetPrecomputedSimulationData();
    if (!PrecomputedData.BoneOptimizationCache.IsValidForMesh(
            SkeletalMesh,
            LODIndex,
            PrecomputedData.VertexCount,
            PrecomputedData.MeshSignature))
    {
        DWC::Error::SetMessage(OutErrorMessage, TEXT("Precomputed bone optimization cache is stale or invalid for the target mesh."));
        return false;
    }

    if (!PrecomputedData.BoneOptimizationCache.CopyToRuntimeCache(SkeletalMesh, OutRuntimeCache))
    {
        DWC::Error::SetMessage(OutErrorMessage, TEXT("Failed to copy precomputed bone optimization cache to runtime cache."));
        return false;
    }

    DWC::Error::SetMessage(OutErrorMessage, TEXT(""));
    return true;
}

bool FWetPrecomputedSimulationDataBridge::TryCopyPrecomputedNeighborGraph(
    const UWetClothingAsset*         WetClothingAsset,
    const USkeletalMesh*             SkeletalMesh,
    const int32                      LODIndex,
    const int32                      VertexCount,
    TArray<FWetVertexNeighborRange>& OutNeighborRanges,
    TArray<int32>&                   OutFlatNeighborIndices,
    FString*                         OutErrorMessage)
{
    OutNeighborRanges.Reset();
    OutFlatNeighborIndices.Reset();

    if (WetClothingAsset == nullptr || SkeletalMesh == nullptr)
    {
        DWC::Error::SetMessage(OutErrorMessage, TEXT("No WetClothingAsset or SkeletalMesh is available."));
        return false;
    }

    if (!WetClothingAsset->IsPrecomputedSimulationDataValidForMesh(SkeletalMesh, LODIndex))
    {
        DWC::Error::SetMessage(OutErrorMessage, TEXT("Precomputed simulation data is stale or invalid for the target mesh."));
        return false;
    }

    const FWetClothingPrecomputedSimulationData& PrecomputedData = WetClothingAsset->GetPrecomputedSimulationData();
    if (PrecomputedData.NeighborGraph.Num() != VertexCount)
    {
        DWC::Error::SetMessage(OutErrorMessage, TEXT("Precomputed neighbor graph vertex count does not match the mesh."));
        return false;
    }

    int64 TotalNeighborCount = 0;
    for (int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
    {
        TotalNeighborCount += PrecomputedData.NeighborGraph[VertexIndex].Neighbors.Num();
    }

    if (TotalNeighborCount > TNumericLimits<int32>::Max())
    {
        DWC::Error::SetMessage(OutErrorMessage, TEXT("Precomputed neighbor graph is too large for the runtime flat adjacency buffer."));
        return false;
    }

    OutNeighborRanges.SetNum(VertexCount);
    OutFlatNeighborIndices.Reserve(static_cast<int32>(TotalNeighborCount));
    for (int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
    {
        const TArray<int32>& PrecomputedNeighbors = PrecomputedData.NeighborGraph[VertexIndex].Neighbors;
        FWetVertexNeighborRange& RuntimeRange = OutNeighborRanges[VertexIndex];
        RuntimeRange.StartOffset = OutFlatNeighborIndices.Num();
        RuntimeRange.Count = PrecomputedNeighbors.Num();

        for (const int32 NeighborIndex : PrecomputedNeighbors)
        {
            if (!OutNeighborRanges.IsValidIndex(NeighborIndex))
            {
                OutNeighborRanges.Reset();
                OutFlatNeighborIndices.Reset();
                DWC::Error::SetMessage(OutErrorMessage, TEXT("Precomputed neighbor graph contains an invalid vertex index."));
                return false;
            }

            OutFlatNeighborIndices.Add(NeighborIndex);
        }
    }

    DWC::Error::SetMessage(OutErrorMessage, TEXT(""));
    return true;
}
