//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetClothingTransparencyData.h"
#include "WetClothing/Modes/Transparency/Brush/DWCTransparencyAlphaTileStore.h"
#include "WetClothing/Modes/Transparency/Brush/DWCTransparencyRevealColorTileStore.h"

struct FDWCTransparencySourcePayload;
struct FDWCTransparencyAlphaDomainSnapshot;

/** Non-owning alpha-only view used by Stage 4 brush workers. */
struct FDWCTransparencyAlphaRasterContext
{
    FIntPoint Resolution = FIntPoint::ZeroValue;
    int32 MaterialSlotIndex = INDEX_NONE;
    TConstArrayView<uint8> BaseAlpha;
    TConstArrayView<uint8> OuterCoverage;
    TConstArrayView<uint16> OuterIslandIDs;

    static FDWCTransparencyAlphaRasterContext FromSourcePayload(
        const FDWCTransparencySourcePayload& SourcePayload);
    static FDWCTransparencyAlphaRasterContext FromAlphaDomain(
        const FDWCTransparencyAlphaDomainSnapshot& AlphaDomain);
    bool IsValid() const;
    int32 ResolveOuterIslandIDAtUV(
        const FVector2D& PositionUV,
        int32 FallbackUVIslandID,
        bool bWrap) const;
    bool PassesIslandClip(int32 PixelIndex, int32 UVIslandID) const;
};

class FDWCTransparencyBrushRasterizer
{
  public:
    static void BuildSampleRegions(
        const FDWCTransparencyBrushSample& Sample,
        FIntPoint Resolution,
        EDWCTransparencyUVAddressMode AddressMode,
        TArray<FIntRect>& OutRegions);

    static bool RasterizeSamplesToTiles(
        const FDWCTransparencySourcePayload& SourcePayload,
        const FDWCTransparencyBrushStroke& Stroke,
        const TArray<FDWCTransparencyBrushSample>& Samples,
        const TArray<FIntPoint>& OutputTileCoordinates,
        TArray<FDWCTransparencyAlphaTilePayload>& InOutTilePayloads);

    static bool RasterizeSamplesToTiles(
        const FDWCTransparencyAlphaRasterContext& AlphaContext,
        const FDWCTransparencyBrushStroke& Stroke,
        const TArray<FDWCTransparencyBrushSample>& Samples,
        const TArray<FIntPoint>& OutputTileCoordinates,
        TArray<FDWCTransparencyAlphaTilePayload>& InOutTilePayloads);

    static bool RasterizeRevealColorSamplesToTiles(
        const FDWCTransparencySourcePayload& SourcePayload,
        const FDWCTransparencyRevealColorStroke& Stroke,
        const TArray<FDWCTransparencyBrushSample>& Samples,
        const FLinearColor& BaseRevealColor,
        const TArray<FIntPoint>& OutputTileCoordinates,
        TArray<FDWCTransparencyRevealColorTilePayload>& InOutTilePayloads);

    static void RebuildFromStrokes(
        const FDWCTransparencySourcePayload& SourcePayload,
        const TArray<FDWCTransparencyBrushStroke>& Strokes,
        int32 BaselineStrokeCount,
        int32 MaterialSlotIndex,
        int32 UVChannelIndex,
        TArray<uint8>& OutManualPremultipliedBuffer,
        TArray<uint8>& OutManualWeightBuffer);

    static void RebuildFromStrokes(
        const FDWCTransparencyAlphaRasterContext& AlphaContext,
        const TArray<FDWCTransparencyBrushStroke>& Strokes,
        int32 BaselineStrokeCount,
        int32 MaterialSlotIndex,
        int32 UVChannelIndex,
        TArray<uint8>& OutManualPremultipliedBuffer,
        TArray<uint8>& OutManualWeightBuffer);

    static float ResolveEditedAlpha(
        const FDWCTransparencySourcePayload& SourcePayload,
        const TArray<uint8>& ManualPremultipliedBuffer,
        const TArray<uint8>& ManualWeightBuffer,
        int32 PixelIndex);

    static float ResolveEditedAlpha(
        const FDWCTransparencySourcePayload& SourcePayload,
        const FDWCTransparencyAlphaTileStore& TileStore,
        int32 PixelIndex);

    static float ResolveEditedAlpha(
        const FDWCTransparencyAlphaRasterContext& AlphaContext,
        const FDWCTransparencyAlphaTileStore& TileStore,
        int32 PixelIndex);
};
