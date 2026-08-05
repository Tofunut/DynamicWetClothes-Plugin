#include "WetClothing/Modes/Transparency/Viewport/DWCTransparencyAlphaIncrementalWorker.h"

#include "WetClothing/Foundation/Jobs/DWCEditorCancellationToken.h"
#include "WetClothing/Modes/Transparency/Brush/DWCTransparencyBrushRasterizer.h"
#include "WetClothing/Modes/Transparency/Processing/DWCTransparencyComposite.h"

FDWCEditorWorkerMemoryEstimate FDWCTransparencyAlphaIncrementalWorker::EstimateMemory(
    const TArray<FDWCTransparencyAlphaTilePayload>& SnapshotTiles,
    const TArray<FDWCTransparencyAlphaComposeTileSnapshot>& ComposeTiles,
    const int32 OutputTileCount)
{
    FDWCEditorWorkerMemoryEstimate Estimate;
    for (const FDWCTransparencyAlphaTilePayload& Tile : SnapshotTiles)
    {
        Estimate.SnapshotBytes += Tile.GetAllocatedBytes();
    }
    for (const FDWCTransparencyAlphaComposeTileSnapshot& Tile : ComposeTiles)
    {
        Estimate.SnapshotBytes += Tile.RevealColor.GetAllocatedSize();
        Estimate.SnapshotBytes += Tile.WrinkleSuppression.GetAllocatedSize();
        Estimate.SnapshotBytes += Tile.OuterEdgeFeather.GetAllocatedSize();
    }
    const uint64 FullTilePixels =
        static_cast<uint64>(FMath::Max(OutputTileCount, 0)) *
        FDWCTransparencyAlphaTileStore::TileSize *
        FDWCTransparencyAlphaTileStore::TileSize;
    Estimate.WorkingBytes = FullTilePixels * sizeof(uint8) * 2ull;
    Estimate.OutputBytes = FullTilePixels * (sizeof(uint8) * 2ull + sizeof(FColor));
    return Estimate;
}

TSharedPtr<FDWCTransparencyAlphaIncrementalJobResult, ESPMode::ThreadSafe>
FDWCTransparencyAlphaIncrementalWorker::Build(
    FDWCTransparencyAlphaIncrementalJobInput&& Input,
    const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& CancellationToken)
{
    TSharedPtr<FDWCTransparencyAlphaIncrementalJobResult, ESPMode::ThreadSafe> Output =
        MakeShared<FDWCTransparencyAlphaIncrementalJobResult, ESPMode::ThreadSafe>();
    Output->ExpectedAlphaRevision = Input.ExpectedAlphaRevision;
    Output->PreviewTarget = Input.PreviewTarget;
    if (!Input.AutoResult.IsValid() || CancellationToken->IsCanceled())
    {
        Output->bSucceeded = false;
        Output->Error = TEXT("The transparency alpha incremental snapshot is unavailable.");
        return Output;
    }

    TArray<FDWCTransparencyAlphaTilePayload> WorkingTiles = MoveTemp(Input.SnapshotTiles);
    Output->bHasChanges = FDWCTransparencyBrushRasterizer::RasterizeSamplesToTiles(
            *Input.AutoResult,
            Input.Stroke,
            Input.Samples,
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
        Output->Error = TEXT("The transparency alpha incremental job was canceled.");
        return Output;
    }

    FDWCTransparencyAlphaTileStore ComposedStore;
    ComposedStore.Initialize(Input.AutoResult->Resolution);
    if (!ComposedStore.Commit(ComposedStore.GetRevision(), WorkingTiles))
    {
        Output->bSucceeded = false;
        Output->Error = TEXT("The transparency alpha incremental tile payload is invalid.");
        return Output;
    }

    FDWCTransparencyPixelComposeContext Context;
    Context.AutoResult = Input.AutoResult.Get();
    Context.ManualAlphaTileStore = &ComposedStore;
    Context.VisualizationMode = Input.VisualizationMode;
    Context.TransparencyStrength = Input.TransparencyPreviewStrength;
    Context.WrinkleSuppressionStrength = Input.WrinkleSuppressionStrength;
    Context.MaximumHitDistance = Input.VisualizationMode == EDWCTransparencyVisualizationMode::HitDistance
        ? FDWCTransparencyComposite::ComputeMaximumHitDistance(*Input.AutoResult)
        : KINDA_SMALL_NUMBER;

    Output->AlphaTiles = MoveTemp(WorkingTiles);
    for (const FDWCTransparencyAlphaComposeTileSnapshot& Tile : Input.ComposeTiles)
    {
        if (CancellationToken->IsCanceled())
        {
            Output->bSucceeded = false;
            Output->Error = TEXT("The transparency alpha incremental job was canceled.");
            Output->AlphaTiles.Reset();
            Output->PreviewRegions.Reset();
            return Output;
        }
        const int32 PixelCount = Tile.Rect.Width() * Tile.Rect.Height();
        if (Tile.Rect.IsEmpty() ||
            (!Tile.RevealColor.IsEmpty() && Tile.RevealColor.Num() != PixelCount) ||
            (!Tile.WrinkleSuppression.IsEmpty() && Tile.WrinkleSuppression.Num() != PixelCount) ||
            (!Tile.OuterEdgeFeather.IsEmpty() && Tile.OuterEdgeFeather.Num() != PixelCount))
        {
            Output->bSucceeded = false;
            Output->Error = TEXT("The transparency alpha compose tile is invalid.");
            Output->AlphaTiles.Reset();
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
                const int32 PixelIndex = Y * Input.AutoResult->Resolution.X + X;
                Region.Pixels[LocalIndex] = FDWCTransparencyComposite::ComposeVisualizationPixel(
                    Context,
                    PixelIndex,
                    TOptional<float>(),
                    Tile.RevealColor.IsValidIndex(LocalIndex)
                        ? TOptional<FColor>(Tile.RevealColor[LocalIndex])
                        : TOptional<FColor>(),
                    Tile.WrinkleSuppression.IsValidIndex(LocalIndex)
                        ? TOptional<uint8>(Tile.WrinkleSuppression[LocalIndex])
                        : TOptional<uint8>(),
                    Tile.OuterEdgeFeather.IsValidIndex(LocalIndex)
                        ? TOptional<uint8>(Tile.OuterEdgeFeather[LocalIndex])
                        : TOptional<uint8>());
            }
        }
    }

    Output->ResultBytes = 0;
    for (const FDWCTransparencyAlphaTilePayload& Tile : Output->AlphaTiles)
    {
        Output->ResultBytes += Tile.GetAllocatedBytes();
    }
    for (const FDWCEditorBGRA8RegionPayload& Region : Output->PreviewRegions)
    {
        Output->ResultBytes += Region.Pixels.GetAllocatedSize();
    }
    return Output;
}
