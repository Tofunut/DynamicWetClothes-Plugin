//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Modes/Transparency/Viewport/DWCTransparencyDirtyTileReplayWorker.h"

#include "WetClothing/Foundation/Jobs/DWCEditorCancellationToken.h"
#include "WetClothing/Modes/Transparency/Brush/DWCTransparencyBrushRasterizer.h"
#include "WetClothing/Modes/Transparency/Processing/DWCTransparencyComposite.h"

namespace
{
    void ComposeAlphaRegions(
        const FDWCTransparencyDirtyTileReplayJobInput& Input,
        const FDWCTransparencyAlphaTileStore& AlphaStore,
        TArray<FDWCEditorBGRA8RegionPayload>& OutRegions)
    {
        FDWCTransparencyPixelComposeContext Context;
        Context.SourcePayload = Input.SourcePayload.Get();
        Context.ManualAlphaTileStore = &AlphaStore;
        Context.VisualizationMode = Input.VisualizationMode;
        Context.RevealMetallicDarkeningStrength = Input.RevealMetallicDarkeningStrength;
        Context.bDeferPresentationToMaterial = true;
        Context.MaximumHitDistance = Input.VisualizationMode == EDWCTransparencyVisualizationMode::HitDistance
            ? FDWCTransparencyComposite::ComputeMaximumHitDistance(*Input.SourcePayload)
            : KINDA_SMALL_NUMBER;

        for (const FDWCTransparencyAlphaComposeTileSnapshot& Tile : Input.AlphaComposeTiles)
        {
            FDWCEditorBGRA8RegionPayload& Region = OutRegions.AddDefaulted_GetRef();
            Region.Rect = Tile.Rect;
            const int32 PixelCount = Tile.Rect.Width() * Tile.Rect.Height();
            Region.Pixels.SetNumUninitialized(PixelCount);
            for (int32 Y = Tile.Rect.Min.Y; Y < Tile.Rect.Max.Y; ++Y)
            {
                for (int32 X = Tile.Rect.Min.X; X < Tile.Rect.Max.X; ++X)
                {
                    const int32 LocalIndex = (Y - Tile.Rect.Min.Y) * Tile.Rect.Width() + X - Tile.Rect.Min.X;
                    const int32 PixelIndex = Y * Input.SourcePayload->Resolution.X + X;
                    Region.Pixels[LocalIndex] = FDWCTransparencyComposite::ComposeVisualizationPixel(
                        Context,
                        PixelIndex,
                        TOptional<float>(),
                        Tile.RevealColor.IsValidIndex(LocalIndex)
                            ? TOptional<FColor>(Tile.RevealColor[LocalIndex])
                            : TOptional<FColor>(),
                        TOptional<uint8>(),
                        Tile.OuterEdgeFeather.IsValidIndex(LocalIndex)
                            ? TOptional<uint8>(Tile.OuterEdgeFeather[LocalIndex])
                            : TOptional<uint8>());
                }
            }
        }
    }

    bool BuildAlphaReplay(
        FDWCTransparencyDirtyTileReplayJobInput& Input,
        FDWCTransparencyDirtyTileReplayJobResult& Output,
        const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& CancellationToken)
    {
        FDWCTransparencyAlphaTileStore WorkingStore;
        WorkingStore.Initialize(Input.SourcePayload->Resolution);
        WorkingStore.SnapshotTiles(Input.DirtyTileCoordinates, Output.AlphaTiles);

        const int32 FirstStroke = FMath::Clamp(Input.BaselineStrokeCount, 0, Input.AlphaStrokes.Num());
        TArray<FDWCTransparencyBrushSample> DecodedSamples;
        for (int32 Index = FirstStroke; Index < Input.AlphaStrokes.Num(); ++Index)
        {
            if (CancellationToken->IsCanceled())
            {
                return false;
            }
            const FDWCTransparencyBrushStroke& Stroke = Input.AlphaStrokes[Index];
            if (!Stroke.bEnabled || Stroke.MaterialSlotIndex != Input.MaterialSlotIndex || !Stroke.HasSamples())
            {
                continue;
            }
            Stroke.DecodeSamples(DecodedSamples);
            FDWCTransparencyBrushRasterizer::RasterizeSamplesToTiles(
                *Input.SourcePayload,
                Stroke,
                DecodedSamples,
                Input.DirtyTileCoordinates,
                Output.AlphaTiles);
        }

        FDWCTransparencyAlphaTileStore FinalStore;
        FinalStore.Initialize(Input.SourcePayload->Resolution);
        if (!FinalStore.Commit(FinalStore.GetRevision(), Output.AlphaTiles))
        {
            Output.Error = TEXT("The replayed transparency alpha tiles are invalid.");
            return false;
        }
        ComposeAlphaRegions(Input, FinalStore, Output.PreviewRegions);
        return true;
    }

