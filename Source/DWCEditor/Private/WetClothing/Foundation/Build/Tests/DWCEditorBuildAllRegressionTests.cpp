// Copyright 2026 Team Tofunut. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "WetClothing/Foundation/Build/DWCEditorBuildActionRegistry.h"
#include "WetClothing/Foundation/Build/DWCEditorBuildPlanResolver.h"
#include "WetClothing/Foundation/Jobs/DWCEditorCancellationToken.h"
#include "WetClothing/Foundation/Operations/DWCEditorOperationPhaseGraph.h"
#include "WetClothing/Foundation/Resources/DWCEditorResourceBroker.h"

namespace
{
    constexpr uint64 MiB = FDWCEditorResourceBudgetConfig::MiB;

    struct FBuildAllTestActionContract
    {
        EDWCEditorResourcePool Pool = EDWCEditorResourcePool::WorkerPrivateCPU;
        uint64 PeakBytes = 0;
    };

    struct FBuildAllTestResult
    {
        bool bSucceeded = false;
        bool bCanceled = false;
        int32 CompletionCount = 0;
        TArray<EDWCEditorBuildAction> ExecutedActions;
        TMap<EDWCEditorBuildAction, uint64> OutputDigests;
        FDWCEditorOperationPhaseGraphSnapshot PhaseSnapshot;
        FString Error;
    };

    FDWCEditorResourceBudgetConfig MakeBuildAllBudget()
    {
        FDWCEditorResourceBudgetConfig Config;
        Config.GlobalEditorCPUBytes = 512ull * MiB;
        Config.WorkerPrivateCPUBytes = 384ull * MiB;
        Config.AssetCommitCPUBytes = 256ull * MiB;
        Config.PreviewWorkspaceCPUBytes = 256ull * MiB;
        Config.SharedCacheCPUBytes = 256ull * MiB;
        Config.UploadStagingCPUBytes = 64ull * MiB;
        Config.PreviewGPUBytes = 256ull * MiB;
        Config.bAllowCPUPoolBorrowing = true;
        return Config;
    }

    FDWCEditorBuildStatusSnapshot MakeRequiredBuildAllSnapshot()
    {
        FDWCEditorBuildStatusSnapshot Snapshot;
        for (const FDWCEditorBuildActionDescriptor& Descriptor :
             FDWCEditorBuildActionRegistry::GetDescriptors())
        {
            FDWCEditorBuildActionStatus Status;
            Status.Action = Descriptor.Action;
            Status.State = EDWCEditorBuildActionState::Required;
            Snapshot.Actions.Add(Descriptor.Action, MoveTemp(Status));
        }
        return Snapshot;
    }

    FBuildAllTestActionContract GetActionContract(const EDWCEditorBuildAction Action)
    {
        switch (Action)
        {
        case EDWCEditorBuildAction::InitializeDataUV:
            return {EDWCEditorResourcePool::WorkerPrivateCPU, 48ull * MiB};
        case EDWCEditorBuildAction::BuildCPURuntimeData:
            return {EDWCEditorResourcePool::WorkerPrivateCPU, 96ull * MiB};
        case EDWCEditorBuildAction::BuildGPURuntimeData:
            return {EDWCEditorResourcePool::WorkerPrivateCPU, 128ull * MiB};
        case EDWCEditorBuildAction::BakeRenderProfileData:
            return {EDWCEditorResourcePool::WorkerPrivateCPU, 72ull * MiB};
        case EDWCEditorBuildAction::GenerateMaterials:
            return {EDWCEditorResourcePool::WorkerPrivateCPU, 80ull * MiB};
        case EDWCEditorBuildAction::BakeWrinkleTextures:
            return {EDWCEditorResourcePool::WorkerPrivateCPU, 220ull * MiB};
        case EDWCEditorBuildAction::BakeTransparencyTextures:
            return {EDWCEditorResourcePool::WorkerPrivateCPU, 300ull * MiB};
        case EDWCEditorBuildAction::RebakeAffectedTransparencyMaps:
            return {EDWCEditorResourcePool::WorkerPrivateCPU, 180ull * MiB};
        case EDWCEditorBuildAction::SaveAsset:
            return {EDWCEditorResourcePool::AssetCommitCPU, 128ull * MiB};
        case EDWCEditorBuildAction::Count:
            break;
        }
        return {};
    }

