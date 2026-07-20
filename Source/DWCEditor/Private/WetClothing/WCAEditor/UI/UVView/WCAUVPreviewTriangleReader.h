#pragma once

#include "CoreMinimal.h"

class USkeletalMesh;
class UWetClothingAsset;

/** Transient triangle source used only to build UV view and hit-test data. */
struct FWCAUVPreviewSourceTriangle
{
    int32 TriangleID = INDEX_NONE;
    int32 MaterialSlotIndex = INDEX_NONE;
    FVector2D UVs[3] = { FVector2D::ZeroVector, FVector2D::ZeroVector, FVector2D::ZeroVector };
    FVector LocalPositions[3] = { FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector };
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

    static bool ReadFromDataUV(
        const UWetClothingAsset& Asset,
        int32 LODIndex,
        int32 MaterialSlotIndex,
        TArray<FWCAUVPreviewSourceTriangle>& OutTriangles,
        FString* OutErrorMessage = nullptr);
};
