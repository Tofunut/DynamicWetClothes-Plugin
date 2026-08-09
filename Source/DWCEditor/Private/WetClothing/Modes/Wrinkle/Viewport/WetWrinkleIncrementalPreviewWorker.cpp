//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Modes/Wrinkle/Viewport/WetWrinkleIncrementalPreviewWorker.h"

#include "WetClothing/Foundation/Jobs/DWCEditorCancellationToken.h"
#include "WetClothing/Foundation/Raster/DWCEditorNormalRasterCore.h"
#include "WetClothing/Foundation/Raster/DWCEditorRasterPostProcess.h"
#include "WetClothing/Foundation/Raster/DWCEditorSurfacePatchRasterBuilder.h"
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

    constexpr int32 HoverTileSize = 128;
    constexpr int32 HoverSparseMinBoundedPixels = 128 * 1024;
    constexpr int32 HoverSparseMaxTiles = 256;
    constexpr uint64 HoverSparseMaxFragmentReferences = 1024ull * 1024ull;
    constexpr uint64 HoverUploadRegionPenaltyPixels = 4096;

    void MergeRectsWithOverlappingSourceReads(
        TArray<FIntRect>& OutputRects,
        const FIntPoint WorkingTextureSize,
        const FIntPoint TextureSize)
    {
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
                    OutputRects[AIndex] = FIntRect(
                        FMath::Min(OutputRects[AIndex].Min.X, OutputRects[BIndex].Min.X),
                        FMath::Min(OutputRects[AIndex].Min.Y, OutputRects[BIndex].Min.Y),
                        FMath::Max(OutputRects[AIndex].Max.X, OutputRects[BIndex].Max.X),
                        FMath::Max(OutputRects[AIndex].Max.Y, OutputRects[BIndex].Max.Y));
                    OutputRects.RemoveAtSwap(BIndex, 1, EAllowShrinking::No);
                    bMerged = true;
                    break;
                }
            }
        }
    }

    FIntRect MakeOutputTileRect(
        const int32 TileKey,
        const int32 TileCountX,
        const FIntPoint TextureSize)
    {
        const int32 TileX = TileKey % TileCountX;
        const int32 TileY = TileKey / TileCountX;
        return FIntRect(
            TileX * HoverTileSize,
            TileY * HoverTileSize,
            FMath::Min((TileX + 1) * HoverTileSize, TextureSize.X),
            FMath::Min((TileY + 1) * HoverTileSize, TextureSize.Y));
    }

    template <typename TileVisitorType>
    void VisitOutputTilesForRect(
        const FIntRect& Rect,
        const FIntPoint TextureSize,
        TileVisitorType&& Visitor)
    {
        if (Rect.IsEmpty() || TextureSize.X <= 0 || TextureSize.Y <= 0)
        {
            return;
        }
        const int32 TileCountX = FMath::DivideAndRoundUp(TextureSize.X, HoverTileSize);
        const int32 TileCountY = FMath::DivideAndRoundUp(TextureSize.Y, HoverTileSize);
        const int32 MinTileX = FMath::Clamp(Rect.Min.X / HoverTileSize, 0, TileCountX - 1);
        const int32 MinTileY = FMath::Clamp(Rect.Min.Y / HoverTileSize, 0, TileCountY - 1);
        const int32 MaxTileX = FMath::Clamp((Rect.Max.X - 1) / HoverTileSize, 0, TileCountX - 1);
        const int32 MaxTileY = FMath::Clamp((Rect.Max.Y - 1) / HoverTileSize, 0, TileCountY - 1);
        for (int32 TileY = MinTileY; TileY <= MaxTileY; ++TileY)
        {
            for (int32 TileX = MinTileX; TileX <= MaxTileX; ++TileX)
            {
                Visitor(TileY * TileCountX + TileX);
            }
        }
    }

    void AddOutputTilesForRect(
        const FIntRect& Rect,
        const FIntPoint TextureSize,
        TSet<int32>& OutTileKeys)
    {
        VisitOutputTilesForRect(Rect, TextureSize, [&OutTileKeys](const int32 TileKey)
        {
            OutTileKeys.Add(TileKey);
        });
    }

    FIntRect ComputeFragmentWorkingBounds(
        const FDWCEditorSurfacePatchFragment& Fragment,
        const FIntPoint WorkingTextureSize)
    {
        FIntRect Bounds(
            FMath::FloorToInt(Fragment.TargetUVBounds.Min.X * WorkingTextureSize.X) - 1,
            FMath::FloorToInt(Fragment.TargetUVBounds.Min.Y * WorkingTextureSize.Y) - 1,
            FMath::CeilToInt(Fragment.TargetUVBounds.Max.X * WorkingTextureSize.X) + 2,
            FMath::CeilToInt(Fragment.TargetUVBounds.Max.Y * WorkingTextureSize.Y) + 2);
        Bounds.Min.X = FMath::Clamp(Bounds.Min.X, 0, WorkingTextureSize.X);
        Bounds.Min.Y = FMath::Clamp(Bounds.Min.Y, 0, WorkingTextureSize.Y);
        Bounds.Max.X = FMath::Clamp(Bounds.Max.X, 0, WorkingTextureSize.X);
        Bounds.Max.Y = FMath::Clamp(Bounds.Max.Y, 0, WorkingTextureSize.Y);
        return Bounds;
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
            if (Command.ProjectedPatch.IsValid())
            {
                FDWCEditorNormalRasterCore::ComputeProjectedPatchBounds(
                    Command.ProjectedPatch,
                    WorkingTextureSize,
                    PatchBounds);
            }
            else
            {
                FDWCEditorNormalRasterCore::ComputeStampBounds(Command.Patch, WorkingTextureSize, PatchBounds);
            }
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
    MergeRectsWithOverlappingSourceReads(OutputRects, WorkingTextureSize, TextureSize);

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
        Estimate.SnapshotBytes += Command.ProjectedPatch.GetAllocatedSizeBytes();
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

FDWCEditorWorkerMemoryEstimate FWetWrinkleIncrementalPreviewWorker::EstimateProjectedHoverMemory(
    const FDWCEditorSurfaceNormalPatchInput& SurfaceInput,
    const FIntPoint /*WorkingTextureSize*/,
    const FIntPoint /*TextureSize*/)
{
    FDWCEditorWorkerMemoryEstimate Estimate;
    if (SurfaceInput.NormalSource.Texture.RawData.IsValid())
    {
        Estimate.ResidentSharedBytes = SurfaceInput.NormalSource.Texture.RawData->GetAllocatedSize();
    }
    Estimate.SnapshotBytes = sizeof(FWetWrinkleProjectedHoverPreviewJobInput);
    // Phase A owns only projection output and projection scratch. Raster
    // buffers are admitted from the exact dirty-region plan after projection.
    Estimate.WorkingBytes = SurfaceInput.Projection.MaxResultBytes;
    Estimate.ScratchBytes = SurfaceInput.Projection.MaxWorkingSetBytes;
    return Estimate;
}

uint64 FWetWrinkleProjectedHoverRasterPlan::GetRetainedSizeBytes() const
{
    uint64 Bytes = sizeof(*this) + Commands.GetAllocatedSize() + Regions.GetAllocatedSize() +
        RegionFragmentIndices.GetAllocatedSize() + CurrentOutputRects.GetAllocatedSize();
    for (const FWetWrinkleIncrementalCommand& Command : Commands)
    {
        Bytes += Command.ProjectedPatch.GetAllocatedSizeBytes();
        if (Command.ProjectedPatch.SharedProjection.IsValid())
        {
            Bytes += Command.ProjectedPatch.SharedProjection->GetAllocatedSizeBytes();
        }
        if (Command.ProjectedPatch.NormalSource.Texture.RawData.IsValid())
        {
            Bytes += Command.ProjectedPatch.NormalSource.Texture.RawData->GetAllocatedSize();
        }
    }
    for (const TArray<int32>& FragmentIndices : RegionFragmentIndices)
    {
        Bytes += FragmentIndices.GetAllocatedSize();
    }
    return Bytes;
}

FDWCEditorWorkerMemoryEstimate FWetWrinkleIncrementalPreviewWorker::EstimateProjectedHoverRasterMemory(
    const FWetWrinkleProjectedHoverRasterPlan& Plan)
{
    FDWCEditorWorkerMemoryEstimate Estimate;
    Estimate.SnapshotBytes = Plan.GetRetainedSizeBytes() +
        static_cast<uint64>(Plan.Regions.Num()) *
            (sizeof(FWetWrinkleIncrementalRegionSnapshot) + sizeof(FDWCEditorNormalRegionPayload));
    uint64 LargestOutputPixels = 0;
    for (const FWetWrinkleIncrementalRegionPlan& Region : Plan.Regions)
    {
        Estimate.WorkingBytes += RectPixels(Region.WorkingRect) * sizeof(uint32);
        const uint64 OutputPixels = RectPixels(Region.OutputRect);
        Estimate.OutputBytes += OutputPixels * sizeof(FColor);
        LargestOutputPixels = FMath::Max(LargestOutputPixels, OutputPixels);
    }
    if (Plan.TextureSize != Plan.WorkingTextureSize)
    {
        Estimate.ScratchBytes = LargestOutputPixels * sizeof(FVector3f);
    }
    return Estimate;
}

TSharedPtr<FWetWrinkleIncrementalPreviewJobResult, ESPMode::ThreadSafe>
FWetWrinkleIncrementalPreviewWorker::Build(
    FWetWrinkleIncrementalPreviewJobInput Input,
    const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& CancellationToken,
    FWetWrinkleHoverPerformanceDiagnostics* HoverDiagnostics)
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
                FDWCEditorProjectedRasterDiagnostics RasterDiagnostics;
                const double RasterStartSeconds = HoverDiagnostics != nullptr
                    ? FPlatformTime::Seconds()
                    : 0.0;
                FDWCEditorRasterResult RasterResult;
                if (Command.ProjectedPatch.IsValid())
                {
                    if (!Snapshot.bUseProjectedFragmentSubset)
                    {
                        RasterResult = FDWCEditorNormalRasterCore::RasterizeProjectedPatchRegion(
                            Command.ProjectedPatch,
                            Snapshot.Region,
                            &CancellationToken.Get(),
                            nullptr,
                            HoverDiagnostics != nullptr ? &RasterDiagnostics : nullptr);
                    }
                    else if (!Snapshot.ProjectedFragmentIndices.IsEmpty())
                    {
                        RasterResult = FDWCEditorNormalRasterCore::RasterizeProjectedPatchRegionSubset(
                            Command.ProjectedPatch,
                            Snapshot.ProjectedFragmentIndices,
                            Snapshot.Region,
                            &CancellationToken.Get(),
                            nullptr,
                            HoverDiagnostics != nullptr ? &RasterDiagnostics : nullptr);
                    }
                }
                else
                {
                    RasterResult = FDWCEditorNormalRasterCore::RasterizeStampRegion(
                        Command.Patch,
                        Snapshot.Region,
                        &CancellationToken.Get());
                }
                if (HoverDiagnostics != nullptr)
                {
                    HoverDiagnostics->RasterMs +=
                        (FPlatformTime::Seconds() - RasterStartSeconds) * 1000.0;
                    HoverDiagnostics->CandidatePixelCount += RasterDiagnostics.CandidatePixelCount;
                    HoverDiagnostics->RowReferenceCount += RasterDiagnostics.RowReferenceCount;
                    HoverDiagnostics->ParallelRowCount += RasterDiagnostics.ParallelRowCount;
                    HoverDiagnostics->bUsedParallelRaster |= RasterDiagnostics.bUsedParallelRows;
                }
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
        const double EncodeStartSeconds = HoverDiagnostics != nullptr
            ? FPlatformTime::Seconds()
            : 0.0;
        const FDWCEditorNormalRegionEncodeResult EncodeResult =
            FDWCEditorRasterPostProcess::ResampleAndEncodeNormalRegion(
                Snapshot.Region,
                Input.TextureSize,
                Snapshot.Plan.OutputRect,
                Payload.EncodedPixels,
                false,
                &CancellationToken.Get());
        const double EncodeElapsedMs = HoverDiagnostics != nullptr
            ? (FPlatformTime::Seconds() - EncodeStartSeconds) * 1000.0
            : 0.0;
        if (!EncodeResult.IsSucceeded())
        {
            Result->Regions.Reset();
            Result->bSucceeded = false;
            if (EncodeResult.Status == EDWCEditorNormalRegionEncodeStatus::Canceled)
            {
                if (HoverDiagnostics != nullptr)
                {
                    HoverDiagnostics->bCanceledDuringEncode = true;
                }
                Result->Error = TEXT("The wrinkle incremental preview encode was canceled.");
            }
            else
            {
                Result->Error = TEXT("Failed to encode the wrinkle incremental preview region.");
            }
            return Result;
        }
        if (HoverDiagnostics != nullptr)
        {
            HoverDiagnostics->ResampleEncodeMs += EncodeElapsedMs;
            HoverDiagnostics->bUsedDirectEncode |= EncodeResult.bUsedDirectEncode;
            HoverDiagnostics->bUsedParallelEncode |= EncodeResult.bUsedParallelRows;
            if (EncodeResult.bUsedDirectEncode)
            {
                HoverDiagnostics->DirectEncodeMs += EncodeElapsedMs;
            }
            else
            {
                HoverDiagnostics->NormalAwareResampleMs += EncodeElapsedMs;
            }
            HoverDiagnostics->DirtyWorkingPixelCount += RectPixels(Snapshot.Plan.WorkingRect);
            HoverDiagnostics->EncodedOutputPixelCount += EncodeResult.EncodedPixelCount;
        }
        Payload.PackedNormalXY = MoveTemp(Snapshot.Region.Surface.PackedNormalXY);
        Payload.Coverage = MoveTemp(Snapshot.Region.Surface.Coverage);
        Result->ResultBytes += Payload.PackedNormalXY.GetAllocatedSize() +
            Payload.Coverage.GetAllocatedSize() + Payload.EncodedPixels.GetAllocatedSize();
    }
    return Result;
}

TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>
FWetWrinkleIncrementalPreviewWorker::BuildProjectedHoverProjectionPhase(
    FWetWrinkleProjectedHoverPreviewJobInput Input,
    const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& CancellationToken)
{
    FWetWrinkleHoverPerformanceDiagnostics Diagnostics = Input.PerformanceDiagnostics;
    FWetWrinkleHoverPerformanceDiagnostics* DiagnosticsPtr =
        Input.bCollectPerformanceDiagnostics ? &Diagnostics : nullptr;
    const double WorkerStartSeconds = DiagnosticsPtr != nullptr
        ? FPlatformTime::Seconds()
        : 0.0;
    if (DiagnosticsPtr != nullptr && Diagnostics.SubmitSeconds > 0.0)
    {
        Diagnostics.AdmissionWaitMs =
            (WorkerStartSeconds - Diagnostics.SubmitSeconds) * 1000.0;
    }
    const auto MakeFailure = [](FString Error)
    {
        TSharedPtr<FWetWrinkleIncrementalPreviewJobResult, ESPMode::ThreadSafe> Result =
            MakeShared<FWetWrinkleIncrementalPreviewJobResult, ESPMode::ThreadSafe>();
        Result->bSucceeded = false;
        Result->Error = MoveTemp(Error);
        return Result;
    };

    if (CancellationToken->IsCanceled())
    {
        return MakeFailure(TEXT("The projected wrinkle hover was canceled before projection."));
    }

    FWetWrinkleIncrementalCommand Command;
    Command.Kind = EWetWrinkleIncrementalCommandKind::Patch;
    FString ProjectionError;
    const double ProjectionStartSeconds = DiagnosticsPtr != nullptr
        ? FPlatformTime::Seconds()
        : 0.0;
    if (!FDWCEditorSurfacePatchRasterBuilder::BuildProjectedPatchCommand(
            Input.SurfaceInput,
            Command.ProjectedPatch,
            &ProjectionError,
            &CancellationToken.Get(),
            Input.ProjectionCache.Get(),
            EDWCEditorSurfacePatchCachePolicy::ReadOnlyThenEphemeral))
    {
        return MakeFailure(ProjectionError.IsEmpty()
            ? TEXT("The projected wrinkle hover did not reach the target surface.")
            : MoveTemp(ProjectionError));
    }
    if (DiagnosticsPtr != nullptr)
    {
        Diagnostics.ProjectionMs =
            (FPlatformTime::Seconds() - ProjectionStartSeconds) * 1000.0;
        Diagnostics.ProjectedFragmentCount = Command.ProjectedPatch.GetFragments().Num();
        Diagnostics.VisitedTriangleCount = Command.ProjectedPatch.SharedProjection.IsValid()
            ? Command.ProjectedPatch.SharedProjection->VisitedTriangleCount
            : Diagnostics.ProjectedFragmentCount;
    }
    if (CancellationToken->IsCanceled())
    {
        return MakeFailure(TEXT("The projected wrinkle hover was canceled after projection."));
    }

    TArray<FWetWrinkleIncrementalCommand> Commands;
    Commands.Add(MoveTemp(Command));
    TArray<FIntRect> ProjectedWorkingRects;
    FDWCEditorNormalRasterCore::ComputeProjectedPatchBounds(
        Commands[0].ProjectedPatch,
        Input.WorkingTextureSize,
        ProjectedWorkingRects);

    TArray<FIntRect> CurrentOutputRects;
    for (const FIntRect& WorkingRect : ProjectedWorkingRects)
    {
        AddMergedRect(
            CurrentOutputRects,
            FDWCEditorRasterPostProcess::MapRect(
                WorkingRect,
                Input.WorkingTextureSize,
                Input.TextureSize));
    }

    const double RegionPlanStartSeconds = DiagnosticsPtr != nullptr
        ? FPlatformTime::Seconds()
        : 0.0;
    TArray<FIntRect> PreviousWorkingRects;
    PreviousWorkingRects.Reserve(Input.PreviousOutputRects.Num());
    for (const FIntRect& PreviousOutputRect : Input.PreviousOutputRects)
    {
        const FIntRect PreviousWorkingRect =
            FDWCEditorRasterPostProcess::MapDestinationRectToSourceReadRect(
                PreviousOutputRect,
                Input.WorkingTextureSize,
                Input.TextureSize);
        if (!PreviousWorkingRect.IsEmpty())
        {
            PreviousWorkingRects.Add(PreviousWorkingRect);
        }
    }

    TArray<FWetWrinkleIncrementalRegionPlan> RegionPlan;
    if (!BuildRegionPlan(
            Commands,
            Input.WorkingTextureSize,
            Input.TextureSize,
            RegionPlan,
            &PreviousWorkingRects))
    {
        return MakeFailure(TEXT("The projected wrinkle hover produced no valid dirty region."));
    }

    uint64 BoundedOutputPixels = 0;
    for (const FWetWrinkleIncrementalRegionPlan& Plan : RegionPlan)
    {
        BoundedOutputPixels += RectPixels(Plan.OutputRect);
    }

    TArray<TArray<int32>> RegionFragmentIndices;
    bool bUseSparseRegions = false;
    uint64 SparseOutputPixels = 0;
    uint64 TileFragmentReferences = 0;
    int32 CurrentTileCount = 0;
    int32 PreviousTileCount = 0;
    int32 ClearOnlyTileCount = 0;
    FDWCEditorSparseUploadDecision UploadDecision;
    const double TileBinningStartSeconds = DiagnosticsPtr != nullptr
        ? FPlatformTime::Seconds()
        : 0.0;

    if (BoundedOutputPixels >= HoverSparseMinBoundedPixels)
    {
        const int32 TileCountX = FMath::DivideAndRoundUp(Input.TextureSize.X, HoverTileSize);
        TMap<int32, TArray<int32>> CurrentTileFragments;
        TSet<int32> CurrentTileKeys;
        const TArray<FDWCEditorSurfacePatchFragment>& Fragments =
            Commands[0].ProjectedPatch.GetFragments();

        for (int32 FragmentIndex = 0; FragmentIndex < Fragments.Num(); ++FragmentIndex)
        {
            if ((FragmentIndex & 63) == 0 && CancellationToken->IsCanceled())
            {
                return MakeFailure(TEXT("The projected wrinkle hover was canceled during tile binning."));
            }

            const FIntRect FragmentWorkingBounds =
                ComputeFragmentWorkingBounds(Fragments[FragmentIndex], Input.WorkingTextureSize);
            const FIntRect FragmentOutputBounds = FDWCEditorRasterPostProcess::MapRect(
                FragmentWorkingBounds,
                Input.WorkingTextureSize,
                Input.TextureSize);
            VisitOutputTilesForRect(FragmentOutputBounds, Input.TextureSize,
                [&CurrentTileKeys, &CurrentTileFragments, &TileFragmentReferences, FragmentIndex](
                    const int32 TileKey)
            {
                CurrentTileKeys.Add(TileKey);
                CurrentTileFragments.FindOrAdd(TileKey).Add(FragmentIndex);
                ++TileFragmentReferences;
            });
        }

        TSet<int32> PreviousTileKeys;
        for (const FIntRect& PreviousOutputRect : Input.PreviousOutputRects)
        {
            AddOutputTilesForRect(PreviousOutputRect, Input.TextureSize, PreviousTileKeys);
        }

        TSet<int32> DirtyTileKeys = CurrentTileKeys;
        DirtyTileKeys.Append(PreviousTileKeys);
        CurrentTileCount = CurrentTileKeys.Num();
        PreviousTileCount = PreviousTileKeys.Num();
        for (const int32 PreviousTileKey : PreviousTileKeys)
        {
            ClearOnlyTileCount += CurrentTileKeys.Contains(PreviousTileKey) ? 0 : 1;
        }

        TArray<int32> SortedDirtyTileKeys = DirtyTileKeys.Array();
        SortedDirtyTileKeys.Sort();
        TArray<FIntRect> SparseTileRects;
        SparseTileRects.Reserve(SortedDirtyTileKeys.Num());
        for (const int32 TileKey : SortedDirtyTileKeys)
        {
            const FIntRect TileRect = MakeOutputTileRect(TileKey, TileCountX, Input.TextureSize);
            SparseTileRects.Add(TileRect);
            SparseOutputPixels += RectPixels(TileRect);
        }

        const bool bWithinSparseLimits =
            SortedDirtyTileKeys.Num() <= HoverSparseMaxTiles &&
            TileFragmentReferences <= HoverSparseMaxFragmentReferences;
        TArray<FIntRect> BoundedOutputRects;
        BoundedOutputRects.Reserve(RegionPlan.Num());
        for (const FWetWrinkleIncrementalRegionPlan& Region : RegionPlan)
        {
            BoundedOutputRects.Add(Region.OutputRect);
        }
        FDWCEditorSparseUploadPolicyConfig UploadPolicyConfig;
        UploadPolicyConfig.MaxRegions = FDWCEditorDirtyRegionSet::MaxRegions;
        UploadPolicyConfig.RegionSubmissionPenaltyPixels = HoverUploadRegionPenaltyPixels;
        UploadDecision = FDWCEditorSparseUploadPolicy::Choose(
            SparseTileRects,
            BoundedOutputRects,
            Input.TextureSize,
            UploadPolicyConfig);
        if (!bWithinSparseLimits && UploadDecision.Plan != EDWCEditorSparseUploadPlan::Bounded)
        {
            const int32 SourceRegionCount = UploadDecision.SourceRegionCount;
            const uint64 SourcePixelCount = UploadDecision.SourcePixelCount;
            UploadDecision = FDWCEditorSparseUploadPolicy::Choose(
                {}, BoundedOutputRects, Input.TextureSize, UploadPolicyConfig);
            UploadDecision.SourceRegionCount = SourceRegionCount;
            UploadDecision.SourcePixelCount = SourcePixelCount;
        }
        bUseSparseRegions = bWithinSparseLimits &&
            UploadDecision.Plan != EDWCEditorSparseUploadPlan::Bounded;

        if (bUseSparseRegions)
        {
            MergeRectsWithOverlappingSourceReads(
                UploadDecision.Regions,
                Input.WorkingTextureSize,
                Input.TextureSize);
            RegionPlan.Reset(UploadDecision.Regions.Num());
            RegionFragmentIndices.Reset(UploadDecision.Regions.Num());

            TArray<FIntRect> CurrentTileRects;
            CurrentTileRects.Reserve(CurrentTileKeys.Num());
            TArray<int32> SortedCurrentTileKeys = CurrentTileKeys.Array();
            SortedCurrentTileKeys.Sort();
            for (const int32 TileKey : SortedCurrentTileKeys)
            {
                CurrentTileRects.Add(MakeOutputTileRect(TileKey, TileCountX, Input.TextureSize));
            }
            const FDWCEditorSparseUploadDecision CurrentDecision =
                FDWCEditorSparseUploadPolicy::Choose(
                    CurrentTileRects,
                    CurrentOutputRects,
                    Input.TextureSize,
                    UploadPolicyConfig);
            CurrentOutputRects = CurrentDecision.Regions;
            MergeRectsWithOverlappingSourceReads(
                CurrentOutputRects,
                Input.WorkingTextureSize,
                Input.TextureSize);

            for (const FIntRect& PlannedOutputRect : UploadDecision.Regions)
            {
                FWetWrinkleIncrementalRegionPlan& Plan = RegionPlan.AddDefaulted_GetRef();
                Plan.OutputRect = PlannedOutputRect;
                Plan.WorkingRect = FDWCEditorRasterPostProcess::MapDestinationRectToSourceReadRect(
                    Plan.OutputRect,
                    Input.WorkingTextureSize,
                    Input.TextureSize);
                TArray<int32>& FragmentIndices = RegionFragmentIndices.AddDefaulted_GetRef();
                TSet<int32> UniqueFragmentIndices;
                VisitOutputTilesForRect(PlannedOutputRect, Input.TextureSize,
                    [&CurrentTileFragments, &UniqueFragmentIndices](const int32 TileKey)
                {
                    if (const TArray<int32>* CurrentFragments = CurrentTileFragments.Find(TileKey))
                    {
                        for (const int32 FragmentIndex : *CurrentFragments)
                        {
                            UniqueFragmentIndices.Add(FragmentIndex);
                        }
                    }
                });
                FragmentIndices = UniqueFragmentIndices.Array();
                FragmentIndices.Sort();
            }
        }
        else if (!UploadDecision.Regions.IsEmpty())
        {
            MergeRectsWithOverlappingSourceReads(
                UploadDecision.Regions,
                Input.WorkingTextureSize,
                Input.TextureSize);
            RegionPlan.Reset(UploadDecision.Regions.Num());
            for (const FIntRect& PlannedOutputRect : UploadDecision.Regions)
            {
                FWetWrinkleIncrementalRegionPlan& PlannedRegion = RegionPlan.AddDefaulted_GetRef();
                PlannedRegion.OutputRect = PlannedOutputRect;
                PlannedRegion.WorkingRect =
                    FDWCEditorRasterPostProcess::MapDestinationRectToSourceReadRect(
                        PlannedOutputRect,
                        Input.WorkingTextureSize,
                        Input.TextureSize);
            }
        }
    }

    if (DiagnosticsPtr != nullptr)
    {
        Diagnostics.RegionPlanMs =
            (FPlatformTime::Seconds() - RegionPlanStartSeconds) * 1000.0;
        Diagnostics.DirtyRegionCount = RegionPlan.Num();
        Diagnostics.bUsedSparseRegions = bUseSparseRegions;
        Diagnostics.CurrentTileCount = CurrentTileCount;
        Diagnostics.PreviousTileCount = PreviousTileCount;
        Diagnostics.ClearOnlyTileCount = ClearOnlyTileCount;
        Diagnostics.BoundedOutputPixelCount = BoundedOutputPixels;
        Diagnostics.SparseOutputPixelCount = SparseOutputPixels;
        Diagnostics.PlannedOutputPixelCount = 0;
        for (const FWetWrinkleIncrementalRegionPlan& PlannedRegion : RegionPlan)
        {
            Diagnostics.PlannedOutputPixelCount += RectPixels(PlannedRegion.OutputRect);
        }
        Diagnostics.SourceUploadRegionCount = UploadDecision.SourceRegionCount;
        Diagnostics.UploadPlan = bUseSparseRegions
            ? UploadDecision.Plan
            : EDWCEditorSparseUploadPlan::Bounded;
        Diagnostics.TileFragmentReferenceCount = TileFragmentReferences;
        Diagnostics.TileBinningMs = TileBinningStartSeconds > 0.0
            ? (FPlatformTime::Seconds() - TileBinningStartSeconds) * 1000.0
            : 0.0;
    }

    FWetWrinkleProjectedHoverRasterPlan Plan;
    Plan.TextureSize = Input.TextureSize;
    Plan.WorkingTextureSize = Input.WorkingTextureSize;
    Plan.Commands = MoveTemp(Commands);
    Plan.Regions = MoveTemp(RegionPlan);
    Plan.RegionFragmentIndices = MoveTemp(RegionFragmentIndices);
    Plan.CurrentOutputRects = MoveTemp(CurrentOutputRects);
    Plan.Target = Input.Target;
    Plan.bTouchesUVSeam =
        Plan.Commands[0].ProjectedPatch.SharedProjection.IsValid() &&
        Plan.Commands[0].ProjectedPatch.SharedProjection->bTouchesUVSeam;
    Plan.bUseSparseRegions = bUseSparseRegions;
    Plan.bCollectPerformanceDiagnostics = Input.bCollectPerformanceDiagnostics;
    if (DiagnosticsPtr != nullptr)
    {
        Diagnostics.WorkerTotalMs =
            (FPlatformTime::Seconds() - WorkerStartSeconds) * 1000.0;
    }
    Plan.PerformanceDiagnostics = MoveTemp(Diagnostics);

    const FDWCEditorWorkerMemoryEstimate RasterEstimate =
        EstimateProjectedHoverRasterMemory(Plan);
    FDWCEditorWorkerMemoryEstimate RetainedEstimate;
    RetainedEstimate.SnapshotBytes = Plan.GetRetainedSizeBytes();
    if (Plan.bCollectPerformanceDiagnostics)
    {
        Plan.PerformanceDiagnostics.RetainedPhaseBytes = RetainedEstimate.GetTotalBytes();
        Plan.PerformanceDiagnostics.RasterPhaseMemoryBytes = RasterEstimate.GetTotalBytes();
        Plan.PerformanceDiagnostics.ProjectionPhaseFinishedSeconds = FPlatformTime::Seconds();
    }

    TSharedPtr<FDWCEditorWorkerPhaseContinuationResult, ESPMode::ThreadSafe> Continuation =
        MakeShared<FDWCEditorWorkerPhaseContinuationResult, ESPMode::ThreadSafe>();
    Continuation->RetainedMemoryEstimate = RetainedEstimate;
    Continuation->NextPhaseMemoryEstimate = RasterEstimate;
    Continuation->NextPhaseName = TEXT("WrinkleHoverRaster");
    Continuation->NextWork = [Plan = MoveTemp(Plan)](
        const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& WorkerToken) mutable
    {
        return BuildProjectedHoverRasterPhase(MoveTemp(Plan), WorkerToken);
    };
    return Continuation;
}

