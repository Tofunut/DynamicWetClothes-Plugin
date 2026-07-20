#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetClothingWrinkleData.h"

struct FWetProceduralRidgeRasterResult
{
    bool bAffectedPixels = false;
    FIntRect DirtyRect = FIntRect(0, 0, 0, 0);
};

class FWetProceduralRidgeRasterizer
{
  public:
    static FIntRect ComputeBounds(
        const FWetProceduralRidgeStroke& Stroke,
        FIntPoint TextureSize,
        int32 FirstPointIndex = 0);

    static FWetProceduralRidgeRasterResult Rasterize(
        const FWetProceduralRidgeStroke& Stroke,
        FIntPoint TextureSize,
        TArray<FColor>& InOutPixels,
        const FIntRect* ClipRect = nullptr,
        bool bBlendWithExisting = true);

    static FWetProceduralRidgeRasterResult RasterizeToNormalCoverageBuffers(
        const FWetProceduralRidgeStroke& Stroke,
        FIntPoint TextureSize,
        TArray<FVector>& InOutNormalBuffer,
        TArray<float>& InOutCoverageBuffer,
        const FIntRect* ClipRect = nullptr);
};
