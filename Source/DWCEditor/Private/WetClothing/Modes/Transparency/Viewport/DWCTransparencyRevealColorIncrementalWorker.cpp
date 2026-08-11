//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Modes/Transparency/Viewport/DWCTransparencyRevealColorIncrementalWorker.h"

#include "WetClothing/Foundation/Jobs/DWCEditorCancellationToken.h"
#include "WetClothing/Modes/Transparency/Brush/DWCTransparencyBrushRasterizer.h"
#include "WetClothing/Modes/Transparency/Processing/DWCTransparencyComposite.h"

FDWCEditorWorkerMemoryEstimate FDWCTransparencyRevealColorIncrementalWorker::EstimateMemory(
    const TArray<FDWCTransparencyRevealColorTilePayload>& SnapshotTiles,
    const TArray<FDWCTransparencyRevealColorComposeTileSnapshot>& ComposeTiles,
    const int32 OutputTileCount)
{
    FDWCEditorWorkerMemoryEstimate Estimate;
    for (const FDWCTransparencyRevealColorTilePayload& Tile : SnapshotTiles)
    {
        Estimate.SnapshotBytes += Tile.GetAllocatedBytes();
    }
    for (const FDWCTransparencyRevealColorComposeTileSnapshot& Tile : ComposeTiles)
    {
        Estimate.SnapshotBytes += Tile.ManualPremultiplied.GetAllocatedSize();
        Estimate.SnapshotBytes += Tile.ManualWeight.GetAllocatedSize();
        Estimate.SnapshotBytes += Tile.OuterEdgeFeather.GetAllocatedSize();
    }
    const uint64 FullTilePixels =
        static_cast<uint64>(FMath::Max(OutputTileCount, 0)) *
        FDWCTransparencyRevealColorTileStore::TileSize *
        FDWCTransparencyRevealColorTileStore::TileSize;
    Estimate.WorkingBytes = FullTilePixels * sizeof(FColor);
    Estimate.OutputBytes = FullTilePixels * sizeof(FColor) * 2ull;
    return Estimate;
}

TSharedPtr<FDWCTransparencyRevealColorIncrementalJobResult, ESPMode::ThreadSafe>
FDWCTransparencyRevealColorIncrementalWorker::Build(
    FDWCTransparencyRevealColorIncrementalJobInput&& Input,
    const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& CancellationToken)
{
    TSharedPtr<FDWCTransparencyRevealColorIncrementalJobResult, ESPMode::ThreadSafe> Output =
        MakeShared<FDWCTransparencyRevealColorIncrementalJobResult, ESPMode::ThreadSafe>();
    Output->ExpectedRevealRevision = Input.ExpectedRevealRevision;
    Output->PreviewTarget = Input.PreviewTarget;
    if (!Input.SourcePayload.IsValid() || CancellationToken->IsCanceled())
    {
        Output->bSucceeded = false;
        Output->Error = TEXT("The transparency reveal-color incremental snapshot is unavailable.");
        return Output;
    }

    TArray<FDWCTransparencyRevealColorTilePayload> WorkingTiles = MoveTemp(Input.SnapshotTiles);
    Output->bHasChanges = FDWCTransparencyBrushRasterizer::RasterizeRevealColorSamplesToTiles(
        *Input.SourcePayload,
        Input.Stroke,
        Input.Samples,
        Input.BaseRevealColor,
        Input.OutputTileCoordinates,
        WorkingTiles);
    if (!Output->bHasChanges)
    {
        Output->bSucceeded = true;
        return Output;
    }
    if (CancellationToken->IsCanceled())
    {
        Output->bSucceeded = false;
        Output->Error = TEXT("The transparency reveal-color incremental job was canceled.");
        return Output;
    }

    FDWCTransparencyRevealColorTileStore RevealStore;
    RevealStore.Initialize(Input.SourcePayload->Resolution);
    if (!RevealStore.Commit(
            RevealStore.GetRevision(),
            WorkingTiles,
            MakeArrayView(Input.SourcePayload->InnerColorBuffer)))
    {
        Output->bSucceeded = false;
        Output->Error = TEXT("The transparency reveal-color incremental tile payload is invalid.");
        return Output;
    }

    FDWCTransparencyAlphaTileStore AlphaStore;
    AlphaStore.Initialize(Input.SourcePayload->Resolution);
    TArray<FDWCTransparencyAlphaTilePayload> AlphaPayloads;
    AlphaPayloads.Reserve(Input.ComposeTiles.Num());
    for (const FDWCTransparencyRevealColorComposeTileSnapshot& Tile : Input.ComposeTiles)
    {
        FDWCTransparencyAlphaTilePayload& Payload = AlphaPayloads.AddDefaulted_GetRef();
        Payload.TileCoordinate = Tile.TileCoordinate;
        Payload.Rect = Tile.Rect;
        Payload.Premultiplied = Tile.ManualPremultiplied;
        Payload.Weight = Tile.ManualWeight;
    }
    if (!AlphaPayloads.IsEmpty() && !AlphaStore.Commit(AlphaStore.GetRevision(), AlphaPayloads))
    {
        Output->bSucceeded = false;
        Output->Error = TEXT("The transparency reveal-color alpha compose snapshot is invalid.");
        return Output;
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

    Output->RevealTiles = MoveTemp(WorkingTiles);
    for (const FDWCTransparencyRevealColorComposeTileSnapshot& Tile : Input.ComposeTiles)
    {
        if (CancellationToken->IsCanceled())
        {
            Output->bSucceeded = false;
            Output->Error = TEXT("The transparency reveal-color incremental job was canceled.");
            Output->RevealTiles.Reset();
            Output->PreviewRegions.Reset();
            return Output;
        }
        const int32 PixelCount = Tile.Rect.Width() * Tile.Rect.Height();
        if (Tile.Rect.IsEmpty() || Tile.ManualPremultiplied.Num() != PixelCount ||
            Tile.ManualWeight.Num() != PixelCount ||
            (!Tile.OuterEdgeFeather.IsEmpty() && Tile.OuterEdgeFeather.Num() != PixelCount))
        {
            Output->bSucceeded = false;
            Output->Error = TEXT("The transparency reveal-color compose tile is invalid.");
            Output->RevealTiles.Reset();
            Output->PreviewRegions.Reset();
            return Output;
        }

        FDWCEditorBGRA8RegionPayload& Region = Output->PreviewRegions.AddDefaulted_GetRef();
        Region.Rect = Tile.Rect;
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

    Output->ResultBytes = 0;
    for (const FDWCTransparencyRevealColorTilePayload& Tile : Output->RevealTiles)
    {
        Output->ResultBytes += Tile.GetAllocatedBytes();
    }
    for (const FDWCEditorBGRA8RegionPayload& Region : Output->PreviewRegions)
    {
        Output->ResultBytes += Region.Pixels.GetAllocatedSize();
    }
    return Output;
}
