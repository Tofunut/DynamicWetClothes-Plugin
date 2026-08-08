// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "WetClothing/Foundation/Jobs/DWCEditorWorkerJobScheduler.h"

#include "Async/Async.h"
#include "HAL/PlatformTime.h"
#include "WetClothing/Foundation/Async/DWCEditorAsyncOperationContract.h"
#include "WetClothing/Foundation/Async/DWCEditorResourceGovernor.h"
#include "WetClothing/Foundation/Jobs/DWCEditorWorkerAsyncCompatibility.h"

DEFINE_LOG_CATEGORY_STATIC(LogDWCEditorWorkerJobs, Log, All);

namespace
{
    const TCHAR* WorkerLifecycleToString(const EDWCEditorWorkerJobLifecycleState State)
    {
        switch (State)
        {
        case EDWCEditorWorkerJobLifecycleState::PendingAdmission:
            return TEXT("PendingAdmission");
        case EDWCEditorWorkerJobLifecycleState::Preparing:
            return TEXT("Preparing");
        case EDWCEditorWorkerJobLifecycleState::Ready:
            return TEXT("Ready");
        case EDWCEditorWorkerJobLifecycleState::Running:
            return TEXT("Running");
        case EDWCEditorWorkerJobLifecycleState::CancelRequested:
            return TEXT("CancelRequested");
        case EDWCEditorWorkerJobLifecycleState::Finalizing:
            return TEXT("Finalizing");
        case EDWCEditorWorkerJobLifecycleState::Completed:
            return TEXT("Completed");
        default:
            return TEXT("Unknown");
        }
    }

    FString FormatMiB(const uint64 Bytes)
    {
        return FString::Printf(TEXT("%.2f MiB"), static_cast<double>(Bytes) / (1024.0 * 1024.0));
    }

    FDWCEditorResourceBudgetConfig MakeWorkerBudgetConfig(const uint64 TotalMemoryBudgetBytes)
    {
        FDWCEditorResourceBudgetConfig Config;
        Config.GlobalEditorCPUBytes = FMath::Max<uint64>(TotalMemoryBudgetBytes, 1);
        Config.WorkerPrivateCPUBytes = FMath::Max<uint64>(TotalMemoryBudgetBytes, 1);
        return Config;
    }
} // namespace

struct FDWCEditorWorkerJobScheduler::FQueuedJob
{
    FDWCEditorWorkerJobDescriptor                                Descriptor;
    FDWCEditorWorkerJobTicket                                    Ticket;
    TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe> CancellationToken =
        MakeShared<FDWCEditorCancellationToken, ESPMode::ThreadSafe>();
    FPrepare                          Prepare;
    FWork                             Work;
    FApply                            Apply;
    FFinished                         Finished;
    FDWCEditorMemoryLease             MemoryLease;
    EDWCEditorAsyncOperationState     OperationState = EDWCEditorAsyncOperationState::Pending;
    EDWCEditorAsyncCancellationState  CancellationState = EDWCEditorAsyncCancellationState::None;
    EDWCEditorWorkerJobLifecycleState LifecycleState = EDWCEditorWorkerJobLifecycleState::PendingAdmission;
    FDWCEditorWorkerMemoryEstimate    ActualMemoryEstimate;
    double                            SubmittedSeconds = 0.0;
    double                            PrepareStartedSeconds = 0.0;
    double                            PrepareFinishedSeconds = 0.0;
    double                            StartedSeconds = 0.0;
    double                            CommitStartedSeconds = 0.0;
    double                            CommitFinishedSeconds = 0.0;
    double                            CancelRequestedSeconds = 0.0;
    uint64                            ResultBytes = 0;
    bool                              bFinishedNotified = false;
    bool                              bAdmissionDeferred = false;
    EDWCEditorWorkerJobCompletion     Completion = EDWCEditorWorkerJobCompletion::Failed;
    FString                           CompletionError;
};

FDWCEditorWorkerJobScheduler::FDWCEditorWorkerJobScheduler(
    const int32  InMaxActiveJobs,
    const uint64 InTotalMemoryBudgetBytes,
    const uint64 InPerJobMemoryBudgetBytes,
    const int32  InMaxQueuedJobs)
    : FDWCEditorWorkerJobScheduler(
          MakeShared<FDWCEditorResourceGovernor>(MakeWorkerBudgetConfig(InTotalMemoryBudgetBytes)),
          InMaxActiveJobs,
          InTotalMemoryBudgetBytes,
          InPerJobMemoryBudgetBytes,
          InMaxQueuedJobs)
{
}

FDWCEditorWorkerJobScheduler::FDWCEditorWorkerJobScheduler(
    TSharedRef<FDWCEditorResourceGovernor> InResourceGovernor,
    const int32                            InMaxActiveJobs,
    const uint64                           InTotalMemoryBudgetBytes,
    const uint64                           InPerJobMemoryBudgetBytes,
    const int32                            InMaxQueuedJobs)
    : ResourceGovernor(MoveTemp(InResourceGovernor)), SessionEpoch(FGuid::NewGuid()), MaxActiveJobs(FMath::Max(1, InMaxActiveJobs)), MaxQueuedJobs(FMath::Max(1, InMaxQueuedJobs)), TotalMemoryBudgetBytes(FMath::Max<uint64>(1, InTotalMemoryBudgetBytes)), PerJobMemoryBudgetBytes(FMath::Max<uint64>(1, InPerJobMemoryBudgetBytes))
{
}

FDWCEditorWorkerJobScheduler::~FDWCEditorWorkerJobScheduler()
{
    Shutdown();
}

void FDWCEditorWorkerJobScheduler::SetDomainRevisionProvider(FDomainRevisionProvider InProvider)
{
    check(IsInGameThread());
    DomainRevisionProvider = MoveTemp(InProvider);
}

