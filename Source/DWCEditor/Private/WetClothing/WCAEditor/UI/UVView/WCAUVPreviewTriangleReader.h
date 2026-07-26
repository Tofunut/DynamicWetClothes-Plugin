#pragma once

#include "CoreMinimal.h"

class USkeletalMesh;
class UWetClothingAsset;

/** Transient triangle source used only to build UV view and hit-test data. */
struct FWCAUVPreviewSourceTriangle
{
    int32 TriangleID = INDEX_NONE;
    int32 MaterialSlotIndex = INDEX_NONE;
    FVector2f UVs[3] = { FVector2f::ZeroVector, FVector2f::ZeroVector, FVector2f::ZeroVector };
    FVector3f LocalPositions[3] = { FVector3f::ZeroVector, FVector3f::ZeroVector, FVector3f::ZeroVector };
};

/** Reads UV preview triangles from persistent mesh-owned UV coordinates. */
class FWCAUVPreviewTriangleReader
{
public:
    static bool ReadFromSkeletalMesh(
        const USkeletalMesh* SkeletalMesh,
        int32 LODIndex,
        int32 UVChannelIndex,
        int32 MaterialSlotIndex,
        TArray<FWCAUVPreviewSourceTriangle>& OutTriangles,
        FString* OutErrorMessage = nullptr);

    /** Reads multiple material slots with one LOD index-buffer traversal. */
    static bool ReadFromSkeletalMesh(
        const USkeletalMesh* SkeletalMesh,
        int32 LODIndex,
        int32 UVChannelIndex,
        TConstArrayView<int32> MaterialSlotIndices,
        TArray<FWCAUVPreviewSourceTriangle>& OutTriangles,
        FString* OutErrorMessage = nullptr);

    static bool ReadFromDataUV(
        const UWetClothingAsset& Asset,
        int32 LODIndex,
        int32 MaterialSlotIndex,
        TArray<FWCAUVPreviewSourceTriangle>& OutTriangles,
        FString* OutErrorMessage = nullptr);
};
