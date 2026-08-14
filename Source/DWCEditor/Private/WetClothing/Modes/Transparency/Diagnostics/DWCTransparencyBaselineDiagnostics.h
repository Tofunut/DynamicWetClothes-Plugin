// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

enum class EDWCTransparencyBaselineGenerationOutcome : uint8
{
    Completed,
    Canceled,
    Failed
};

struct FDWCTransparencyBaselineSnapshot
{
    uint64 FullLayoutRebuildCount = 0;
    uint64 ModelRefreshCount = 0;
    uint64 SourceModelRefreshCount = 0;
    uint64 StageContentRefreshCount = 0;
    uint64 LayerListRefreshCount = 0;

    uint64 Stage2GenerationRequestCount = 0;
    uint64 Stage2GenerationCompletedCount = 0;
    uint64 Stage2GenerationCanceledCount = 0;
    uint64 Stage2GenerationFailedCount = 0;
    double LastStage2GenerationMilliseconds = 0.0;
    double MaxStage2GenerationMilliseconds = 0.0;

    uint64 BlueprintHierarchyBuildCount = 0;
    uint64 BlueprintHierarchyFailureCount = 0;
    uint64 BlueprintMaterialSnapshotCount = 0;
    double LastBlueprintHierarchyMilliseconds = 0.0;
    double MaxBlueprintHierarchyMilliseconds = 0.0;

    uint64 ResidentMaterialSurfaceHitCount = 0;
    uint64 PersistentMaterialSurfaceHitCount = 0;
    uint64 MaterialSurfaceBakeCount = 0;
    uint64 ProjectionSnapshotBuildCount = 0;
    uint64 ProjectionSourceSurfaceCount = 0;
    uint64 ProjectionOuterSampleCount = 0;

    uint64 BakedBaselineRestoreCount = 0;
    uint64 BakedBaselineReadbackBytes = 0;
};

/** Low-overhead counters used to compare Transparency editor changes against a stable baseline. */
class FDWCTransparencyBaselineDiagnostics final
{
public:
    static void RecordFullLayoutRebuild();
    static void RecordModelRefresh();
    static void RecordSourceModelRefresh();
    static void RecordStageContentRefresh();
    static void RecordLayerListRefresh();
    static void RecordStage2Generation(
        EDWCTransparencyBaselineGenerationOutcome Outcome,
        double DurationMilliseconds);
    static void RecordBlueprintHierarchyBuild(
        bool bSucceeded,
        int32 MaterialSnapshotCount,
        double DurationMilliseconds);
    static void RecordResidentMaterialSurfaceHit();
    static void RecordPersistentMaterialSurfaceHit();
    static void RecordMaterialSurfaceBake();
    static void RecordProjectionSnapshotBuild(int32 SourceSurfaceCount, int32 OuterSampleCount);
    static void RecordBakedBaselineRestore(uint64 ReadbackBytes);

    static FDWCTransparencyBaselineSnapshot CaptureSnapshot();
    static void Reset();
    static void Dump();
};

class FDWCTransparencyStage2GenerationDiagnosticScope final
{
public:
    FDWCTransparencyStage2GenerationDiagnosticScope();
    ~FDWCTransparencyStage2GenerationDiagnosticScope();

    void MarkCompleted();
    void MarkCanceled();
    void MarkFailed();

private:
    double StartSeconds = 0.0;
    EDWCTransparencyBaselineGenerationOutcome Outcome =
        EDWCTransparencyBaselineGenerationOutcome::Failed;
};