FDWCEditorWorkerJobTicket FDWCEditorWorkerJobScheduler::Submit(
    const FDWCEditorWorkerJobDescriptor& Descriptor,
    FWork                                Work,
    FApply                               Apply,
    FString*                             OutError,
    FFinished                            Finished)
{
    if (!Work)
    {
        if (OutError != nullptr)
        {
            *OutError = TEXT("The editor worker job is missing its work callback.");
        }
        return {};
    }

    return SubmitInternal(
        Descriptor,
        [LegacyWork = MoveTemp(Work)](
            const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>&,
            FPreparedWorkerJob& OutPrepared,
            FString&) mutable
        {
            OutPrepared.Work = MoveTemp(LegacyWork);
            return static_cast<bool>(OutPrepared.Work);
        },
        MoveTemp(Apply),
        OutError,
        MoveTemp(Finished));
}

FDWCEditorWorkerJobTicket FDWCEditorWorkerJobScheduler::SubmitTwoPhase(
    const FDWCEditorWorkerJobDescriptor& Descriptor,
    FPrepare                             Prepare,
    FApply                               Apply,
    FString*                             OutError,
    FFinished                            Finished)
{
    return SubmitInternal(
        Descriptor,
        MoveTemp(Prepare),
        MoveTemp(Apply),
        OutError,
        MoveTemp(Finished));
}

FDWCEditorWorkerJobTicket FDWCEditorWorkerJobScheduler::SubmitInternal(
    const FDWCEditorWorkerJobDescriptor& Descriptor,
    FPrepare                             Prepare,
    FApply                               Apply,
    FString*                             OutError,
    FFinished                            Finished)
{
    check(IsInGameThread());
    if (OutError != nullptr)
    {
        OutError->Reset();
    }
    if (bShuttingDown)
    {
        if (OutError != nullptr)
            *OutError = TEXT("The editor worker scheduler is shutting down.");
        return {};
    }
    if (!Prepare || !Apply)
    {
        if (OutError != nullptr)
            *OutError = TEXT("The editor worker job is missing its prepare or apply callback.");
        return {};
    }

    const uint64 RequestedBytes = Descriptor.GetReservedBytes();
    if (RequestedBytes > PerJobMemoryBudgetBytes)
    {
        ++BudgetRejectionCount;
        if (OutError != nullptr)
            *OutError = BuildBudgetFailureDiagnostic(Descriptor, true);
        return {};
    }

    const EDWCEditorAsyncRequestPolicy RequestPolicy = Descriptor.GetRequestPolicy();
    if (RequestPolicy == EDWCEditorAsyncRequestPolicy::Singleton &&
        HasOutstandingJobForKey(Descriptor.Key))
    {
        ++SingletonRejectionCount;
        if (OutError != nullptr)
        {
            *OutError = TEXT("An editor worker request with the same singleton key is already pending or running.");
        }
        return {};
    }

    const auto IsQueuedSameKey = [&Descriptor](const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& Job)
    {
        return Job->Descriptor.Key == Descriptor.Key;
    };
    const bool bReplacesQueuedLatest = RequestPolicy == EDWCEditorAsyncRequestPolicy::LatestWins &&
                                       (PendingAdmissionJobs.ContainsByPredicate(IsQueuedSameKey) ||
                                        PreparingJobs.ContainsByPredicate(IsQueuedSameKey) ||
                                        ReadyJobs.ContainsByPredicate(IsQueuedSameKey));
    if (GetOutstandingQueueCount() >= MaxQueuedJobs && !bReplacesQueuedLatest)
    {
        ++QueueRejectionCount;
        if (OutError != nullptr)
            *OutError = TEXT("The editor worker scheduler queue is full.");
        return {};
    }

    const uint64 Generation = GenerationByKey.FindRef(Descriptor.Key) + 1;
    GenerationByKey.Add(Descriptor.Key, Generation);
    TSharedRef<FQueuedJob, ESPMode::ThreadSafe> Job = MakeShared<FQueuedJob, ESPMode::ThreadSafe>();
    Job->Descriptor = Descriptor;
    Job->Ticket.Key = Descriptor.Key;
    Job->Ticket.SessionEpoch = SessionEpoch;
    Job->Ticket.JobId = NextJobId++;
    Job->Ticket.Generation = Generation;
    Job->Ticket.Domain = Descriptor.Domain;
    Job->Ticket.DomainRevision = Descriptor.DomainRevision;
    Job->Prepare = MoveTemp(Prepare);
    Job->Apply = MoveTemp(Apply);
    Job->Finished = MoveTemp(Finished);
    Job->SubmittedSeconds = FPlatformTime::Seconds();
    PendingAdmissionJobs.Add(Job);
    if (RequestPolicy == EDWCEditorAsyncRequestPolicy::LatestWins)
    {
        SupersedeLatestJobs(Job);
    }
    PumpAdmissions();
    if (Job->LifecycleState == EDWCEditorWorkerJobLifecycleState::Completed)
    {
        if (OutError != nullptr && Job->Completion == EDWCEditorWorkerJobCompletion::Failed)
        {
            *OutError = Job->CompletionError;
        }
        return {};
    }
    return Job->Ticket;
}

