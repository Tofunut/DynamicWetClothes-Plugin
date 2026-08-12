// Copyright 2026 Team Tofunut. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "WetClothing/DerivedAssets/Textures/Wrinkle/WetWrinkleNormalMapBaker.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FWetWrinkleBakeMemoryPlanPhaseTest,
    "DWC.Editor.Wrinkle.Bake.MemoryPlan.PhaseOwnership",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWetWrinkleBakeMemoryPlanPhaseTest::RunTest(const FString&)
{
    FWetWrinkleNormalMapBakeMemoryPlan Plan;
    Plan.SnapshotBytes = 10;
    Plan.RasterBytes = 20;
    Plan.PostProcessBytes = 30;
    Plan.OutputBytes = 40;
    Plan.CommitMetadataBytes = 5;

    TestEqual(TEXT("Worker admission includes immutable input, raster, post-process, and output"),
        Plan.GetWorkerBytes(), 100ull);
    TestEqual(TEXT("Commit admission retains only metadata and encoded output"),
        Plan.GetCommitBytes(), 45ull);
    TestTrue(TEXT("Commit phase does not retain worker raster memory"),
        Plan.GetCommitBytes() < Plan.GetWorkerBytes());
    return true;
}

#endif
