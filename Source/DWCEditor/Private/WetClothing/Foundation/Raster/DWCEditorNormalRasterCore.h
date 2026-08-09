//Copyright 2026 Team Tofunut. All Rights Reserved.
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

    static FDWCEditorRasterResult RasterizeProjectedPatch(
        const FDWCEditorProjectedNormalPatchCommand& Command,
        FDWCEditorNormalRasterSurface& Surface,
        const FDWCEditorCancellationToken* CancellationToken = nullptr,
        const FIntRect* ClipRect = nullptr,
        FDWCEditorProjectedRasterDiagnostics* Diagnostics = nullptr);

    static FDWCEditorRasterResult RasterizeProjectedPatchRegion(
        const FDWCEditorProjectedNormalPatchCommand& Command,
        FDWCEditorNormalRasterRegion& Region,
        const FDWCEditorCancellationToken* CancellationToken = nullptr,
        const FIntRect* ClipRect = nullptr,
        FDWCEditorProjectedRasterDiagnostics* Diagnostics = nullptr);

    /** Rasterizes only the indexed fragments while preserving command order. */
    static FDWCEditorRasterResult RasterizeProjectedPatchRegionSubset(
        const FDWCEditorProjectedNormalPatchCommand& Command,
        TConstArrayView<int32> FragmentIndices,
        FDWCEditorNormalRasterRegion& Region,
        const FDWCEditorCancellationToken* CancellationToken = nullptr,
        const FIntRect* ClipRect = nullptr,
        FDWCEditorProjectedRasterDiagnostics* Diagnostics = nullptr);

    static void ComputeStampBounds(
        const FDWCEditorNormalStampCommand& Command,
        FIntPoint CanvasSize,
        TArray<FIntRect>& OutBounds);

    static void ComputeProjectedPatchBounds(
        const FDWCEditorProjectedNormalPatchCommand& Command,
        FIntPoint CanvasSize,
        TArray<FIntRect>& OutBounds);

    static FVector3f BlendAngleCorrected(const FVector3f& BaseNormal, const FVector3f& DetailNormal);
};