void FDWCEditorWorkerJobScheduler::PumpAdmissions()
{
    check(IsInGameThread());
    if (bShuttingDown)
    {
        return;
    }
    if (bPumpingAdmissions)
    {
        bAdmissionPumpRequested = true;
        return;
    }

    TGuardValue<bool> PumpGuard(bPumpingAdmissions, true);
    do
    {
        bAdmissionPumpRequested = false;
        bool bMadeProgress = false;
        PendingAdmissionJobs.StableSort(
            [](const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& A,
               const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& B)
            {
                if (A->Descriptor.Priority != B->Descriptor.Priority)
                {
                    return static_cast<uint8>(A->Descriptor.Priority) >
                           static_cast<uint8>(B->Descriptor.Priority);
                }
                return A->Ticket.JobId < B->Ticket.JobId;
            });

        const TArray<TSharedRef<FQueuedJob, ESPMode::ThreadSafe>> PendingSnapshot = PendingAdmissionJobs;
        for (const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& Job : PendingSnapshot)
        {
            if (!PendingAdmissionJobs.Contains(Job))
            {
                continue;
            }
            if (Job->CancellationToken->IsCanceled())
            {
                FinalizeNonRunningJob(Job, EDWCEditorWorkerJobCompletion::Canceled, FString());
                bMadeProgress = true;
                continue;
            }
            if (Job->Descriptor.GetRequestPolicy() == EDWCEditorAsyncRequestPolicy::LatestWins &&
                !IsCurrentGeneration(Job->Ticket))
            {
                FinalizeNonRunningJob(Job, EDWCEditorWorkerJobCompletion::Superseded, FString());
                bMadeProgress = true;
                continue;
            }
            const bool bDomainCurrent = Job->Ticket.Domain == EDWCEditorAuthoringDomain::None ||
                                        !DomainRevisionProvider ||
                                        DomainRevisionProvider(Job->Ticket.Domain) == Job->Ticket.DomainRevision;
            if (!bDomainCurrent)
            {
                FinalizeNonRunningJob(Job, EDWCEditorWorkerJobCompletion::Stale, FString());
                bMadeProgress = true;
                continue;
            }
            if (HasActiveJobForKey(Job->Descriptor.Key) ||
                (Job->Descriptor.GetRequestPolicy() == EDWCEditorAsyncRequestPolicy::FIFO &&
                 HasOlderFIFOJob(Job)))
            {
                continue;
            }
            if (TryAdmitPendingJob(Job))
            {
                bMadeProgress = true;
            }
        }

        StartEligibleJobs();
        if (!bMadeProgress && !bAdmissionPumpRequested)
        {
            break;
        }
    } while (!bShuttingDown);
}

bool FDWCEditorWorkerJobScheduler::TryAdmitPendingJob(
    const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& Job)
{
    const uint64                         ReservationBytes = FMath::Max<uint64>(Job->Descriptor.GetReservedBytes(), 1);
    FDWCEditorResourceReservationRequest ReservationRequest;
    ReservationRequest.Pool = EDWCEditorResourcePool::WorkerPrivateCPU;
    ReservationRequest.Bytes = ReservationBytes;
    ReservationRequest.Owner = DWCEditorWorkerAsyncCompatibility::MakeOperationIdentity(
        Job->Ticket,
        SessionEpoch);
    ReservationRequest.DebugName = Job->Descriptor.DebugName;

    EDWCEditorResourceAdmissionResult AdmissionResult =
        EDWCEditorResourceAdmissionResult::InvalidRequest;
    FString GovernorError;
    Job->MemoryLease = ResourceGovernor->TryAcquireForAdmission(
        ReservationRequest,
        AdmissionResult,
        &GovernorError);
    if (AdmissionResult == EDWCEditorResourceAdmissionResult::TemporarilyUnavailable)
    {
        if (!Job->bAdmissionDeferred)
        {
            Job->bAdmissionDeferred = true;
            ++AdmissionDeferredCount;
        }
        return false;
    }
    if (!Job->MemoryLease.IsValid())
    {
        ++BudgetRejectionCount;
        FinalizeNonRunningJob(
            Job,
            EDWCEditorWorkerJobCompletion::Failed,
            BuildBudgetFailureDiagnostic(Job->Descriptor, false, GovernorError));
        return true;
    }

    PendingAdmissionJobs.RemoveSingle(Job);
    PreparingJobs.Add(Job);
    Job->bAdmissionDeferred = false;
    Job->LifecycleState = EDWCEditorWorkerJobLifecycleState::Preparing;
    HighWaterReservedBytes = FMath::Max(HighWaterReservedBytes, CalculateReservedBytes());
    TransitionJob(Job, EDWCEditorAsyncOperationState::Admitted);
    TransitionJob(Job, EDWCEditorAsyncOperationState::Preparing);

    Job->PrepareStartedSeconds = FPlatformTime::Seconds();
    FPreparedWorkerJob Prepared;
    FString            PrepareError;
    const bool         bPrepared = Job->Prepare(Job->CancellationToken, Prepared, PrepareError);
    Job->PrepareFinishedSeconds = FPlatformTime::Seconds();
    Job->Prepare = nullptr;

    const bool bLatestStillCurrent =
        Job->Descriptor.GetRequestPolicy() != EDWCEditorAsyncRequestPolicy::LatestWins ||
        IsCurrentGeneration(Job->Ticket);
    if (!bPrepared || !Prepared.Work || Job->CancellationToken->IsCanceled() || !bLatestStillCurrent)
    {
        EDWCEditorWorkerJobCompletion Completion = EDWCEditorWorkerJobCompletion::Failed;
        if (!bLatestStillCurrent)
        {
            Completion = EDWCEditorWorkerJobCompletion::Superseded;
        }
        else if (Job->CancellationToken->IsCanceled())
        {
            Completion = EDWCEditorWorkerJobCompletion::Canceled;
        }
        else if (PrepareError.IsEmpty())
        {
            PrepareError = TEXT("The editor worker job prepare phase did not produce executable work.");
        }
        FinalizeNonRunningJob(Job, Completion, PrepareError);
        return true;
    }

    const uint64 PreparedBytes = Prepared.GetReservedBytes();
    if (PreparedBytes > PerJobMemoryBudgetBytes)
    {
        ++BudgetRejectionCount;
        FinalizeNonRunningJob(
            Job,
            EDWCEditorWorkerJobCompletion::Failed,
            BuildBudgetFailureDiagnostic(Job->Descriptor, true));
        return true;
    }
    if (PreparedBytes > Job->MemoryLease.GetReservedBytes())
    {
        const uint64 AdditionalBytes = PreparedBytes - Job->MemoryLease.GetReservedBytes();
        if (!Job->MemoryLease.TryGrow(AdditionalBytes, &GovernorError))
        {
            ++BudgetRejectionCount;
            FinalizeNonRunningJob(
                Job,
                EDWCEditorWorkerJobCompletion::Failed,
                FString::Printf(
                    TEXT("The prepared worker snapshot exceeded its admitted estimate. %s"),
                    *GovernorError));
            return true;
        }
        HighWaterReservedBytes = FMath::Max(HighWaterReservedBytes, CalculateReservedBytes());
    }

    Job->ActualMemoryEstimate = Prepared.ActualMemoryEstimate.IsEmpty()
                                    ? Job->Descriptor.MemoryEstimate
                                    : Prepared.ActualMemoryEstimate;
    Job->Work = MoveTemp(Prepared.Work);
    PreparingJobs.RemoveSingle(Job);
    ReadyJobs.Add(Job);
    Job->LifecycleState = EDWCEditorWorkerJobLifecycleState::Ready;
    TransitionJob(Job, EDWCEditorAsyncOperationState::Ready);
    ReadyJobs.StableSort(
        [](const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& A,
           const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& B)
        {
            if (A->Descriptor.Priority != B->Descriptor.Priority)
            {
                return static_cast<uint8>(A->Descriptor.Priority) >
                       static_cast<uint8>(B->Descriptor.Priority);
            }
            return A->Ticket.JobId < B->Ticket.JobId;
        });
    return true;
}

