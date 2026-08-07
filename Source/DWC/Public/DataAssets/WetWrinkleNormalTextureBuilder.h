//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetClothingWrinkleData.h"
#include "DataAssets/WetWrinkleNormalTextureData.h"

class UTexture2D;

class DWC_API FWetWrinkleNormalTextureBuilder
{
  public:
    static bool ReadTextureSourcePixels(
        UTexture2D* Texture,
        FWetWrinkleTexturePixelBuffer& OutBuffer,
        FString& OutError);

    static bool BuildTextureBuffers(
        UTexture2D* SourceNormalTexture,
        bool bUseCorrection,
        const FWetWrinkleNormalCorrectionSettings& CorrectionSettings,
        const FWetWrinkleCoverageExtractionSettings& CoverageSettings,
        FWetWrinkleNormalBuildOutput& OutOutput,
        FString& OutError,
        int32 MaxOutputDimension = 0);

    static bool BuildConvexSeparationBuffer(
        UTexture2D* CorrectedNormalTexture,
        const FWetWrinkleCoverageExtractionSettings& Settings,
        FWetWrinkleTextureScalarBuffer& OutBuffer,
        FString& OutError);

    static bool BuildConvexSeparationBufferFromPixels(
        const FWetWrinkleTexturePixelBuffer& CorrectedNormal,
        bool bFlipGreenChannel,
        const FWetWrinkleCoverageExtractionSettings& Settings,
        FWetWrinkleTextureScalarBuffer& OutBuffer,
        FString& OutError);
};