    void ComposeRevealRegions(
        const FDWCTransparencyDirtyTileReplayJobInput& Input,
        const FDWCTransparencyRevealColorTileStore& RevealStore,
        TArray<FDWCEditorBGRA8RegionPayload>& OutRegions)
    {
        FDWCTransparencyAlphaTileStore AlphaStore;
        AlphaStore.Initialize(Input.SourcePayload->Resolution);
        TArray<FDWCTransparencyAlphaTilePayload> AlphaPayloads;
        for (const FDWCTransparencyRevealColorComposeTileSnapshot& Tile : Input.RevealComposeTiles)
        {
            FDWCTransparencyAlphaTilePayload& Payload = AlphaPayloads.AddDefaulted_GetRef();
            Payload.TileCoordinate = Tile.TileCoordinate;
            Payload.Rect = Tile.Rect;
            Payload.Premultiplied = Tile.ManualPremultiplied;
            Payload.Weight = Tile.ManualWeight;
        }
        if (!AlphaPayloads.IsEmpty())
        {
            AlphaStore.Commit(AlphaStore.GetRevision(), AlphaPayloads);
        }

        FDWCTransparencyPixelComposeContext Context;
        Context.SourcePayload = Input.SourcePayload.Get();
        Context.RevealColorTileStore = &RevealStore;
        Context.ManualAlphaTileStore = &AlphaStore;
        Context.VisualizationMode = Input.VisualizationMode;
        Context.RevealMetallicDarkeningStrength = Input.RevealMetallicDarkeningStrength;
        Context.bDeferPresentationToMaterial = true;
        Context.MaximumHitDistance = Input.VisualizationMode == EDWCTransparencyVisualizationMode::HitDistance
            ? FDWCTransparencyComposite::ComputeMaximumHitDistance(*Input.SourcePayload)
            : KINDA_SMALL_NUMBER;

        for (const FDWCTransparencyRevealColorComposeTileSnapshot& Tile : Input.RevealComposeTiles)
        {
            FDWCEditorBGRA8RegionPayload& Region = OutRegions.AddDefaulted_GetRef();
            Region.Rect = Tile.Rect;
            const int32 PixelCount = Tile.Rect.Width() * Tile.Rect.Height();
            Region.Pixels.SetNumUninitialized(PixelCount);
            for (int32 Y = Tile.Rect.Min.Y; Y < Tile.Rect.Max.Y; ++Y)
            {
                for (int32 X = Tile.Rect.Min.X; X < Tile.Rect.Max.X; ++X)
                {
                    const int32 LocalIndex = (Y - Tile.Rect.Min.Y) * Tile.Rect.Width() + X - Tile.Rect.Min.X;
                    const int32 PixelIndex = Y * Input.SourcePayload->Resolution.X + X;
                    Region.Pixels[LocalIndex] = FDWCTransparencyComposite::ComposeVisualizationPixel(
                        Context,
                        PixelIndex,
                        TOptional<float>(),
                        TOptional<FColor>(),
                        TOptional<uint8>(),
                        Tile.OuterEdgeFeather.IsValidIndex(LocalIndex)
                            ? TOptional<uint8>(Tile.OuterEdgeFeather[LocalIndex])
                            : TOptional<uint8>());
                }
            }
        }
    }

