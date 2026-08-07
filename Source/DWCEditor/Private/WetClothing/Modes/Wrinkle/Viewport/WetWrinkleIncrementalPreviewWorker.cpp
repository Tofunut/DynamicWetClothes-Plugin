//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Modes/Wrinkle/Viewport/WetWrinkleIncrementalPreviewWorker.h"

#include "WetClothing/Foundation/Jobs/DWCEditorCancellationToken.h"
#include "WetClothing/Foundation/Raster/DWCEditorNormalRasterCore.h"
#include "WetClothing/Foundation/Raster/DWCEditorRasterPostProcess.h"
#include "WetClothing/Modes/Wrinkle/Stroke/WetProceduralRidgeRasterizer.h"

namespace
{
    bool RectsOverlapOrTouch(const FIntRect& A, const FIntRect& B)
    {
        return A.Min.X <= B.Max.X && A.Max.X >= B.Min.X &&
            A.Min.Y <= B.Max.Y && A.Max.Y >= B.Min.Y;
    }

    void AddMergedRect(TArray<FIntRect>& Rects, FIntRect Rect)
    {
        if (Rect.IsEmpty())
        {
            return;
        }
        for (int32 Index = 0; Index < Rects.Num();)
        {
            if (!RectsOverlapOrTouch(Rect, Rects[Index]))
            {
                ++Index;
                continue;
            }
            Rect.Min.X = FMath::Min(Rect.Min.X, Rects[Index].Min.X);
            Rect.Min.Y = FMath::Min(Rect.Min.Y, Rects[Index].Min.Y);
            Rect.Max.X = FMath::Max(Rect.Max.X, Rects[Index].Max.X);
            Rect.Max.Y = FMath::Max(Rect.Max.Y, Rects[Index].Max.Y);
            Rects.RemoveAtSwap(Index, 1, EAllowShrinking::No);
            Index = 0;
        }
        Rects.Add(Rect);
    }

    uint64 RectPixels(const FIntRect& Rect)
    {
        return Rect.IsEmpty()
            ? 0
            : static_cast<uint64>(Rect.Width()) * static_cast<uint64>(Rect.Height());
    }
}

