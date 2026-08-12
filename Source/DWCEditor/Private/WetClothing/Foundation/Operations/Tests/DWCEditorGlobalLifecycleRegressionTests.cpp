// Copyright 2026 Team Tofunut. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "WetClothing/DerivedAssets/Textures/Transparency/DWCTransparencyEditedMapBaker.h"
#include "WetClothing/Foundation/Operations/DWCEditorOperationPhaseGraph.h"
#include "WetClothing/Foundation/Resources/DWCEditorResourceBroker.h"

namespace
{
    FDWCEditorResourceBudgetConfig MakeLifecycleBudget()
    {
        constexpr uint64 LifecycleMiB = FDWCEditorResourceBudgetConfig::MiB;
        FDWCEditorResourceBudgetConfig Config;
        Config.GlobalEditorCPUBytes = 512ull * LifecycleMiB;
        Config.WorkerPrivateCPUBytes = 512ull * LifecycleMiB;
        Config.AssetCommitCPUBytes = 384ull * LifecycleMiB;
        Config.PreviewWorkspaceCPUBytes = 512ull * LifecycleMiB;
        Config.SharedCacheCPUBytes = 192ull * LifecycleMiB;
        Config.UploadStagingCPUBytes = 64ull * LifecycleMiB;
        Config.PreviewGPUBytes = 384ull * LifecycleMiB;
        Config.bAllowCPUPoolBorrowing = true;
        return Config;
    }

    FDWCEditorResourceReservationRequest MakeLifecycleRequest(
        const EDWCEditorResourcePool Pool,
        const uint64 Bytes,
        const FGuid SessionEpoch,
        const uint64 OperationId,
        const TCHAR* DebugName)
    {
        FDWCEditorResourceReservationRequest Request;
        Request.Pool = Pool;
        Request.Bytes = Bytes;
        Request.Owner.Key.Namespace = TEXT("DWC.Test.GlobalLifecycle");
        Request.Owner.SessionEpoch = SessionEpoch;
        Request.Owner.OperationId = OperationId;
        Request.Owner.Generation = 1;
        Request.DebugName = DebugName;
        return Request;
    }

