//Copyright 2026 Team Tofunut. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Async/TaskGraphInterfaces.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "WetClothing/Foundation/Build/DWCEditorBuildOperationManager.h"
#include "WetClothing/Foundation/Jobs/DWCEditorWorkerJobScheduler.h"

namespace
{
    FDWCEditorWorkerJobTicket SubmitPreparedBuildTestWork(
        const TSharedRef<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe>& Scheduler,
        const FDWCEditorWorkerJobDescriptor& Descriptor,
        FDWCEditorWorkerJobScheduler::FWork Work,
        FDWCEditorWorkerJobScheduler::FApply Apply,
        FString* OutError = nullptr,
        FDWCEditorWorkerJobScheduler::FFinished Finished = nullptr)
    {
        return Scheduler->SubmitPrepared(
            Descriptor,
            [Work = MoveTemp(Work), Estimate = Descriptor.MemoryEstimate](
                const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>&,
                FDWCEditorWorkerJobScheduler::FPreparedWorkerJob& OutPrepared,
                FString&) mutable
            {
                OutPrepared.ActualMemoryEstimate = Estimate;
                OutPrepared.Work = MoveTemp(Work);
                return static_cast<bool>(OutPrepared.Work);
            },
            MoveTemp(Apply),
            OutError,
            MoveTemp(Finished));
    }

