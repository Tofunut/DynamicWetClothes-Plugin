//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Modes/Transparency/Viewport/DWCTransparencyVisualizationWorker.h"

#include "WetClothing/Foundation/Jobs/DWCEditorCancellationToken.h"
#include "WetClothing/Modes/Transparency/Processing/DWCTransparencyComposite.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyAlphaSnapshotMaterializer.h"

TSharedPtr<FDWCTransparencyVisualizationJobResult, ESPMode::ThreadSafe>
FDWCTransparencyVisualizationWorker::Build(
    FDWCTransparencyVisualizationJobInput&& Input,
    const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& CancellationToken)
{
    TSharedPtr<FDWCTransparencyVisualizationJobResult, ESPMode::ThreadSafe> Output =
        MakeShared<FDWCTransparencyVisualizationJobResult, ESPMode::ThreadSafe>();
    if (!Input.SourcePayload.IsValid())
    {
        Output->bSucceeded = false;
        Output->Error = TEXT("The transparency visualization snapshot is missing its auto-bake result.");
        return Output;
    }
    const FDWCTransparencySourcePayload& Result = *Input.SourcePayload;
    Output->Resolution = Result.Resolution;
    const int32 PixelCount = Result.Resolution.X * Result.Resolution.Y;
    if (Result.Resolution.X <= 0 || Result.Resolution.Y <= 0 ||
        Result.InnerColorBuffer.Num() != PixelCount || Result.AutoAlphaBuffer.Num() != PixelCount)
    {
        Output->bSucceeded = false;
        Output->Error = TEXT("The transparency visualization snapshot is invalid.");
        return Output;
    }

    const bool bMaterializedFallback =
        Input.AlphaSnapshot.Mode == EDWCTransparencyAlphaSnapshotMode::StrokeReplay;
    FDWCTransparencyAlphaWorkingSnapshot SparseAlphaSnapshot;
    FString AlphaError;
    if (!FDWCTransparencyAlphaSnapshotMaterializer::Materialize(
            Result,
            MoveTemp(Input.AlphaSnapshot),
            SparseAlphaSnapshot,
            AlphaError,
            &CancellationToken.Get()))
    {
        Output->bSucceeded = false;
        Output->Error = AlphaError;
        return Output;
    }
    FDWCTransparencyAlphaSnapshotView AlphaView;
    if (!AlphaView.Initialize(SparseAlphaSnapshot, &AlphaError))
    {
        Output->bSucceeded = false;
        Output->Error = AlphaError;
        return Output;
    }

    if (Input.bRebuildRevealColorFromStrokes)
    {
        TArray<FColor> DenseRevealColor = Result.InnerColorBuffer;
        FDWCTransparencyAutoMapGenerator::ApplyRevealColorPaintStrokes(
            Result,
            Input.RevealColorPaintStrokes,
            Input.MaterialSlotIndex,
            Input.BaseRevealColor,
            DenseRevealColor);
        if (DenseRevealColor.Num() != PixelCount)
        {
            Output->bSucceeded = false;
            Output->Error = TEXT("The transparency reveal-color rebuild produced an invalid buffer.");
            return Output;
        }
        Input.RevealColorTileStore.Initialize(Result.Resolution);
        Input.RevealColorTileStore.BuildFromDense(
            MakeArrayView(DenseRevealColor),
            MakeArrayView(Result.InnerColorBuffer));
        Output->bIncludesRebuiltRevealColor = true;
    }
    else if (!Input.RevealColorTileStore.IsValid())
    {
        Input.RevealColorTileStore.Initialize(Result.Resolution);
    }

    FDWCTransparencyPixelComposeContext Context;
    Context.SourcePayload = &Result;
    Context.RevealColorTileStore = &Input.RevealColorTileStore;
    Context.AlphaSnapshotView = &AlphaView;
    Context.OuterEdgeFeatherBuffer = MakeArrayView(Input.OuterEdgeFeatherBuffer);
    Context.VisualizationMode = Input.VisualizationMode;
    Context.bDeferPresentationToMaterial = true;
    Context.MaximumHitDistance = Input.VisualizationMode == EDWCTransparencyVisualizationMode::HitDistance
        ? FDWCTransparencyComposite::ComputeMaximumHitDistance(Result)
        : KINDA_SMALL_NUMBER;
    if (!FDWCTransparencyComposite::ComposeVisualizationPixels(
            Context,
            Output->Pixels,
            &CancellationToken.Get()))
    {
        Output->bSucceeded = false;
        Output->Error = CancellationToken->IsCanceled()
            ? TEXT("The transparency visualization job was canceled.")
            : TEXT("The transparency visualization snapshot is invalid.");
        return Output;
    }
    // Compose must read the rebuilt working buffers first. Transfer ownership
    // only after composition so the worker result does not duplicate them.
    if (bMaterializedFallback)
    {
        Output->MaterializedAlphaSnapshot = MoveTemp(SparseAlphaSnapshot);
        Output->bIncludesMaterializedAlphaSnapshot = true;
    }
    if (Output->bIncludesRebuiltRevealColor)
    {
        Output->RebuiltRevealColorTileStore = MoveTemp(Input.RevealColorTileStore);
    }
    Output->ResultBytes =
        static_cast<uint64>(Output->Pixels.GetAllocatedSize()) +
        Output->MaterializedAlphaSnapshot.GetAllocatedBytes() +
        Output->RebuiltRevealColorTileStore.GetAllocatedBytes();
    return Output;
}
