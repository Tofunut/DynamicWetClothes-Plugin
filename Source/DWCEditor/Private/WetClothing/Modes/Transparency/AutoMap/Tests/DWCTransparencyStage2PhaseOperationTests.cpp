// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "WetClothing/Foundation/Resources/DWCEditorAccountedMemory.h"
#include "WetClothing/Modes/Transparency/AutoMap/DWCTransparencyStage2PhaseOperation.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
    FDWCEditorAsyncOperationIdentity MakeOwner()
    {
        FDWCEditorAsyncOperationIdentity Owner;
        Owner.Key.Namespace = TEXT("DWC.Test.Transparency.Stage2");
        Owner.Key.MaterialSlotIndex = 3;
        Owner.Key.ResourceGuid = FGuid::NewGuid();
        Owner.SessionEpoch = FGuid::NewGuid();
        Owner.OperationId = 1;
        Owner.Generation = 1;
        Owner.Domain = EDWCEditorAuthoringDomain::Transparency;
        return Owner;
    }

    FDWCEditorResourceBudgetConfig MakeBudget()
    {
        FDWCEditorResourceBudgetConfig Budget;
        Budget.GlobalEditorCPUBytes = 4096;
        Budget.WorkerPrivateCPUBytes = 2048;
        Budget.PreviewWorkspaceCPUBytes = 2048;
        Budget.AssetCommitCPUBytes = 512;
        Budget.SharedCacheCPUBytes = 512;
        Budget.UploadStagingCPUBytes = 512;
        return Budget;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyStage2PhaseOwnershipTest,
    "DWC.Editor.Transparency.Stage2.ResourcePhaseOwnership",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyStage2PhaseOwnershipTest::RunTest(const FString&)
{
    const TSharedRef<FDWCEditorResourceGovernor> Governor =
        MakeShared<FDWCEditorResourceGovernor>(MakeBudget());
    const FDWCEditorAsyncOperationIdentity Owner = MakeOwner();
    FDWCTransparencyStage2PhaseOperation Operation(Governor, Owner, false);
    FString Error;

    FDWCTransparencyStage2PhaseResources Prepare;
    Prepare.WorkerPeakBytes = 256;
    Prepare.WorkerRetainedBytes = 192;
    TestTrue(TEXT("Target preparation is admitted."), Operation.BeginPhase(
        EDWCTransparencyStage2OperationPhase::PrepareTarget, Prepare, Error));

    FDWCTransparencyStage2PhaseResources Raster;
    Raster.WorkerPeakBytes = 768;
    Raster.WorkerRetainedBytes = 640;
    TestTrue(TEXT("Target rasterization replaces the previous phase ownership."),
        Operation.BeginPhase(
            EDWCTransparencyStage2OperationPhase::RasterizeTarget, Raster, Error));

    FDWCTransparencyStage2PhaseResources Sources;
    Sources.WorkerPeakBytes = 1024;
    Sources.WorkerRetainedBytes = 896;
    TestTrue(TEXT("Source preparation is admitted."), Operation.BeginPhase(
        EDWCTransparencyStage2OperationPhase::PrepareSources, Sources, Error));

    FDWCTransparencyStage2PhaseResources Projection;
    Projection.WorkerPeakBytes = 1408;
    Projection.WorkerRetainedBytes = 896;
    TestTrue(TEXT("Streaming projection owns bounded scratch."), Operation.BeginPhase(
        EDWCTransparencyStage2OperationPhase::StreamProjection, Projection, Error));

    FDWCTransparencyStage2PhaseResources Compose;
    Compose.WorkerPeakBytes = 896;
    Compose.WorkerRetainedBytes = 896;
    TestTrue(TEXT("Composition keeps only the snapshot/result allocation."),
        Operation.BeginPhase(
            EDWCTransparencyStage2OperationPhase::ComposeResult, Compose, Error));

    FDWCTransparencyStage2PhaseResources Transfer;
    Transfer.WorkerPeakBytes = 896;
    Transfer.PreviewPeakBytes = 512;
    Transfer.PreviewRetainedBytes = 512;
    TestTrue(TEXT("Result transfer overlaps producer and retained ownership."),
        Operation.BeginPhase(
            EDWCTransparencyStage2OperationPhase::TransferResult, Transfer, Error));
    TestTrue(TEXT("Every Stage 2 phase completes exactly once."), Operation.Complete(Error));

    FDWCEditorMemoryLease PreviewLease = Operation.TakeRetainedLease(
        EDWCEditorResourcePool::PreviewWorkspaceCPU);
    TestTrue(TEXT("The retained preview lease can be transferred."), PreviewLease.IsValid());
    TestEqual(TEXT("The transferred lease has the result size."),
        PreviewLease.GetReservedBytes(), 512ull);

    FDWCEditorAccountedMemory Account;
    Account.Configure(
        Governor,
        EDWCEditorResourcePool::PreviewWorkspaceCPU,
        Owner,
        TEXT("Stage 2 test payload"));
    TestTrue(TEXT("The payload adopts the existing reservation without re-admission."),
        Account.AdoptExistingLease(MoveTemp(PreviewLease), 512, &Error));
    const FDWCEditorResourceGovernorDiagnostics Retained = Governor->GetDiagnostics();
    TestEqual(TEXT("Only the transferred preview payload remains reserved."),
        Retained.GlobalCPUUsedBytes, 512ull);
    Account.Reset();
    TestEqual(TEXT("All CPU reservations retire with the payload."),
        Governor->GetDiagnostics().GlobalCPUUsedBytes, 0ull);

    const FDWCEditorOperationPhaseGraphSnapshot Snapshot = Operation.GetSnapshot();
    TestTrue(TEXT("The phase graph is terminal."), Snapshot.bTerminal);
    TestEqual(TEXT("The canonical Stage 2 graph contains six phases."),
        Snapshot.Phases.Num(), 6);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyStage2PhaseCancellationTest,
    "DWC.Editor.Transparency.Stage2.ResourcePhaseCancellation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyStage2PhaseCancellationTest::RunTest(const FString&)
{
    const TSharedRef<FDWCEditorResourceGovernor> Governor =
        MakeShared<FDWCEditorResourceGovernor>(MakeBudget());
    FDWCTransparencyStage2PhaseOperation Operation(Governor, MakeOwner(), false);
    FDWCTransparencyStage2PhaseResources Prepare;
    Prepare.WorkerPeakBytes = 512;
    Prepare.WorkerRetainedBytes = 512;
    FString Error;
    TestTrue(TEXT("The first phase starts."), Operation.BeginPhase(
        EDWCTransparencyStage2OperationPhase::PrepareTarget, Prepare, Error));
    Operation.Cancel(TEXT("Regression cancellation"));
    TestTrue(TEXT("Cancellation makes the graph terminal."), Operation.GetSnapshot().bTerminal);
    TestEqual(TEXT("Cancellation releases every reservation."),
        Governor->GetDiagnostics().GlobalCPUUsedBytes, 0ull);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCTransparencyStage2PhaseAdmissionFailureTest,
    "DWC.Editor.Transparency.Stage2.ResourcePhaseAdmissionFailure",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCTransparencyStage2PhaseAdmissionFailureTest::RunTest(const FString&)
{
    FDWCEditorResourceBudgetConfig Budget = MakeBudget();
    Budget.GlobalEditorCPUBytes = 128;
    Budget.WorkerPrivateCPUBytes = 128;
    const TSharedRef<FDWCEditorResourceGovernor> Governor =
        MakeShared<FDWCEditorResourceGovernor>(Budget);
    FDWCTransparencyStage2PhaseOperation Operation(Governor, MakeOwner(), false);
    FDWCTransparencyStage2PhaseResources Oversized;
    Oversized.WorkerPeakBytes = 256;
    Oversized.WorkerRetainedBytes = 256;
    FString Error;
    TestFalse(TEXT("An oversized phase is rejected before execution."), Operation.BeginPhase(
        EDWCTransparencyStage2OperationPhase::PrepareTarget, Oversized, Error));
    TestFalse(TEXT("Admission failure reports a useful error."), Error.IsEmpty());
    TestTrue(TEXT("Admission failure makes the graph terminal."),
        Operation.GetSnapshot().bTerminal);
    TestEqual(TEXT("A rejected phase leaves no partial reservation."),
        Governor->GetDiagnostics().GlobalCPUUsedBytes, 0ull);
    return true;
}

#endif
