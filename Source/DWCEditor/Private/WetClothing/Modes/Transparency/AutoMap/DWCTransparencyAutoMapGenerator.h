#pragma once

#include "CoreMinimal.h"

class UWetClothingAsset;
struct FWetClothingTransparencyLayerData;

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
    TArray<uint8> ValidHitBuffer;
    TArray<float> HitDistanceBuffer;
    TArray<uint8> RayConfidenceBuffer;
    TArray<int16> SourcePriorityBuffer;
    TArray<FDWCTransparencySourceHitStats> SourceStats;
};

class FDWCTransparencyAutoMapGenerator
{
  public:
    static bool GenerateSameMesh(
        const UWetClothingAsset& WetClothingAsset,
        const FWetClothingTransparencyLayerData& Layer,
        FDWCTransparencyAutoBakeResult& OutResult,
        FString& OutSummary,
        TArray<FString>& OutWarnings);
};
