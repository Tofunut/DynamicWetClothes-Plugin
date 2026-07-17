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
    TArray<float> AutoAlphaBuffer;
    TArray<uint8> ValidHitBuffer;
    TArray<float> HitDistanceBuffer;
    TArray<float> RayConfidenceBuffer;
    TArray<int32> SourcePriorityBuffer;
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
