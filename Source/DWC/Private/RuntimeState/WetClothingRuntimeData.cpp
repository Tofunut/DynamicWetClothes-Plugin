#include "RuntimeState/WetClothingRuntimeData.h"

void FWetClothingRuntimeData::ResetWetPartData()
{
    VertexWetPartIDs.Reset();
    VertexWettableFlags.Reset();
    VertexAbsorbedWetnessFlags.Reset();
    VertexSurfaceWaterFlags.Reset();
    VertexWetnessProfileParameters.Reset();
    VertexWetPartDebugColors.Reset();
    SurfaceWaterUVs.Reset();
    SurfaceWaterUVValidFlags.Reset();
    SurfaceWaterMaterialSlotIndices.Reset();
}

void FWetClothingRuntimeData::ResetNeighborGraph()
{
    NeighborRanges.Reset();
    FlatNeighborIndices.Reset();
    NeighborGraph.Reset();
    bHasNeighborGraph = false;
}

void FWetClothingRuntimeData::ResetBoneOptimizationCache()
{
    BoneOptimizationCache = FWetBoneOptimizationCache();
    bHasBoneOptimizationCache = false;
    BoneOptimizationCacheFallbackReason.Reset();
}
