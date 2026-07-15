#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetWrinklePreset.h"

class UWetWrinklePreset;

class DWC_API FWetWrinklePresetBuilder
{
  public:
    static bool BuildPresetBuffers(
        const UWetWrinklePreset* Preset,
        FWetWrinklePresetBuildOutput& OutOutput,
        FString& OutError);

    static bool BuildConvexSeparationBuffer(
        UTexture2D* CorrectedNormalTexture,
        const FWetWrinklePresetSeparationSettings& Settings,
        FWetWrinklePresetScalarBuffer& OutBuffer,
        FString& OutError);

    static FString MakeBuildSignature(const UWetWrinklePreset* Preset);
};
