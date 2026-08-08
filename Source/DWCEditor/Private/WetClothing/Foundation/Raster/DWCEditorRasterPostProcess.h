// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Raster/DWCEditorRasterTypes.h"

class FDWCEditorRasterPostProcess final
{
  public:
    static FIntRect MapRect(
        const FIntRect& SourceRect,
        FIntPoint       SourceSize,
        FIntPoint       DestinationSize);

    static FIntRect MapDestinationRectToSourceReadRect(
        const FIntRect& DestinationRect,
        FIntPoint       SourceSize,
        FIntPoint       DestinationSize);

    static bool DownsampleNormalSurface(
        const FDWCEditorNormalRasterSurface& Source,
        FIntPoint                            DestinationSize,
        FDWCEditorNormalRasterSurface&       OutDestination,
        const FIntRect*                      DestinationRect = nullptr);

    static void EncodeNormalPixels(
        const FDWCEditorNormalRasterSurface& Surface,
        TArray<FColor>&                      InOutPixels,
        const FIntRect*                      Rect = nullptr,
        bool                                 bEncodeCoverageInAlpha = false);

    // Resamples a normal surface directly into encoded pixels. This avoids
    // allocating a full floating-point destination surface for high-resolution
    // preview outputs.
    static bool ResampleAndEncodeNormalPixels(
        const FDWCEditorNormalRasterSurface& Source,
        FIntPoint                            DestinationSize,
        TArray<FColor>&                      InOutPixels,
        const FIntRect*                      DestinationRect = nullptr,
        bool                                 bEncodeCoverageInAlpha = false);

    /** Encodes a compact source region into compact destination-rect pixels. */
    static bool ResampleAndEncodeNormalRegion(
        const FDWCEditorNormalRasterRegion& SourceRegion,
        FIntPoint                           DestinationSize,
        const FIntRect&                     DestinationRect,
        TArray<FColor>&                     OutPixels,
        bool                                bEncodeCoverageInAlpha = false);

    static void EncodeCoveragePixels(
        const FDWCEditorNormalRasterSurface& Surface,
        TArray<uint8>&                       OutPixels);

    static void ClipToMask(
        FDWCEditorNormalRasterSurface& Surface,
        TConstArrayView<uint8>         Mask);

    static void DilateIntoPadding(
        FDWCEditorNormalRasterSurface& Surface,
        TConstArrayView<uint8>         IslandMask,
        int32                          PaddingPixels);
};