void FDWCEditorWorkerJobScheduler::Cancel(const FDWCEditorWorkerJobKey& Key)
{
    check(IsInGameThread());
    GenerationByKey.FindOrAdd(Key) += 1;
    CancelJobsByKey(Key);
    PumpAdmissions();
}

void FDWCEditorWorkerJobScheduler::CancelDomain(const EDWCEditorAuthoringDomain Domain)
{
    check(IsInGameThread());
    for (int32 Index = PendingAdmissionJobs.Num() - 1; Index >= 0; --Index)
    {
        const TSharedRef<FQueuedJob, ESPMode::ThreadSafe> Job = PendingAdmissionJobs[Index];
        if (Job->Descriptor.Domain == Domain)
        {
            GenerationByKey.FindOrAdd(Job->Descriptor.Key) += 1;
            RequestJobCancellation(Job);
            FinalizeNonRunningJob(Job, EDWCEditorWorkerJobCompletion::Canceled, FString());
        }
    }
    for (const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& Job : PreparingJobs)
    {
        if (Job->Descriptor.Domain == Domain)
        {
            GenerationByKey.FindOrAdd(Job->Descriptor.Key) += 1;
            RequestJobCancellation(Job);
        }
    }
    for (int32 Index = ReadyJobs.Num() - 1; Index >= 0; --Index)
    {
        const TSharedRef<FQueuedJob, ESPMode::ThreadSafe> Job = ReadyJobs[Index];
        if (Job->Descriptor.Domain == Domain)
        {
            GenerationByKey.FindOrAdd(Job->Descriptor.Key) += 1;
            RequestJobCancellation(Job);
            FinalizeNonRunningJob(Job, EDWCEditorWorkerJobCompletion::Canceled, FString());
        }
    }
    for (const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& Job : ActiveJobs)
    {
        if (Job->Descriptor.Domain == Domain)
        {
            GenerationByKey.FindOrAdd(Job->Descriptor.Key) += 1;
            RequestJobCancellation(Job);
        }
    }
    PumpAdmissions();
}

void FDWCEditorWorkerJobScheduler::Shutdown()
{
    if (bShuttingDown)
    {
        return;
    }
    check(IsInGameThread());
    bShuttingDown = true;
    while (!PendingAdmissionJobs.IsEmpty())
    {
        const TSharedRef<FQueuedJob, ESPMode::ThreadSafe> Job = PendingAdmissionJobs.Last();
        RequestJobCancellation(Job);
        FinalizeNonRunningJob(Job, EDWCEditorWorkerJobCompletion::Canceled, FString());
    }
    for (const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& Job : PreparingJobs)
    {
        RequestJobCancellation(Job);
    }
    while (!ReadyJobs.IsEmpty())
    {
        const TSharedRef<FQueuedJob, ESPMode::ThreadSafe> Job = ReadyJobs.Last();
        RequestJobCancellation(Job);
        FinalizeNonRunningJob(Job, EDWCEditorWorkerJobCompletion::Canceled, FString());
    }
    for (const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& Job : ActiveJobs)
    {
        RequestJobCancellation(Job);
    }
    DomainRevisionProvider = nullptr;
}

int32 FDWCEditorWorkerJobScheduler::GetQueuedJobCount() const
{
    check(IsInGameThread());
    return GetOutstandingQueueCount();
}

int32 FDWCEditorWorkerJobScheduler::GetPendingAdmissionCount() const
{
    check(IsInGameThread());
    return PendingAdmissionJobs.Num();
}

int32 FDWCEditorWorkerJobScheduler::GetActiveJobCount() const
{
    check(IsInGameThread());
    return ActiveJobs.Num();
}

uint64 FDWCEditorWorkerJobScheduler::GetReservedBytes() const
{
    check(IsInGameThread());
    return CalculateReservedBytes();
}

uint64 FDWCEditorWorkerJobScheduler::GetCurrentDomainRevision(
    const EDWCEditorAuthoringDomain Domain) const
{
    check(IsInGameThread());
    return DomainRevisionProvider ? DomainRevisionProvider(Domain) : 0;
}

