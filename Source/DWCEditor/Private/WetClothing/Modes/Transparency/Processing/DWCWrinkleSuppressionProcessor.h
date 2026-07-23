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

    static FString MakeSettingsSignature(
        float CoverageThreshold,
        float MaskSoftness,
        float SuppressionStrength,
        float TransparencyStrength);
};
