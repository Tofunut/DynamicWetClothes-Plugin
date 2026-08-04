#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "HAL/PlatformProcess.h"

#include "WetClothing/Foundation/Jobs/DWCEditorWorkerJobScheduler.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorWorkerCancellationTokenTest,
    "DWC.Editor.Authoring.Worker.CancellationToken",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorWorkerCancellationTokenTest::RunTest(const FString& Parameters)
{
    TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe> Token =
        MakeShared<FDWCEditorCancellationToken, ESPMode::ThreadSafe>();
    TestFalse(TEXT("A new token is active"), Token->IsCanceled());
    Token->Cancel();
    TestTrue(TEXT("Cancellation is sticky"), Token->IsCanceled());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorWorkerBudgetContractTest,
    "DWC.Editor.Authoring.Worker.BudgetContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorWorkerBudgetContractTest::RunTest(const FString& Parameters)
{
    TSharedRef<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe> Scheduler =
        MakeShared<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe>(1, 1024, 512);
    FDWCEditorWorkerJobDescriptor Descriptor;
    Descriptor.EstimatedBytes = 513;
    FString Error;
    const FDWCEditorWorkerJobTicket Ticket = Scheduler->Submit(
        Descriptor,
        [](const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>&)
        {
            return MakeShared<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>();
        },
        [](const FDWCEditorWorkerJobTicket&, TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>) {},
        &Error);
    TestFalse(TEXT("An over-budget job is rejected"), Ticket.IsValid());
    TestTrue(TEXT("The rejection explains the budget"), Error.Contains(TEXT("memory budget")));
    Scheduler->Shutdown();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorWorkerQueuedMemoryReservationTest,
    "DWC.Editor.Authoring.Worker.QueuedMemoryReservation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorWorkerQueuedMemoryReservationTest::RunTest(const FString& Parameters)
{
    TSharedRef<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe> Scheduler =
        MakeShared<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe>(1, 1024, 512);

    FDWCEditorWorkerJobDescriptor ActiveDescriptor;
    ActiveDescriptor.Key.MaterialSlotIndex = 1;
    ActiveDescriptor.EstimatedBytes = 512;
    Scheduler->Submit(
        ActiveDescriptor,
        [](const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>&)
        {
            // Keep the reservation active while the second job is submitted.
            FPlatformProcess::Sleep(0.1f);
            return MakeShared<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>();
        },
        [](const FDWCEditorWorkerJobTicket&, TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>) {});

    FDWCEditorWorkerJobDescriptor QueuedDescriptor;
    QueuedDescriptor.Key.MaterialSlotIndex = 2;
    QueuedDescriptor.EstimatedBytes = 512;
    FString Error;
    const FDWCEditorWorkerJobTicket QueuedTicket = Scheduler->Submit(
        QueuedDescriptor,
        [](const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>&)
        {
            return MakeShared<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>();
        },
        [](const FDWCEditorWorkerJobTicket&, TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>) {},
        &Error);

    TestTrue(TEXT("A job that fits the total reserved budget is accepted"), QueuedTicket.IsValid());
    TestTrue(TEXT("The accepted job is not rejected with a memory-budget error"), Error.IsEmpty());
    TestEqual(TEXT("Queued and active snapshots both reserve worker memory"), Scheduler->GetReservedBytes(), 1024ull);

    FDWCEditorWorkerJobDescriptor RejectedDescriptor;
    RejectedDescriptor.Key.MaterialSlotIndex = 3;
    RejectedDescriptor.EstimatedBytes = 1;
    FString RejectedError;
    const FDWCEditorWorkerJobTicket RejectedTicket = Scheduler->Submit(
        RejectedDescriptor,
        [](const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>&)
        {
            return MakeShared<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>();
        },
        [](const FDWCEditorWorkerJobTicket&, TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>) {},
        &RejectedError);
    TestFalse(TEXT("A snapshot that exceeds the total queued reservation is rejected"), RejectedTicket.IsValid());
    TestTrue(TEXT("The queued reservation rejection explains the budget"), RejectedError.Contains(TEXT("memory budget")));
    Scheduler->Cancel(QueuedDescriptor.Key);
    Scheduler->Shutdown();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorWorkerQueuedCancellationCompletionTest,
    "DWC.Editor.Authoring.Worker.QueuedCancellationCompletion",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorWorkerQueuedCancellationCompletionTest::RunTest(const FString& Parameters)
{
    TSharedRef<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe> Scheduler =
        MakeShared<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe>(1, 1024, 1024);

    FDWCEditorWorkerJobDescriptor ActiveDescriptor;
    ActiveDescriptor.Key.MaterialSlotIndex = 1;
    ActiveDescriptor.EstimatedBytes = 1;
    Scheduler->Submit(
        ActiveDescriptor,
        [](const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>&)
        {
            return MakeShared<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>();
        },
        [](const FDWCEditorWorkerJobTicket&, TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>) {});

    FDWCEditorWorkerJobDescriptor QueuedDescriptor;
    QueuedDescriptor.Key.MaterialSlotIndex = 2;
    QueuedDescriptor.EstimatedBytes = 1;
    bool bFinishedCalled = false;
    EDWCEditorWorkerJobCompletion Completion = EDWCEditorWorkerJobCompletion::Applied;
    const FDWCEditorWorkerJobTicket QueuedTicket = Scheduler->Submit(
        QueuedDescriptor,
        [](const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>&)
        {
            return MakeShared<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>();
        },
        [](const FDWCEditorWorkerJobTicket&, TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>) {},
        nullptr,
        [&bFinishedCalled, &Completion](
            const FDWCEditorWorkerJobTicket&,
            const EDWCEditorWorkerJobCompletion InCompletion,
            const FString&)
        {
            bFinishedCalled = true;
            Completion = InCompletion;
        });

    TestTrue(TEXT("The second job is queued"), QueuedTicket.IsValid());
    Scheduler->Cancel(QueuedDescriptor.Key);
    TestTrue(TEXT("Canceling a queued job reports completion"), bFinishedCalled);
    TestEqual(
        TEXT("A directly canceled queued generation is reported as superseded"),
        Completion,
        EDWCEditorWorkerJobCompletion::Superseded);
    Scheduler->Shutdown();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorWorkerDomainCancellationCompletionTest,
    "DWC.Editor.Authoring.Worker.DomainCancellationCompletion",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorWorkerDomainCancellationCompletionTest::RunTest(const FString& Parameters)
{
    TSharedRef<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe> Scheduler =
        MakeShared<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe>(1, 1024, 1024);

    FDWCEditorWorkerJobDescriptor ActiveDescriptor;
    ActiveDescriptor.Key.MaterialSlotIndex = 1;
    ActiveDescriptor.Domain = EDWCEditorAuthoringDomain::Wrinkle;
    ActiveDescriptor.EstimatedBytes = 1;
    Scheduler->Submit(
        ActiveDescriptor,
        [](const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>&)
        {
            return MakeShared<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>();
        },
        [](const FDWCEditorWorkerJobTicket&, TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>) {});

    FDWCEditorWorkerJobDescriptor QueuedDescriptor;
    QueuedDescriptor.Key.MaterialSlotIndex = 2;
    QueuedDescriptor.Domain = EDWCEditorAuthoringDomain::Wrinkle;
    QueuedDescriptor.EstimatedBytes = 1;
    bool bFinishedCalled = false;
    EDWCEditorWorkerJobCompletion Completion = EDWCEditorWorkerJobCompletion::Applied;
    const FDWCEditorWorkerJobTicket QueuedTicket = Scheduler->Submit(
        QueuedDescriptor,
        [](const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>&)
        {
            return MakeShared<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>();
        },
        [](const FDWCEditorWorkerJobTicket&, TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>) {},
        nullptr,
        [&bFinishedCalled, &Completion](
            const FDWCEditorWorkerJobTicket&,
            const EDWCEditorWorkerJobCompletion InCompletion,
            const FString&)
        {
            bFinishedCalled = true;
            Completion = InCompletion;
        });

    TestTrue(TEXT("The second domain job is queued"), QueuedTicket.IsValid());
    Scheduler->CancelDomain(EDWCEditorAuthoringDomain::Wrinkle);
    TestTrue(TEXT("CancelDomain reports queued completion before returning"), bFinishedCalled);
    TestEqual(
        TEXT("A queued domain job is reported as canceled"),
        Completion,
        EDWCEditorWorkerJobCompletion::Canceled);
    Scheduler->Shutdown();
    return true;
}

#endif
