#include "RuntimeData/WetClothingRuntimeData.h"

void FWetClothingRuntimeData::ResetWetPartData()
{
    VertexWetPartIDs.Reset();
    VertexWetnessProfileParameters.Reset();
    VertexWetPartDebugColors.Reset();
}

void FWetClothingRuntimeData::ResetNeighborGraph()
{
    NeighborGraph.Reset();
}

void FWetClothingRuntimeData::ResetBoneOptimizationCache()
{
    BoneOptimizationCache = FWetBoneOptimizationCache();
    bHasBoneOptimizationCache = false;
}
