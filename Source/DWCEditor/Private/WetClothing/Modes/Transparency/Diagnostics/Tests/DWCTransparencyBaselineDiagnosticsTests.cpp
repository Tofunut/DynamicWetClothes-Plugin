// Copyright 2026 Team Tofunut. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "WetClothing/Modes/Transparency/Diagnostics/DWCTransparencyBaselineDiagnostics.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyBaselineDiagnosticsLifetimeTest,
    "DWC.Editor.Transparency.Diagnostics.BaselineLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyBaselineDiagnosticsLifetimeTest::RunTest(const FString&)
{
    FDWCTransparencyBaselineDiagnostics::Reset();
    FDWCTransparencyBaselineDiagnostics::RecordFullLayoutRebuild();
    FDWCTransparencyBaselineDiagnostics::RecordModelRefresh();
    FDWCTransparencyBaselineDiagnostics::RecordSourceModelRefresh();
    FDWCTransparencyBaselineDiagnostics::RecordStageContentRefresh();
    FDWCTransparencyBaselineDiagnostics::RecordLayerListRefresh();
    FDWCTransparencyBaselineDiagnostics::RecordStage2Generation(
        EDWCTransparencyBaselineGenerationOutcome::Completed, 12.5);
    FDWCTransparencyBaselineDiagnostics::RecordStage2Generation(
        EDWCTransparencyBaselineGenerationOutcome::Canceled, 4.0);
    FDWCTransparencyBaselineDiagnostics::RecordStage2Generation(
        EDWCTransparencyBaselineGenerationOutcome::Failed, 7.0);
    FDWCTransparencyBaselineDiagnostics::RecordBlueprintHierarchyBuild(true, 6, 9.0);
    FDWCTransparencyBaselineDiagnostics::RecordBlueprintHierarchyBuild(false, 2, 3.0);
    FDWCTransparencyBaselineDiagnostics::RecordResidentMaterialSurfaceHit();
    FDWCTransparencyBaselineDiagnostics::RecordPersistentMaterialSurfaceHit();
    FDWCTransparencyBaselineDiagnostics::RecordMaterialSurfaceBake();
    FDWCTransparencyBaselineDiagnostics::RecordProjectionSnapshotBuild(3, 4096);
    FDWCTransparencyBaselineDiagnostics::RecordBakedBaselineRestore(1024);

    const FDWCTransparencyBaselineSnapshot Snapshot =
        FDWCTransparencyBaselineDiagnostics::CaptureSnapshot();
    TestEqual(TEXT("Full layout rebuilds are retained"), Snapshot.FullLayoutRebuildCount, 1ull);
    TestEqual(TEXT("Model refreshes are retained"), Snapshot.ModelRefreshCount, 1ull);
    TestEqual(TEXT("Source-model refreshes are retained"), Snapshot.SourceModelRefreshCount, 1ull);
    TestEqual(TEXT("Stage content refreshes are retained"), Snapshot.StageContentRefreshCount, 1ull);
    TestEqual(TEXT("Layer list refreshes are retained"), Snapshot.LayerListRefreshCount, 1ull);
    TestEqual(TEXT("Every Stage 2 outcome contributes to the request count"),
        Snapshot.Stage2GenerationRequestCount, 3ull);
    TestEqual(TEXT("Completed Stage 2 requests are classified"),
        Snapshot.Stage2GenerationCompletedCount, 1ull);
    TestEqual(TEXT("Canceled Stage 2 requests are classified"),
        Snapshot.Stage2GenerationCanceledCount, 1ull);
    TestEqual(TEXT("Failed Stage 2 requests are classified"),
        Snapshot.Stage2GenerationFailedCount, 1ull);
    TestEqual(TEXT("Hierarchy material snapshots accumulate"),
        Snapshot.BlueprintMaterialSnapshotCount, 8ull);
    TestEqual(TEXT("Hierarchy failures are retained"),
        Snapshot.BlueprintHierarchyFailureCount, 1ull);
    TestEqual(TEXT("Resident material hits are retained"),
        Snapshot.ResidentMaterialSurfaceHitCount, 1ull);
    TestEqual(TEXT("Persistent material hits are retained"),
        Snapshot.PersistentMaterialSurfaceHitCount, 1ull);
    TestEqual(TEXT("Material bakes are retained"), Snapshot.MaterialSurfaceBakeCount, 1ull);
    TestEqual(TEXT("Projection source surface totals are retained"),
        Snapshot.ProjectionSourceSurfaceCount, 3ull);
    TestEqual(TEXT("Projection outer sample totals are retained"),
        Snapshot.ProjectionOuterSampleCount, 4096ull);
    TestEqual(TEXT("Baked baseline readback bytes are retained"),
        Snapshot.BakedBaselineReadbackBytes, 1024ull);

    FDWCTransparencyBaselineDiagnostics::Reset();
    const FDWCTransparencyBaselineSnapshot ResetSnapshot =
        FDWCTransparencyBaselineDiagnostics::CaptureSnapshot();
    TestEqual(TEXT("Reset clears Stage 2 requests"), ResetSnapshot.Stage2GenerationRequestCount, 0ull);
    TestEqual(TEXT("Reset clears material bakes"), ResetSnapshot.MaterialSurfaceBakeCount, 0ull);
    TestEqual(TEXT("Reset clears readback bytes"), ResetSnapshot.BakedBaselineReadbackBytes, 0ull);
    return true;
}

#endif
