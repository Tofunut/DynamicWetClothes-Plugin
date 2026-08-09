//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetClothingWrinkleData.h"
#include "WetClothing/Foundation/Jobs/DWCEditorWorkerJobTypes.h"
#include "WetClothing/Foundation/Preview/Region/DWCEditorPreviewRegionTypes.h"
#include "WetClothing/Foundation/Raster/DWCEditorRasterTypes.h"

class FDWCEditorCancellationToken;
class FDWCEditorSurfacePatchProjectionCacheService;

enum class EWetWrinkleIncrementalCommandKind : uint8
{
    Patch,
    Ridge
};

struct FWetWrinkleIncrementalCommand
{
    EWetWrinkleIncrementalCommandKind Kind = EWetWrinkleIncrementalCommandKind::Patch;
    uint64 Sequence = 0;
    FDWCEditorNormalStampCommand Patch;
    FDWCEditorProjectedNormalPatchCommand ProjectedPatch;
    FWetProceduralRidgeStroke Ridge;
};

struct FWetWrinkleIncrementalRegionPlan
{
    FIntRect WorkingRect;
    FIntRect OutputRect;
};

struct FWetWrinkleIncrementalRegionSnapshot
{
    FWetWrinkleIncrementalRegionPlan Plan;
    FDWCEditorNormalRasterRegion Region;
    TArray<int32> ProjectedFragmentIndices;
    bool bUseProjectedFragmentSubset = false;
};

struct FWetWrinkleIncrementalPreviewJobInput
{
    FIntPoint TextureSize = FIntPoint::ZeroValue;
    FIntPoint WorkingTextureSize = FIntPoint::ZeroValue;
    TArray<FWetWrinkleIncrementalCommand> Commands;
    TArray<FWetWrinkleIncrementalRegionSnapshot> Regions;
    FDWCEditorPreviewRegionTarget Target;
    bool bClearRegionsToFlat = false;
    uint64 FirstSequence = 0;
    uint64 LastSequence = 0;
};

struct FWetWrinkleHoverPerformanceDiagnostics
{
    uint64 RequestId = 0;
    int32 MaterialSlotIndex = INDEX_NONE;
    EWetWrinklePatchProjectionMode ProjectionMode = EWetWrinklePatchProjectionMode::NonUVSeam;
    float BrushDiameterLocal = 0.0f;
    FIntPoint TextureSize = FIntPoint::ZeroValue;
    FIntPoint WorkingTextureSize = FIntPoint::ZeroValue;
    int32 VisitedTriangleCount = 0;
    int32 ProjectedFragmentCount = 0;
    int32 DirtyRegionCount = 0;
    int32 ParallelRowCount = 0;
    uint64 DirtyWorkingPixelCount = 0;
    uint64 EncodedOutputPixelCount = 0;
    uint64 AffectedPixelCount = 0;
    uint64 CandidatePixelCount = 0;
    uint64 RowReferenceCount = 0;
    uint64 EstimatedMemoryBytes = 0;
    uint64 RetainedPhaseBytes = 0;
    uint64 RasterPhaseMemoryBytes = 0;
    uint64 ActualResultBytes = 0;
    bool bUsedParallelRaster = false;
    bool bUsedDirectEncode = false;
    bool bUsedParallelEncode = false;
    bool bCanceledDuringEncode = false;
    bool bUsedSparseRegions = false;
    int32 CurrentTileCount = 0;
    int32 PreviousTileCount = 0;
    int32 ClearOnlyTileCount = 0;
    uint64 BoundedOutputPixelCount = 0;
    uint64 SparseOutputPixelCount = 0;
    uint64 PlannedOutputPixelCount = 0;
    int32 SourceUploadRegionCount = 0;
    EDWCEditorSparseUploadPlan UploadPlan = EDWCEditorSparseUploadPlan::Bounded;
    uint64 TileFragmentReferenceCount = 0;
    double RequestStartSeconds = 0.0;
    double SubmitSeconds = 0.0;
    double AdmissionWaitMs = 0.0;
    double RasterAdmissionWaitMs = 0.0;
    double ProjectionPhaseFinishedSeconds = 0.0;
    double DescriptorBuildMs = 0.0;
    double TextureResolveMs = 0.0;
    double ProjectionMs = 0.0;
    double RegionPlanMs = 0.0;
    double RegionAllocationMs = 0.0;
    double RasterMs = 0.0;
    double ResampleEncodeMs = 0.0;
    double DirectEncodeMs = 0.0;
    double NormalAwareResampleMs = 0.0;
    double TileBinningMs = 0.0;
    double WorkerTotalMs = 0.0;
    double CommitMs = 0.0;
    double UploadWaitMs = 0.0;
    double UploadQueueWaitMs = 0.0;
    double UploadSliceDelayMs = 0.0;
    double UploadStagingCopyMs = 0.0;
    double UploadSubmitCallMs = 0.0;
    double UploadPollDelayMs = 0.0;
    double UploadRenderCallbackLatencyMs = 0.0;
    double PresentationSwapMs = 0.0;
    uint64 UploadBytes = 0;
    uint64 UploadPreparedPayloadBytes = 0;
    uint64 UploadAvoidedStagingCopyBytes = 0;
    uint32 UploadRequestedRegionCount = 0;
    uint32 UploadSubmittedRegionCount = 0;
    uint32 UploadCompletedRegionCount = 0;
    uint32 UploadCoalescedRequestCount = 0;
    uint32 UploadQueueDepthAtSelection = 0;
    bool bFullTextureUpload = false;
    bool bUsedPreparedUpload = false;
    double EndToEndMs = 0.0;
    double CommitFinishedSeconds = 0.0;
};

