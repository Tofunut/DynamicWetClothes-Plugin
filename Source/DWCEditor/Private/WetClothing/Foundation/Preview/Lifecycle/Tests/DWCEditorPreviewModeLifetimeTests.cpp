// Copyright 2026 Team Tofunut. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Async/TaskGraphInterfaces.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/AutomationTest.h"
#include "WetClothing/Foundation/Jobs/DWCEditorWorkerJobScheduler.h"
#include "WetClothing/Foundation/Preview/Lifecycle/DWCEditorPreviewModeLifetime.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorPreviewModeLifetimeGenerationTest,
    "DWC.Editor.Lifecycle.PreviewModeGeneration",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorPreviewModeLifetimeGenerationTest::RunTest(const FString&)
{
    const FGuid SessionEpoch = FGuid::NewGuid();
    FDWCEditorPreviewModeLifetime Lifetime(EDWCEditorPreviewMode::Wrinkle, SessionEpoch);

    TestFalse(TEXT("An inactive mode cannot issue work."), Lifetime.CaptureToken().IsValid());
    Lifetime.Activate(2);
    const FDWCEditorPreviewRunToken FirstToken = Lifetime.CaptureToken();
    TestTrue(TEXT("An active mode issues a current token."), FirstToken.IsCurrent());

    Lifetime.Suspend(3);
    TestFalse(TEXT("Suspending invalidates the previous token."), FirstToken.IsCurrent());
    TestFalse(TEXT("A suspended mode cannot issue a token."), Lifetime.CaptureToken().IsValid());

    Lifetime.Activate(4);
    const FDWCEditorPreviewRunToken ResumedToken = Lifetime.CaptureToken();
    TestTrue(TEXT("Resume issues a new current token."), ResumedToken.IsCurrent());
    TestNotEqual(TEXT("Resume advances generation."), ResumedToken.Generation, FirstToken.Generation);

    Lifetime.Deactivate(4);
    TestFalse(TEXT("A mode switch invalidates the active token."), ResumedToken.IsCurrent());
    Lifetime.Revoke(5);
    Lifetime.Activate(6);
    TestEqual(TEXT("Revocation is terminal."),
              Lifetime.GetRunState(), EDWCEditorPreviewModeRunState::Revoked);
    TestFalse(TEXT("A revoked mode cannot issue work."), Lifetime.CaptureToken().IsValid());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorPreviewModeWorkerLifetimeTest,
    "DWC.Editor.Lifecycle.PreviewWorkerLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorPreviewModeWorkerLifetimeTest::RunTest(const FString&)
{
    TSharedRef<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe> Scheduler =
        MakeShared<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe>(1, 1024, 1024, 8);
    TSharedRef<FDWCEditorPreviewModeLifetime> Lifetime =
        MakeShared<FDWCEditorPreviewModeLifetime>(
            EDWCEditorPreviewMode::Wrinkle,
            Scheduler->GetSessionEpoch());

    Scheduler->SetPreviewLifecycleProvider(
        [Lifetime](const FDWCEditorWorkerJobDescriptor&)
        {
            return Lifetime->CaptureToken();
        });

    FDWCEditorWorkerJobDescriptor Descriptor;
    Descriptor.Key.Kind = EDWCEditorWorkerJobKind::WrinkleHoverPreview;
    Descriptor.Key.MaterialSlotIndex = 3;
    Descriptor.Priority = EDWCEditorWorkerJobPriority::Interactive;
    Descriptor.RequestPolicy = EDWCEditorAsyncRequestPolicy::LatestWins;
    Descriptor.WorkClass = EDWCEditorWorkClass::InteractivePreview;
    Descriptor.MemoryEstimate.SnapshotBytes = 1;

    FString Error;
    const FDWCEditorWorkerJobTicket Rejected = Scheduler->SubmitPrepared(
        Descriptor,
        [](const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>&,
           FDWCEditorWorkerJobScheduler::FPreparedWorkerJob&,
           FString&) { return false; },
        [](const FDWCEditorWorkerJobTicket&,
           TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>) {},
        &Error);
    TestFalse(TEXT("An inactive mode is rejected before prepare."), Rejected.IsValid());
    TestTrue(TEXT("Inactive rejection reports the lifecycle gate."), Error.Contains(TEXT("not interactive")));
    TestEqual(TEXT("Lifecycle rejection is diagnosed."),
              Scheduler->GetDiagnostics().PreviewLifecycleRejectionCount, uint64{1});

    Lifetime->Activate(2);
    FEvent* WorkerGate = FPlatformProcess::GetSynchEventFromPool(false);
    bool bApplied = false;
    bool bFinished = false;
    EDWCEditorWorkerJobCompletion Completion = EDWCEditorWorkerJobCompletion::Failed;
    const FDWCEditorWorkerJobTicket Ticket = Scheduler->SubmitPrepared(
        Descriptor,
        [WorkerGate](const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>&,
                     FDWCEditorWorkerJobScheduler::FPreparedWorkerJob& OutPrepared,
                     FString&)
        {
            OutPrepared.ActualMemoryEstimate.SnapshotBytes = 1;
            OutPrepared.Work = [WorkerGate](
                const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>&)
            {
                WorkerGate->Wait();
                return MakeShared<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>();
            };
            return true;
        },
        [&bApplied](const FDWCEditorWorkerJobTicket&,
                    TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>)
        {
            bApplied = true;
        },
        &Error,
        [&bFinished, &Completion](const FDWCEditorWorkerJobTicket&,
                                  const EDWCEditorWorkerJobCompletion InCompletion,
                                  const FString&)
        {
            bFinished = true;
            Completion = InCompletion;
        });
    TestTrue(TEXT("An active mode admits interactive work."), Ticket.IsValid());

    Lifetime->Suspend(3);
    Scheduler->CancelPreviewMode(EDWCEditorPreviewMode::Wrinkle);
    WorkerGate->Trigger();

    const double Deadline = FPlatformTime::Seconds() + 5.0;
    while (!bFinished && FPlatformTime::Seconds() < Deadline)
    {
        FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);
        FPlatformProcess::Sleep(0.001f);
    }
    FTaskGraphInterface::Get().ProcessThreadUntilIdle(ENamedThreads::GameThread);

    TestTrue(TEXT("Canceled preview work retires."), bFinished);
    TestFalse(TEXT("A previous-generation result is never applied."), bApplied);
    TestTrue(TEXT("The retired work reports cancellation or staleness."),
             Completion == EDWCEditorWorkerJobCompletion::Canceled ||
                 Completion == EDWCEditorWorkerJobCompletion::Stale ||
                 Completion == EDWCEditorWorkerJobCompletion::Superseded);

    Scheduler->Shutdown();
    FPlatformProcess::ReturnSynchEventToPool(WorkerGate);
    return true;
}

#endif
