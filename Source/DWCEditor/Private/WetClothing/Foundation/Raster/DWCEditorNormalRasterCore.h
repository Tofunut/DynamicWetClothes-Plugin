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

    static FVector3f BlendAngleCorrected(const FVector3f& BaseNormal, const FVector3f& DetailNormal);
};

