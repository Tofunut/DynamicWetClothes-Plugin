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

struct FWetVertexNeighborList
{
    TArray<int32> Neighbors;
};

class DWC_API FWetClothingRuntimeData
{
  public:
    // Identity of this immutable shared payload.
    int32 LODIndex = INDEX_NONE;
    int32 VertexCount = 0;
    int32 DataVersion = 0;
    FString MeshSignature;
    FString SourceDataSignature;

    void ResetWetPartData();
    void ResetNeighborGraph();
    void ResetBoneOptimizationCache();
    uint64 GetAllocatedMemoryBytes() const;

    TArray<int32>                     VertexWetPartIDs; // What Part(included in Material Slot) this vertex stands for 
    TArray<bool>                      VertexWettableFlags;// Is this Vertex Wettable?
    TArray<bool>                      VertexAbsorbedWetnessFlags; // Can This Vertex absorb water?
    TArray<bool>                      VertexSurfaceWaterFlags; // Can This Vertex get Surface Water?
    TArray<FWetnessProfileParameters> VertexWetnessProfileParameters; // What Settings does this Vertex Get? TODO : Maybe this can be compacted... just one pointer or index for ProfileParameter can be effective 
    TArray<FWetVertexNeighborRange>   NeighborRanges; // ??
    TArray<int32>                     FlatNeighborIndices; // ?? 
    TArray<FWetVertexNeighborList>    NeighborGraph; // why does this get still alive?
    TArray<FVector2f>                 SurfaceWaterUVs; // What surface uv does this Vertex use
    TArray<bool>                      SurfaceWaterUVValidFlags; // is UV valid for this?
    TArray<int32>                     SurfaceWaterMaterialSlotIndices; //??
    bool                              bHasNeighborGraph = false; //??

    FWetBoneOptimizationCache BoneOptimizationCache;
    bool                      bHasBoneOptimizationCache = false;
    FString                   BoneOptimizationCacheFallbackReason;

    bool SupportsAbsorbedWetness(int32 VertexIndex) const
    {
        return VertexAbsorbedWetnessFlags.IsValidIndex(VertexIndex) && VertexAbsorbedWetnessFlags[VertexIndex];
    }
    bool SupportsSurfaceWater(int32 VertexIndex) const
    {
        return VertexSurfaceWaterFlags.IsValidIndex(VertexIndex) && VertexSurfaceWaterFlags[VertexIndex];
    }
    bool SupportsWaterContact(int32 VertexIndex) const
    {
        return SupportsAbsorbedWetness(VertexIndex) || SupportsSurfaceWater(VertexIndex);
    }
    bool IsVertexWettable(int32 VertexIndex) const
    {
        return SupportsAbsorbedWetness(VertexIndex);
    }
    bool TryGetSurfaceWaterUV(int32 VertexIndex, FVector2f& OutUV) const
    {
        if (!SurfaceWaterUVs.IsValidIndex(VertexIndex) || !SurfaceWaterUVValidFlags.IsValidIndex(VertexIndex) || !SurfaceWaterUVValidFlags[VertexIndex]) return false;
        OutUV = SurfaceWaterUVs[VertexIndex]; return true;
    }
    bool TryGetSurfaceWaterBinding(int32 VertexIndex, int32& OutMaterialSlotIndex, FVector2f& OutUV) const
    {
        if (!SupportsSurfaceWater(VertexIndex) || !TryGetSurfaceWaterUV(VertexIndex, OutUV) || !SurfaceWaterMaterialSlotIndices.IsValidIndex(VertexIndex)) return false;
        OutMaterialSlotIndex = SurfaceWaterMaterialSlotIndices[VertexIndex];
        return OutMaterialSlotIndex != INDEX_NONE;
    }
};
