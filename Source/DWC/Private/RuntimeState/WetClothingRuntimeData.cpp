#include "RuntimeState/WetClothingRuntimeData.h"

void FWetClothingRuntimeData::ResetWetPartData()
{
    VertexWetPartIDs.Reset();
    VertexWettableFlags.Reset();
    VertexWetnessProfileParameters.Reset();
    VertexWetPartDebugColors.Reset();
}

void FWetClothingRuntimeData::ResetNeighborGraph()
{
    NeighborGraph.Reset();
    bHasNeighborGraph = false;
}

void FWetClothingRuntimeData::ResetBoneOptimizationCache()
{
    BoneOptimizationCache = FWetBoneOptimizationCache();
    bHasBoneOptimizationCache = false;
    BoneOptimizationCacheFallbackReason.Reset();
}