FDWCEditorWorkerSchedulerDiagnostics FDWCEditorWorkerJobScheduler::GetDiagnostics() const
{
    check(IsInGameThread());
    FDWCEditorWorkerSchedulerDiagnostics Diagnostics;
    Diagnostics.PendingAdmissionCount = PendingAdmissionJobs.Num();
    Diagnostics.PreparingCount = PreparingJobs.Num();
    Diagnostics.ReadyCount = ReadyJobs.Num();
    Diagnostics.ActiveCount = ActiveJobs.Num();
    Diagnostics.ReservedBytes = CalculateReservedBytes();
    Diagnostics.TotalBudgetBytes = TotalMemoryBudgetBytes;
    Diagnostics.PerJobBudgetBytes = PerJobMemoryBudgetBytes;
    Diagnostics.HighWaterReservedBytes = HighWaterReservedBytes;
    Diagnostics.BudgetRejectionCount = BudgetRejectionCount;
    Diagnostics.QueueRejectionCount = QueueRejectionCount;
    Diagnostics.MailboxReplacementCount = MailboxReplacementCount;
    Diagnostics.AdmissionDeferredCount = AdmissionDeferredCount;
    Diagnostics.SingletonRejectionCount = SingletonRejectionCount;
    Diagnostics.CompletedJobCount = CompletedJobCount;
    Diagnostics.TotalQueueSeconds = TotalQueueSeconds;
    Diagnostics.TotalWorkerSeconds = TotalWorkerSeconds;
    Diagnostics.MaxQueueSeconds = MaxQueueSeconds;
    Diagnostics.MaxWorkerSeconds = MaxWorkerSeconds;

    const double Now = FPlatformTime::Seconds();
    auto         AppendJob = [&Diagnostics, Now](const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& Job)
    {
        FDWCEditorWorkerJobDiagnostic& Item = Diagnostics.Jobs.AddDefaulted_GetRef();
        Item.Ticket = Job->Ticket;
        Item.DebugName = Job->Descriptor.DebugName;
        Item.Priority = Job->Descriptor.Priority;
        Item.LifecycleState = Job->LifecycleState;
        Item.OperationState = Job->OperationState;
        Item.CancellationState = Job->CancellationState;
        Item.RequestPolicy = Job->Descriptor.GetRequestPolicy();
        Item.MemoryEstimate = Job->ActualMemoryEstimate.IsEmpty()
                                  ? Job->Descriptor.MemoryEstimate
                                  : Job->ActualMemoryEstimate;
        Item.ReservedBytes = Job->MemoryLease.GetReservedBytes();
        Item.ResultBytes = Job->ResultBytes;
        Item.PrepareSeconds = Job->PrepareFinishedSeconds > 0.0
                                  ? Job->PrepareFinishedSeconds - Job->PrepareStartedSeconds
                                  : 0.0;
        Item.QueueSeconds = Job->StartedSeconds > 0.0
                                ? Job->StartedSeconds - Job->SubmittedSeconds
                                : Now - Job->SubmittedSeconds;
        Item.WorkerSeconds = Job->StartedSeconds > 0.0
                                 ? (Job->CommitStartedSeconds > 0.0 ? Job->CommitStartedSeconds : Now) - Job->StartedSeconds
                                 : 0.0;
        Item.CommitSeconds = Job->CommitFinishedSeconds > 0.0
                                 ? Job->CommitFinishedSeconds - Job->CommitStartedSeconds
                                 : 0.0;
        Item.CancellationSeconds = Job->CancelRequestedSeconds > 0.0
                                       ? Now - Job->CancelRequestedSeconds
                                       : 0.0;
    };
    for (const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& Job : PendingAdmissionJobs)
        AppendJob(Job);
    for (const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& Job : PreparingJobs)
        AppendJob(Job);
    for (const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& Job : ActiveJobs)
        AppendJob(Job);
    for (const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& Job : ReadyJobs)
        AppendJob(Job);
    return Diagnostics;
}

void FDWCEditorWorkerJobScheduler::ResetDiagnosticCounters()
{
    check(IsInGameThread());
    HighWaterReservedBytes = CalculateReservedBytes();
    BudgetRejectionCount = 0;
    QueueRejectionCount = 0;
    MailboxReplacementCount = 0;
    AdmissionDeferredCount = 0;
    SingletonRejectionCount = 0;
    CompletedJobCount = 0;
    TotalQueueSeconds = 0.0;
    TotalWorkerSeconds = 0.0;
    MaxQueueSeconds = 0.0;
    MaxWorkerSeconds = 0.0;
}

void FDWCEditorWorkerJobScheduler::StartEligibleJobs()
{
    check(IsInGameThread());
    while (!bShuttingDown && ActiveJobs.Num() < MaxActiveJobs && !ReadyJobs.IsEmpty())
    {
        int32 EligibleIndex = INDEX_NONE;
        for (int32 Index = 0; Index < ReadyJobs.Num(); ++Index)
        {
            const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& Candidate = ReadyJobs[Index];
            const bool                                         bSameKeyAlreadyRunning = ActiveJobs.ContainsByPredicate(
                [&Candidate](const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& ActiveJob)
                {
                    return ActiveJob->Descriptor.Key == Candidate->Descriptor.Key;
                });
            if (!Candidate->CancellationToken->IsCanceled() && Candidate->MemoryLease.IsValid() &&
                !bSameKeyAlreadyRunning)
            {
                EligibleIndex = Index;
                break;
            }
        }
        if (EligibleIndex == INDEX_NONE)
        {
            break;
        }

        TSharedRef<FQueuedJob, ESPMode::ThreadSafe> Job = ReadyJobs[EligibleIndex];
        ReadyJobs.RemoveAt(EligibleIndex, 1, EAllowShrinking::No);
        if (Job->CancellationToken->IsCanceled())
        {
            FinalizeNonRunningJob(Job, EDWCEditorWorkerJobCompletion::Canceled, FString());
            continue;
        }

        ActiveJobs.Add(Job);
        TransitionJob(Job, EDWCEditorAsyncOperationState::Running);
        Job->LifecycleState = EDWCEditorWorkerJobLifecycleState::Running;
        Job->StartedSeconds = FPlatformTime::Seconds();
        UE_LOG(
            LogDWCEditorWorkerJobs,
            Verbose,
            TEXT("Worker job %llu '%s' -> %s after %.3fs total wait (prepare %.3fs)."),
            Job->Ticket.JobId,
            *Job->Descriptor.DebugName,
            WorkerLifecycleToString(Job->LifecycleState),
            Job->StartedSeconds - Job->SubmittedSeconds,
            Job->PrepareFinishedSeconds - Job->PrepareStartedSeconds);

        TWeakPtr<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe> WeakScheduler = AsShared();
        AsyncTask(
            ENamedThreads::AnyBackgroundThreadNormalTask,
            [WeakScheduler, Job]()
            {
                TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe> Result;
                if (!Job->CancellationToken->IsCanceled())
                {
                    Result = Job->Work(Job->CancellationToken);
                }
                AsyncTask(
                    ENamedThreads::GameThread,
                    [WeakScheduler, Job, Result = MoveTemp(Result)]() mutable
                    {
                        if (const TSharedPtr<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe> Scheduler = WeakScheduler.Pin())
                        {
                            Scheduler->HandleWorkerFinished(Job, MoveTemp(Result));
                        }
                        else
                        {
                            const FString Error = Result.IsValid() ? Result->Error : FString();
                            Result.Reset();
                            FinalizeDetachedJob(Job, Error);
                        }
                    });
            });
    }
}

