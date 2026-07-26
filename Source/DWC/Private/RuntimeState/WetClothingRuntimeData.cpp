#include "RuntimeState/WetClothingRuntimeData.h"

uint64 FWetClothingRuntimeData::GetAllocatedMemoryBytes() const
{
    uint64 Bytes = sizeof(*this);
    Bytes += MeshSignature.GetAllocatedSize();
    Bytes += SourceDataSignature.GetAllocatedSize();
    Bytes += VertexWetPartIDs.GetAllocatedSize();
    Bytes += VertexWettableFlags.GetAllocatedSize();
    Bytes += VertexAbsorbedWetnessFlags.GetAllocatedSize();
    Bytes += WetnessProfileTable.GetAllocatedSize();
    Bytes += VertexWetnessProfileIndices.GetAllocatedSize();
    Bytes += NeighborRanges.GetAllocatedSize();
    Bytes += FlatNeighborIndices.GetAllocatedSize();
    Bytes += BoneOptimizationCacheFallbackReason.GetAllocatedSize();

    Bytes += BoneOptimizationCache.PrimaryVertexCache.BoneStartOffsets.GetAllocatedSize();
    Bytes += BoneOptimizationCache.PrimaryVertexCache.FlatVertexIndices.GetAllocatedSize();
    Bytes += BoneOptimizationCache.ResolvedIncludeRules.GetAllocatedSize();
    for (const FWetResolvedBoneIncludeRule& Rule : BoneOptimizationCache.ResolvedIncludeRules)
    {
        Bytes += Rule.IncludedBoneIndices.GetAllocatedSize();
    }

    return Bytes;
}

void FWetClothingRuntimeData::ResetWetPartData()
{
    VertexWetPartIDs.Reset();
    VertexWettableFlags.Reset();
    VertexAbsorbedWetnessFlags.Reset();
    WetnessProfileTable.Reset();
    VertexWetnessProfileIndices.Reset();
}

void FWetClothingRuntimeData::ResetNeighborGraph()
{
    NeighborRanges.Reset();
    FlatNeighborIndices.Reset();
    bHasNeighborGraph = false;
}

void FWetClothingRuntimeData::ResetBoneOptimizationCache()
{
    BoneOptimizationCache = FWetBoneOptimizationCache();
    bHasBoneOptimizationCache = false;
    BoneOptimizationCacheFallbackReason.Reset();
}