    uint64 MakeActionOutputDigest(const EDWCEditorBuildAction Action)
    {
        const uint64 ActionValue = static_cast<uint64>(Action) + 1ull;
        return 1469598103934665603ull ^ (ActionValue * 1099511628211ull);
    }

    uint64 MakeAggregateDigest(
        const FDWCEditorBuildPlan& Plan,
        const TMap<EDWCEditorBuildAction, uint64>& Outputs)
    {
        uint64 Digest = 1469598103934665603ull;
        for (const FDWCEditorBuildPlanStep& Step : Plan.Steps)
        {
            const uint64* Output = Outputs.Find(Step.Action);
            if (Output == nullptr)
            {
                continue;
            }
            Digest ^= *Output;
            Digest *= 1099511628211ull;
        }
        return Digest;
    }

    FDWCEditorResourceReservationRequest MakeBuildAllReservation(
        const EDWCEditorResourcePool Pool,
        const uint64 Bytes,
        const FGuid& SessionEpoch,
        const uint64 OperationId,
        const FString& DebugName)
    {
        FDWCEditorResourceReservationRequest Request;
        Request.Pool = Pool;
        Request.Bytes = Bytes;
        Request.Owner.Key.Namespace = TEXT("DWC.Test.BuildAll");
        Request.Owner.SessionEpoch = SessionEpoch;
        Request.Owner.OperationId = OperationId;
        Request.Owner.Generation = 1;
        Request.DebugName = DebugName;
        return Request;
    }