void FDWCEditorWorkerJobScheduler::HandleWorkerFinished(
    const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>&         Job,
    TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe> Result)
{
    check(IsInGameThread());
    TransitionJob(Job, EDWCEditorAsyncOperationState::CommitPending);
    Job->LifecycleState = EDWCEditorWorkerJobLifecycleState::Finalizing;
    Job->CommitStartedSeconds = FPlatformTime::Seconds();
    Job->ResultBytes = Result.IsValid() ? Result->ResultBytes : 0;
    const FString ResultError = Result.IsValid() ? Result->Error : FString();

    const bool bDomainRevisionCurrent = Job->Ticket.Domain == EDWCEditorAuthoringDomain::None ||
                                        !DomainRevisionProvider ||
                                        DomainRevisionProvider(Job->Ticket.Domain) == Job->Ticket.DomainRevision;
    const bool bGenerationCurrent = Job->Descriptor.GetRequestPolicy() != EDWCEditorAsyncRequestPolicy::LatestWins ||
                                    IsCurrentGeneration(Job->Ticket);

    EDWCEditorWorkerJobCompletion Completion = EDWCEditorWorkerJobCompletion::Failed;
    if (bShuttingDown)
    {
        Completion = EDWCEditorWorkerJobCompletion::Canceled;
    }
    else if (!bGenerationCurrent)
    {
        Completion = EDWCEditorWorkerJobCompletion::Superseded;
    }
    else if (Job->CancellationToken->IsCanceled())
    {
        Completion = EDWCEditorWorkerJobCompletion::Canceled;
    }
    else if (!bDomainRevisionCurrent)
    {
        Completion = EDWCEditorWorkerJobCompletion::Stale;
    }
    else if (Result.IsValid() && Result->bSucceeded)
    {
        TransitionJob(Job, EDWCEditorAsyncOperationState::Committing);
        Job->Apply(Job->Ticket, MoveTemp(Result));
        Completion = EDWCEditorWorkerJobCompletion::Applied;
    }

    NotifyFinished(Job, Completion, ResultError);
    Result.Reset();
    Job->Prepare = nullptr;
    Job->Work = nullptr;
    Job->Apply = nullptr;
    Job->Finished = nullptr;
    Job->CommitFinishedSeconds = FPlatformTime::Seconds();
    if (Job->OperationState == EDWCEditorAsyncOperationState::Committing)
    {
        TransitionJob(Job, EDWCEditorAsyncOperationState::Retiring);
    }
    MarkCompleted(Job, Completion);
    ActiveJobs.RemoveSingle(Job);
    Job->MemoryLease.Reset();
    PumpAdmissions();
}

bool FDWCEditorWorkerJobScheduler::IsCurrentGeneration(const FDWCEditorWorkerJobTicket& Ticket) const
{
    const uint64* CurrentGeneration = GenerationByKey.Find(Ticket.Key);
    return CurrentGeneration != nullptr && *CurrentGeneration == Ticket.Generation;
}

bool FDWCEditorWorkerJobScheduler::HasOutstandingJobForKey(
    const FDWCEditorWorkerJobKey& Key) const
{
    const auto ContainsKey = [&Key](const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& Job)
    {
        return Job->Descriptor.Key == Key;
    };
    return PendingAdmissionJobs.ContainsByPredicate(ContainsKey) ||
           PreparingJobs.ContainsByPredicate(ContainsKey) ||
           ReadyJobs.ContainsByPredicate(ContainsKey) ||
           ActiveJobs.ContainsByPredicate(ContainsKey);
}

bool FDWCEditorWorkerJobScheduler::HasOlderFIFOJob(
    const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& Job) const
{
    const auto IsOlderSameKey = [&Job](const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& Candidate)
    {
        return Candidate != Job &&
               Candidate->Descriptor.Key == Job->Descriptor.Key &&
               Candidate->Ticket.JobId < Job->Ticket.JobId;
    };
    return PendingAdmissionJobs.ContainsByPredicate(IsOlderSameKey) ||
           PreparingJobs.ContainsByPredicate(IsOlderSameKey) ||
           ReadyJobs.ContainsByPredicate(IsOlderSameKey) ||
           ActiveJobs.ContainsByPredicate(IsOlderSameKey);
}

bool FDWCEditorWorkerJobScheduler::HasActiveJobForKey(
    const FDWCEditorWorkerJobKey& Key) const
{
    const auto ContainsKey = [&Key](const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& Job)
    {
        return Job->Descriptor.Key == Key;
    };
    return PreparingJobs.ContainsByPredicate(ContainsKey) ||
           ReadyJobs.ContainsByPredicate(ContainsKey) ||
           ActiveJobs.ContainsByPredicate(ContainsKey);
}

