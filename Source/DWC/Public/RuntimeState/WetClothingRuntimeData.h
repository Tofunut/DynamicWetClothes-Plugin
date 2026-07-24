#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetnessProfile.h"
#include "RuntimeState/WetBoneOptimizationCache.h"

struct FWetnessProfileParameters;


/**
 * Transient CPU-side representative triangle used to convert one projected
 * surface-flow direction into both DWCDataUV and SurfaceWaterNormalUV angles.
 */
struct FSurfaceWaterFlowTriangleBinding
{
    FIntVector VertexIndices = FIntVector(INDEX_NONE, INDEX_NONE, INDEX_NONE);

    // DWCDataUV is used to rotate the actual RT stamp geometry.
    FVector2f DataUV0 = FVector2f::ZeroVector;
    FVector2f DataUV1 = FVector2f::ZeroVector;
    FVector2f DataUV2 = FVector2f::ZeroVector;

    // SurfaceWaterNormalUV is used to orient the repeating rivulet normal.
    FVector2f NormalUV0 = FVector2f::ZeroVector;
    FVector2f NormalUV1 = FVector2f::ZeroVector;
    FVector2f NormalUV2 = FVector2f::ZeroVector;

    bool IsValid() const
    {
        return VertexIndices.X != INDEX_NONE &&
               VertexIndices.Y != INDEX_NONE &&
               VertexIndices.Z != INDEX_NONE;
    }
};

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
    static constexpr uint16 InvalidWetnessProfileIndex = 0xFFFFu;

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

    const FWetnessProfileParameters* GetWetnessProfileParameters(int32 VertexIndex) const
    {
        if (!VertexWetnessProfileIndices.IsValidIndex(VertexIndex))
        {
            return nullptr;
        }

        const uint16 ProfileIndex = VertexWetnessProfileIndices[VertexIndex];
        return ProfileIndex != InvalidWetnessProfileIndex && WetnessProfileTable.IsValidIndex(ProfileIndex)
                   ? &WetnessProfileTable[ProfileIndex]
                   : nullptr;
    }

    TArray<int32>                     VertexWetPartIDs;
    TArray<bool>                      VertexWettableFlags;
    TArray<bool>                      VertexAbsorbedWetnessFlags;
    TArray<bool>                      VertexSurfaceWaterFlags;
    TArray<FWetnessProfileParameters> WetnessProfileTable;
    TArray<uint16>                    VertexWetnessProfileIndices;
    TArray<FWetVertexNeighborRange>   NeighborRanges;
    TArray<int32>                     FlatNeighborIndices;
    TArray<FVector2f>                 SurfaceWaterUVs;
    TArray<bool>                      SurfaceWaterUVValidFlags;
    TArray<int32>                     SurfaceWaterMaterialSlotIndices;
    TArray<FSurfaceWaterFlowTriangleBinding> SurfaceWaterFlowTriangleBindings;
    bool                              bHasNeighborGraph = false;

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

    const FSurfaceWaterFlowTriangleBinding* FindSurfaceWaterFlowTriangleBinding(int32 VertexIndex) const
    {
        return SurfaceWaterFlowTriangleBindings.IsValidIndex(VertexIndex) &&
               SurfaceWaterFlowTriangleBindings[VertexIndex].IsValid()
            ? &SurfaceWaterFlowTriangleBindings[VertexIndex]
            : nullptr;
    }
};