    class FBuildAllTestRunner
    {
    public:
        static FBuildAllTestResult Run(
            const FDWCEditorBuildPlan& Plan,
            const TSharedRef<FDWCEditorResourceBroker>& Broker,
            const FGuid& SessionId,
            const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& CancellationToken,
            const int32 CancelBeforeStep = INDEX_NONE)
        {
            FBuildAllTestResult Result;
            if (!Plan.IsExecutable())
            {
                Result.Error = TEXT("The Build All plan is blocked.");
                Complete(Result);
                return Result;
            }

            FDWCEditorExclusiveBuildRequest BuildRequest;
            BuildRequest.SessionId = SessionId;
            BuildRequest.AssetPath = TEXT("/Game/Test/WCA_BuildAllRegression");
            BuildRequest.DebugName = TEXT("Build All Required regression");
            TUniquePtr<FDWCEditorExclusiveBuildLease> ExclusiveLease =
                Broker->TryBeginExclusiveBuild(BuildRequest, &Result.Error);
            if (!ExclusiveLease.IsValid())
            {
                Complete(Result);
                return Result;
            }
            Broker->SetExclusiveBuildState(
                ExclusiveLease->GetScopeId(), EDWCEditorExclusiveBuildState::Active);

            FDWCEditorOperationPhaseGraph PhaseGraph;
            FName PreviousPhase;
            for (const FDWCEditorBuildPlanStep& Step : Plan.Steps)
            {
                const FDWCEditorBuildActionDescriptor* Descriptor =
                    FDWCEditorBuildActionRegistry::Find(Step.Action);
                if (Descriptor == nullptr)
                {
                    Result.Error = TEXT("A Build All action has no registry descriptor.");
                    break;
                }

                const FBuildAllTestActionContract Contract = GetActionContract(Step.Action);
                FDWCEditorOperationPhaseDescriptor Phase;
                Phase.Name = Descriptor->StableName;
                Phase.DebugName = Descriptor->DisplayName.ToString();
                Phase.Thread = EDWCEditorOperationPhaseThread::GameThread;
                if (!PreviousPhase.IsNone())
                {
                    Phase.Dependencies.Add(PreviousPhase);
                }
                Phase.Resources.AddPeak(Contract.Pool, Contract.PeakBytes);
                if (!PhaseGraph.AddPhase(MoveTemp(Phase), &Result.Error))
                {
                    break;
                }
                PreviousPhase = Descriptor->StableName;
            }

            if (Result.Error.IsEmpty() && !PhaseGraph.Validate(&Result.Error))
            {
                PhaseGraph.CancelOutstanding(Result.Error);
            }

            const FGuid SessionEpoch = FGuid::NewGuid();
            uint64 OperationId = 1;
            for (int32 StepIndex = 0;
                 Result.Error.IsEmpty() && StepIndex < Plan.Steps.Num();
                 ++StepIndex)
            {
                if (StepIndex == CancelBeforeStep)
                {
                    CancellationToken->Cancel();
                }
                if (CancellationToken->IsCanceled())
                {
                    Result.bCanceled = true;
                    PhaseGraph.CancelOutstanding(TEXT("Build All regression cancellation"));
                    break;
                }

                const FDWCEditorBuildPlanStep& Step = Plan.Steps[StepIndex];
                const FDWCEditorBuildActionDescriptor* Descriptor =
                    FDWCEditorBuildActionRegistry::Find(Step.Action);
                const FBuildAllTestActionContract Contract = GetActionContract(Step.Action);
                if (Descriptor == nullptr || !PhaseGraph.MarkRunning(Descriptor->StableName, &Result.Error))
                {
                    break;
                }

                FString AdmissionError;
                if (!Broker->CanAdmitWork(
                        SessionId,
                        EDWCEditorWorkClass::ExclusiveBuild,
                        ExclusiveLease->GetScopeId(),
                        &AdmissionError))
                {
                    Result.Error = MoveTemp(AdmissionError);
                    PhaseGraph.MarkFailed(Descriptor->StableName, Result.Error);
                    break;
                }

                FDWCEditorMemoryLease PhaseLease = Broker->GetResourceGovernor()->TryAcquire(
                    MakeBuildAllReservation(
                        Contract.Pool,
                        Contract.PeakBytes,
                        SessionEpoch,
                        OperationId++,
                        Descriptor->StableName.ToString()),
                    &Result.Error);
                if (!PhaseLease.IsValid())
                {
                    PhaseGraph.MarkFailed(Descriptor->StableName, Result.Error);
                    break;
                }

                Result.ExecutedActions.Add(Step.Action);
                Result.OutputDigests.Add(Step.Action, MakeActionOutputDigest(Step.Action));
                if (!PhaseGraph.MarkCompleted(Descriptor->StableName, &Result.Error))
                {
                    break;
                }
                PhaseLease.Reset();
            }

            if (!Result.Error.IsEmpty())
            {
                PhaseGraph.CancelOutstanding(Result.Error);
            }
            Result.bSucceeded = Result.Error.IsEmpty() && !Result.bCanceled && PhaseGraph.IsTerminal();
            Result.PhaseSnapshot = PhaseGraph.GetSnapshot();

            Broker->SetExclusiveBuildState(
                ExclusiveLease->GetScopeId(), EDWCEditorExclusiveBuildState::Retiring);
            ExclusiveLease.Reset();
            Complete(Result);
            return Result;
        }

    private:
        static void Complete(FBuildAllTestResult& Result)
        {
            ++Result.CompletionCount;
        }
    };

