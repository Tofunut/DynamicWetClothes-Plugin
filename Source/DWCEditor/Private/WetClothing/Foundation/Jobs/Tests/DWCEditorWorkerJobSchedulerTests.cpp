//Copyright 2026 Team Tofunut. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Async/TaskGraphInterfaces.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"

#include "WetClothing/Foundation/Async/DWCEditorResourceGovernor.h"
#include "WetClothing/Foundation/Jobs/DWCEditorWorkerJobScheduler.h"

namespace
{
    FDWCEditorWorkerJobTicket SubmitPreparedWork(
        const TSharedPtr<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe>& Scheduler,
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

    bool PumpWorkerCompletionsUntil(
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
    Descriptor.MemoryEstimate.SnapshotBytes = 513;
    FString Error;
    const FDWCEditorWorkerJobTicket Ticket = SubmitPreparedWork(Scheduler,
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
    FDWCEditorWorkerMemoryEstimateTest,
    "DWC.Editor.Authoring.Worker.MemoryEstimate",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorWorkerMemoryEstimateTest::RunTest(const FString& Parameters)
{
    FDWCEditorWorkerMemoryEstimate Estimate;
    Estimate.ResidentSharedBytes = 1;
    Estimate.SnapshotBytes = 2;
    Estimate.WorkingBytes = 4;
    Estimate.OutputBytes = 8;
    Estimate.ScratchBytes = 16;
    TestEqual(TEXT("Categorized memory sums every ownership bucket"), Estimate.GetTotalBytes(), 31ull);

    FDWCEditorWorkerJobDescriptor Descriptor;
    Descriptor.MemoryEstimate = Estimate;
    TestEqual(TEXT("A categorized descriptor reserves its category total"), Descriptor.GetReservedBytes(), 31ull);
    const FDWCEditorMemoryBreakdown Breakdown = Estimate.ToMemoryBreakdown();
    TestEqual(TEXT("Canonical memory preserves shared bytes"), Breakdown.SharedResidentBytes, 1ull);
    TestEqual(TEXT("Canonical memory preserves snapshot bytes"), Breakdown.SnapshotBytes, 2ull);

    FDWCEditorWorkerMemoryEstimate OverflowEstimate;
    OverflowEstimate.SnapshotBytes = MAX_uint64;
    OverflowEstimate.WorkingBytes = 1;
    TestEqual(
        TEXT("An overflowing estimate saturates instead of under-reserving"),
        OverflowEstimate.GetTotalBytes(),
        MAX_uint64);
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
    ActiveDescriptor.MemoryEstimate.SnapshotBytes = 512;
    ActiveDescriptor.DebugName = TEXT("Active reservation owner");
    SubmitPreparedWork(Scheduler,
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
    QueuedDescriptor.MemoryEstimate.SnapshotBytes = 512;
    QueuedDescriptor.DebugName = TEXT("Queued reservation owner");
    FString Error;
    const FDWCEditorWorkerJobTicket QueuedTicket = SubmitPreparedWork(Scheduler,
        QueuedDescriptor,
        [](const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>&)
        {
            return MakeShared<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>();
        },
        [](const FDWCEditorWorkerJobTicket&, TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>) {},
        &Error);

    TestTrue(TEXT("A job that fits the total reserved budget is accepted"), QueuedTicket.IsValid());
    TestTrue(TEXT("The accepted job is not rejected with a memory-budget error"), Error.IsEmpty());
    TestEqual(TEXT("Admitted active and ready snapshots reserve worker memory"), Scheduler->GetReservedBytes(), 1024ull);

    FDWCEditorWorkerJobDescriptor RejectedDescriptor;
    RejectedDescriptor.Key.MaterialSlotIndex = 3;
    RejectedDescriptor.MemoryEstimate.SnapshotBytes = 1;
    FString RejectedError;
    const FDWCEditorWorkerJobTicket DeferredTicket = SubmitPreparedWork(Scheduler,
        RejectedDescriptor,
        [](const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>&)
        {
            return MakeShared<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>();
        },
        [](const FDWCEditorWorkerJobTicket&, TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>) {},
        &RejectedError);
    TestTrue(TEXT("Temporary memory pressure defers rather than rejects the request"), DeferredTicket.IsValid());
    TestTrue(TEXT("A deferred admission is not reported as a submission error"), RejectedError.IsEmpty());
    const FDWCEditorWorkerSchedulerDiagnostics Diagnostics = Scheduler->GetDiagnostics();
    TestEqual(TEXT("Diagnostics expose the total reservation"), Diagnostics.ReservedBytes, 1024ull);
    TestEqual(TEXT("Diagnostics expose the reservation high-water mark"), Diagnostics.HighWaterReservedBytes, 1024ull);
    TestEqual(TEXT("Temporary admission pressure is not a budget rejection"), Diagnostics.BudgetRejectionCount, 0ull);
    TestEqual(TEXT("Diagnostics expose one pending admission"), Diagnostics.PendingAdmissionCount, 1);
    TestEqual(TEXT("Diagnostics count the first admission deferral"), Diagnostics.AdmissionDeferredCount, 1ull);
    TestEqual(TEXT("Diagnostics expose active, ready, and pending work"), Diagnostics.Jobs.Num(), 3);
    Scheduler->Cancel(RejectedDescriptor.Key);
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
    ActiveDescriptor.MemoryEstimate.SnapshotBytes = 1;
    SubmitPreparedWork(Scheduler,
        ActiveDescriptor,
        [](const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>&)
        {
            return MakeShared<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>();
        },
        [](const FDWCEditorWorkerJobTicket&, TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>) {});

    FDWCEditorWorkerJobDescriptor QueuedDescriptor;
    QueuedDescriptor.Key.MaterialSlotIndex = 2;
    QueuedDescriptor.MemoryEstimate.SnapshotBytes = 1;
    bool bFinishedCalled = false;
    EDWCEditorWorkerJobCompletion Completion = EDWCEditorWorkerJobCompletion::Applied;
    const FDWCEditorWorkerJobTicket QueuedTicket = SubmitPreparedWork(Scheduler,
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
        TEXT("A directly canceled queued generation is reported as canceled"),
        Completion,
        EDWCEditorWorkerJobCompletion::Canceled);
    Scheduler->Shutdown();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorWorkerRejectedReplacementKeepsActiveJobTest,
    "DWC.Editor.Authoring.Worker.LatestRequestMailbox",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorWorkerRejectedReplacementKeepsActiveJobTest::RunTest(const FString& Parameters)
{
    TSharedRef<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe> Scheduler =
        MakeShared<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe>(1, 1024, 1024);
    FEvent* WorkStarted = FPlatformProcess::GetSynchEventFromPool(true);
    FEvent* ReleaseWork = FPlatformProcess::GetSynchEventFromPool(true);
    FEvent* WorkFinished = FPlatformProcess::GetSynchEventFromPool(true);
    TSharedPtr<FDWCEditorCancellationToken, ESPMode::ThreadSafe> ActiveToken;

    FDWCEditorWorkerJobDescriptor Descriptor;
    Descriptor.Key.MaterialSlotIndex = 7;
    Descriptor.MemoryEstimate.SnapshotBytes = 1024;
    Descriptor.RequestPolicy = EDWCEditorAsyncRequestPolicy::LatestWins;
    const FDWCEditorWorkerJobTicket ActiveTicket = SubmitPreparedWork(Scheduler,
        Descriptor,
        [&ActiveToken, WorkStarted, ReleaseWork, WorkFinished](
            const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& Token)
        {
            ActiveToken = Token;
            WorkStarted->Trigger();
            ReleaseWork->Wait();
            TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe> Result =
                MakeShared<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>();
            Result->bSucceeded = true;
            WorkFinished->Trigger();
            return Result;
        },
        [](const FDWCEditorWorkerJobTicket&, TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>) {});

    TestTrue(TEXT("The original job is accepted"), ActiveTicket.IsValid());
    const bool bStarted = WorkStarted->Wait(5000);
    TestTrue(TEXT("The original job starts"), bStarted);

    bool bReplacedPendingFinished = false;
    EDWCEditorWorkerJobCompletion ReplacedPendingCompletion = EDWCEditorWorkerJobCompletion::Applied;
    FString Error;
    const FDWCEditorWorkerJobTicket ReplacementTicket = SubmitPreparedWork(Scheduler,
        Descriptor,
        [](const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>&)
        {
            return MakeShared<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>();
        },
        [](const FDWCEditorWorkerJobTicket&, TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>) {},
        &Error,
        [&bReplacedPendingFinished, &ReplacedPendingCompletion](
            const FDWCEditorWorkerJobTicket&,
            const EDWCEditorWorkerJobCompletion Completion,
            const FString&)
        {
            bReplacedPendingFinished = true;
            ReplacedPendingCompletion = Completion;
        });

    TestTrue(TEXT("A latest request waits in the mailbox while memory is owned"), ReplacementTicket.IsValid());
    TestTrue(TEXT("A mailbox wait is not a submission error"), Error.IsEmpty());
    TestTrue(TEXT("The active job token was captured"), ActiveToken.IsValid());
    if (ActiveToken.IsValid())
    {
        TestTrue(TEXT("A newer latest request cancels obsolete active work"), ActiveToken->IsCanceled());
    }

    bool bLatestApplied = false;
    const FDWCEditorWorkerJobTicket LatestTicket = SubmitPreparedWork(Scheduler,
        Descriptor,
        [](const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>&)
        {
            return MakeShared<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>();
        },
        [&bLatestApplied](
            const FDWCEditorWorkerJobTicket&,
            TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>)
        {
            bLatestApplied = true;
        });
    TestTrue(TEXT("A newer mailbox request is accepted"), LatestTicket.IsValid());
    TestTrue(TEXT("The previous pending request is retired immediately"), bReplacedPendingFinished);
    TestEqual(
        TEXT("A replaced pending request is reported as superseded"),
        ReplacedPendingCompletion,
        EDWCEditorWorkerJobCompletion::Superseded);
    const FDWCEditorWorkerSchedulerDiagnostics MailboxDiagnostics = Scheduler->GetDiagnostics();
    TestEqual(TEXT("Only one latest request remains pending per key"), MailboxDiagnostics.PendingAdmissionCount, 1);
    TestEqual(TEXT("Mailbox replacement is diagnosed"), MailboxDiagnostics.MailboxReplacementCount, 1ull);

    ReleaseWork->Trigger();
    TestTrue(TEXT("The worker exits before test cleanup"), WorkFinished->Wait(5000));
    TestTrue(
        TEXT("The latest mailbox request runs after the obsolete active job retires"),
        PumpWorkerCompletionsUntil([&bLatestApplied]() { return bLatestApplied; }));
    Scheduler->Shutdown();
    FPlatformProcess::ReturnSynchEventToPool(WorkFinished);
    FPlatformProcess::ReturnSynchEventToPool(ReleaseWork);
    FPlatformProcess::ReturnSynchEventToPool(WorkStarted);
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
    ActiveDescriptor.MemoryEstimate.SnapshotBytes = 1;
    SubmitPreparedWork(Scheduler,
        ActiveDescriptor,
        [](const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>&)
        {
            return MakeShared<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>();
        },
        [](const FDWCEditorWorkerJobTicket&, TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>) {});

    FDWCEditorWorkerJobDescriptor QueuedDescriptor;
    QueuedDescriptor.Key.MaterialSlotIndex = 2;
    QueuedDescriptor.Domain = EDWCEditorAuthoringDomain::Wrinkle;
    QueuedDescriptor.MemoryEstimate.SnapshotBytes = 1;
    bool bFinishedCalled = false;
    EDWCEditorWorkerJobCompletion Completion = EDWCEditorWorkerJobCompletion::Applied;
    const FDWCEditorWorkerJobTicket QueuedTicket = SubmitPreparedWork(Scheduler,
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorWorkerTwoPhaseAdmissionLifetimeTest,
    "DWC.Editor.Authoring.Worker.TwoPhaseAdmissionLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorWorkerTwoPhaseAdmissionLifetimeTest::RunTest(const FString& Parameters)
{
    TSharedRef<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe> Scheduler =
        MakeShared<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe>(1, 1024, 1024);

    FDWCEditorWorkerJobDescriptor Descriptor;
    Descriptor.Key.MaterialSlotIndex = 4;
    Descriptor.MemoryEstimate.SnapshotBytes = 128;
    Descriptor.DebugName = TEXT("Two-phase lifetime");

    bool bPrepareSawAdmission = false;
    bool bApplySawLease = false;
    bool bFinishedSawLease = false;
    bool bFinished = false;
    const FDWCEditorWorkerJobTicket Ticket = Scheduler->SubmitPrepared(
        Descriptor,
        [&Scheduler, &bPrepareSawAdmission](
            const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>&,
            FDWCEditorWorkerJobScheduler::FPreparedWorkerJob& OutPrepared,
            FString&)
        {
            bPrepareSawAdmission = Scheduler->GetReservedBytes() == 128;
            OutPrepared.ActualMemoryEstimate.SnapshotBytes = 256;
            OutPrepared.Work = [](
                const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>&)
            {
                return MakeShared<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>();
            };
            return true;
        },
        [&Scheduler, &bApplySawLease](
            const FDWCEditorWorkerJobTicket&,
            TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>)
        {
            bApplySawLease = Scheduler->GetReservedBytes() == 256;
        },
        nullptr,
        [&Scheduler, &bFinishedSawLease, &bFinished](
            const FDWCEditorWorkerJobTicket&,
            const EDWCEditorWorkerJobCompletion Completion,
            const FString&)
        {
            bFinishedSawLease = Scheduler->GetReservedBytes() == 256;
            bFinished = Completion == EDWCEditorWorkerJobCompletion::Applied;
        });

    TestTrue(TEXT("The two-phase job is admitted"), Ticket.IsValid());
    TestTrue(TEXT("Prepare runs only after the initial reservation exists"), bPrepareSawAdmission);
    TestEqual(TEXT("The reservation grows to the prepared estimate"), Scheduler->GetReservedBytes(), 256ull);
    TestTrue(TEXT("The two-phase job reaches completion"), PumpWorkerCompletionsUntil([&bFinished]() { return bFinished; }));
    TestTrue(TEXT("Apply executes while the lease is still held"), bApplySawLease);
    TestTrue(TEXT("Finished executes while the lease is still held"), bFinishedSawLease);
    TestEqual(TEXT("The lease is released after completion callbacks retire"), Scheduler->GetReservedBytes(), 0ull);
    Scheduler->Shutdown();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorWorkerDeferredPrepareTest,
    "DWC.Editor.Authoring.Worker.DeferredPrepare",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorWorkerDeferredPrepareTest::RunTest(const FString& Parameters)
{
    TSharedRef<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe> Scheduler =
        MakeShared<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe>(1, 128, 128);
    FEvent* OwnerStarted = FPlatformProcess::GetSynchEventFromPool(true);
    FEvent* ReleaseOwner = FPlatformProcess::GetSynchEventFromPool(true);

    FDWCEditorWorkerJobDescriptor OwnerDescriptor;
    OwnerDescriptor.Key.MaterialSlotIndex = 1;
    OwnerDescriptor.MemoryEstimate.SnapshotBytes = 128;
    OwnerDescriptor.RequestPolicy = EDWCEditorAsyncRequestPolicy::FIFO;
    SubmitPreparedWork(Scheduler,
        OwnerDescriptor,
        [OwnerStarted, ReleaseOwner](const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>&)
        {
            OwnerStarted->Trigger();
            ReleaseOwner->Wait();
            return MakeShared<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>();
        },
        [](const FDWCEditorWorkerJobTicket&, TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>) {});
    TestTrue(TEXT("The memory owner starts"), OwnerStarted->Wait(5000));

    int32 PrepareCount = 0;
    bool bDeferredApplied = false;
    FDWCEditorWorkerJobDescriptor DeferredDescriptor;
    DeferredDescriptor.Key.MaterialSlotIndex = 2;
    DeferredDescriptor.MemoryEstimate.SnapshotBytes = 128;
    DeferredDescriptor.RequestPolicy = EDWCEditorAsyncRequestPolicy::LatestWins;
    const FDWCEditorWorkerJobTicket DeferredTicket = Scheduler->SubmitPrepared(
        DeferredDescriptor,
        [&PrepareCount](
            const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>&,
            FDWCEditorWorkerJobScheduler::FPreparedWorkerJob& OutPrepared,
            FString&)
        {
            ++PrepareCount;
            OutPrepared.ActualMemoryEstimate.SnapshotBytes = 128;
            OutPrepared.Work = [](
                const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>&)
            {
                return MakeShared<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>();
            };
            return true;
        },
        [&bDeferredApplied](
            const FDWCEditorWorkerJobTicket&,
            TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>)
        {
            bDeferredApplied = true;
        });

    TestTrue(TEXT("The deferred request receives a mailbox ticket"), DeferredTicket.IsValid());
    TestEqual(TEXT("Prepare is not called before admission"), PrepareCount, 0);
    TestEqual(TEXT("The request remains pending without owning memory"), Scheduler->GetPendingAdmissionCount(), 1);
    TestEqual(TEXT("Only the active owner is reserved"), Scheduler->GetReservedBytes(), 128ull);

    ReleaseOwner->Trigger();
    TestTrue(
        TEXT("The deferred request prepares and applies after memory is released"),
        PumpWorkerCompletionsUntil([&bDeferredApplied]() { return bDeferredApplied; }));
    TestEqual(TEXT("Deferred prepare executes exactly once"), PrepareCount, 1);
    Scheduler->Shutdown();
    FPlatformProcess::ReturnSynchEventToPool(ReleaseOwner);
    FPlatformProcess::ReturnSynchEventToPool(OwnerStarted);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorWorkerPrepareFailureReleasesLeaseTest,
    "DWC.Editor.Authoring.Worker.PrepareFailureReleasesLease",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorWorkerPrepareFailureReleasesLeaseTest::RunTest(const FString& Parameters)
{
    TSharedRef<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe> Scheduler =
        MakeShared<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe>(1, 1024, 1024);
    FDWCEditorWorkerJobDescriptor Descriptor;
    Descriptor.MemoryEstimate.SnapshotBytes = 128;
    Descriptor.DebugName = TEXT("Prepare failure");

    int32 FinishedCount = 0;
    EDWCEditorWorkerJobCompletion Completion = EDWCEditorWorkerJobCompletion::Applied;
    FString FinishedError;
    FString Error;
    const FDWCEditorWorkerJobTicket Ticket = Scheduler->SubmitPrepared(
        Descriptor,
        [](const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>&,
           FDWCEditorWorkerJobScheduler::FPreparedWorkerJob&,
           FString& OutError)
        {
            OutError = TEXT("Expected prepare failure");
            return false;
        },
        [](const FDWCEditorWorkerJobTicket&, TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>) {},
        &Error,
        [&FinishedCount, &Completion, &FinishedError](
            const FDWCEditorWorkerJobTicket&,
            const EDWCEditorWorkerJobCompletion InCompletion,
            const FString& InError)
        {
            ++FinishedCount;
            Completion = InCompletion;
            FinishedError = InError;
        });

    TestFalse(TEXT("A synchronously failed prepare does not return a live ticket"), Ticket.IsValid());
    TestTrue(TEXT("Prepare failure is returned as a submission error"), Error.Contains(TEXT("Expected prepare failure")));
    TestEqual(TEXT("Prepare failure reports completion exactly once"), FinishedCount, 1);
    TestEqual(TEXT("Prepare failure is reported as failed"), Completion, EDWCEditorWorkerJobCompletion::Failed);
    TestTrue(TEXT("Prepare failure preserves its diagnostic"), FinishedError.Contains(TEXT("Expected prepare failure")));
    TestEqual(TEXT("Prepare failure immediately releases its lease"), Scheduler->GetReservedBytes(), 0ull);
    Scheduler->Shutdown();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorWorkerFIFORequestPolicyTest,
    "DWC.Editor.Authoring.Worker.FIFORequestPolicy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorWorkerFIFORequestPolicyTest::RunTest(const FString& Parameters)
{
    TSharedRef<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe> Scheduler =
        MakeShared<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe>(1, 512, 256);
    FEvent* FirstStarted = FPlatformProcess::GetSynchEventFromPool(true);
    FEvent* ReleaseFirst = FPlatformProcess::GetSynchEventFromPool(true);
    TSharedPtr<FDWCEditorCancellationToken, ESPMode::ThreadSafe> FirstToken;
    int32 AppliedCount = 0;
    int32 FinishedCount = 0;

    FDWCEditorWorkerJobDescriptor Descriptor;
    Descriptor.Key.Kind = EDWCEditorWorkerJobKind::WrinkleBake;
    Descriptor.Key.MaterialSlotIndex = 9;
    Descriptor.MemoryEstimate.SnapshotBytes = 128;
    Descriptor.DebugName = TEXT("FIFO bake");
    const FDWCEditorWorkerJobTicket FirstTicket = SubmitPreparedWork(Scheduler,
        Descriptor,
        [&FirstToken, FirstStarted, ReleaseFirst](
            const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& Token)
        {
            FirstToken = Token;
            FirstStarted->Trigger();
            ReleaseFirst->Wait();
            return MakeShared<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>();
        },
        [&AppliedCount](const FDWCEditorWorkerJobTicket&, TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>)
        {
            ++AppliedCount;
        },
        nullptr,
        [&FinishedCount](const FDWCEditorWorkerJobTicket&, EDWCEditorWorkerJobCompletion, const FString&)
        {
            ++FinishedCount;
        });
    TestTrue(TEXT("The first FIFO job starts"), FirstStarted->Wait(5000));

    const FDWCEditorWorkerJobTicket SecondTicket = SubmitPreparedWork(Scheduler,
        Descriptor,
        [](const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>&)
        {
            return MakeShared<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>();
        },
        [&AppliedCount](const FDWCEditorWorkerJobTicket&, TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>)
        {
            ++AppliedCount;
        },
        nullptr,
        [&FinishedCount](const FDWCEditorWorkerJobTicket&, EDWCEditorWorkerJobCompletion, const FString&)
        {
            ++FinishedCount;
        });

    TestTrue(TEXT("Both FIFO requests are accepted"), FirstTicket.IsValid() && SecondTicket.IsValid());
    TestTrue(TEXT("The first FIFO job retains its cancellation token"), FirstToken.IsValid());
    if (FirstToken.IsValid())
    {
        TestFalse(TEXT("A later FIFO request does not supersede the active request"), FirstToken->IsCanceled());
    }
    TestEqual(TEXT("The later FIFO request waits in the queue"), Scheduler->GetQueuedJobCount(), 1);
    ReleaseFirst->Trigger();
    TestTrue(
        TEXT("Both FIFO requests complete"),
        PumpWorkerCompletionsUntil([&FinishedCount]() { return FinishedCount == 2; }));
    TestEqual(TEXT("Both FIFO results are applied"), AppliedCount, 2);
    Scheduler->Shutdown();
    FPlatformProcess::ReturnSynchEventToPool(ReleaseFirst);
    FPlatformProcess::ReturnSynchEventToPool(FirstStarted);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorWorkerSingletonRequestPolicyTest,
    "DWC.Editor.Authoring.Worker.SingletonRequestPolicy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorWorkerSingletonRequestPolicyTest::RunTest(const FString& Parameters)
{
    TSharedRef<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe> Scheduler =
        MakeShared<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe>(1, 1024, 1024);
    FEvent* WorkStarted = FPlatformProcess::GetSynchEventFromPool(true);
    FEvent* ReleaseWork = FPlatformProcess::GetSynchEventFromPool(true);

    FDWCEditorWorkerJobDescriptor Descriptor;
    Descriptor.Key.Kind = EDWCEditorWorkerJobKind::TransparencyFinalBake;
    Descriptor.Key.MaterialSlotIndex = 5;
    Descriptor.MemoryEstimate.SnapshotBytes = 128;
    Descriptor.RequestPolicy = EDWCEditorAsyncRequestPolicy::Singleton;
    Descriptor.DebugName = TEXT("Singleton test");
    const FDWCEditorWorkerJobTicket FirstTicket = SubmitPreparedWork(Scheduler,
        Descriptor,
        [WorkStarted, ReleaseWork](const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>&)
        {
            WorkStarted->Trigger();
            ReleaseWork->Wait();
            return MakeShared<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>();
        },
        [](const FDWCEditorWorkerJobTicket&, TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>) {});
    TestTrue(TEXT("The first singleton request starts"), WorkStarted->Wait(5000));

    FString DuplicateError;
    const FDWCEditorWorkerJobTicket DuplicateTicket = SubmitPreparedWork(Scheduler,
        Descriptor,
        [](const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>&)
        {
            return MakeShared<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>();
        },
        [](const FDWCEditorWorkerJobTicket&, TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>) {},
        &DuplicateError);
    TestTrue(TEXT("The first singleton request is valid"), FirstTicket.IsValid());
    TestFalse(TEXT("A duplicate singleton request is rejected"), DuplicateTicket.IsValid());
    TestTrue(TEXT("The singleton rejection is explicit"), DuplicateError.Contains(TEXT("singleton")));
    TestEqual(
        TEXT("Singleton rejection is diagnosed"),
        Scheduler->GetDiagnostics().SingletonRejectionCount,
        1ull);

    ReleaseWork->Trigger();
    TestTrue(
        TEXT("The singleton owner retires"),
        PumpWorkerCompletionsUntil([&Scheduler]() { return Scheduler->GetActiveJobCount() == 0; }));
    Scheduler->Shutdown();
    FPlatformProcess::ReturnSynchEventToPool(ReleaseWork);
    FPlatformProcess::ReturnSynchEventToPool(WorkStarted);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorWorkerPhaseContinuationAdmissionTest,
    "DWC.Editor.Authoring.Worker.PhaseContinuationAdmission",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorWorkerPhaseContinuationAdmissionTest::RunTest(const FString& Parameters)
{
    TSharedRef<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe> Scheduler =
        MakeShared<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe>(1, 256, 256);
    int32 AppliedCount = 0;

    FDWCEditorWorkerJobDescriptor Descriptor;
    Descriptor.Key.Kind = EDWCEditorWorkerJobKind::WrinkleHoverPreview;
    Descriptor.Key.MaterialSlotIndex = 3;
    Descriptor.RequestPolicy = EDWCEditorAsyncRequestPolicy::LatestWins;
    Descriptor.MemoryEstimate.SnapshotBytes = 128;
    Descriptor.DebugName = TEXT("Two-phase hover");
    const FDWCEditorWorkerJobTicket Ticket = SubmitPreparedWork(
        Scheduler,
        Descriptor,
        [](const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>&)
        {
            TSharedPtr<FDWCEditorWorkerPhaseContinuationResult, ESPMode::ThreadSafe> Continuation =
                MakeShared<FDWCEditorWorkerPhaseContinuationResult, ESPMode::ThreadSafe>();
            Continuation->NextPhaseName = TEXT("Raster");
            Continuation->RetainedMemoryEstimate.SnapshotBytes = 32;
            Continuation->NextPhaseMemoryEstimate.SnapshotBytes = 32;
            Continuation->NextPhaseMemoryEstimate.OutputBytes = 32;
            Continuation->NextWork = [](
                const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>&)
            {
                return MakeShared<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>();
            };
            return Continuation;
        },
        [&AppliedCount](const FDWCEditorWorkerJobTicket&,
            TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>)
        {
            ++AppliedCount;
        });

    TestTrue(TEXT("The two-phase request is accepted"), Ticket.IsValid());
    TestTrue(
        TEXT("Both phases complete under one logical request"),
        PumpWorkerCompletionsUntil([&AppliedCount]() { return AppliedCount == 1; }));
    TestEqual(TEXT("The phase continuation applies exactly once"), AppliedCount, 1);
    TestEqual(TEXT("The completed continuation releases its lease"), Scheduler->GetReservedBytes(), 0ull);
    Scheduler->Shutdown();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorWorkerTicketCancellationLifetimeTest,
    "DWC.Editor.Authoring.Worker.TicketCancellationLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorWorkerTicketCancellationLifetimeTest::RunTest(const FString& Parameters)
{
    TSharedRef<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe> Scheduler =
        MakeShared<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe>(1, 256, 256);
    FEvent* BlockerStarted = FPlatformProcess::GetSynchEventFromPool(true);
    FEvent* ReleaseBlocker = FPlatformProcess::GetSynchEventFromPool(true);

    FDWCEditorWorkerJobDescriptor BlockerDescriptor;
    BlockerDescriptor.Key.MaterialSlotIndex = 40;
    BlockerDescriptor.MemoryEstimate.SnapshotBytes = 128;
    SubmitPreparedWork(
        Scheduler,
        BlockerDescriptor,
        [BlockerStarted, ReleaseBlocker](
            const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>&)
        {
            BlockerStarted->Trigger();
            ReleaseBlocker->Wait();
            return MakeShared<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>();
        },
        [](const FDWCEditorWorkerJobTicket&,
            TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>) {});
    TestTrue(TEXT("The cancellation blocker starts"), BlockerStarted->Wait(5000));

    int32 ReadyFinishedCount = 0;
    EDWCEditorWorkerJobCompletion ReadyCompletion = EDWCEditorWorkerJobCompletion::Applied;
    FDWCEditorWorkerJobDescriptor ReadyDescriptor;
    ReadyDescriptor.Key.MaterialSlotIndex = 41;
    ReadyDescriptor.MemoryEstimate.SnapshotBytes = 64;
    const FDWCEditorWorkerJobTicket ReadyTicket = SubmitPreparedWork(
        Scheduler,
        ReadyDescriptor,
        [](const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>&)
        {
            return MakeShared<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>();
        },
        [](const FDWCEditorWorkerJobTicket&,
            TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>) {},
        nullptr,
        [&ReadyFinishedCount, &ReadyCompletion](
            const FDWCEditorWorkerJobTicket&,
            const EDWCEditorWorkerJobCompletion Completion,
            const FString&)
        {
            ++ReadyFinishedCount;
            ReadyCompletion = Completion;
        });

    int32 PendingFinishedCount = 0;
    bool bReentrantReadyCancelSucceeded = false;
    EDWCEditorWorkerJobCompletion PendingCompletion = EDWCEditorWorkerJobCompletion::Applied;
    FDWCEditorWorkerJobDescriptor PendingDescriptor;
    PendingDescriptor.Key.MaterialSlotIndex = 42;
    PendingDescriptor.MemoryEstimate.SnapshotBytes = 128;
    const FDWCEditorWorkerJobTicket PendingTicket = SubmitPreparedWork(
        Scheduler,
        PendingDescriptor,
        [](const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>&)
        {
            return MakeShared<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>();
        },
        [](const FDWCEditorWorkerJobTicket&,
            TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>) {},
        nullptr,
        [&Scheduler, ReadyTicket, &bReentrantReadyCancelSucceeded,
            &PendingFinishedCount, &PendingCompletion](
            const FDWCEditorWorkerJobTicket&,
            const EDWCEditorWorkerJobCompletion Completion,
            const FString&)
        {
            ++PendingFinishedCount;
            PendingCompletion = Completion;
            bReentrantReadyCancelSucceeded = Scheduler->CancelTicket(ReadyTicket);
        });

    TestTrue(TEXT("The ready ticket is accepted"), ReadyTicket.IsValid());
    TestTrue(TEXT("The pending-admission ticket is accepted"), PendingTicket.IsValid());
    TestEqual(
        TEXT("One request waits for admission"),
        Scheduler->GetDiagnostics().PendingAdmissionCount,
        1);
    TestTrue(
        TEXT("CancelTicket finalizes a pending request without invalidating its owner"),
        Scheduler->CancelTicket(PendingTicket));
    TestEqual(TEXT("The pending callback runs exactly once"), PendingFinishedCount, 1);
    TestTrue(TEXT("A completion callback can safely cancel another ticket"),
        bReentrantReadyCancelSucceeded);
    TestEqual(TEXT("The ready callback runs exactly once during reentrant cancellation"), ReadyFinishedCount, 1);
    TestEqual(TEXT("The pending request reports cancellation"), PendingCompletion,
        EDWCEditorWorkerJobCompletion::Canceled);
    TestEqual(TEXT("The ready request reports cancellation"), ReadyCompletion,
        EDWCEditorWorkerJobCompletion::Canceled);
    TestFalse(TEXT("A completed ticket cannot be canceled twice"), Scheduler->CancelTicket(PendingTicket));
    TestFalse(TEXT("A reentrantly completed ticket cannot be canceled twice"), Scheduler->CancelTicket(ReadyTicket));

    ReleaseBlocker->Trigger();
    TestTrue(
        TEXT("The blocker retires after ticket cancellation"),
        PumpWorkerCompletionsUntil([&Scheduler]() { return Scheduler->GetActiveJobCount() == 0; }));
    Scheduler->Shutdown();
    FPlatformProcess::ReturnSynchEventToPool(ReleaseBlocker);
    FPlatformProcess::ReturnSynchEventToPool(BlockerStarted);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorWorkerPendingPhaseTicketCancellationTest,
    "DWC.Editor.Authoring.Worker.PendingPhaseTicketCancellation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorWorkerPendingPhaseTicketCancellationTest::RunTest(const FString& Parameters)
{
    TSharedRef<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe> Scheduler =
        MakeShared<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe>(2, 192, 192);
    FEvent* BlockerStarted = FPlatformProcess::GetSynchEventFromPool(true);
    FEvent* ReleaseBlocker = FPlatformProcess::GetSynchEventFromPool(true);

    FDWCEditorWorkerJobDescriptor BlockerDescriptor;
    BlockerDescriptor.Key.MaterialSlotIndex = 50;
    BlockerDescriptor.MemoryEstimate.SnapshotBytes = 128;
    SubmitPreparedWork(
        Scheduler,
        BlockerDescriptor,
        [BlockerStarted, ReleaseBlocker](
            const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>&)
        {
            BlockerStarted->Trigger();
            ReleaseBlocker->Wait();
            return MakeShared<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>();
        },
        [](const FDWCEditorWorkerJobTicket&,
            TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>) {});
    TestTrue(TEXT("The phase cancellation blocker starts"), BlockerStarted->Wait(5000));

    int32 FinishedCount = 0;
    EDWCEditorWorkerJobCompletion Completion = EDWCEditorWorkerJobCompletion::Applied;
    FDWCEditorWorkerJobDescriptor PhaseDescriptor;
    PhaseDescriptor.Key.Kind = EDWCEditorWorkerJobKind::WrinkleHoverPreview;
    PhaseDescriptor.Key.MaterialSlotIndex = 51;
    PhaseDescriptor.MemoryEstimate.SnapshotBytes = 64;
    const FDWCEditorWorkerJobTicket PhaseTicket = SubmitPreparedWork(
        Scheduler,
        PhaseDescriptor,
        [](const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>&)
        {
            TSharedPtr<FDWCEditorWorkerPhaseContinuationResult, ESPMode::ThreadSafe> Continuation =
                MakeShared<FDWCEditorWorkerPhaseContinuationResult, ESPMode::ThreadSafe>();
            Continuation->NextPhaseName = TEXT("Pending raster");
            Continuation->RetainedMemoryEstimate.SnapshotBytes = 16;
            Continuation->NextPhaseMemoryEstimate.SnapshotBytes = 16;
            Continuation->NextPhaseMemoryEstimate.WorkingBytes = 64;
            Continuation->NextWork = [](
                const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>&)
            {
                return MakeShared<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>();
            };
            return Continuation;
        },
        [](const FDWCEditorWorkerJobTicket&,
            TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>) {},
        nullptr,
        [&FinishedCount, &Completion](
            const FDWCEditorWorkerJobTicket&,
            const EDWCEditorWorkerJobCompletion InCompletion,
            const FString&)
        {
            ++FinishedCount;
            Completion = InCompletion;
        });

    TestTrue(TEXT("The phase ticket is accepted"), PhaseTicket.IsValid());
    TestTrue(
        TEXT("The request reaches pending phase admission"),
        PumpWorkerCompletionsUntil([&Scheduler]()
        {
            return Scheduler->GetDiagnostics().PendingPhaseAdmissionCount == 1;
        }));
    TestTrue(TEXT("CancelTicket safely finalizes retained phase state"),
        Scheduler->CancelTicket(PhaseTicket));
    TestEqual(TEXT("Pending phase completion is reported once"), FinishedCount, 1);
    TestEqual(TEXT("Pending phase completion is canceled"), Completion,
        EDWCEditorWorkerJobCompletion::Canceled);
    TestEqual(TEXT("Only the blocker lease remains"), Scheduler->GetReservedBytes(), 128ull);

    ReleaseBlocker->Trigger();
    TestTrue(
        TEXT("The phase blocker retires"),
        PumpWorkerCompletionsUntil([&Scheduler]() { return Scheduler->GetActiveJobCount() == 0; }));
    Scheduler->Shutdown();
    FPlatformProcess::ReturnSynchEventToPool(ReleaseBlocker);
    FPlatformProcess::ReturnSynchEventToPool(BlockerStarted);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorWorkerPendingPhaseLatestWinsTest,
    "DWC.Editor.Authoring.Worker.PendingPhaseLatestWins",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorWorkerPendingPhaseLatestWinsTest::RunTest(const FString& Parameters)
{
    TSharedRef<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe> Scheduler =
        MakeShared<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe>(2, 192, 192);
    FEvent* BlockerStarted = FPlatformProcess::GetSynchEventFromPool(true);
    FEvent* ReleaseBlocker = FPlatformProcess::GetSynchEventFromPool(true);
    int32 AppliedCount = 0;
    int32 SupersededCount = 0;

    FDWCEditorWorkerJobDescriptor BlockerDescriptor;
    BlockerDescriptor.Key.MaterialSlotIndex = 30;
    BlockerDescriptor.MemoryEstimate.SnapshotBytes = 128;
    SubmitPreparedWork(Scheduler, BlockerDescriptor,
        [BlockerStarted, ReleaseBlocker](
            const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>&)
        {
            BlockerStarted->Trigger();
            ReleaseBlocker->Wait();
            return MakeShared<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>();
        },
        [](const FDWCEditorWorkerJobTicket&,
            TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>) {});
    TestTrue(TEXT("The LatestWins blocker starts"), BlockerStarted->Wait(5000));

    FDWCEditorWorkerJobDescriptor HoverDescriptor;
    HoverDescriptor.Key.Kind = EDWCEditorWorkerJobKind::WrinkleHoverPreview;
    HoverDescriptor.Key.MaterialSlotIndex = 31;
    HoverDescriptor.RequestPolicy = EDWCEditorAsyncRequestPolicy::LatestWins;
    HoverDescriptor.MemoryEstimate.SnapshotBytes = 64;
    const auto MakePhaseWork = [](
        const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>&)
    {
        TSharedPtr<FDWCEditorWorkerPhaseContinuationResult, ESPMode::ThreadSafe> Continuation =
            MakeShared<FDWCEditorWorkerPhaseContinuationResult, ESPMode::ThreadSafe>();
        Continuation->NextPhaseName = TEXT("Raster");
        Continuation->RetainedMemoryEstimate.SnapshotBytes = 16;
        Continuation->NextPhaseMemoryEstimate.SnapshotBytes = 16;
        Continuation->NextPhaseMemoryEstimate.WorkingBytes = 64;
        Continuation->NextWork = [](
            const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>&)
        {
            return MakeShared<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>();
        };
        return Continuation;
    };
    const auto Finished = [&SupersededCount](
        const FDWCEditorWorkerJobTicket&,
        const EDWCEditorWorkerJobCompletion Completion,
        const FString&)
    {
        SupersededCount += Completion == EDWCEditorWorkerJobCompletion::Superseded ? 1 : 0;
    };
    SubmitPreparedWork(Scheduler, HoverDescriptor, MakePhaseWork,
        [&AppliedCount](const FDWCEditorWorkerJobTicket&,
            TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>) { ++AppliedCount; },
        nullptr, Finished);
    TestTrue(TEXT("The first hover reaches phase admission"),
        PumpWorkerCompletionsUntil([&Scheduler]()
        {
            return Scheduler->GetDiagnostics().PendingPhaseAdmissionCount == 1;
        }));

    SubmitPreparedWork(Scheduler, HoverDescriptor, MakePhaseWork,
        [&AppliedCount](const FDWCEditorWorkerJobTicket&,
            TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>) { ++AppliedCount; },
        nullptr, Finished);
    TestTrue(TEXT("A newer hover releases the old retained continuation"),
        PumpWorkerCompletionsUntil([&SupersededCount]() { return SupersededCount == 1; }));

    ReleaseBlocker->Trigger();
    TestTrue(TEXT("Only the newest hover reaches apply"),
        PumpWorkerCompletionsUntil([&AppliedCount]() { return AppliedCount == 1; }));
    TestEqual(TEXT("Exactly one old phase is superseded"), SupersededCount, 1);
    TestEqual(TEXT("LatestWins phase completion retires every lease"),
        Scheduler->GetReservedBytes(), 0ull);
    Scheduler->Shutdown();
    FPlatformProcess::ReturnSynchEventToPool(ReleaseBlocker);
    FPlatformProcess::ReturnSynchEventToPool(BlockerStarted);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorWorkerDeferredPhaseAdmissionTest,
    "DWC.Editor.Authoring.Worker.DeferredPhaseAdmission",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorWorkerDeferredPhaseAdmissionTest::RunTest(const FString& Parameters)
{
    TSharedRef<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe> Scheduler =
        MakeShared<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe>(2, 256, 256);
    FEvent* BlockerStarted = FPlatformProcess::GetSynchEventFromPool(true);
    FEvent* ReleaseBlocker = FPlatformProcess::GetSynchEventFromPool(true);
    int32 AppliedCount = 0;

    FDWCEditorWorkerJobDescriptor BlockerDescriptor;
    BlockerDescriptor.Key.MaterialSlotIndex = 20;
    BlockerDescriptor.MemoryEstimate.SnapshotBytes = 192;
    BlockerDescriptor.DebugName = TEXT("Phase admission blocker");
    SubmitPreparedWork(
        Scheduler,
        BlockerDescriptor,
        [BlockerStarted, ReleaseBlocker](
            const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>&)
        {
            BlockerStarted->Trigger();
            ReleaseBlocker->Wait();
            return MakeShared<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>();
        },
        [](const FDWCEditorWorkerJobTicket&,
            TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>) {});
    TestTrue(TEXT("The budget blocker starts"), BlockerStarted->Wait(5000));

    FDWCEditorWorkerJobDescriptor HoverDescriptor;
    HoverDescriptor.Key.Kind = EDWCEditorWorkerJobKind::WrinkleHoverPreview;
    HoverDescriptor.Key.MaterialSlotIndex = 21;
    HoverDescriptor.MemoryEstimate.SnapshotBytes = 64;
    HoverDescriptor.DebugName = TEXT("Deferred raster phase");
    SubmitPreparedWork(
        Scheduler,
        HoverDescriptor,
        [](const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>&)
        {
            TSharedPtr<FDWCEditorWorkerPhaseContinuationResult, ESPMode::ThreadSafe> Continuation =
                MakeShared<FDWCEditorWorkerPhaseContinuationResult, ESPMode::ThreadSafe>();
            Continuation->NextPhaseName = TEXT("Raster");
            Continuation->RetainedMemoryEstimate.SnapshotBytes = 32;
            Continuation->NextPhaseMemoryEstimate.SnapshotBytes = 32;
            Continuation->NextPhaseMemoryEstimate.WorkingBytes = 96;
            Continuation->NextWork = [](
                const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>&)
            {
                return MakeShared<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>();
            };
            return Continuation;
        },
        [&AppliedCount](const FDWCEditorWorkerJobTicket&,
            TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>)
        {
            ++AppliedCount;
        });

    TestTrue(
        TEXT("The raster phase waits while retaining only its plan"),
        PumpWorkerCompletionsUntil([&Scheduler]()
        {
            return Scheduler->GetDiagnostics().PendingPhaseAdmissionCount == 1;
        }));
    const FDWCEditorWorkerSchedulerDiagnostics DeferredDiagnostics = Scheduler->GetDiagnostics();
    TestEqual(TEXT("Only the continuation plan is retained"),
        DeferredDiagnostics.RetainedPhaseBytes, 32ull);
    TestEqual(TEXT("The pending phase does not consume an active worker slot"),
        DeferredDiagnostics.ActiveCount, 1);

    ReleaseBlocker->Trigger();
    TestTrue(
        TEXT("The raster phase resumes when memory becomes available"),
        PumpWorkerCompletionsUntil([&AppliedCount]() { return AppliedCount == 1; }));
    TestTrue(TEXT("Deferred phase admission is diagnosed"),
        Scheduler->GetDiagnostics().PhaseAdmissionDeferredCount > 0);
    TestEqual(TEXT("All phase leases retire"), Scheduler->GetReservedBytes(), 0ull);
    Scheduler->Shutdown();
    FPlatformProcess::ReturnSynchEventToPool(ReleaseBlocker);
    FPlatformProcess::ReturnSynchEventToPool(BlockerStarted);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorWorkerDetachedLeaseRetirementTest,
    "DWC.Editor.Authoring.Worker.DetachedLeaseRetirement",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorWorkerDetachedLeaseRetirementTest::RunTest(const FString& Parameters)
{
    FDWCEditorResourceBudgetConfig Budget;
    Budget.GlobalEditorCPUBytes = 1024;
    Budget.WorkerPrivateCPUBytes = 1024;
    TSharedRef<FDWCEditorResourceGovernor> Governor = MakeShared<FDWCEditorResourceGovernor>(Budget);
    TSharedPtr<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe> Scheduler =
        MakeShared<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe>(Governor, 1, 1024, 1024);
    FEvent* WorkStarted = FPlatformProcess::GetSynchEventFromPool(true);
    FEvent* ReleaseWork = FPlatformProcess::GetSynchEventFromPool(true);
    int32 FinishedCount = 0;

    FDWCEditorWorkerJobDescriptor Descriptor;
    Descriptor.Key.MaterialSlotIndex = 11;
    Descriptor.MemoryEstimate.SnapshotBytes = 128;
    Descriptor.DebugName = TEXT("Detached lease");
    SubmitPreparedWork(Scheduler,
        Descriptor,
        [WorkStarted, ReleaseWork](const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>&)
        {
            WorkStarted->Trigger();
            ReleaseWork->Wait();
            return MakeShared<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>();
        },
        [](const FDWCEditorWorkerJobTicket&, TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>) {},
        nullptr,
        [&FinishedCount](const FDWCEditorWorkerJobTicket&, EDWCEditorWorkerJobCompletion, const FString&)
        {
            ++FinishedCount;
        });
    TestTrue(TEXT("The detached worker starts"), WorkStarted->Wait(5000));
    TestEqual(TEXT("The governor owns the active worker reservation"), Governor->GetDiagnostics().GlobalCPUUsedBytes, 128ull);

    Scheduler->Shutdown();
    Scheduler.Reset();
    ReleaseWork->Trigger();
    TestTrue(
        TEXT("Destroying the scheduler does not strand the worker lease"),
        PumpWorkerCompletionsUntil(
            [&Governor]() { return Governor->GetDiagnostics().GlobalCPUUsedBytes == 0; }));
    TestEqual(TEXT("Detached completion is reported exactly once"), FinishedCount, 1);
    FPlatformProcess::ReturnSynchEventToPool(ReleaseWork);
    FPlatformProcess::ReturnSynchEventToPool(WorkStarted);
    return true;
}

#endif
