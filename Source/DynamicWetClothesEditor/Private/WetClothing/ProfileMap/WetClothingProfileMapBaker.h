#pragma once

#include "CoreMinimal.h"

class UTexture;
class UTexture2D;
class UWetClothingAsset;

struct FWetClothingProfileMapBakeSettings
{
    int32 Resolution = 512;
    int32 PaddingPixels = 4;
};

struct FWetClothingProfileMapBakeResult
{
    TObjectPtr<UTexture2D> ProfileMap0 = nullptr;
    TArray<int32>          MaterialSlotIndices;
    int32                  PaintedPixelCount = 0;
};

class FWetClothingProfileMapBaker
{
  public:
    static bool BakeProfileMap0(
        UWetClothingAsset*                         WetClothingAsset,
        UTexture*                                  SourceTexture,
        int32                                      UVChannelIndex,
        const TArray<int32>&                       MaterialSlotIndices,
        const FWetClothingProfileMapBakeSettings& Settings,
        FWetClothingProfileMapBakeResult&          OutResult,
        FString&                                   OutErrorMessage);
};