TSharedPtr<FWetWrinkleIncrementalPreviewJobResult, ESPMode::ThreadSafe>
FWetWrinkleIncrementalPreviewWorker::BuildProjectedHoverRasterPhase(
    FWetWrinkleProjectedHoverRasterPlan Plan,
    const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& CancellationToken)
{
    const double RasterPhaseStartSeconds = Plan.bCollectPerformanceDiagnostics
        ? FPlatformTime::Seconds()
        : 0.0;
    FWetWrinkleHoverPerformanceDiagnostics* DiagnosticsPtr = Plan.bCollectPerformanceDiagnostics
        ? &Plan.PerformanceDiagnostics
        : nullptr;
    if (DiagnosticsPtr != nullptr && DiagnosticsPtr->ProjectionPhaseFinishedSeconds > 0.0)
    {
        DiagnosticsPtr->RasterAdmissionWaitMs =
            (RasterPhaseStartSeconds - DiagnosticsPtr->ProjectionPhaseFinishedSeconds) * 1000.0;
    }
    FWetWrinkleIncrementalPreviewJobInput RasterInput;
    RasterInput.TextureSize = Plan.TextureSize;
    RasterInput.WorkingTextureSize = Plan.WorkingTextureSize;
    RasterInput.bClearRegionsToFlat = true;
    RasterInput.Commands = MoveTemp(Plan.Commands);
    RasterInput.Target = Plan.Target;
    const double AllocationStartSeconds = DiagnosticsPtr != nullptr
        ? FPlatformTime::Seconds()
        : 0.0;
    for (int32 RegionIndex = 0; RegionIndex < Plan.Regions.Num(); ++RegionIndex)
    {
        if (CancellationToken->IsCanceled())
        {
            TSharedPtr<FWetWrinkleIncrementalPreviewJobResult, ESPMode::ThreadSafe> Result =
                MakeShared<FWetWrinkleIncrementalPreviewJobResult, ESPMode::ThreadSafe>();
            Result->bSucceeded = false;
            Result->Error = TEXT("The projected wrinkle hover was canceled before region allocation.");
            return Result;
        }
        FWetWrinkleIncrementalRegionSnapshot& Snapshot = RasterInput.Regions.AddDefaulted_GetRef();
        Snapshot.Plan = Plan.Regions[RegionIndex];
        if (!Snapshot.Region.Initialize(
                Plan.WorkingTextureSize, Snapshot.Plan.WorkingRect, false))
        {
            TSharedPtr<FWetWrinkleIncrementalPreviewJobResult, ESPMode::ThreadSafe> Result =
                MakeShared<FWetWrinkleIncrementalPreviewJobResult, ESPMode::ThreadSafe>();
            Result->bSucceeded = false;
            Result->Error = TEXT("Failed to allocate a projected wrinkle hover region.");
            return Result;
        }
        if (Plan.bUseSparseRegions)
        {
            Snapshot.bUseProjectedFragmentSubset = true;
            Snapshot.ProjectedFragmentIndices = MoveTemp(Plan.RegionFragmentIndices[RegionIndex]);
        }
    }
    if (DiagnosticsPtr != nullptr)
    {
        DiagnosticsPtr->RegionAllocationMs =
            (FPlatformTime::Seconds() - AllocationStartSeconds) * 1000.0;
    }

    TSharedPtr<FWetWrinkleIncrementalPreviewJobResult, ESPMode::ThreadSafe> Result =
        Build(MoveTemp(RasterInput), CancellationToken, DiagnosticsPtr);
    if (Result.IsValid())
    {
        if (Result->bSucceeded)
        {
            Result->ProjectedOutputRects = MoveTemp(Plan.CurrentOutputRects);
            Result->bTouchesUVSeam = Plan.bTouchesUVSeam;
        }
        if (DiagnosticsPtr != nullptr)
        {
            DiagnosticsPtr->WorkerTotalMs +=
                (FPlatformTime::Seconds() - RasterPhaseStartSeconds) * 1000.0;
            DiagnosticsPtr->ActualResultBytes = Result->ResultBytes;
            DiagnosticsPtr->AffectedPixelCount = Result->AffectedPixelCount;
            Result->HoverDiagnostics = *DiagnosticsPtr;
        }
    }
    return Result;
}

TSharedPtr<FWetWrinkleIncrementalPreviewJobResult, ESPMode::ThreadSafe>
FWetWrinkleIncrementalPreviewWorker::BuildProjectedHover(
    FWetWrinkleProjectedHoverPreviewJobInput Input,
    const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& CancellationToken)
{
    TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe> PhaseResult =
        BuildProjectedHoverProjectionPhase(MoveTemp(Input), CancellationToken);
    if (!PhaseResult.IsValid() || !PhaseResult->bIsPhaseContinuation)
    {
        return StaticCastSharedPtr<FWetWrinkleIncrementalPreviewJobResult>(PhaseResult);
    }
    TSharedPtr<FDWCEditorWorkerPhaseContinuationResult, ESPMode::ThreadSafe> Continuation =
        StaticCastSharedPtr<FDWCEditorWorkerPhaseContinuationResult>(PhaseResult);
    return StaticCastSharedPtr<FWetWrinkleIncrementalPreviewJobResult>(
        Continuation->NextWork(CancellationToken));
}