bool FWetWrinkleIncrementalPreviewWorker::BuildRegionPlan(
    const TArray<FWetWrinkleIncrementalCommand>& Commands,
    const FIntPoint WorkingTextureSize,
    const FIntPoint TextureSize,
    TArray<FWetWrinkleIncrementalRegionPlan>& OutPlan,
    const TArray<FIntRect>* AdditionalWorkingRects)
{
    OutPlan.Reset();
    if (Commands.IsEmpty() || WorkingTextureSize.X <= 0 || WorkingTextureSize.Y <= 0 ||
        TextureSize.X <= 0 || TextureSize.Y <= 0)
    {
        return false;
    }

    TArray<FIntRect> WorkingDirtyRects;
    if (AdditionalWorkingRects != nullptr)
    {
        for (const FIntRect& Rect : *AdditionalWorkingRects)
        {
            AddMergedRect(WorkingDirtyRects, Rect);
        }
    }
    for (const FWetWrinkleIncrementalCommand& Command : Commands)
    {
        if (Command.Kind == EWetWrinkleIncrementalCommandKind::Patch)
        {
            TArray<FIntRect> PatchBounds;
            FDWCEditorNormalRasterCore::ComputeStampBounds(Command.Patch, WorkingTextureSize, PatchBounds);
            for (const FIntRect& Rect : PatchBounds)
            {
                AddMergedRect(WorkingDirtyRects, Rect);
            }
        }
        else
        {
            AddMergedRect(
                WorkingDirtyRects,
                FWetProceduralRidgeRasterizer::ComputeBounds(Command.Ridge, WorkingTextureSize));
        }
    }
    if (WorkingDirtyRects.IsEmpty())
    {
        return false;
    }

    TArray<FIntRect> OutputRects;
    for (const FIntRect& WorkingRect : WorkingDirtyRects)
    {
        AddMergedRect(
            OutputRects,
            FDWCEditorRasterPostProcess::MapRect(WorkingRect, WorkingTextureSize, TextureSize));
    }

    // A mapped output halo can make otherwise separate source regions overlap.
    // Merge until both the commit's output and working rectangles are disjoint.
    bool bMerged = true;
    while (bMerged)
    {
        bMerged = false;
        for (int32 AIndex = 0; AIndex < OutputRects.Num() && !bMerged; ++AIndex)
        {
            const FIntRect AWorking = FDWCEditorRasterPostProcess::MapDestinationRectToSourceReadRect(
                OutputRects[AIndex], WorkingTextureSize, TextureSize);
            for (int32 BIndex = AIndex + 1; BIndex < OutputRects.Num(); ++BIndex)
            {
                const FIntRect BWorking = FDWCEditorRasterPostProcess::MapDestinationRectToSourceReadRect(
                    OutputRects[BIndex], WorkingTextureSize, TextureSize);
                if (!RectsOverlapOrTouch(OutputRects[AIndex], OutputRects[BIndex]) &&
                    !RectsOverlapOrTouch(AWorking, BWorking))
                {
                    continue;
                }
                FIntRect Merged(
                    FMath::Min(OutputRects[AIndex].Min.X, OutputRects[BIndex].Min.X),
                    FMath::Min(OutputRects[AIndex].Min.Y, OutputRects[BIndex].Min.Y),
                    FMath::Max(OutputRects[AIndex].Max.X, OutputRects[BIndex].Max.X),
                    FMath::Max(OutputRects[AIndex].Max.Y, OutputRects[BIndex].Max.Y));
                OutputRects[AIndex] = Merged;
                OutputRects.RemoveAtSwap(BIndex, 1, EAllowShrinking::No);
                bMerged = true;
                break;
            }
        }
    }

    for (const FIntRect& OutputRect : OutputRects)
    {
        FWetWrinkleIncrementalRegionPlan& Region = OutPlan.AddDefaulted_GetRef();
        Region.OutputRect = OutputRect;
        Region.WorkingRect = FDWCEditorRasterPostProcess::MapDestinationRectToSourceReadRect(
            OutputRect,
            WorkingTextureSize,
            TextureSize);
        if (Region.WorkingRect.IsEmpty())
        {
            OutPlan.Reset();
            return false;
        }
    }
    return !OutPlan.IsEmpty();
}

FDWCEditorWorkerMemoryEstimate FWetWrinkleIncrementalPreviewWorker::EstimateMemory(
    const TArray<FWetWrinkleIncrementalCommand>& Commands,
    const TArray<FWetWrinkleIncrementalRegionPlan>& Plan,
    const bool bWithCoverage)
{
    FDWCEditorWorkerMemoryEstimate Estimate;
    Estimate.SnapshotBytes = Commands.GetAllocatedSize();
    for (const FWetWrinkleIncrementalCommand& Command : Commands)
    {
        Estimate.SnapshotBytes += Command.Ridge.Points.GetAllocatedSize();
        Estimate.SnapshotBytes += Command.Ridge.DisplayName.GetAllocatedSize();
    }
    for (const FWetWrinkleIncrementalRegionPlan& Region : Plan)
    {
        const uint64 WorkingPixels = RectPixels(Region.WorkingRect);
        Estimate.SnapshotBytes += WorkingPixels * sizeof(uint32);
        if (bWithCoverage)
        {
            Estimate.SnapshotBytes += WorkingPixels * sizeof(float);
        }
        Estimate.OutputBytes += RectPixels(Region.OutputRect) * sizeof(FColor);
    }
    const bool bHasRidge = Commands.ContainsByPredicate(
        [](const FWetWrinkleIncrementalCommand& Command)
        {
            return Command.Kind == EWetWrinkleIncrementalCommandKind::Ridge;
        });
    Estimate.ScratchBytes = bHasRidge
        ? FWetProceduralRidgeRasterizer::GetTransientScratchBytesUpperBound()
        : 0;
    return Estimate;
}

