// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "WetClothing/Modes/Transparency/Diagnostics/DWCTransparencyBaselineDiagnostics.h"

#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/ScopeLock.h"
#include "WetClothing/Foundation/Preview/Diagnostics/DWCEditorPreviewDiagnostics.h"

DEFINE_LOG_CATEGORY_STATIC(LogDWCTransparencyBaseline, Log, All);

namespace
{
    struct FBaselineState
    {
        FCriticalSection Mutex;
        FDWCTransparencyBaselineSnapshot Snapshot;
    };

    FBaselineState& GetBaselineState()
    {
        static FBaselineState* State = new FBaselineState();
        return *State;
    }

    FAutoConsoleCommand DumpBaselineCommand(
        TEXT("dwc.Transparency.Baseline.Dump"),
        TEXT("Dumps Transparency workflow baseline counters and active preview diagnostics."),
        FConsoleCommandDelegate::CreateStatic([]()
        {
            FDWCTransparencyBaselineDiagnostics::Dump();
            FDWCEditorPreviewDiagnostics::DumpAllSessions();
        }));

    FAutoConsoleCommand ResetBaselineCommand(
        TEXT("dwc.Transparency.Baseline.Reset"),
        TEXT("Resets Transparency workflow and preview diagnostic counters."),
        FConsoleCommandDelegate::CreateStatic([]()
        {
            FDWCTransparencyBaselineDiagnostics::Reset();
            FDWCEditorPreviewDiagnostics::ResetAllCounters();
        }));
}

void FDWCTransparencyBaselineDiagnostics::RecordFullLayoutRebuild()
{
    FBaselineState& State = GetBaselineState();
    FScopeLock Lock(&State.Mutex);
    ++State.Snapshot.FullLayoutRebuildCount;
}

void FDWCTransparencyBaselineDiagnostics::RecordModelRefresh()
{
    FBaselineState& State = GetBaselineState();
    FScopeLock Lock(&State.Mutex);
    ++State.Snapshot.ModelRefreshCount;
}

void FDWCTransparencyBaselineDiagnostics::RecordSourceModelRefresh()
{
    FBaselineState& State = GetBaselineState();
    FScopeLock Lock(&State.Mutex);
    ++State.Snapshot.SourceModelRefreshCount;
}

void FDWCTransparencyBaselineDiagnostics::RecordStageContentRefresh()
{
    FBaselineState& State = GetBaselineState();
    FScopeLock Lock(&State.Mutex);
    ++State.Snapshot.StageContentRefreshCount;
}

void FDWCTransparencyBaselineDiagnostics::RecordLayerListRefresh()
{
    FBaselineState& State = GetBaselineState();
    FScopeLock Lock(&State.Mutex);
    ++State.Snapshot.LayerListRefreshCount;
}

void FDWCTransparencyBaselineDiagnostics::RecordStage2Generation(
    const EDWCTransparencyBaselineGenerationOutcome Outcome,
    const double DurationMilliseconds)
{
    FBaselineState& State = GetBaselineState();
    FScopeLock Lock(&State.Mutex);
    ++State.Snapshot.Stage2GenerationRequestCount;
    switch (Outcome)
    {
    case EDWCTransparencyBaselineGenerationOutcome::Completed:
        ++State.Snapshot.Stage2GenerationCompletedCount;
        break;
    case EDWCTransparencyBaselineGenerationOutcome::Canceled:
        ++State.Snapshot.Stage2GenerationCanceledCount;
        break;
    case EDWCTransparencyBaselineGenerationOutcome::Failed:
        ++State.Snapshot.Stage2GenerationFailedCount;
        break;
    }
    State.Snapshot.LastStage2GenerationMilliseconds = FMath::Max(DurationMilliseconds, 0.0);
    State.Snapshot.MaxStage2GenerationMilliseconds = FMath::Max(
        State.Snapshot.MaxStage2GenerationMilliseconds,
        State.Snapshot.LastStage2GenerationMilliseconds);
}

void FDWCTransparencyBaselineDiagnostics::RecordBlueprintHierarchyBuild(
    const bool bSucceeded,
    const int32 MaterialSnapshotCount,
    const double DurationMilliseconds)
{
    FBaselineState& State = GetBaselineState();
    FScopeLock Lock(&State.Mutex);
    ++State.Snapshot.BlueprintHierarchyBuildCount;
    State.Snapshot.BlueprintHierarchyFailureCount += bSucceeded ? 0 : 1;
    State.Snapshot.BlueprintMaterialSnapshotCount += FMath::Max(MaterialSnapshotCount, 0);
    State.Snapshot.LastBlueprintHierarchyMilliseconds = FMath::Max(DurationMilliseconds, 0.0);
    State.Snapshot.MaxBlueprintHierarchyMilliseconds = FMath::Max(
        State.Snapshot.MaxBlueprintHierarchyMilliseconds,
        State.Snapshot.LastBlueprintHierarchyMilliseconds);
}

void FDWCTransparencyBaselineDiagnostics::RecordResidentMaterialSurfaceHit()
{
    FBaselineState& State = GetBaselineState();
    FScopeLock Lock(&State.Mutex);
    ++State.Snapshot.ResidentMaterialSurfaceHitCount;
}

