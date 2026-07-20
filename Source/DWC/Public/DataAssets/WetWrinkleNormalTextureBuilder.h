#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetClothingWrinkleData.h"
#include "DataAssets/WetWrinkleNormalTextureData.h"

class UTexture2D;

class DWC_API FWetWrinkleNormalTextureBuilder
{
  public:
    static bool BuildTextureBuffers(
        UTexture2D* SourceNormalTexture,
        bool bUseCorrection,
        const FWetWrinkleNormalCorrectionSettings& CorrectionSettings,
        const FWetWrinkleCoverageExtractionSettings& CoverageSettings,
        FWetWrinkleNormalBuildOutput& OutOutput,
        FString& OutError);

    static bool BuildConvexSeparationBuffer(
        UTexture2D* CorrectedNormalTexture,
        const FWetWrinkleCoverageExtractionSettings& Settings,
        FWetWrinkleTextureScalarBuffer& OutBuffer,
        FString& OutError);
};