int32 FDWCEditorWorkerJobScheduler::GetOutstandingQueueCount() const
{
    return PendingAdmissionJobs.Num() + PreparingJobs.Num() + ReadyJobs.Num();
}

void FDWCEditorWorkerJobScheduler::SupersedeLatestJobs(
    const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& Replacement)
{
    const FDWCEditorWorkerJobKey                              Key = Replacement->Descriptor.Key;
    const TArray<TSharedRef<FQueuedJob, ESPMode::ThreadSafe>> PendingSnapshot = PendingAdmissionJobs;
    for (const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& Job : PendingSnapshot)
    {
        if (Job != Replacement && Job->Descriptor.Key == Key &&
            PendingAdmissionJobs.Contains(Job))
        {
            ++MailboxReplacementCount;
            RequestJobCancellation(Job);
            FinalizeNonRunningJob(Job, EDWCEditorWorkerJobCompletion::Superseded, FString());
        }
    }
    for (const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& Job : PreparingJobs)
    {
        if (Job->Descriptor.Key == Key)
        {
            RequestJobCancellation(Job);
        }
    }
    const TArray<TSharedRef<FQueuedJob, ESPMode::ThreadSafe>> ReadySnapshot = ReadyJobs;
    for (const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& Job : ReadySnapshot)
    {
        if (Job->Descriptor.Key == Key && ReadyJobs.Contains(Job))
        {
            RequestJobCancellation(Job);
            FinalizeNonRunningJob(Job, EDWCEditorWorkerJobCompletion::Superseded, FString());
        }
    }
    for (const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& Job : ActiveJobs)
    {
        if (Job->Descriptor.Key == Key)
        {
            RequestJobCancellation(Job);
        }
    }
}

void FDWCEditorWorkerJobScheduler::CancelJobsByKey(const FDWCEditorWorkerJobKey& Key)
{
    const TArray<TSharedRef<FQueuedJob, ESPMode::ThreadSafe>> PendingSnapshot = PendingAdmissionJobs;
    for (const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& Job : PendingSnapshot)
    {
        if (Job->Descriptor.Key == Key && PendingAdmissionJobs.Contains(Job))
        {
            RequestJobCancellation(Job);
            FinalizeNonRunningJob(Job, EDWCEditorWorkerJobCompletion::Canceled, FString());
        }
    }
    for (const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& Job : PreparingJobs)
    {
        if (Job->Descriptor.Key == Key)
        {
            RequestJobCancellation(Job);
        }
    }
    const TArray<TSharedRef<FQueuedJob, ESPMode::ThreadSafe>> ReadySnapshot = ReadyJobs;
    for (const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& Job : ReadySnapshot)
    {
        if (Job->Descriptor.Key == Key && ReadyJobs.Contains(Job))
        {
            RequestJobCancellation(Job);
            FinalizeNonRunningJob(Job, EDWCEditorWorkerJobCompletion::Canceled, FString());
        }
    }
    for (const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& Job : ActiveJobs)
    {
        if (Job->Descriptor.Key == Key)
        {
            RequestJobCancellation(Job);
        }
    }
}

void FDWCEditorWorkerJobScheduler::FinalizeNonRunningJob(
    const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& Job,
    const EDWCEditorWorkerJobCompletion                Completion,
    const FString&                                     Error)
{
    check(IsInGameThread());
    PendingAdmissionJobs.RemoveSingle(Job);
    PreparingJobs.RemoveSingle(Job);
    ReadyJobs.RemoveSingle(Job);
    NotifyFinished(Job, Completion, Error);
    Job->Prepare = nullptr;
    Job->Work = nullptr;
    Job->Apply = nullptr;
    Job->Finished = nullptr;
    MarkCompleted(Job, Completion);
    Job->MemoryLease.Reset();
}

void FDWCEditorWorkerJobScheduler::FinalizeDetachedJob(
    const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& Job,
    const FString&                                     Error)
{
    check(IsInGameThread());
    RequestJobCancellation(Job);
    NotifyFinished(Job, EDWCEditorWorkerJobCompletion::Canceled, Error);
    Job->Prepare = nullptr;
    Job->Work = nullptr;
    Job->Apply = nullptr;
    Job->Finished = nullptr;
    if (Job->CancellationState == EDWCEditorAsyncCancellationState::CancelRequested)
    {
        FDWCEditorAsyncOperationContract::ValidateCancellationTransition(
            Job->CancellationState,
            EDWCEditorAsyncCancellationState::CancelAcknowledged,
            *Job->Descriptor.DebugName);
        Job->CancellationState = EDWCEditorAsyncCancellationState::CancelAcknowledged;
    }
    TransitionJob(Job, EDWCEditorAsyncOperationState::Completed);
    Job->LifecycleState = EDWCEditorWorkerJobLifecycleState::Completed;
    Job->MemoryLease.Reset();
}

void FDWCEditorWorkerJobScheduler::NotifyFinished(
    const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& Job,
    const EDWCEditorWorkerJobCompletion                Completion,
    const FString&                                     Error)
{
    if (Job->bFinishedNotified)
    {
        return;
    }
    Job->bFinishedNotified = true;
    Job->Completion = Completion;
    Job->CompletionError = Error;
    if (Job->Finished)
    {
        Job->Finished(Job->Ticket, Completion, Error);
    }
}

bool FDWCEditorWorkerJobScheduler::TransitionJob(
    const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& Job,
    const EDWCEditorAsyncOperationState                NewState)
{
    if (Job->OperationState == NewState)
    {
        return true;
    }
    if (!FDWCEditorAsyncOperationContract::ValidateTransition(
            Job->OperationState,
            NewState,
            *Job->Descriptor.DebugName))
    {
        return false;
    }
    Job->OperationState = NewState;
    return true;
}

