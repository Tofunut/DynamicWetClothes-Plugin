//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetClothingTransparencyData.h"
#include "WetClothing/Modes/Transparency/Brush/DWCTransparencyAlphaTileStore.h"
#include "WetClothing/Modes/Transparency/Brush/DWCTransparencyRevealColorTileStore.h"

struct FDWCTransparencyAutoBakeResult;

class FDWCTransparencyBrushRasterizer
{
  public:
    static void BuildSampleRegions(
        const FDWCTransparencyBrushSample& Sample,
        FIntPoint Resolution,
        EDWCTransparencyUVAddressMode AddressMode,
        TArray<FIntRect>& OutRegions);

    static bool RasterizeSamplesToTiles(
        const FDWCTransparencyAutoBakeResult& AutoResult,
        const FDWCTransparencyBrushStroke& Stroke,
        const TArray<FDWCTransparencyBrushSample>& Samples,
        const TArray<FIntPoint>& OutputTileCoordinates,
        TArray<FDWCTransparencyAlphaTilePayload>& InOutTilePayloads);

    static bool RasterizeRevealColorSamplesToTiles(
        const FDWCTransparencyAutoBakeResult& AutoResult,
        const FDWCTransparencyRevealColorStroke& Stroke,
        const TArray<FDWCTransparencyBrushSample>& Samples,
        const FLinearColor& BaseRevealColor,
        const TArray<FIntPoint>& OutputTileCoordinates,
        TArray<FDWCTransparencyRevealColorTilePayload>& InOutTilePayloads);

    static void RebuildFromStrokes(
        const FDWCTransparencyAutoBakeResult& AutoResult,
        const TArray<FDWCTransparencyBrushStroke>& Strokes,
        int32 BaselineStrokeCount,
        int32 MaterialSlotIndex,
        int32 UVChannelIndex,
        TArray<uint8>& OutManualPremultipliedBuffer,
        TArray<uint8>& OutManualWeightBuffer);

    static float ResolveEditedAlpha(
        const FDWCTransparencyAutoBakeResult& AutoResult,
        const TArray<uint8>& ManualPremultipliedBuffer,
        const TArray<uint8>& ManualWeightBuffer,
        int32 PixelIndex);

    static float ResolveEditedAlpha(
        const FDWCTransparencyAutoBakeResult& AutoResult,
        const FDWCTransparencyAlphaTileStore& TileStore,
        int32 PixelIndex);
};
