// Copyright 2026 Team Tofunut. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "WetClothing/Foundation/Operations/DWCEditorOperationPhaseGraph.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorOperationPhaseGraphLifecycleTest,
    "DWC.Editor.Foundation.Operations.PhaseGraphLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorOperationPhaseGraphLifecycleTest::RunTest(const FString&)
{
    FDWCEditorOperationPhaseGraph Graph;
    FDWCEditorOperationPhaseDescriptor Prepare;
    Prepare.Name = TEXT("Prepare");
    Prepare.Thread = EDWCEditorOperationPhaseThread::GameThread;
    Prepare.Resources.AddPeak(EDWCEditorResourcePool::WorkerPrivateCPU, 64);
    Prepare.Resources.AddRetained(EDWCEditorResourcePool::WorkerPrivateCPU, 32);
    TestTrue(TEXT("Prepare phase is registered"), Graph.AddPhase(MoveTemp(Prepare)));

    FDWCEditorOperationPhaseDescriptor Worker;
    Worker.Name = TEXT("Worker");
    Worker.Dependencies.Add(TEXT("Prepare"));
    Worker.Thread = EDWCEditorOperationPhaseThread::WorkerThread;
    Worker.Resources.AddPeak(EDWCEditorResourcePool::WorkerPrivateCPU, 128);
    Worker.Resources.AddRetained(EDWCEditorResourcePool::WorkerPrivateCPU, 16);
    TestTrue(TEXT("Worker phase is registered"), Graph.AddPhase(MoveTemp(Worker)));
    TestTrue(TEXT("The complete graph is valid"), Graph.Validate());

    TArray<FName> Ready = Graph.GetReadyPhases();
    TestEqual(TEXT("Only the root phase is initially ready"), Ready.Num(), 1);
    TestEqual(TEXT("Prepare is the initial ready phase"), Ready[0], FName(TEXT("Prepare")));
    TestTrue(TEXT("Prepare starts"), Graph.MarkRunning(TEXT("Prepare")));
    TestTrue(TEXT("Prepare completes"), Graph.MarkCompleted(TEXT("Prepare")));
    Ready = Graph.GetReadyPhases();
    TestEqual(TEXT("Worker becomes ready after its dependency"), Ready.Num(), 1);
    TestEqual(TEXT("Worker is now ready"), Ready[0], FName(TEXT("Worker")));
    TestTrue(TEXT("Worker starts"), Graph.MarkRunning(TEXT("Worker")));
    TestTrue(TEXT("Worker completes"), Graph.MarkCompleted(TEXT("Worker")));
    TestTrue(TEXT("A completed graph is terminal"), Graph.IsTerminal());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorOperationPhaseGraphValidationTest,
    "DWC.Editor.Foundation.Operations.PhaseGraphValidation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorOperationPhaseGraphValidationTest::RunTest(const FString&)
{
    FDWCEditorOperationPhaseGraph Graph;
    FDWCEditorOperationPhaseDescriptor Invalid;
    Invalid.Name = TEXT("Invalid");
    Invalid.Dependencies.Add(TEXT("Missing"));
    TestTrue(TEXT("A phase may be assembled before all descriptors are present"),
        Graph.AddPhase(MoveTemp(Invalid)));
    FString Error;
    TestFalse(TEXT("Validation rejects a missing dependency"), Graph.Validate(&Error));
    TestFalse(TEXT("Missing dependency validation reports a reason"), Error.IsEmpty());

    FDWCEditorOperationPhaseResourcePlan Resources;
    Resources.AddPeak(EDWCEditorResourcePool::WorkerPrivateCPU, 16);
    Resources.AddRetained(EDWCEditorResourcePool::WorkerPrivateCPU, 32);
    Error.Reset();
    TestFalse(TEXT("A phase cannot retain more memory than its peak"), Resources.IsValid(&Error));
    return true;
}

#endif