    bool BuildRevealReplay(
        FDWCTransparencyDirtyTileReplayJobInput& Input,
        FDWCTransparencyDirtyTileReplayJobResult& Output,
        const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& CancellationToken)
    {
        FDWCTransparencyRevealColorTileStore WorkingStore;
        WorkingStore.Initialize(Input.SourcePayload->Resolution);
        WorkingStore.SnapshotTiles(
            Input.DirtyTileCoordinates,
            MakeArrayView(Input.SourcePayload->InnerColorBuffer),
            Output.RevealColorTiles);

        TArray<FDWCTransparencyBrushSample> DecodedSamples;
        for (const FDWCTransparencyRevealColorStroke& Stroke : Input.RevealColorStrokes)
        {
            if (CancellationToken->IsCanceled())
            {
                return false;
            }
            if (!Stroke.bEnabled || Stroke.MaterialSlotIndex != Input.MaterialSlotIndex || !Stroke.HasSamples())
            {
                continue;
            }
            Stroke.DecodeSamples(DecodedSamples);
            FDWCTransparencyBrushRasterizer::RasterizeRevealColorSamplesToTiles(
                *Input.SourcePayload,
                Stroke,
                DecodedSamples,
                Input.BaseRevealColor,
                Input.DirtyTileCoordinates,
                Output.RevealColorTiles);
        }

        FDWCTransparencyRevealColorTileStore FinalStore;
        FinalStore.Initialize(Input.SourcePayload->Resolution);
        if (!FinalStore.Commit(
                FinalStore.GetRevision(),
                Output.RevealColorTiles,
                MakeArrayView(Input.SourcePayload->InnerColorBuffer)))
        {
            Output.Error = TEXT("The replayed transparency reveal-color tiles are invalid.");
            return false;
        }
        ComposeRevealRegions(Input, FinalStore, Output.PreviewRegions);
        return true;
    }
}

FDWCEditorWorkerMemoryEstimate FDWCTransparencyDirtyTileReplayWorker::EstimateMemory(
    const FDWCTransparencyDirtyTileReplayJobInput& Input)
{
    FDWCEditorWorkerMemoryEstimate Estimate;
    const uint64 TilePixels = static_cast<uint64>(Input.DirtyTileCoordinates.Num()) *
        FDWCTransparencyAlphaTileStore::TileSize * FDWCTransparencyAlphaTileStore::TileSize;
    Estimate.WorkingBytes = Input.Target == EDWCTransparencyDirtyReplayTarget::Alpha
        ? TilePixels * 2ull
        : TilePixels * sizeof(FColor);
    Estimate.OutputBytes = TilePixels * (sizeof(FColor) +
        (Input.Target == EDWCTransparencyDirtyReplayTarget::Alpha ? 2ull : sizeof(FColor)));
    for (const FDWCTransparencyBrushStroke& Stroke : Input.AlphaStrokes)
    {
        Estimate.SnapshotBytes += Stroke.GetSampleAllocatedSize();
    }
    for (const FDWCTransparencyRevealColorStroke& Stroke : Input.RevealColorStrokes)
    {
        Estimate.SnapshotBytes += Stroke.GetSampleAllocatedSize();
    }
    return Estimate;
}

TSharedPtr<FDWCTransparencyDirtyTileReplayJobResult, ESPMode::ThreadSafe>
FDWCTransparencyDirtyTileReplayWorker::Build(
    FDWCTransparencyDirtyTileReplayJobInput&& Input,
    const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& CancellationToken)
{
    TSharedPtr<FDWCTransparencyDirtyTileReplayJobResult, ESPMode::ThreadSafe> Output =
        MakeShared<FDWCTransparencyDirtyTileReplayJobResult, ESPMode::ThreadSafe>();
    Output->Target = Input.Target;
    Output->ExpectedStoreRevision = Input.ExpectedStoreRevision;
    Output->PreviewTarget = Input.PreviewTarget;
    if (!Input.SourcePayload.IsValid() || Input.DirtyTileCoordinates.IsEmpty() || CancellationToken->IsCanceled())
    {
        Output->bSucceeded = false;
        Output->Error = TEXT("The transparency dirty-tile replay snapshot is unavailable.");
        return Output;
    }

    const bool bBuilt = Input.Target == EDWCTransparencyDirtyReplayTarget::Alpha
        ? BuildAlphaReplay(Input, *Output, CancellationToken)
        : BuildRevealReplay(Input, *Output, CancellationToken);
    Output->bSucceeded = bBuilt && !CancellationToken->IsCanceled();
    if (!Output->bSucceeded && Output->Error.IsEmpty())
    {
        Output->Error = TEXT("The transparency dirty-tile replay was canceled.");
    }
    for (const FDWCTransparencyAlphaTilePayload& Tile : Output->AlphaTiles)
    {
        Output->ResultBytes += Tile.GetAllocatedBytes();
    }
    for (const FDWCTransparencyRevealColorTilePayload& Tile : Output->RevealColorTiles)
    {
        Output->ResultBytes += Tile.GetAllocatedBytes();
    }
    for (const FDWCEditorBGRA8RegionPayload& Region : Output->PreviewRegions)
    {
        Output->ResultBytes += Region.Pixels.GetAllocatedSize();
    }
    return Output;
}