    void AddLifecyclePhase(
        FDWCEditorOperationPhaseGraph& Graph,
        const FName Name,
        const FName Dependency,
        const EDWCEditorOperationPhaseThread Thread,
        const EDWCEditorResourcePool Pool,
        const uint64 PeakBytes)
    {
        FDWCEditorOperationPhaseDescriptor Phase;
        Phase.Name = Name;
        if (!Dependency.IsNone())
        {
            Phase.Dependencies.Add(Dependency);
        }
        Phase.Thread = Thread;
        Phase.Resources.AddPeak(Pool, PeakBytes);
        Phase.DebugName = Name.ToString();
        Graph.AddPhase(MoveTemp(Phase));
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorGlobalMemoryPhaseHandoffTest,
    "DWC.Editor.Integration.Memory.PhaseHandoff4K",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorGlobalMemoryPhaseHandoffTest::RunTest(const FString&)
{
    constexpr uint64 LifecycleMiB = FDWCEditorResourceBudgetConfig::MiB;
    FDWCTransparencyStage4MemoryPlan MemoryPlan;
    FString Error;
    const FIntPoint Resolution(4096, 4096);
    const uint64 SourceBytes =
        FDWCTransparencyEditedMapBaker::EstimateCanonicalSourcePayloadBytes(Resolution);
    TestTrue(TEXT("A full 4K Stage 4 request has a valid phase-aware memory plan."),
        FDWCTransparencyEditedMapBaker::BuildMemoryPlan(
            Resolution,
            SourceBytes,
            2ull * LifecycleMiB,
            true,
            true,
            true,
            MemoryPlan,
            Error));
    if (!Error.IsEmpty())
    {
        AddInfo(Error);
    }

    const FDWCEditorResourceBudgetConfig Config = MakeLifecycleBudget();
    TestTrue(TEXT("The 4K prepare phase fits the global editor CPU budget."),
        MemoryPlan.GetPreparePeakBytes() <= Config.GlobalEditorCPUBytes);
    TestTrue(TEXT("The 4K worker phase fits the global editor CPU budget."),
        MemoryPlan.GetWorkerPeakBytes() <= Config.GlobalEditorCPUBytes);

    const TSharedRef<FDWCEditorResourceBroker> Broker =
        FDWCEditorResourceBroker::Create(Config);
    const TSharedRef<FDWCEditorResourceGovernor> Governor = Broker->GetResourceGovernor();
    const FGuid SessionId = Broker->OpenSession(TEXT("4K phase handoff test"));
    const FGuid SessionEpoch = FGuid::NewGuid();

    const uint64 CommitPeakBytes = 4096ull * 4096ull * sizeof(FColor) * 2ull;
    FDWCEditorOperationPhaseGraph Graph;
    AddLifecyclePhase(Graph, TEXT("Prepare"), NAME_None,
        EDWCEditorOperationPhaseThread::GameThread,
        EDWCEditorResourcePool::WorkerPrivateCPU,
        MemoryPlan.GetPreparePeakBytes());
    AddLifecyclePhase(Graph, TEXT("Worker"), TEXT("Prepare"),
        EDWCEditorOperationPhaseThread::WorkerThread,
        EDWCEditorResourcePool::WorkerPrivateCPU,
        MemoryPlan.GetWorkerPeakBytes());
    AddLifecyclePhase(Graph, TEXT("Commit"), TEXT("Worker"),
        EDWCEditorOperationPhaseThread::GameThread,
        EDWCEditorResourcePool::AssetCommitCPU,
        CommitPeakBytes);
    TestTrue(TEXT("The WCA operation phase graph is valid."), Graph.Validate(&Error));

    uint64 OperationId = 1;
    const auto RunPhase = [&](const FName PhaseName, const EDWCEditorResourcePool Pool,
                              const uint64 Bytes)
    {
        TestTrue(*FString::Printf(TEXT("%s starts."), *PhaseName.ToString()),
            Graph.MarkRunning(PhaseName));
        FDWCEditorMemoryLease Lease = Governor->TryAcquire(
            MakeLifecycleRequest(Pool, Bytes, SessionEpoch, OperationId++,
                *PhaseName.ToString()),
            &Error);
        TestTrue(*FString::Printf(TEXT("%s memory is admitted."), *PhaseName.ToString()),
            Lease.IsValid());
        TestTrue(*FString::Printf(TEXT("%s stays under the global budget."), *PhaseName.ToString()),
            Governor->GetDiagnostics().GlobalCPUUsedBytes <= Config.GlobalEditorCPUBytes);
        TestTrue(*FString::Printf(TEXT("%s completes."), *PhaseName.ToString()),
            Graph.MarkCompleted(PhaseName));
        Lease.Reset();
        TestEqual(*FString::Printf(TEXT("%s releases its phase ownership."), *PhaseName.ToString()),
            Governor->GetDiagnostics().GlobalCPUUsedBytes, 0ull);
    };

    RunPhase(TEXT("Prepare"), EDWCEditorResourcePool::WorkerPrivateCPU,
        MemoryPlan.GetPreparePeakBytes());
    RunPhase(TEXT("Worker"), EDWCEditorResourcePool::WorkerPrivateCPU,
        MemoryPlan.GetWorkerPeakBytes());
    RunPhase(TEXT("Commit"), EDWCEditorResourcePool::AssetCommitCPU, CommitPeakBytes);

    const FDWCEditorResourceGovernorDiagnostics Diagnostics = Governor->GetDiagnostics();
    const uint64 ExpectedHighWater = FMath::Max3(
        MemoryPlan.GetPreparePeakBytes(), MemoryPlan.GetWorkerPeakBytes(), CommitPeakBytes);
    TestEqual(TEXT("Mutually exclusive phases use the largest phase peak, not their sum."),
        Diagnostics.GlobalCPUHighWaterBytes, ExpectedHighWater);
    TestTrue(TEXT("The completed phase graph is terminal."), Graph.IsTerminal());
    TestTrue(TEXT("No reservation survives the complete WCA operation."),
        Diagnostics.Reservations.IsEmpty());

    Broker->CloseSession(SessionId);
    TestEqual(TEXT("Closing the integration session returns the broker to baseline."),
        Broker->GetDiagnostics().SessionCount, 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorGlobalCancellationLifetimeTest,
    "DWC.Editor.Integration.Cancellation.ReleasesOwnership",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorGlobalCancellationLifetimeTest::RunTest(const FString&)
{
    const FDWCEditorResourceBudgetConfig Config = MakeLifecycleBudget();
    const TSharedRef<FDWCEditorResourceBroker> Broker =
        FDWCEditorResourceBroker::Create(Config);
    const TSharedRef<FDWCEditorResourceGovernor> Governor = Broker->GetResourceGovernor();
    const FGuid SessionId = Broker->OpenSession(TEXT("Cancellation lifetime test"));
    const FGuid SessionEpoch = FGuid::NewGuid();

    FDWCEditorOperationPhaseGraph Graph;
    AddLifecyclePhase(Graph, TEXT("Prepare"), NAME_None,
        EDWCEditorOperationPhaseThread::GameThread,
        EDWCEditorResourcePool::WorkerPrivateCPU, 32);
    AddLifecyclePhase(Graph, TEXT("Worker"), TEXT("Prepare"),
        EDWCEditorOperationPhaseThread::WorkerThread,
        EDWCEditorResourcePool::WorkerPrivateCPU, 128);
    AddLifecyclePhase(Graph, TEXT("Commit"), TEXT("Worker"),
        EDWCEditorOperationPhaseThread::GameThread,
        EDWCEditorResourcePool::AssetCommitCPU, 64);
    TestTrue(TEXT("The cancellation graph is valid."), Graph.Validate());
    TestTrue(TEXT("Prepare starts."), Graph.MarkRunning(TEXT("Prepare")));
    TestTrue(TEXT("Prepare completes."), Graph.MarkCompleted(TEXT("Prepare")));
    TestTrue(TEXT("Worker starts."), Graph.MarkRunning(TEXT("Worker")));

    FDWCEditorMemoryLease WorkerLease = Governor->TryAcquire(
        MakeLifecycleRequest(EDWCEditorResourcePool::WorkerPrivateCPU, 128,
            SessionEpoch, 1, TEXT("Cancelable worker")));
    TestTrue(TEXT("The canceled worker owns memory before cancellation."),
        WorkerLease.IsValid());

    uint64 ParticipantId = 0;
    FDWCEditorReclaimParticipantDescriptor Participant;
    Participant.Name = TEXT("Cancellation test participant");
    Participant.SessionId = SessionId;
    Participant.Pool = EDWCEditorResourcePool::WorkerPrivateCPU;
    Participant.QueryReclaimableBytes = [] { return 0ull; };
    Participant.Reclaim = [](const FDWCEditorResourceReclaimRequest&)
    {
        return FDWCEditorResourceReclaimResult();
    };
    ParticipantId = Broker->RegisterParticipant(MoveTemp(Participant));
    TestTrue(TEXT("The session owns a reclaim participant."), ParticipantId != 0);

    Graph.CancelOutstanding(TEXT("Editor session closed"));
    WorkerLease.Reset();
    Broker->CloseSession(SessionId);

    TestEqual(TEXT("Completed prepare work remains completed."),
        Graph.GetState(TEXT("Prepare")), EDWCEditorOperationPhaseState::Completed);
    TestEqual(TEXT("Active worker work becomes canceled."),
        Graph.GetState(TEXT("Worker")), EDWCEditorOperationPhaseState::Canceled);
    TestEqual(TEXT("Dependent commit work never starts."),
        Graph.GetState(TEXT("Commit")), EDWCEditorOperationPhaseState::Canceled);
    TestTrue(TEXT("A canceled graph is terminal."), Graph.IsTerminal());
    TestTrue(TEXT("Cancellation removes all ready phases."), Graph.GetReadyPhases().IsEmpty());
    TestEqual(TEXT("Cancellation releases all CPU reservations."),
        Governor->GetDiagnostics().GlobalCPUUsedBytes, 0ull);
    TestTrue(TEXT("Cancellation leaves no live reservation diagnostics."),
        Governor->GetDiagnostics().Reservations.IsEmpty());
    TestEqual(TEXT("Session close unregisters reclaim participants."),
        Broker->GetDiagnostics().ParticipantCount, 0);
    TestEqual(TEXT("Session close removes the broker session."),
        Broker->GetDiagnostics().SessionCount, 0);
    return true;
}

#endif
