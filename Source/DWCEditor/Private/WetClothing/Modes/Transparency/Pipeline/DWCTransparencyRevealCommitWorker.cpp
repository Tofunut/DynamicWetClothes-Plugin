//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyRevealCommitWorker.h"

#include "WetClothing/Foundation/Jobs/DWCEditorCancellationToken.h"
#include "WetClothing/Modes/Transparency/AutoMap/DWCTransparencyAutoMapGenerator.h"

FDWCEditorWorkerMemoryEstimate FDWCTransparencyRevealCommitWorker::EstimateMemory(
    const FDWCTransparencyRevealCommitJobInput& Input)
{
    FDWCEditorWorkerMemoryEstimate Estimate;
    if (Input.SourceResult.IsValid())
    {
        Estimate.ResidentSharedBytes = Input.SourceResult->InnerColorBuffer.GetAllocatedSize();
        Estimate.OutputBytes = static_cast<uint64>(Input.SourceResult->Resolution.X) *
            Input.SourceResult->Resolution.Y * sizeof(FColor);
    }
    for (const FDWCTransparencyRevealColorTilePayload& Tile : Input.ModifiedTiles)
    {
        Estimate.SnapshotBytes += Tile.GetAllocatedBytes();
    }
    Estimate.SnapshotBytes += Input.ModifiedTiles.GetAllocatedSize();
    Estimate.SnapshotBytes += Input.FallbackStrokes.GetAllocatedSize();
    for (const FDWCTransparencyRevealColorStroke& Stroke : Input.FallbackStrokes)
    {
        Estimate.SnapshotBytes += Stroke.Samples.GetAllocatedSize();
    }
    return Estimate;
}

TSharedPtr<FDWCTransparencyRevealCommitJobResult, ESPMode::ThreadSafe>
FDWCTransparencyRevealCommitWorker::Build(
    FDWCTransparencyRevealCommitJobInput&& Input,
    const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& CancellationToken)
{
    TSharedPtr<FDWCTransparencyRevealCommitJobResult, ESPMode::ThreadSafe> Output =
        MakeShared<FDWCTransparencyRevealCommitJobResult, ESPMode::ThreadSafe>();
    if (!Input.SourceResult.IsValid() || CancellationToken->IsCanceled())
    {
        Output->Error = TEXT("The reveal-color commit source is unavailable or canceled.");
        return Output;
    }

    const int64 PixelCount = static_cast<int64>(Input.SourceResult->Resolution.X) *
        Input.SourceResult->Resolution.Y;
    if (PixelCount <= 0 || Input.SourceResult->InnerColorBuffer.Num() != PixelCount)
    {
        Output->Error = TEXT("The reveal-color source has an invalid resolution or pixel buffer.");
        return Output;
    }

    Output->CorrectedRevealPixels = Input.SourceResult->InnerColorBuffer;
    if (Input.bUseSparseTiles)
    {
        for (const FDWCTransparencyRevealColorTilePayload& Tile : Input.ModifiedTiles)
        {
            if (CancellationToken->IsCanceled())
            {
                Output->Error = TEXT("The reveal-color commit was canceled.");
                return Output;
            }
            if (!Tile.IsValidFor(Input.SourceResult->Resolution,
                                 FDWCTransparencyRevealColorTileStore::TileSize))
            {
                Output->Error = TEXT("The reveal-color working set contains an invalid tile.");
                return Output;
            }
            for (int32 Y = Tile.Rect.Min.Y; Y < Tile.Rect.Max.Y; ++Y)
            {
                const int32 SourceOffset = (Y - Tile.Rect.Min.Y) * Tile.Rect.Width();
                const int32 DestinationOffset = Y * Input.SourceResult->Resolution.X + Tile.Rect.Min.X;
                FMemory::Memcpy(
                    Output->CorrectedRevealPixels.GetData() + DestinationOffset,
                    Tile.Colors.GetData() + SourceOffset,
                    static_cast<SIZE_T>(Tile.Rect.Width()) * sizeof(FColor));
            }
        }
    }
    else
    {
        FDWCTransparencyAutoMapGenerator::ApplyRevealColorPaintStrokes(
            *Input.SourceResult,
            Input.FallbackStrokes,
            Input.SourceResult->MaterialSlotIndex,
            Input.BaseRevealColor,
            Output->CorrectedRevealPixels);
    }

    if (CancellationToken->IsCanceled())
    {
        Output->CorrectedRevealPixels.Reset();
        Output->Error = TEXT("The reveal-color commit was canceled.");
        return Output;
    }
    Output->Resolution = Input.SourceResult->Resolution;
    Output->SourceSignature = Input.SourceResult->BuildSignature;
    Output->bUsedSparseTiles = Input.bUseSparseTiles;
    Output->bSucceeded = true;
    return Output;
}
