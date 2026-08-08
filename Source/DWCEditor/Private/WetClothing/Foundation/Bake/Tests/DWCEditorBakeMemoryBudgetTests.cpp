// Copyright 2026 Team Tofunut. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "WetClothing/Foundation/Bake/DWCEditorBakeMemoryBudget.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorBakeMemoryBudgetTest,
    "DWC.Editor.Foundation.Bake.MemoryBudget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorBakeMemoryBudgetTest::RunTest(const FString&)
{
    FDWCEditorBakeMemoryBudget Budget;
    Budget.Configure(2, 100);

    TestFalse(TEXT("A single oversized snapshot is rejected"), Budget.IsSingleSnapshotAllowed(101));
    TestTrue(TEXT("First snapshot reserves memory"), Budget.TryReserve(60));
    TestFalse(TEXT("Second snapshot waits when it exceeds the remaining byte budget"), Budget.TryReserve(50));
    TestTrue(TEXT("Second snapshot fills the remaining byte budget"), Budget.TryReserve(40));
    TestEqual(TEXT("Job count never exceeds the batch limit"), Budget.GetInFlightJobs(), 2);
    TestEqual(TEXT("Reserved bytes never exceed the batch limit"), Budget.GetInFlightBytes(), 100ull);
    TestEqual(TEXT("Peak job count is recorded for diagnostics"), Budget.GetPeakInFlightJobs(), 2);
    TestEqual(TEXT("Peak bytes are recorded for diagnostics"), Budget.GetPeakInFlightBytes(), 100ull);
    TestEqual(TEXT("Largest accepted snapshot is recorded"), Budget.GetLargestReservedSnapshotBytes(), 60ull);

    Budget.Release(60);
    TestTrue(TEXT("Completed work frees capacity for the next snapshot"), Budget.TryReserve(60));
    TestEqual(TEXT("Released memory is accounted deterministically"), Budget.GetInFlightBytes(), 100ull);

    Budget.Release(40);
    Budget.Release(60);
    TestEqual(TEXT("All completed jobs release their snapshot memory"), Budget.GetInFlightJobs(), 0);
    TestEqual(TEXT("All completed jobs release their snapshot bytes"), Budget.GetInFlightBytes(), 0ull);
    return true;
}

#endif
