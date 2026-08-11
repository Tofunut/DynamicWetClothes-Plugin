//Copyright 2026 Team Tofunut. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Async/TaskGraphInterfaces.h"
#include "Engine/Texture2D.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "RenderingThread.h"
#include "WetClothing/Foundation/Async/DWCEditorResourceGovernor.h"
#include "WetClothing/Foundation/Jobs/DWCEditorWorkerJobScheduler.h"
#include "WetClothing/Foundation/Preview/Commit/DWCEditorPreviewCommitCoordinator.h"
#include "WetClothing/Foundation/Preview/Diagnostics/DWCEditorPreviewDiagnostics.h"
#include "WetClothing/Foundation/TextureWorkspace/DWCEditorRenderUploadQueue.h"
#include "WetClothing/Foundation/TextureWorkspace/DWCEditorTextureWorkspace.h"

namespace
{
    constexpr uint64 TestJobBytes = 4096;

    bool PumpAsyncOwnershipUntil(
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

    class FDeterministicWorkerGate final
    {
      public:
        FDeterministicWorkerGate()
            : Started(FPlatformProcess::GetSynchEventFromPool(true))
            , Release(FPlatformProcess::GetSynchEventFromPool(true))
        {
        }

        ~FDeterministicWorkerGate()
        {
            ReleaseWork();
            FPlatformProcess::ReturnSynchEventToPool(Release);
            FPlatformProcess::ReturnSynchEventToPool(Started);
        }

        void WaitInWorker() const
        {
            Started->Trigger();
            Release->Wait();
        }

        bool WaitUntilStarted(const uint32 TimeoutMilliseconds = 5000) const
        {
            return Started->Wait(TimeoutMilliseconds);
        }

        void ReleaseWork() const
        {
            Release->Trigger();
        }

      private:
        FEvent* Started = nullptr;
        FEvent* Release = nullptr;
    };

    struct FIntegratedPreviewResult final : FDWCEditorWorkerJobResult
    {
        TArray<FColor> Pixels;
        FDWCEditorNormalRasterSurface WorkingSurface;
        TArray<uint8> RebuiltCPUState;
        bool bNormalPayload = false;
    };

    class FIntegratedAsyncOwnershipHarness final
    {
      public:
        FIntegratedAsyncOwnershipHarness()
        {
            FDWCEditorResourceBudgetConfig BudgetConfig;
            BudgetConfig.GlobalEditorCPUBytes = 8ull * 1024ull * 1024ull;
            BudgetConfig.WorkerPrivateCPUBytes = 4ull * 1024ull * 1024ull;
            ResourceGovernor = MakeShared<FDWCEditorResourceGovernor>(BudgetConfig);
            Scheduler = MakeShared<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe>(
                ResourceGovernor.ToSharedRef(),
                1,
                4ull * 1024ull * 1024ull,
                4ull * 1024ull * 1024ull,
                8);
            Scheduler->SetDomainRevisionProvider(
                [this](const EDWCEditorAuthoringDomain Domain)
                {
                    return Domain == EDWCEditorAuthoringDomain::None ? 0 : DomainRevision;
                });

            UploadQueue = MakeShared<FDWCEditorRenderUploadQueue>();
            Workspace = MakeShared<FDWCEditorTextureWorkspace>(
                UploadQueue.ToSharedRef(),
                4ull * 1024ull * 1024ull,
                4ull * 1024ull * 1024ull);
            Coordinator = MakeShared<FDWCEditorPreviewCommitCoordinator>(
                Workspace.ToSharedRef(),
                Scheduler->GetSessionEpoch());
            Owner = NewObject<UTexture2D>(GetTransientPackage());
        }

        FDWCEditorWorkerJobTicket Submit(
            const FColor PixelColor,
            const bool bNormalPayload,
            TArray<uint8> RebuiltCPUState,
            const TSharedPtr<FDeterministicWorkerGate>& Gate = nullptr,
            const EDWCEditorAsyncRequestPolicy RequestPolicy = EDWCEditorAsyncRequestPolicy::LatestWins)
        {
            FDWCEditorTextureDescriptor Descriptor = MakeDescriptor();
            TArray<FColor> Pixels;
            Pixels.Init(PixelColor, Descriptor.Size.X * Descriptor.Size.Y);

            FDWCEditorNormalRasterSurface WorkingSurface;
            if (bNormalPayload)
            {
                WorkingSurface.Initialize(Descriptor.WorkingSize, false);
                for (int32 PixelIndex = 0; PixelIndex < WorkingSurface.GetPixelCount(); ++PixelIndex)
                {
                    WorkingSurface.SetNormal(PixelIndex, FVector3f(0.25f, 0.0f, 0.9682458f));
                }
            }

            FDWCEditorWorkerJobDescriptor JobDescriptor;
            JobDescriptor.Key.Kind = bNormalPayload
                ? EDWCEditorWorkerJobKind::WrinkleAccumulatedPreview
                : EDWCEditorWorkerJobKind::TransparencyVisualization;
            JobDescriptor.Key.MaterialSlotIndex = 3;
            JobDescriptor.Domain = bNormalPayload
                ? EDWCEditorAuthoringDomain::Wrinkle
                : EDWCEditorAuthoringDomain::Transparency;
            JobDescriptor.DomainRevision = DomainRevision;
            JobDescriptor.Priority = EDWCEditorWorkerJobPriority::Interactive;
            JobDescriptor.RequestPolicy = RequestPolicy;
            JobDescriptor.MemoryEstimate.SnapshotBytes = TestJobBytes;
            JobDescriptor.DebugName = bNormalPayload
                ? TEXT("Integrated wrinkle preview ownership")
                : TEXT("Integrated transparency preview ownership");

            const FDWCEditorPreviewConsumerToken ConsumerToken = ConsumerLifetime.CaptureToken();
            FString SubmissionError;
            const FDWCEditorWorkerJobTicket Ticket = Scheduler->SubmitPrepared(
                JobDescriptor,
                [Pixels = MoveTemp(Pixels),
                 WorkingSurface = MoveTemp(WorkingSurface),
                 RebuiltCPUState = MoveTemp(RebuiltCPUState),
                 bNormalPayload,
                 Gate](
                    const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>&,
                    FDWCEditorWorkerJobScheduler::FPreparedWorkerJob& OutPrepared,
                    FString&) mutable
                {
                    OutPrepared.ActualMemoryEstimate.SnapshotBytes = TestJobBytes;
                    OutPrepared.Work =
                        [Pixels = MoveTemp(Pixels),
                         WorkingSurface = MoveTemp(WorkingSurface),
                         RebuiltCPUState = MoveTemp(RebuiltCPUState),
                         bNormalPayload,
                         Gate](const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>&) mutable
                        -> TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>
                    {
                        if (Gate.IsValid())
                        {
                            Gate->WaitInWorker();
                        }

                        TSharedPtr<FIntegratedPreviewResult, ESPMode::ThreadSafe> Result =
                            MakeShared<FIntegratedPreviewResult, ESPMode::ThreadSafe>();
                        Result->Pixels = MoveTemp(Pixels);
                        Result->WorkingSurface = MoveTemp(WorkingSurface);
                        Result->RebuiltCPUState = MoveTemp(RebuiltCPUState);
                        Result->bNormalPayload = bNormalPayload;
                        Result->ResultBytes =
                            static_cast<uint64>(Result->Pixels.GetAllocatedSize()) +
                            Result->WorkingSurface.GetAllocatedSizeBytes() +
                            static_cast<uint64>(Result->RebuiltCPUState.GetAllocatedSize());
                        return Result;
                    };
                    return true;
                },
                [this, ConsumerToken](
                    const FDWCEditorWorkerJobTicket& AppliedTicket,
                    TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe> Result)
                {
                    ++ApplyCallbackCount;
                    FIntegratedPreviewResult* TypedResult =
                        static_cast<FIntegratedPreviewResult*>(Result.Get());
                    if (TypedResult == nullptr)
                    {
                        return;
                    }

                    FDWCEditorPreviewCommitContext CommitContext;
                    CommitContext.ConsumerToken = ConsumerToken;
                    CommitContext.ProducerSessionEpoch = AppliedTicket.SessionEpoch;
                    CommitContext.IsCurrent = [this, AppliedTicket]()
                    {
                        return CurrentTicket.JobId == AppliedTicket.JobId &&
                            CurrentTicket.Generation == AppliedTicket.Generation;
                    };
                    CommitContext.DebugName = TEXT("Integrated async ownership regression");

                    FDWCEditorTextureLease NewLease;
                    LastCommitResult = TypedResult->bNormalPayload
                        ? Coordinator->CommitNormalBGRA8(
                            CommitContext,
                            MakeTextureKey(true),
                            MakeDescriptor(),
                            MoveTemp(TypedResult->Pixels),
                            MoveTemp(TypedResult->WorkingSurface),
                            NewLease,
                            EDWCEditorTextureUploadPriority::Interactive)
                        : Coordinator->CommitBGRA8(
                            CommitContext,
                            MakeTextureKey(false),
                            MakeDescriptor(),
                            MoveTemp(TypedResult->Pixels),
                            NewLease,
                            EDWCEditorTextureUploadPriority::Interactive);

                    if (LastCommitResult == EDWCEditorPreviewCommitResult::Applied)
                    {
                        ActiveLease = MoveTemp(NewLease);
                        PublishedCPUState = MoveTemp(TypedResult->RebuiltCPUState);
                        ++SuccessfulCommitCount;
                    }
                },
                &SubmissionError,
                [this](
                    const FDWCEditorWorkerJobTicket&,
                    const EDWCEditorWorkerJobCompletion Completion,
                    const FString&)
                {
                    Completions.Add(Completion);
                });

            LastSubmissionError = MoveTemp(SubmissionError);
            if (Ticket.IsValid())
            {
                CurrentTicket = Ticket;
            }
            return Ticket;
        }

        bool WaitForCompletionCount(const int32 ExpectedCount, const double TimeoutSeconds = 5.0)
        {
            return PumpAsyncOwnershipUntil(
                [this, ExpectedCount]() { return Completions.Num() >= ExpectedCount; },
                TimeoutSeconds);
        }

        bool WaitForNoWorkerOwnership(const double TimeoutSeconds = 5.0)
        {
            return PumpAsyncOwnershipUntil(
                [this]()
                {
                    return Scheduler->GetActiveJobCount() == 0 &&
                        Scheduler->GetQueuedJobCount() == 0 &&
                        Scheduler->GetReservedBytes() == 0;
                },
                TimeoutSeconds);
        }

        FDWCEditorTextureDescriptor MakeDescriptor() const
        {
            FDWCEditorTextureDescriptor Descriptor;
            Descriptor.Size = FIntPoint(4, 4);
            Descriptor.WorkingSize = FIntPoint(4, 4);
            Descriptor.PixelFormat = PF_B8G8R8A8;
            Descriptor.InitialBGRA8 = FColor(128, 128, 255, 255);
            return Descriptor;
        }

        FDWCEditorTextureKey MakeTextureKey(const bool bNormalPayload) const
        {
            FDWCEditorTextureKey Key;
            Key.Owner = FObjectKey(Owner);
            Key.Purpose = bNormalPayload
                ? EDWCEditorTexturePurpose::WrinkleAccumulated
                : EDWCEditorTexturePurpose::TransparencyVisualization;
            Key.MaterialSlotIndex = 3;
            return Key;
        }

        void ShutdownAsyncPath()
        {
            ConsumerLifetime.Revoke();
            Coordinator->Shutdown();
            Scheduler->Shutdown();
        }

        void Cleanup()
        {
            if (bCleanedUp)
            {
                return;
            }
            bCleanedUp = true;
            ShutdownAsyncPath();
            WaitForNoWorkerOwnership();
            ActiveLease.Reset();
            Workspace->Reset();
            FlushRenderingCommands();
            Workspace->ProcessRetiredGPUResources();
            UploadQueue->Shutdown();
        }

        TSharedPtr<FDWCEditorResourceGovernor> ResourceGovernor;
        TSharedPtr<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe> Scheduler;
        TSharedPtr<FDWCEditorRenderUploadQueue> UploadQueue;
        TSharedPtr<FDWCEditorTextureWorkspace> Workspace;
        TSharedPtr<FDWCEditorPreviewCommitCoordinator> Coordinator;
        FDWCEditorPreviewConsumerLifetime ConsumerLifetime;
        UTexture2D* Owner = nullptr;
        FDWCEditorTextureLease ActiveLease;
        TArray<uint8> PublishedCPUState;
        TArray<EDWCEditorWorkerJobCompletion> Completions;
        FDWCEditorWorkerJobTicket CurrentTicket;
        EDWCEditorPreviewCommitResult LastCommitResult =
            EDWCEditorPreviewCommitResult::WorkspaceRejected;
        FString LastSubmissionError;
        uint64 DomainRevision = 1;
        int32 ApplyCallbackCount = 0;
        int32 SuccessfulCommitCount = 0;
        bool bCleanedUp = false;
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorAsyncOwnershipLatestWinsRegressionTest,
    "DWC.Editor.Regression.AsyncOwnership.LatestWinsAndMemory",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorAsyncOwnershipLatestWinsRegressionTest::RunTest(const FString&)
{
    FIntegratedAsyncOwnershipHarness Harness;
    const TSharedPtr<FDeterministicWorkerGate> FirstGate =
        MakeShared<FDeterministicWorkerGate>();

    const FDWCEditorWorkerJobTicket FirstTicket = Harness.Submit(
        FColor::Red,
        true,
        {1},
        FirstGate);
    TestTrue(TEXT("The first latest-wins request is accepted"), FirstTicket.IsValid());
    TestTrue(TEXT("The first worker reaches the deterministic gate"), FirstGate->WaitUntilStarted());

    const FDWCEditorWorkerJobTicket LatestTicket = Harness.Submit(
        FColor::Green,
        true,
        {2});
    TestTrue(TEXT("The replacement latest-wins request is accepted"), LatestTicket.IsValid());
    FirstGate->ReleaseWork();

    TestTrue(TEXT("Both request generations retire"), Harness.WaitForCompletionCount(2));
    TestEqual(TEXT("Only the latest generation reaches Apply"), Harness.ApplyCallbackCount, 1);
    TestEqual(TEXT("Only the latest generation acquires a workspace lease"), Harness.SuccessfulCommitCount, 1);
    TestTrue(TEXT("The latest preview lease is active"), Harness.ActiveLease.IsValid());
    if (Harness.ActiveLease.IsValid())
    {
        const TArray<FColor>& Pixels = Harness.ActiveLease->GetBGRA8Pixels();
        TestTrue(TEXT("The published buffer contains the latest generation"),
            !Pixels.IsEmpty() && Pixels[0] == FColor::Green);
        TestEqual(TEXT("Exactly one consumer lease owns the result"),
            Harness.ActiveLease->GetActiveLeaseCount(), 1u);
    }
    TestEqual(TEXT("CPU-side companion state commits atomically with the latest result"),
        Harness.PublishedCPUState, TArray<uint8>({2}));
    TestTrue(TEXT("Worker ownership is released after both generations retire"),
        Harness.WaitForNoWorkerOwnership());

    const FDWCEditorResourceGovernorDiagnostics GovernorDiagnostics =
        Harness.ResourceGovernor->GetDiagnostics();
    TestEqual(TEXT("No governor reservation remains"), GovernorDiagnostics.Reservations.Num(), 0);
    TestEqual(TEXT("No worker CPU bytes remain reserved"), GovernorDiagnostics.GlobalCPUUsedBytes, 0ull);
    Harness.Cleanup();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorAsyncOwnershipStaleAndSuspendRegressionTest,
    "DWC.Editor.Regression.AsyncOwnership.StaleAndSuspend",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorAsyncOwnershipStaleAndSuspendRegressionTest::RunTest(const FString&)
{
    FIntegratedAsyncOwnershipHarness Harness;
    const TSharedPtr<FDeterministicWorkerGate> StaleGate =
        MakeShared<FDeterministicWorkerGate>();
    const FDWCEditorWorkerJobTicket StaleTicket = Harness.Submit(
        FColor::Blue,
        false,
        {3},
        StaleGate,
        EDWCEditorAsyncRequestPolicy::FIFO);
    TestTrue(TEXT("The revision test request is accepted"), StaleTicket.IsValid());
    TestTrue(TEXT("The revision test worker starts"), StaleGate->WaitUntilStarted());
    ++Harness.DomainRevision;
    StaleGate->ReleaseWork();
    TestTrue(TEXT("The stale revision retires"), Harness.WaitForCompletionCount(1));
    TestEqual(TEXT("A stale domain revision never reaches Apply"), Harness.ApplyCallbackCount, 0);
    TestEqual(TEXT("A stale domain revision never publishes a lease"), Harness.SuccessfulCommitCount, 0);
    TestEqual(TEXT("The scheduler reports stale completion"),
        Harness.Completions.Last(), EDWCEditorWorkerJobCompletion::Stale);

    const TSharedPtr<FDeterministicWorkerGate> SuspendedGate =
        MakeShared<FDeterministicWorkerGate>();
    const FDWCEditorWorkerJobTicket SuspendedTicket = Harness.Submit(
        FColor::Yellow,
        false,
        {4},
        SuspendedGate,
        EDWCEditorAsyncRequestPolicy::FIFO);
    TestTrue(TEXT("The consumer lifetime test request is accepted"), SuspendedTicket.IsValid());
    TestTrue(TEXT("The consumer lifetime worker starts"), SuspendedGate->WaitUntilStarted());
    Harness.ConsumerLifetime.Suspend();
    SuspendedGate->ReleaseWork();
    TestTrue(TEXT("The suspended consumer request retires"), Harness.WaitForCompletionCount(2));
    TestEqual(TEXT("The scheduler reaches the consumer commit gate"), Harness.ApplyCallbackCount, 1);
    TestEqual(TEXT("An expired consumer token is rejected"),
        Harness.LastCommitResult, EDWCEditorPreviewCommitResult::ConsumerExpired);
    TestEqual(TEXT("A rejected consumer receives no workspace lease"), Harness.SuccessfulCommitCount, 0);
    TestTrue(TEXT("All stale and rejected jobs release their memory ownership"),
        Harness.WaitForNoWorkerOwnership());
    Harness.Cleanup();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorAsyncOwnershipPayloadAtomicityRegressionTest,
    "DWC.Editor.Regression.AsyncOwnership.PayloadAtomicity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorAsyncOwnershipPayloadAtomicityRegressionTest::RunTest(const FString&)
{
    FIntegratedAsyncOwnershipHarness Harness;
    const FDWCEditorWorkerJobTicket InitialTicket = Harness.Submit(
        FColor(140, 128, 250, 255),
        true,
        {11},
        nullptr,
        EDWCEditorAsyncRequestPolicy::FIFO);
    TestTrue(TEXT("The initial wrinkle payload is accepted"), InitialTicket.IsValid());
    TestTrue(TEXT("The initial wrinkle payload commits"), Harness.WaitForCompletionCount(1));
    TestTrue(TEXT("The committed wrinkle payload owns a lease"), Harness.ActiveLease.IsValid());
    TestEqual(TEXT("The normal working surface transfers with encoded pixels"),
        Harness.ActiveLease.IsValid() && Harness.ActiveLease->GetWorkingNormalSurface().IsValid(), true);
    const FDWCEditorTextureHandle InitialHandle = Harness.ActiveLease.GetHandle();
    const uint64 InitialRevision = InitialHandle.IsValid() ? InitialHandle->GetContentRevision() : 0;
    const TArray<uint8> InitialCPUState = Harness.PublishedCPUState;

    const TSharedPtr<FDeterministicWorkerGate> RejectedGate =
        MakeShared<FDeterministicWorkerGate>();
    const FDWCEditorWorkerJobTicket RejectedTicket = Harness.Submit(
        FColor::Magenta,
        false,
        {22},
        RejectedGate,
        EDWCEditorAsyncRequestPolicy::FIFO);
    TestTrue(TEXT("The replacement payload is accepted by the scheduler"), RejectedTicket.IsValid());
    TestTrue(TEXT("The replacement payload starts"), RejectedGate->WaitUntilStarted());
    Harness.ConsumerLifetime.Suspend();
    RejectedGate->ReleaseWork();
    TestTrue(TEXT("The replacement payload retires"), Harness.WaitForCompletionCount(2));

    TestEqual(TEXT("A rejected result does not replace the prior lease"),
        Harness.ActiveLease.GetHandle(), InitialHandle);
    TestEqual(TEXT("A rejected result does not change prior texture content"),
        InitialHandle.IsValid() ? InitialHandle->GetContentRevision() : 0, InitialRevision);
    TestEqual(TEXT("A rejected result does not partially commit companion CPU state"),
        Harness.PublishedCPUState, InitialCPUState);
    TestEqual(TEXT("Only the initial payload commits"), Harness.SuccessfulCommitCount, 1);

    const TWeakPtr<FDWCEditorTextureWorkspaceEntry> RetiredHandle = InitialHandle;
    Harness.Workspace->Discard(Harness.ActiveLease);
    TestTrue(TEXT("Discard preserves an entry while its consumer lease is active"),
        Harness.ActiveLease.IsValid());
    Harness.ActiveLease.Reset();
    Harness.Workspace->TrimToBudget();
    const FDWCEditorTextureHandle Reacquired = Harness.Workspace->Acquire(
        Harness.MakeTextureKey(true),
        Harness.MakeDescriptor());
    TestTrue(TEXT("Releasing the lease lets the workspace replace the discarded entry"),
        Reacquired.IsValid() && Reacquired != RetiredHandle.Pin());

    Harness.Cleanup();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorAsyncOwnershipShutdownRegressionTest,
    "DWC.Editor.Regression.AsyncOwnership.Shutdown",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorAsyncOwnershipShutdownRegressionTest::RunTest(const FString&)
{
    FIntegratedAsyncOwnershipHarness Harness;
    const TSharedPtr<FDeterministicWorkerGate> ShutdownGate =
        MakeShared<FDeterministicWorkerGate>();
    const FDWCEditorWorkerJobTicket Ticket = Harness.Submit(
        FColor::Cyan,
        true,
        {31},
        ShutdownGate,
        EDWCEditorAsyncRequestPolicy::FIFO);
    TestTrue(TEXT("The shutdown test request is accepted"), Ticket.IsValid());
    TestTrue(TEXT("The shutdown test worker starts"), ShutdownGate->WaitUntilStarted());

    Harness.ShutdownAsyncPath();
    ShutdownGate->ReleaseWork();
    TestTrue(TEXT("Shutdown eventually retires active worker ownership"),
        Harness.WaitForNoWorkerOwnership());
    TestEqual(TEXT("A late shutdown result never reaches Apply"), Harness.ApplyCallbackCount, 0);
    TestFalse(TEXT("A late shutdown result never acquires a texture lease"),
        Harness.ActiveLease.IsValid());
    const FDWCEditorResourceGovernorDiagnostics GovernorDiagnostics =
        Harness.ResourceGovernor->GetDiagnostics();
    TestEqual(TEXT("Shutdown releases every governor reservation"),
        GovernorDiagnostics.Reservations.Num(), 0);
    TestEqual(TEXT("Shutdown releases all reserved CPU bytes"),
        GovernorDiagnostics.GlobalCPUUsedBytes, 0ull);

    Harness.Cleanup();
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorAsyncOwnershipRepeatedSessionLifecycleRegressionTest,
    "DWC.Editor.Regression.AsyncOwnership.RepeatedSessionLifecycle",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorAsyncOwnershipRepeatedSessionLifecycleRegressionTest::RunTest(const FString&)
{
    constexpr int32 SessionCount = 4;
    for (int32 SessionIndex = 0; SessionIndex < SessionCount; ++SessionIndex)
    {
        FIntegratedAsyncOwnershipHarness Harness;
        const FColor ExpectedColor(
            static_cast<uint8>(32 + SessionIndex * 17),
            static_cast<uint8>(64 + SessionIndex * 13),
            static_cast<uint8>(128 + SessionIndex * 7),
            255);

        const FDWCEditorWorkerJobTicket Ticket = Harness.Submit(
            ExpectedColor,
            (SessionIndex % 2) == 0,
            {static_cast<uint8>(SessionIndex + 1)},
            nullptr,
            EDWCEditorAsyncRequestPolicy::FIFO);
        TestTrue(TEXT("Each preview session accepts its request"), Ticket.IsValid());
        TestTrue(TEXT("Each preview session completes its request"),
            Harness.WaitForCompletionCount(1));
        TestEqual(TEXT("Each preview session commits exactly once"),
            Harness.SuccessfulCommitCount, 1);
        TestTrue(TEXT("Each committed preview owns one consumer lease"),
            Harness.ActiveLease.IsValid() &&
            Harness.ActiveLease->GetActiveLeaseCount() == 1);
        if (Harness.ActiveLease.IsValid())
        {
            const TArray<FColor>& Pixels = Harness.ActiveLease->GetBGRA8Pixels();
            TestTrue(TEXT("Each session publishes its own result"),
                !Pixels.IsEmpty() && Pixels[0] == ExpectedColor);
        }

        Harness.Cleanup();

        const FDWCEditorWorkerSchedulerDiagnostics SchedulerDiagnostics =
            Harness.Scheduler->GetDiagnostics();
        TestEqual(TEXT("Closed sessions retain no pending admissions"),
            SchedulerDiagnostics.PendingAdmissionCount, 0);
        TestEqual(TEXT("Closed sessions retain no active jobs"),
            SchedulerDiagnostics.ActiveCount, 0);
        TestEqual(TEXT("Closed sessions retain no worker reservation"),
            SchedulerDiagnostics.ReservedBytes, 0ull);

        const FDWCEditorResourceGovernorDiagnostics GovernorDiagnostics =
            Harness.ResourceGovernor->GetDiagnostics();
        TestEqual(TEXT("Closed sessions retain no governor reservations"),
            GovernorDiagnostics.Reservations.Num(), 0);
        TestEqual(TEXT("Closed sessions release worker CPU ownership"),
            GovernorDiagnostics.GlobalCPUUsedBytes, 0ull);

        TArray<FDWCEditorPreviewMemoryBucket> Buckets;
        Harness.Workspace->AppendDiagnosticMemoryBucket(Buckets);
        const FDWCEditorPreviewMemoryBucket* WorkspaceBucket = Buckets.FindByPredicate(
            [](const FDWCEditorPreviewMemoryBucket& Bucket)
            {
                return Bucket.Name == TEXT("Editor texture workspace");
            });
        TestNotNull(TEXT("Workspace lifecycle diagnostics remain available"), WorkspaceBucket);
        if (WorkspaceBucket != nullptr)
        {
            TestEqual(TEXT("Closed sessions retain no workspace entries"),
                WorkspaceBucket->EntryCount, 0);
            TestEqual(TEXT("Closed sessions retain no workspace leases"),
                WorkspaceBucket->ActiveLeaseCount, 0);
            TestEqual(TEXT("Closed sessions release CPU and GPU workspace bytes"),
                WorkspaceBucket->UsedBytes, 0ull);
        }
    }
    return true;
}

#endif
