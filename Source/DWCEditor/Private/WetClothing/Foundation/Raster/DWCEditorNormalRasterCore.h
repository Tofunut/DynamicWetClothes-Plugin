#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Raster/DWCEditorRasterTypes.h"

class FDWCEditorNormalRasterCore final
{
  public:
    static FDWCEditorRasterResult RasterizeStamp(
        const FDWCEditorNormalStampCommand& Command,
        FDWCEditorNormalRasterSurface& Surface,
        const FDWCEditorCancellationToken* CancellationToken = nullptr,
        const FIntRect* ClipRect = nullptr);

    static FDWCEditorRasterResult RasterizeStampRegion(
        const FDWCEditorNormalStampCommand& Command,
        FDWCEditorNormalRasterRegion& Region,
        const FDWCEditorCancellationToken* CancellationToken = nullptr,
        const FIntRect* ClipRect = nullptr);

    static void ComputeStampBounds(
        const FDWCEditorNormalStampCommand& Command,
        FIntPoint CanvasSize,
        TArray<FIntRect>& OutBounds);

    static FVector3f BlendAngleCorrected(const FVector3f& BaseNormal, const FVector3f& DetailNormal);
};