    FDWCEditorBuildPlan MakeSingleActionPlan(const EDWCEditorBuildAction Action)
    {
        FDWCEditorBuildPlan Plan;
        Plan.Steps.Add({Action});
        return Plan;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorBuildAllPeakMemoryTest,
    "DWC.Editor.Integration.BuildAll.PeakMemory",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorBuildAllPeakMemoryTest::RunTest(const FString&)
{
    const FDWCEditorBuildPlan Plan = FDWCEditorBuildPlanResolver::ResolveRequired(
        MakeRequiredBuildAllSnapshot());
    TestTrue(TEXT("The complete Build All plan is executable."), Plan.IsExecutable());

    const TSharedRef<FDWCEditorResourceBroker> Broker =
        FDWCEditorResourceBroker::Create(MakeBuildAllBudget());
    const TSharedRef<FDWCEditorResourceGovernor> Governor = Broker->GetResourceGovernor();
    const FGuid SessionId = Broker->OpenSession(TEXT("Build All peak-memory test"));

    FDWCEditorResourceReservationRequest CacheRequest = MakeBuildAllReservation(
        EDWCEditorResourcePool::SharedCacheCPU,
        240ull * MiB,
        FGuid::NewGuid(),
        1000,
        TEXT("Reclaimable preview cache"));
    CacheRequest.Owner.Key.Namespace = TEXT("DWC.Test.BuildAll.Cache");
    const TSharedPtr<FDWCEditorMemoryLease> CacheLease = MakeShared<FDWCEditorMemoryLease>(
        Governor->TryAcquire(CacheRequest));
    TestTrue(TEXT("The test starts with a resident preview cache."), CacheLease->IsValid());

    FDWCEditorReclaimParticipantDescriptor CacheParticipant;
    CacheParticipant.Name = TEXT("Build All test preview cache");
    CacheParticipant.ReservationOwnerNamespace = CacheRequest.Owner.Key.Namespace;
    CacheParticipant.ReservationSessionEpoch = CacheRequest.Owner.SessionEpoch;
    CacheParticipant.SessionId = SessionId;
    CacheParticipant.Pool = EDWCEditorResourcePool::SharedCacheCPU;
    CacheParticipant.Priority = EDWCEditorReclaimPriority::InactivePreview;
    CacheParticipant.QueryReclaimableBytes = [CacheLease]
    {
        return CacheLease->GetReservedBytes();
    };
    CacheParticipant.Reclaim = [CacheLease](const FDWCEditorResourceReclaimRequest&)
    {
        FDWCEditorResourceReclaimResult ReclaimResult;
        ReclaimResult.ImmediateBytes = CacheLease->GetReservedBytes();
        ReclaimResult.ReclaimedEntryCount = ReclaimResult.ImmediateBytes > 0 ? 1 : 0;
        CacheLease->Reset();
        return ReclaimResult;
    };
    Broker->RegisterParticipant(MoveTemp(CacheParticipant));

    const FBuildAllTestResult Result = FBuildAllTestRunner::Run(
        Plan,
        Broker,
        SessionId,
        MakeShared<FDWCEditorCancellationToken, ESPMode::ThreadSafe>());
    TestTrue(TEXT("Build All completes under the bounded global budget."), Result.bSucceeded);
    TestEqual(TEXT("Build All presents one terminal result."), Result.CompletionCount, 1);
    TestFalse(TEXT("Pressure reclaim releases the stale preview cache."), CacheLease->IsValid());

    const FDWCEditorResourceGovernorDiagnostics GovernorDiagnostics = Governor->GetDiagnostics();
    const FDWCEditorResourceBrokerDiagnostics BrokerDiagnostics = Broker->GetDiagnostics();
    // The 240 MiB preview cache remains resident through the 220 MiB wrinkle phase.
    // The following 300 MiB transparency phase creates admission pressure, reclaims
    // that cache, and therefore does not raise the process peak above 460 MiB.
    TestEqual(TEXT("Build All peak includes only the live baseline and current phase."),
        GovernorDiagnostics.GlobalCPUHighWaterBytes, 460ull * MiB);
    TestTrue(TEXT("Build All remains below the process-wide editor CPU budget."),
        GovernorDiagnostics.GlobalCPUHighWaterBytes < MakeBuildAllBudget().GlobalEditorCPUBytes);
    TestEqual(TEXT("Every Build All reservation is released at completion."),
        GovernorDiagnostics.GlobalCPUUsedBytes, 0ull);
    TestTrue(TEXT("No reservation owner survives Build All."),
        GovernorDiagnostics.Reservations.IsEmpty());
    TestTrue(TEXT("Build All pressure triggers at least one coordinated reclaim."),
        BrokerDiagnostics.SuccessfulReclaimCount >= 1);
    TestFalse(TEXT("The exclusive Build lease is released."),
        Broker->GetExclusiveBuildSnapshot().IsActive());

    Broker->CloseSession(SessionId);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorBuildAllCancellationTest,
    "DWC.Editor.Integration.BuildAll.Cancellation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorBuildAllCancellationTest::RunTest(const FString&)
{
    const FDWCEditorBuildPlan Plan = FDWCEditorBuildPlanResolver::ResolveRequired(
        MakeRequiredBuildAllSnapshot());
    const int32 CancelBeforeStep = Plan.Steps.IndexOfByPredicate(
        [](const FDWCEditorBuildPlanStep& Step)
        {
            return Step.Action == EDWCEditorBuildAction::BakeWrinkleTextures;
        });
    TestTrue(TEXT("The cancellation target exists in the Build All plan."),
        CancelBeforeStep != INDEX_NONE);

    const TSharedRef<FDWCEditorResourceBroker> Broker =
        FDWCEditorResourceBroker::Create(MakeBuildAllBudget());
    const TSharedRef<FDWCEditorResourceGovernor> Governor = Broker->GetResourceGovernor();
    const FGuid SessionId = Broker->OpenSession(TEXT("Build All cancellation test"));
    int32 SuspendCount = 0;
    int32 ResumeCount = 0;
    Broker->SetSessionBuildBarrierHooks(
        SessionId,
        [&SuspendCount, &ResumeCount](const bool bActive)
        {
            bActive ? ++SuspendCount : ++ResumeCount;
        },
        [] { return false; });

    const FBuildAllTestResult Result = FBuildAllTestRunner::Run(
        Plan,
        Broker,
        SessionId,
        MakeShared<FDWCEditorCancellationToken, ESPMode::ThreadSafe>(),
        CancelBeforeStep);
    TestTrue(TEXT("Build All reports cancellation."), Result.bCanceled);
    TestFalse(TEXT("A canceled Build All is not reported as successful."), Result.bSucceeded);
    TestEqual(TEXT("Cancellation presents exactly one terminal result."),
        Result.CompletionCount, 1);
    TestEqual(TEXT("Only phases before the cancellation point execute."),
        Result.ExecutedActions.Num(), CancelBeforeStep);
    TestFalse(TEXT("The canceled wrinkle action never commits output."),
        Result.OutputDigests.Contains(EDWCEditorBuildAction::BakeWrinkleTextures));
    TestFalse(TEXT("Downstream transparency output never commits."),
        Result.OutputDigests.Contains(EDWCEditorBuildAction::BakeTransparencyTextures));
    TestTrue(TEXT("The canceled phase graph is terminal."), Result.PhaseSnapshot.bTerminal);
    TestTrue(TEXT("The phase graph records cancellation."), Result.PhaseSnapshot.bCanceled);
    TestEqual(TEXT("Cancellation releases all CPU reservations."),
        Governor->GetDiagnostics().GlobalCPUUsedBytes, 0ull);
    TestTrue(TEXT("Cancellation leaves no reservation diagnostics."),
        Governor->GetDiagnostics().Reservations.IsEmpty());
    TestFalse(TEXT("Cancellation releases the exclusive Build scope."),
        Broker->GetExclusiveBuildSnapshot().IsActive());
    TestEqual(TEXT("Build All suspends the preview session once."), SuspendCount, 1);
    TestEqual(TEXT("Build All resumes the preview session once."), ResumeCount, 1);

    Broker->CloseSession(SessionId);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorBuildAllActionParityTest,
    "DWC.Editor.Integration.BuildAll.ActionParity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorBuildAllActionParityTest::RunTest(const FString&)
{
    const FDWCEditorBuildPlan Plan = FDWCEditorBuildPlanResolver::ResolveRequired(
        MakeRequiredBuildAllSnapshot());
    TestTrue(TEXT("The parity Build All plan is executable."), Plan.IsExecutable());

    const TSharedRef<FDWCEditorResourceBroker> BuildAllBroker =
        FDWCEditorResourceBroker::Create(MakeBuildAllBudget());
    const FGuid BuildAllSession = BuildAllBroker->OpenSession(TEXT("Build All parity batch"));
    const FBuildAllTestResult BuildAllResult = FBuildAllTestRunner::Run(
        Plan,
        BuildAllBroker,
        BuildAllSession,
        MakeShared<FDWCEditorCancellationToken, ESPMode::ThreadSafe>());
    TestTrue(TEXT("The batch Build All run succeeds."), BuildAllResult.bSucceeded);
    BuildAllBroker->CloseSession(BuildAllSession);

    TMap<EDWCEditorBuildAction, uint64> IndividualOutputs;
    for (const FDWCEditorBuildPlanStep& Step : Plan.Steps)
    {
        const TSharedRef<FDWCEditorResourceBroker> ActionBroker =
            FDWCEditorResourceBroker::Create(MakeBuildAllBudget());
        const FGuid ActionSession = ActionBroker->OpenSession(TEXT("Individual action parity"));
        const FBuildAllTestResult ActionResult = FBuildAllTestRunner::Run(
            MakeSingleActionPlan(Step.Action),
            ActionBroker,
            ActionSession,
            MakeShared<FDWCEditorCancellationToken, ESPMode::ThreadSafe>());
        TestTrue(TEXT("The individual Build action succeeds."), ActionResult.bSucceeded);
        if (const uint64* Output = ActionResult.OutputDigests.Find(Step.Action))
        {
            IndividualOutputs.Add(Step.Action, *Output);
        }
        TestEqual(TEXT("The individual action releases all reservations."),
            ActionBroker->GetResourceGovernor()->GetDiagnostics().GlobalCPUUsedBytes, 0ull);
        ActionBroker->CloseSession(ActionSession);
    }

    TestEqual(TEXT("Build All and individual execution produce the same output count."),
        BuildAllResult.OutputDigests.Num(), IndividualOutputs.Num());
    for (const FDWCEditorBuildPlanStep& Step : Plan.Steps)
    {
        const uint64* BatchOutput = BuildAllResult.OutputDigests.Find(Step.Action);
        const uint64* IndividualOutput = IndividualOutputs.Find(Step.Action);
        TestTrue(TEXT("Both execution modes produce an action output."),
            BatchOutput != nullptr && IndividualOutput != nullptr);
        if (BatchOutput != nullptr && IndividualOutput != nullptr)
        {
            TestEqual(TEXT("Build All output matches the corresponding individual action."),
                *BatchOutput, *IndividualOutput);
        }
    }
    TestEqual(TEXT("Build All and individual execution have the same aggregate signature."),
        MakeAggregateDigest(Plan, BuildAllResult.OutputDigests),
        MakeAggregateDigest(Plan, IndividualOutputs));

    const TSharedRef<FDWCEditorResourceBroker> RepeatBroker =
        FDWCEditorResourceBroker::Create(MakeBuildAllBudget());
    const FGuid RepeatSession = RepeatBroker->OpenSession(TEXT("Build All idempotency"));
    const FBuildAllTestResult RepeatResult = FBuildAllTestRunner::Run(
        Plan,
        RepeatBroker,
        RepeatSession,
        MakeShared<FDWCEditorCancellationToken, ESPMode::ThreadSafe>());
    TestTrue(TEXT("A repeated Build All run succeeds."), RepeatResult.bSucceeded);
    TestEqual(TEXT("Repeated Build All execution is deterministic."),
        MakeAggregateDigest(Plan, BuildAllResult.OutputDigests),
        MakeAggregateDigest(Plan, RepeatResult.OutputDigests));
    RepeatBroker->CloseSession(RepeatSession);
    return true;
}

#endif
