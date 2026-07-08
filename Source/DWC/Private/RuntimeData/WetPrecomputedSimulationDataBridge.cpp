#include "RuntimeData/WetPrecomputedSimulationDataBridge.h"

#include "DataAssets/WetClothingAsset.h"
#include "Engine/SkeletalMesh.h"
#include "RuntimeData/WetClothingRuntimeData.h"
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
            PrecomputedData.MeshBuildSignature))
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
    const UWetClothingAsset*     WetClothingAsset,
    const USkeletalMesh*         SkeletalMesh,
    const int32                  LODIndex,
    const int32                  VertexCount,
    TArray<FWetVertexNeighbors>& OutNeighborGraph,
    FString*                     OutErrorMessage)
{
    OutNeighborGraph.Reset();

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

    OutNeighborGraph.SetNum(VertexCount);
    for (int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
    {
        for (const int32 NeighborIndex : PrecomputedData.NeighborGraph[VertexIndex].Neighbors)
        {
            if (!OutNeighborGraph.IsValidIndex(NeighborIndex))
            {
                OutNeighborGraph.Reset();
                DWC::Error::SetMessage(OutErrorMessage, TEXT("Precomputed neighbor graph contains an invalid vertex index."));
                return false;
            }
        }

        OutNeighborGraph[VertexIndex].Neighbors = PrecomputedData.NeighborGraph[VertexIndex].Neighbors;
    }

    DWC::Error::SetMessage(OutErrorMessage, TEXT(""));
    return true;
}