struct FWetWrinkleIncrementalPreviewJobResult final : FDWCEditorWorkerJobResult
{
    TArray<FDWCEditorNormalRegionPayload> Regions;
    FDWCEditorPreviewRegionTarget Target;
    uint64 FirstSequence = 0;
    uint64 LastSequence = 0;
    uint64 AffectedPixelCount = 0;
    TArray<FIntRect> ProjectedOutputRects;
    TOptional<FWetWrinkleHoverPerformanceDiagnostics> HoverDiagnostics;
    bool bTouchesUVSeam = false;
};

/** Lightweight admitted hover payload. Projection and region allocation happen on the worker. */
struct FWetWrinkleProjectedHoverPreviewJobInput
{
    FDWCEditorSurfaceNormalPatchInput SurfaceInput;
    TSharedPtr<FDWCEditorSurfacePatchProjectionCacheService> ProjectionCache;
    FIntPoint TextureSize = FIntPoint::ZeroValue;
    FIntPoint WorkingTextureSize = FIntPoint::ZeroValue;
    TArray<FIntRect> PreviousOutputRects;
    FDWCEditorPreviewRegionTarget Target;
    bool bCollectPerformanceDiagnostics = false;
    FWetWrinkleHoverPerformanceDiagnostics PerformanceDiagnostics;
};

/** Immutable output of hover projection. No raster or encoded pixel buffers exist yet. */
struct FWetWrinkleProjectedHoverRasterPlan
{
    FIntPoint TextureSize = FIntPoint::ZeroValue;
    FIntPoint WorkingTextureSize = FIntPoint::ZeroValue;
    TArray<FWetWrinkleIncrementalCommand> Commands;
    TArray<FWetWrinkleIncrementalRegionPlan> Regions;
    TArray<TArray<int32>> RegionFragmentIndices;
    TArray<FIntRect> CurrentOutputRects;
    FDWCEditorPreviewRegionTarget Target;
    bool bTouchesUVSeam = false;
    bool bUseSparseRegions = false;
    bool bCollectPerformanceDiagnostics = false;
    FWetWrinkleHoverPerformanceDiagnostics PerformanceDiagnostics;

    uint64 GetRetainedSizeBytes() const;
};

class FWetWrinkleIncrementalPreviewWorker final
{
  public:
    static bool BuildRegionPlan(
        const TArray<FWetWrinkleIncrementalCommand>& Commands,
        FIntPoint WorkingTextureSize,
        FIntPoint TextureSize,
        TArray<FWetWrinkleIncrementalRegionPlan>& OutPlan,
        const TArray<FIntRect>* AdditionalWorkingRects = nullptr);

    static FDWCEditorWorkerMemoryEstimate EstimateMemory(
        const TArray<FWetWrinkleIncrementalCommand>& Commands,
        const TArray<FWetWrinkleIncrementalRegionPlan>& Plan,
        bool bWithCoverage);

    static FDWCEditorWorkerMemoryEstimate EstimateProjectedHoverMemory(
        const FDWCEditorSurfaceNormalPatchInput& SurfaceInput,
        FIntPoint WorkingTextureSize,
        FIntPoint TextureSize);

    static FDWCEditorWorkerMemoryEstimate EstimateProjectedHoverRasterMemory(
        const FWetWrinkleProjectedHoverRasterPlan& Plan);

    static TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>
        BuildProjectedHoverProjectionPhase(
            FWetWrinkleProjectedHoverPreviewJobInput Input,
            const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& CancellationToken);

    static TSharedPtr<FWetWrinkleIncrementalPreviewJobResult, ESPMode::ThreadSafe>
        BuildProjectedHoverRasterPhase(
            FWetWrinkleProjectedHoverRasterPlan Plan,
            const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& CancellationToken);

    static TSharedPtr<FWetWrinkleIncrementalPreviewJobResult, ESPMode::ThreadSafe> Build(
        FWetWrinkleIncrementalPreviewJobInput Input,
        const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& CancellationToken,
        FWetWrinkleHoverPerformanceDiagnostics* HoverDiagnostics = nullptr);

    static TSharedPtr<FWetWrinkleIncrementalPreviewJobResult, ESPMode::ThreadSafe> BuildProjectedHover(
        FWetWrinkleProjectedHoverPreviewJobInput Input,
        const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& CancellationToken);
};
