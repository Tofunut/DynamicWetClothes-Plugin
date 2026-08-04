#include "WetClothing/Modes/Transparency/Viewport/DWCTransparencyVisualizationWorker.h"

#include "WetClothing/Foundation/Jobs/DWCEditorCancellationToken.h"
#include "WetClothing/Modes/Transparency/Brush/DWCTransparencyBrushRasterizer.h"
#include "WetClothing/Modes/Transparency/Processing/DWCTransparencyComposite.h"

TSharedPtr<FDWCTransparencyVisualizationJobResult, ESPMode::ThreadSafe>
FDWCTransparencyVisualizationWorker::Build(
    FDWCTransparencyVisualizationJobInput&& Input,
    const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& CancellationToken)
{
    TSharedPtr<FDWCTransparencyVisualizationJobResult, ESPMode::ThreadSafe> Output =
        MakeShared<FDWCTransparencyVisualizationJobResult, ESPMode::ThreadSafe>();
    if (!Input.AutoResult.IsValid())
    {
        Output->bSucceeded = false;
        Output->Error = TEXT("The transparency visualization snapshot is missing its auto-bake result.");
        return Output;
    }
    const FDWCTransparencyAutoBakeResult& Result = *Input.AutoResult;
    Output->Resolution = Result.Resolution;
    const int32 PixelCount = Result.Resolution.X * Result.Resolution.Y;
    if (Result.Resolution.X <= 0 || Result.Resolution.Y <= 0 ||
        Result.InnerColorBuffer.Num() != PixelCount || Result.AutoAlphaBuffer.Num() != PixelCount)
    {
        Output->bSucceeded = false;
        Output->Error = TEXT("The transparency visualization snapshot is invalid.");
        return Output;
    }

    if (Input.bRebuildManualOverridesFromStrokes)
    {
        if (Input.ManualPremultipliedBuffer.Num() != PixelCount)
        {
            Input.ManualPremultipliedBuffer.Init(0, PixelCount);
        }
        if (Input.ManualWeightBuffer.Num() != PixelCount)
        {
            Input.ManualWeightBuffer.Init(0, PixelCount);
        }
        FDWCTransparencyBrushRasterizer::RebuildFromStrokes(
            Result,
            Input.EditableStrokes,
            Input.BaselineStrokeCount,
            Input.MaterialSlotIndex,
            Input.UVChannelIndex,
            Input.ManualPremultipliedBuffer,
            Input.ManualWeightBuffer);
        if (Input.ManualPremultipliedBuffer.Num() != PixelCount ||
            Input.ManualWeightBuffer.Num() != PixelCount)
        {
            Output->bSucceeded = false;
            Output->Error = TEXT("The transparency manual-override rebuild produced an invalid buffer.");
            return Output;
        }
        Output->bIncludesRebuiltManualOverrides = true;
    }

    if (Input.bRebuildRevealColorFromStrokes)
    {
        Input.RevealColorBuffer = Result.InnerColorBuffer;
        FDWCTransparencyAutoMapGenerator::ApplyRevealColorPaintStrokes(
            Result,
            Input.RevealColorPaintStrokes,
            Input.MaterialSlotIndex,
            Input.BaseRevealColor,
            Input.RevealColorBuffer);
        if (Input.RevealColorBuffer.Num() != PixelCount)
        {
            Output->bSucceeded = false;
            Output->Error = TEXT("The transparency reveal-color rebuild produced an invalid buffer.");
            return Output;
        }
        Output->bIncludesRebuiltRevealColor = true;
    }

    FDWCTransparencyPixelComposeContext Context;
    Context.AutoResult = &Result;
    Context.RevealColorBuffer = MakeArrayView(Input.RevealColorBuffer);
    Context.ManualPremultipliedBuffer = MakeArrayView(Input.ManualPremultipliedBuffer);
    Context.ManualWeightBuffer = MakeArrayView(Input.ManualWeightBuffer);
    Context.WrinkleSuppressionBuffer = MakeArrayView(Input.WrinkleSuppressionBuffer);
    Context.OuterEdgeFeatherBuffer = MakeArrayView(Input.OuterEdgeFeatherBuffer);
    Context.VisualizationMode = Input.VisualizationMode;
    Context.TransparencyStrength = Input.TransparencyPreviewStrength;
    Context.WrinkleSuppressionStrength = Input.WrinkleSuppressionStrength;
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
    if (Output->bIncludesRebuiltManualOverrides)
    {
        Output->RebuiltManualPremultipliedBuffer = MoveTemp(Input.ManualPremultipliedBuffer);
        Output->RebuiltManualWeightBuffer = MoveTemp(Input.ManualWeightBuffer);
    }
    if (Output->bIncludesRebuiltRevealColor)
    {
        Output->RebuiltRevealColorBuffer = MoveTemp(Input.RevealColorBuffer);
    }
    Output->ResultBytes =
        static_cast<uint64>(Output->Pixels.GetAllocatedSize()) +
        static_cast<uint64>(Output->RebuiltManualPremultipliedBuffer.GetAllocatedSize()) +
        static_cast<uint64>(Output->RebuiltManualWeightBuffer.GetAllocatedSize()) +
        static_cast<uint64>(Output->RebuiltRevealColorBuffer.GetAllocatedSize());
    return Output;
}
