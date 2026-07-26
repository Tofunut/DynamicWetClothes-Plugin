#pragma once

#include "CoreMinimal.h"

class UWetClothingAsset;
struct FWetClothingTransparencyLayerData;
struct FWetClothingTransparencyTargetSurface;

struct FDWCTransparencySourceHitStats
{
    int32 PriorityIndex = INDEX_NONE;
    int32 MaterialSlotIndex = INDEX_NONE;
    FName MaterialSlotName;
    int32 HitCount = 0;
};

struct FDWCTransparencyAutoBakeResult
{
    FGuid LayerGuid;
    int32 MaterialSlotIndex = INDEX_NONE;
    int32 UVChannelIndex = 0;
    int32 LODIndex = 0;
    FIntPoint Resolution = FIntPoint(0, 0);
    FString BuildSignature;
    int32 OuterSampleCount = 0;
    int32 ValidHitCount = 0;
    int32 NoHitCount = 0;
    int32 OverlappedUVPixelCount = 0;
    TArray<FColor> InnerColorBuffer;
    TArray<uint8> AutoAlphaBuffer;
    // Target-slot UV coverage is separate from ray-hit validity. It is used to
    // feather island edges and dilate only outside the target surface.
    TArray<uint8> OuterCoverageBuffer;
    // Editor working data. Used to clip brush edits to the UV island under the
    // cursor so painting across texture seams does not bleed into neighboring
    // islands.
    TArray<int32> OuterIslandIDBuffer;
    TArray<uint8> ValidHitBuffer;
    TArray<float> HitDistanceBuffer;
    TArray<uint8> RayConfidenceBuffer;
    TArray<int16> SourcePriorityBuffer;
    TArray<FDWCTransparencySourceHitStats> SourceStats;

    // A generated result contains pre-final auto alpha. A baked baseline already
    // contains authoring strength, wrinkle suppression, feathering, and padding.
    bool bIsFinalBakedBaseline = false;
    int32 BaselineStrokeCount = 0;
    FGuid BaselineBakeGuid;
};

class FDWCTransparencyAutoMapGenerator
{
  public:
    static bool BuildTargetSurfaceBuffers(
        const UWetClothingAsset& WetClothingAsset,
        const FWetClothingTransparencyTargetSurface& TargetSurface,
        int32 LODIndex,
        FIntPoint Resolution,
        TArray<uint8>& OutCoverageBuffer,
        TArray<int32>& OutIslandIDBuffer,
        int32* OutOuterSampleCount,
        int32* OutOverlappedPixelCount,
        FString& OutErrorMessage);

    static bool GenerateSameMesh(
        const UWetClothingAsset& WetClothingAsset,
        const FWetClothingTransparencyLayerData& Layer,
        FDWCTransparencyAutoBakeResult& OutResult,
        FString& OutSummary,
        TArray<FString>& OutWarnings);

    static bool GenerateBaseRevealColorMap(
        const UWetClothingAsset& WetClothingAsset,
        const FWetClothingTransparencyLayerData& Layer,
        FDWCTransparencyAutoBakeResult& OutResult,
        FString& OutSummary,
        TArray<FString>& OutWarnings);
};