void FDWCTransparencyBaselineDiagnostics::RecordPersistentMaterialSurfaceHit()
{
    FBaselineState& State = GetBaselineState();
    FScopeLock Lock(&State.Mutex);
    ++State.Snapshot.PersistentMaterialSurfaceHitCount;
}

void FDWCTransparencyBaselineDiagnostics::RecordMaterialSurfaceBake()
{
    FBaselineState& State = GetBaselineState();
    FScopeLock Lock(&State.Mutex);
    ++State.Snapshot.MaterialSurfaceBakeCount;
}

void FDWCTransparencyBaselineDiagnostics::RecordProjectionSnapshotBuild(
    const int32 SourceSurfaceCount,
    const int32 OuterSampleCount)
{
    FBaselineState& State = GetBaselineState();
    FScopeLock Lock(&State.Mutex);
    ++State.Snapshot.ProjectionSnapshotBuildCount;
    State.Snapshot.ProjectionSourceSurfaceCount += FMath::Max(SourceSurfaceCount, 0);
    State.Snapshot.ProjectionOuterSampleCount += FMath::Max(OuterSampleCount, 0);
}

void FDWCTransparencyBaselineDiagnostics::RecordBakedBaselineRestore(const uint64 ReadbackBytes)
{
    FBaselineState& State = GetBaselineState();
    FScopeLock Lock(&State.Mutex);
    ++State.Snapshot.BakedBaselineRestoreCount;
    State.Snapshot.BakedBaselineReadbackBytes += ReadbackBytes;
}

FDWCTransparencyBaselineSnapshot FDWCTransparencyBaselineDiagnostics::CaptureSnapshot()
{
    FBaselineState& State = GetBaselineState();
    FScopeLock Lock(&State.Mutex);
    return State.Snapshot;
}

void FDWCTransparencyBaselineDiagnostics::Reset()
{
    FBaselineState& State = GetBaselineState();
    FScopeLock Lock(&State.Mutex);
    State.Snapshot = {};
}

void FDWCTransparencyBaselineDiagnostics::Dump()
{
    const FDWCTransparencyBaselineSnapshot Snapshot = CaptureSnapshot();
    UE_LOG(
        LogDWCTransparencyBaseline,
        Display,
        TEXT("Transparency baseline: UI={layout=%llu model=%llu sourceModel=%llu stage=%llu list=%llu} Stage2={requests=%llu completed=%llu canceled=%llu failed=%llu last=%.2fms max=%.2fms} Hierarchy={builds=%llu failed=%llu materialSnapshots=%llu last=%.2fms max=%.2fms}"),
        Snapshot.FullLayoutRebuildCount,
        Snapshot.ModelRefreshCount,
        Snapshot.SourceModelRefreshCount,
        Snapshot.StageContentRefreshCount,
        Snapshot.LayerListRefreshCount,
        Snapshot.Stage2GenerationRequestCount,
        Snapshot.Stage2GenerationCompletedCount,
        Snapshot.Stage2GenerationCanceledCount,
        Snapshot.Stage2GenerationFailedCount,
        Snapshot.LastStage2GenerationMilliseconds,
        Snapshot.MaxStage2GenerationMilliseconds,
        Snapshot.BlueprintHierarchyBuildCount,
        Snapshot.BlueprintHierarchyFailureCount,
        Snapshot.BlueprintMaterialSnapshotCount,
        Snapshot.LastBlueprintHierarchyMilliseconds,
        Snapshot.MaxBlueprintHierarchyMilliseconds);
    UE_LOG(
        LogDWCTransparencyBaseline,
        Display,
        TEXT("Transparency baseline: MaterialSurface={residentHits=%llu persistentHits=%llu bakes=%llu} Projection={snapshots=%llu sourceSurfaces=%llu outerSamples=%llu} BakedBaseline={restores=%llu readback=%.2fMiB}"),
        Snapshot.ResidentMaterialSurfaceHitCount,
        Snapshot.PersistentMaterialSurfaceHitCount,
        Snapshot.MaterialSurfaceBakeCount,
        Snapshot.ProjectionSnapshotBuildCount,
        Snapshot.ProjectionSourceSurfaceCount,
        Snapshot.ProjectionOuterSampleCount,
        Snapshot.BakedBaselineRestoreCount,
        static_cast<double>(Snapshot.BakedBaselineReadbackBytes) / (1024.0 * 1024.0));
}

FDWCTransparencyStage2GenerationDiagnosticScope::FDWCTransparencyStage2GenerationDiagnosticScope()
    : StartSeconds(FPlatformTime::Seconds())
{
}

FDWCTransparencyStage2GenerationDiagnosticScope::~FDWCTransparencyStage2GenerationDiagnosticScope()
{
    FDWCTransparencyBaselineDiagnostics::RecordStage2Generation(
        Outcome,
        (FPlatformTime::Seconds() - StartSeconds) * 1000.0);
}

void FDWCTransparencyStage2GenerationDiagnosticScope::MarkCompleted()
{
    Outcome = EDWCTransparencyBaselineGenerationOutcome::Completed;
}

void FDWCTransparencyStage2GenerationDiagnosticScope::MarkCanceled()
{
    Outcome = EDWCTransparencyBaselineGenerationOutcome::Canceled;
}

void FDWCTransparencyStage2GenerationDiagnosticScope::MarkFailed()
{
    Outcome = EDWCTransparencyBaselineGenerationOutcome::Failed;
}
