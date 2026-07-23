#pragma once

#include "CoreMinimal.h"

class FDWCTransparencyComposite
{
  public:
    static float ResolveFinalAlpha(
        float EditedAlpha,
        float TransparencyStrength,
        float WrinkleSuppression,
        float WrinkleSuppressionStrength);

    static uint8 ResolveFinalAlpha8(
        float EditedAlpha,
        float TransparencyStrength,
        uint8 WrinkleSuppression,
        float WrinkleSuppressionStrength);

    static bool BuildCoverageEdgeFeatherBuffer(
        FIntPoint Resolution,
        const TArray<uint8>& OuterCoverage,
        float FeatherPixels,
        TArray<uint8>& OutBuffer);
};