TSharedPtr<FWetWrinkleIncrementalPreviewJobResult, ESPMode::ThreadSafe>
FWetWrinkleIncrementalPreviewWorker::Build(
    FWetWrinkleIncrementalPreviewJobInput Input,
    const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& CancellationToken)
{
    TSharedPtr<FWetWrinkleIncrementalPreviewJobResult, ESPMode::ThreadSafe> Result =
        MakeShared<FWetWrinkleIncrementalPreviewJobResult, ESPMode::ThreadSafe>();
    Result->FirstSequence = Input.FirstSequence;
    Result->LastSequence = Input.LastSequence;
    Result->Target = Input.Target;
    if (Input.Commands.IsEmpty() || Input.Regions.IsEmpty())
    {
        Result->bSucceeded = false;
        Result->Error = TEXT("The wrinkle incremental preview request is empty.");
        return Result;
    }

    for (FWetWrinkleIncrementalRegionSnapshot& Snapshot : Input.Regions)
    {
        if (CancellationToken->IsCanceled())
        {
            Result->bSucceeded = false;
            Result->Error = TEXT("The wrinkle incremental preview job was canceled.");
            return Result;
        }
        if (!Snapshot.Region.IsValid() || Snapshot.Region.Rect != Snapshot.Plan.WorkingRect)
        {
            Result->bSucceeded = false;
            Result->Error = TEXT("The wrinkle incremental preview region snapshot is invalid.");
            return Result;
        }
        if (Input.bClearRegionsToFlat)
        {
            Snapshot.Region.Surface.Initialize(
                Snapshot.Region.Surface.Size,
                Snapshot.Region.Surface.HasCoverage());
        }

        for (const FWetWrinkleIncrementalCommand& Command : Input.Commands)
        {
            if (CancellationToken->IsCanceled())
            {
                Result->bSucceeded = false;
                Result->Error = TEXT("The wrinkle incremental preview job was canceled.");
                return Result;
            }
            if (Command.Kind == EWetWrinkleIncrementalCommandKind::Patch)
            {
                const FDWCEditorRasterResult RasterResult = FDWCEditorNormalRasterCore::RasterizeStampRegion(
                    Command.Patch,
                    Snapshot.Region,
                    &CancellationToken.Get());
                if (RasterResult.bCanceled)
                {
                    Result->bSucceeded = false;
                    Result->Error = TEXT("The wrinkle incremental patch raster was canceled.");
                    return Result;
                }
                Result->AffectedPixelCount += RasterResult.AffectedPixelCount;
            }
            else
            {
                const FWetProceduralRidgeRasterResult RasterResult =
                    FWetProceduralRidgeRasterizer::RasterizeToRegion(
                        Command.Ridge,
                        Snapshot.Region,
                        nullptr,
                        &CancellationToken.Get());
                if (RasterResult.bCanceled)
                {
                    Result->bSucceeded = false;
                    Result->Error = TEXT("The wrinkle incremental ridge raster was canceled.");
                    return Result;
                }
            }
        }

        FDWCEditorNormalRegionPayload& Payload = Result->Regions.AddDefaulted_GetRef();
        Payload.WorkingRect = Snapshot.Plan.WorkingRect;
        Payload.OutputRect = Snapshot.Plan.OutputRect;
        if (!FDWCEditorRasterPostProcess::ResampleAndEncodeNormalRegion(
                Snapshot.Region,
                Input.TextureSize,
                Snapshot.Plan.OutputRect,
                Payload.EncodedPixels))
        {
            // The normal arrays were moved only after encoding should have
            // succeeded. Restore a clear error instead of committing a partial result.
            Result->Regions.Reset();
            Result->bSucceeded = false;
            Result->Error = TEXT("Failed to encode the wrinkle incremental preview region.");
            return Result;
        }
        Payload.PackedNormalXY = MoveTemp(Snapshot.Region.Surface.PackedNormalXY);
        Payload.Coverage = MoveTemp(Snapshot.Region.Surface.Coverage);
        Result->ResultBytes += Payload.PackedNormalXY.GetAllocatedSize() +
            Payload.Coverage.GetAllocatedSize() + Payload.EncodedPixels.GetAllocatedSize();
    }
    return Result;
}
