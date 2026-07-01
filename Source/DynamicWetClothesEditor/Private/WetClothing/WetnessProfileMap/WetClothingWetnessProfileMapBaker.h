#pragma once

#include "CoreMinimal.h"

class UTexture;
class UTexture2D;
class UWetClothingAsset;

struct FWetClothingWetnessProfileMapBakeSettings
{
    int32 Resolution = 512;
    int32 PaddingPixels = 4;
};

struct FWetClothingWetnessProfileMapBakeResult
{
    TObjectPtr<UTexture2D> WetnessProfileMap0 = nullptr;
    TArray<int32>          MaterialSlotIndices;
    int32                  PaintedPixelCount = 0;
};

class FWetClothingWetnessProfileMapBaker
{
  public:
    static bool BakeWetnessProfileMap0(
        UWetClothingAsset*                         WetClothingAsset,
        UTexture*                                  SourceTexture,
        int32                                      UVChannelIndex,
        const TArray<int32>&                       MaterialSlotIndices,
        const FWetClothingWetnessProfileMapBakeSettings& Settings,
        FWetClothingWetnessProfileMapBakeResult&          OutResult,
        FString&                                   OutErrorMessage);
};
