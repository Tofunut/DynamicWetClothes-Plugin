#pragma once

#include "CoreMinimal.h"

class UWetClothingAsset;

struct FWetClothingSurfaceWaterUVGenerationResult
{
    bool bSucceeded = false;
    int32 UVChannelIndex = INDEX_NONE;
    int32 GeneratedSlotCount = 0;
    int32 GeneratedIslandCount = 0;
    double GeneratedTexelsPerCentimeter = 0.0;
    FString Message;
};

class FWetClothingSurfaceWaterUVGenerator
{
public:
    static FWetClothingSurfaceWaterUVGenerationResult Generate(
        UWetClothingAsset* Asset,
        int32 LODIndex = 0,
        int32 SourceUVChannelIndex = 0,
        int32 PreferredUVChannelIndex = 1,
        int32 Resolution = 1024,
        int32 PaddingPixels = 8,
        bool bAllowOverwriteExistingChannel = false,
        double TargetTexelsPerCentimeter = 0.0);
};
