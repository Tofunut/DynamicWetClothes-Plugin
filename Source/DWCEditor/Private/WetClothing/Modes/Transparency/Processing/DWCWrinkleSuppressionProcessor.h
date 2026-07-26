#pragma once

#include "CoreMinimal.h"

class UTexture2D;
class UWetClothingAsset;
struct FWetWrinkleBakedMapSet;

struct FDWCWrinkleSuppressionSource
{
    const FWetWrinkleBakedMapSet* BakedMap = nullptr;
    UTexture2D* MaskTexture = nullptr;

    bool IsValid() const
    {
        return BakedMap != nullptr && MaskTexture != nullptr;
    }
};

class FDWCWrinkleSuppressionProcessor
{
  public:
    static FDWCWrinkleSuppressionSource FindExactSource(
        const UWetClothingAsset* WetClothingAsset,
        int32 MaterialSlotIndex,
        int32 UVChannelIndex,
        int32 LODIndex);

    static bool BuildProcessedBuffer(
        const FDWCWrinkleSuppressionSource& Source,
        FIntPoint OutputSize,
        float CoverageThreshold,
        float MaskSoftness,
        TArray<uint8>& OutBuffer,
        FString& OutErrorMessage);

    // The resampled coverage is independent from threshold and softness. Preview
    // callers can retain it while tuning those settings instead of resampling the
    // baked wrinkle mask every time.
    static bool BuildResampledCoverageBuffer(
        const FDWCWrinkleSuppressionSource& Source,
        FIntPoint OutputSize,
        TArray<uint16>& OutCoverage,
        FString& OutErrorMessage);

    static bool BuildProcessedBufferFromCoverage(
        const TArray<uint16>& Coverage,
        float CoverageThreshold,
        float MaskSoftness,
        TArray<uint8>& OutBuffer,
        FString& OutErrorMessage);

    static FString MakeSettingsSignature(
        float CoverageThreshold,
        float MaskSoftness,
        float SuppressionStrength,
        float TransparencyStrength);
};
