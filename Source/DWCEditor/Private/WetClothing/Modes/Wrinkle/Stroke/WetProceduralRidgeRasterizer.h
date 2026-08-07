//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetClothingWrinkleData.h"
#include "WetClothing/Foundation/Raster/DWCEditorRasterTypes.h"

struct FWetProceduralRidgeRasterResult
{
    bool bAffectedPixels = false;
    bool bCanceled = false;
    FIntRect DirtyRect = FIntRect(0, 0, 0, 0);
};

class FDWCEditorCancellationToken;

class FWetProceduralRidgeRasterizer
{
  public:
    static constexpr int32 ScratchTileSize = 128;

    static FIntRect ComputeBounds(
        const FWetProceduralRidgeStroke& Stroke,
        FIntPoint TextureSize,
        int32 FirstPointIndex = 0);

    static FWetProceduralRidgeRasterResult RasterizeToSurface(
        const FWetProceduralRidgeStroke& Stroke,
        FDWCEditorNormalRasterSurface& Surface,
        const FIntRect* ClipRect = nullptr,
        const FDWCEditorCancellationToken* CancellationToken = nullptr);

    static FWetProceduralRidgeRasterResult RasterizeToRegion(
        const FWetProceduralRidgeStroke& Stroke,
        FDWCEditorNormalRasterRegion& Region,
        const FIntRect* ClipRect = nullptr,
        const FDWCEditorCancellationToken* CancellationToken = nullptr);

    static uint64 GetTransientScratchBytesUpperBound();
};