    bool PumpBuildOperationTestsUntil(
        TFunctionRef<bool()> Predicate,
        const double TimeoutSeconds = 5.0)
    {
        const double Deadline = FPlatformTime::Seconds() + TimeoutSeconds;
        while (!Predicate() && FPlatformTime::Seconds() < Deadline)
        {
            FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
            FPlatformProcess::Sleep(0.001f);
        }
        FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
        return Predicate();
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorBuildOperationExactlyOnceTest,
    "DWC.Editor.Foundation.Build.Operation.ExactlyOnce",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorBuildOperationExactlyOnceTest::RunTest(const FString& Parameters)
{
    TSharedRef<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe> Scheduler =
        MakeShared<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe>(1, 1024, 1024);
    TSharedRef<FDWCEditorBuildOperationManager> Manager =
        MakeShared<FDWCEditorBuildOperationManager>(Scheduler);

    int32 CompletionCount = 0;
    TSharedPtr<FDWCEditorBuildOperation> Operation = Manager->BeginOperation(
        EDWCEditorBuildAction::BakeWrinkleTextures,
        EDWCEditorAsyncRequestPolicy::Singleton,
        [&CompletionCount](const FDWCEditorBuildOperationResult&)
        {
            ++CompletionCount;
        });
    TestTrue(TEXT("The operation is accepted"), Operation.IsValid());
    TestTrue(TEXT("The action is active"), Manager->IsActionActive(EDWCEditorBuildAction::BakeWrinkleTextures));

    FDWCEditorBuildOperationResult Result;
    Result.Reason = EDWCEditorBuildTerminalReason::Succeeded;
    TestTrue(TEXT("The first terminal completion succeeds"), Operation->Complete(Result));
    TestFalse(TEXT("A second terminal completion is ignored"), Operation->Complete(Result));
    TestEqual(TEXT("Presentation completion runs exactly once"), CompletionCount, 1);
    TestFalse(TEXT("The completed action is no longer active"), Manager->IsActionActive(EDWCEditorBuildAction::BakeWrinkleTextures));

    Manager->BeginShutdown();
    Scheduler->Shutdown();
    Manager->CompleteShutdown();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorBuildOperationAdmissionPolicyTest,
    "DWC.Editor.Foundation.Build.Operation.AdmissionPolicy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorBuildOperationAdmissionPolicyTest::RunTest(const FString& Parameters)
{
    TSharedRef<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe> Scheduler =
        MakeShared<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe>(1, 1024, 1024);
    TSharedRef<FDWCEditorBuildOperationManager> Manager =
        MakeShared<FDWCEditorBuildOperationManager>(Scheduler);

    int32 SupersededCompletionCount = 0;
    TSharedPtr<FDWCEditorBuildOperation> First = Manager->BeginOperation(
        EDWCEditorBuildAction::BakeWrinkleTextures,
        EDWCEditorAsyncRequestPolicy::LatestWins,
        [&SupersededCompletionCount](const FDWCEditorBuildOperationResult& Result)
        {
            if (Result.Reason == EDWCEditorBuildTerminalReason::Superseded)
            {
                ++SupersededCompletionCount;
            }
        });
    TSharedPtr<FDWCEditorBuildOperation> Replacement = Manager->BeginOperation(
        EDWCEditorBuildAction::BakeWrinkleTextures,
        EDWCEditorAsyncRequestPolicy::LatestWins,
        [](const FDWCEditorBuildOperationResult&) {});
    TestTrue(TEXT("Latest-wins accepts the replacement"), First.IsValid() && Replacement.IsValid());
    TestTrue(TEXT("The older request is terminal"), First->IsTerminal());
    TestEqual(TEXT("The older request reports superseded exactly once"), SupersededCompletionCount, 1);
    TestTrue(TEXT("The replacement owns the action"), Manager->IsCurrent(Replacement.ToSharedRef()));

    FString CrossActionError;
    const TSharedPtr<FDWCEditorBuildOperation> Conflicting = Manager->BeginOperation(
        EDWCEditorBuildAction::BakeTransparencyTextures,
        EDWCEditorAsyncRequestPolicy::Singleton,
        [](const FDWCEditorBuildOperationResult&) {},
        &CrossActionError);
    TestFalse(TEXT("A second mutating action is rejected"), Conflicting.IsValid());
    TestFalse(TEXT("The rejection explains the ownership conflict"), CrossActionError.IsEmpty());

    FString FifoError;
    const TSharedPtr<FDWCEditorBuildOperation> QueuedWithoutQueue = Manager->BeginOperation(
        EDWCEditorBuildAction::BakeWrinkleTextures,
        EDWCEditorAsyncRequestPolicy::FIFO,
        [](const FDWCEditorBuildOperationResult&) {},
        &FifoError);
    TestFalse(TEXT("FIFO does not orphan an active logical operation"), QueuedWithoutQueue.IsValid());
    TestFalse(TEXT("FIFO rejection is reported"), FifoError.IsEmpty());

    Manager->BeginShutdown();
    Scheduler->Shutdown();
    Manager->CompleteShutdown();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorBuildOperationTicketCancellationIsolationTest,
    "DWC.Editor.Foundation.Build.Operation.TicketCancellationIsolation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorBuildOperationTicketCancellationIsolationTest::RunTest(const FString& Parameters)
{
    TSharedRef<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe> Scheduler =
        MakeShared<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe>(1, 1024, 1024);

    FDWCEditorWorkerJobDescriptor Descriptor;
    Descriptor.Key.Kind = EDWCEditorWorkerJobKind::WrinkleBake;
    Descriptor.Key.MaterialSlotIndex = 7;
    Descriptor.MemoryEstimate.SnapshotBytes = 1;
    Descriptor.RequestPolicy = EDWCEditorAsyncRequestPolicy::FIFO;

    EDWCEditorWorkerJobCompletion FirstCompletion = EDWCEditorWorkerJobCompletion::Applied;
    EDWCEditorWorkerJobCompletion SecondCompletion = EDWCEditorWorkerJobCompletion::Failed;
    bool bFirstFinished = false;
    bool bSecondFinished = false;
    const FDWCEditorWorkerJobTicket FirstTicket = SubmitPreparedBuildTestWork(Scheduler,
        Descriptor,
        [](const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& Token)
        {
            FPlatformProcess::Sleep(0.03f);
            TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe> Result =
                MakeShared<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>();
            Result->bSucceeded = !Token->IsCanceled();
            return Result;
        },
        [](const FDWCEditorWorkerJobTicket&, TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>) {},
        nullptr,
        [&bFirstFinished, &FirstCompletion](const FDWCEditorWorkerJobTicket&, const EDWCEditorWorkerJobCompletion Completion, const FString&)
        {
            bFirstFinished = true;
            FirstCompletion = Completion;
        });
    const FDWCEditorWorkerJobTicket SecondTicket = SubmitPreparedBuildTestWork(Scheduler,
        Descriptor,
        [](const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>&)
        {
            return MakeShared<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>();
        },
        [](const FDWCEditorWorkerJobTicket&, TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>) {},
        nullptr,
        [&bSecondFinished, &SecondCompletion](const FDWCEditorWorkerJobTicket&, const EDWCEditorWorkerJobCompletion Completion, const FString&)
        {
            bSecondFinished = true;
            SecondCompletion = Completion;
        });

    TestTrue(TEXT("Both FIFO tickets are accepted"), FirstTicket.IsValid() && SecondTicket.IsValid());
    TestTrue(TEXT("The first ticket is canceled directly"), Scheduler->CancelTicket(FirstTicket));
    TestTrue(TEXT("Both requests retire"), PumpBuildOperationTestsUntil([&]() { return bFirstFinished && bSecondFinished; }));
    TestEqual(TEXT("The selected ticket is canceled"), FirstCompletion, EDWCEditorWorkerJobCompletion::Canceled);
    TestEqual(TEXT("The newer same-key ticket still applies"), SecondCompletion, EDWCEditorWorkerJobCompletion::Applied);
    Scheduler->Shutdown();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorBuildOperationShutdownTest,
    "DWC.Editor.Foundation.Build.Operation.ShutdownDetachesPresentation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorBuildOperationShutdownTest::RunTest(const FString& Parameters)
{
    TSharedRef<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe> Scheduler =
        MakeShared<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe>(1, 1024, 1024);
    TSharedRef<FDWCEditorBuildOperationManager> Manager =
        MakeShared<FDWCEditorBuildOperationManager>(Scheduler);
    int32 PresentationCount = 0;
    TSharedPtr<FDWCEditorBuildOperation> Operation = Manager->BeginOperation(
        EDWCEditorBuildAction::BakeTransparencyTextures,
        EDWCEditorAsyncRequestPolicy::Singleton,
        [&PresentationCount](const FDWCEditorBuildOperationResult&)
        {
            ++PresentationCount;
        });

    Manager->BeginShutdown();
    Scheduler->Shutdown();
    Manager->CompleteShutdown();
    TestTrue(TEXT("The operation reaches a terminal state during shutdown"), Operation->IsTerminal());
    TestEqual(TEXT("Shutdown does not call a detached UI callback"), PresentationCount, 0);
    TestTrue(TEXT("The manager releases all operation ownership"), Manager->GetSnapshots().IsEmpty());
    return true;
}

#endif