void FDWCEditorWorkerJobScheduler::RequestJobCancellation(
    const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& Job)
{
    Job->CancellationToken->Cancel();
    if (Job->CancellationState == EDWCEditorAsyncCancellationState::None)
    {
        FDWCEditorAsyncOperationContract::ValidateCancellationTransition(
            Job->CancellationState,
            EDWCEditorAsyncCancellationState::CancelRequested,
            *Job->Descriptor.DebugName);
        Job->CancellationState = EDWCEditorAsyncCancellationState::CancelRequested;
        Job->CancelRequestedSeconds = FPlatformTime::Seconds();
    }
    if (Job->OperationState == EDWCEditorAsyncOperationState::Running)
    {
        Job->LifecycleState = EDWCEditorWorkerJobLifecycleState::CancelRequested;
    }
}

void FDWCEditorWorkerJobScheduler::MarkCompleted(
    const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& Job,
    const EDWCEditorWorkerJobCompletion                Completion)
{
    if (Job->OperationState == EDWCEditorAsyncOperationState::Completed)
    {
        return;
    }
    if (Job->CancellationState == EDWCEditorAsyncCancellationState::CancelRequested)
    {
        FDWCEditorAsyncOperationContract::ValidateCancellationTransition(
            Job->CancellationState,
            EDWCEditorAsyncCancellationState::CancelAcknowledged,
            *Job->Descriptor.DebugName);
        Job->CancellationState = EDWCEditorAsyncCancellationState::CancelAcknowledged;
    }
    TransitionJob(Job, EDWCEditorAsyncOperationState::Completed);

    const double Now = FPlatformTime::Seconds();
    const double QueueSeconds = Job->StartedSeconds > 0.0
                                    ? Job->StartedSeconds - Job->SubmittedSeconds
                                    : Now - Job->SubmittedSeconds;
    const double WorkerSeconds = Job->StartedSeconds > 0.0
                                     ? (Job->CommitStartedSeconds > 0.0 ? Job->CommitStartedSeconds : Now) - Job->StartedSeconds
                                     : 0.0;
    ++CompletedJobCount;
    TotalQueueSeconds += FMath::Max(QueueSeconds, 0.0);
    TotalWorkerSeconds += FMath::Max(WorkerSeconds, 0.0);
    MaxQueueSeconds = FMath::Max(MaxQueueSeconds, QueueSeconds);
    MaxWorkerSeconds = FMath::Max(MaxWorkerSeconds, WorkerSeconds);
    Job->LifecycleState = EDWCEditorWorkerJobLifecycleState::Completed;
    UE_LOG(
        LogDWCEditorWorkerJobs,
        Verbose,
        TEXT("Worker job %llu '%s' -> %s (completion=%d, queue=%.3fs, worker=%.3fs, reserved=%s/%s)."),
        Job->Ticket.JobId,
        *Job->Descriptor.DebugName,
        WorkerLifecycleToString(Job->LifecycleState),
        static_cast<int32>(Completion),
        QueueSeconds,
        WorkerSeconds,
        *FormatMiB(CalculateReservedBytes()),
        *FormatMiB(TotalMemoryBudgetBytes));
}

FString FDWCEditorWorkerJobScheduler::BuildBudgetFailureDiagnostic(
    const FDWCEditorWorkerJobDescriptor& Descriptor,
    const bool                           bPerJobLimit,
    const FString&                       GovernorError) const
{
    TArray<FString> Owners;
    auto            AppendOwner = [&Owners](const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& Job)
    {
        Owners.Add(FString::Printf(
            TEXT("%s [job=%llu, state=%s, slot=%d, reserved=%s]"),
            Job->Descriptor.DebugName.IsEmpty() ? TEXT("Unnamed") : *Job->Descriptor.DebugName,
            Job->Ticket.JobId,
            WorkerLifecycleToString(Job->LifecycleState),
            Job->Descriptor.Key.MaterialSlotIndex,
            *FormatMiB(Job->MemoryLease.GetReservedBytes())));
    };
    for (const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& Job : PreparingJobs)
        AppendOwner(Job);
    for (const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& Job : ActiveJobs)
        AppendOwner(Job);
    for (const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& Job : ReadyJobs)
        AppendOwner(Job);

    return FString::Printf(
        TEXT("The editor worker job '%s' exceeds the %s memory budget. requested=%s, reserved=%s, totalBudget=%s, perJobBudget=%s. Reservation owners: %s%s%s"),
        Descriptor.DebugName.IsEmpty() ? TEXT("Unnamed") : *Descriptor.DebugName,
        bPerJobLimit ? TEXT("per-job") : TEXT("total scheduler"),
        *FormatMiB(Descriptor.GetReservedBytes()),
        *FormatMiB(CalculateReservedBytes()),
        *FormatMiB(TotalMemoryBudgetBytes),
        *FormatMiB(PerJobMemoryBudgetBytes),
        Owners.IsEmpty() ? TEXT("none") : *FString::Join(Owners, TEXT("; ")),
        GovernorError.IsEmpty() ? TEXT("") : TEXT(". Governor: "),
        GovernorError.IsEmpty() ? TEXT("") : *GovernorError);
}

uint64 FDWCEditorWorkerJobScheduler::CalculateReservedBytes() const
{
    uint64 ReservedBytes = 0;
    auto   AddReservation = [&ReservedBytes](const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& Job)
    {
        const uint64 JobBytes = Job->MemoryLease.GetReservedBytes();
        ReservedBytes = JobBytes <= MAX_uint64 - ReservedBytes ? ReservedBytes + JobBytes : MAX_uint64;
    };
    for (const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& Job : PreparingJobs)
        AddReservation(Job);
    for (const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& Job : ActiveJobs)
        AddReservation(Job);
    for (const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& Job : ReadyJobs)
        AddReservation(Job);
    return ReservedBytes;
}
