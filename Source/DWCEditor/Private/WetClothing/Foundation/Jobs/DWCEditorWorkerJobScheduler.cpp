#include "WetClothing/Foundation/Jobs/DWCEditorWorkerJobScheduler.h"

#include "Async/Async.h"

struct FDWCEditorWorkerJobScheduler::FQueuedJob
{
    FDWCEditorWorkerJobDescriptor Descriptor;
    FDWCEditorWorkerJobTicket Ticket;
    TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe> CancellationToken =
        MakeShared<FDWCEditorCancellationToken, ESPMode::ThreadSafe>();
    FWork Work;
    FApply Apply;
    FFinished Finished;
    bool bReservationAcquired = false;
    bool bReservationReleased = false;
};

FDWCEditorWorkerJobScheduler::FDWCEditorWorkerJobScheduler(
    const int32 InMaxActiveJobs,
    const uint64 InTotalMemoryBudgetBytes,
    const uint64 InPerJobMemoryBudgetBytes,
    const int32 InMaxQueuedJobs)
    : MaxActiveJobs(FMath::Max(1, InMaxActiveJobs))
    , MaxQueuedJobs(FMath::Max(1, InMaxQueuedJobs))
    , TotalMemoryBudgetBytes(FMath::Max<uint64>(1, InTotalMemoryBudgetBytes))
    , PerJobMemoryBudgetBytes(FMath::Max<uint64>(1, InPerJobMemoryBudgetBytes))
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
    FWork Work,
    FApply Apply,
    FString* OutError,
    FFinished Finished)
{
    check(IsInGameThread());
    if (OutError != nullptr)
    {
        OutError->Reset();
    }
    if (bShuttingDown)
    {
        if (OutError != nullptr) *OutError = TEXT("The editor worker scheduler is shutting down.");
        return {};
    }
    if (!Work || !Apply)
    {
        if (OutError != nullptr) *OutError = TEXT("The editor worker job is missing its work or apply callback.");
        return {};
    }
    if (Descriptor.EstimatedBytes > PerJobMemoryBudgetBytes)
    {
        if (OutError != nullptr) *OutError = TEXT("The editor worker job exceeds the per-job memory budget.");
        return {};
    }

    CancelSupersededJobs(Descriptor.Key);
    if (QueuedJobs.Num() >= MaxQueuedJobs)
    {
        if (OutError != nullptr) *OutError = TEXT("The editor worker scheduler queue is full.");
        return {};
    }
    if (Descriptor.EstimatedBytes > TotalMemoryBudgetBytes - FMath::Min(ReservedBytes, TotalMemoryBudgetBytes))
    {
        if (OutError != nullptr) *OutError = TEXT("The editor worker scheduler memory budget is fully reserved by queued or active jobs.");
        return {};
    }

    const uint64 Generation = GenerationByKey.FindOrAdd(Descriptor.Key) + 1;
    GenerationByKey.Add(Descriptor.Key, Generation);

    TSharedRef<FQueuedJob, ESPMode::ThreadSafe> Job = MakeShared<FQueuedJob, ESPMode::ThreadSafe>();
    Job->Descriptor = Descriptor;
    Job->Ticket.Key = Descriptor.Key;
    Job->Ticket.JobId = NextJobId++;
    Job->Ticket.Generation = Generation;
    Job->Ticket.Domain = Descriptor.Domain;
    Job->Ticket.DomainRevision = Descriptor.DomainRevision;
    Job->Work = MoveTemp(Work);
    Job->Apply = MoveTemp(Apply);
    Job->Finished = MoveTemp(Finished);
    Job->bReservationAcquired = true;
    ReservedBytes += Job->Descriptor.EstimatedBytes;
    QueuedJobs.Add(Job);
    QueuedJobs.StableSort(
        [](const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& A,
           const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& B)
        {
            return static_cast<uint8>(A->Descriptor.Priority) > static_cast<uint8>(B->Descriptor.Priority);
        });
    StartEligibleJobs();
    return Job->Ticket;
}

void FDWCEditorWorkerJobScheduler::Cancel(const FDWCEditorWorkerJobKey& Key)
{
    check(IsInGameThread());
    CancelSupersededJobs(Key);
    GenerationByKey.FindOrAdd(Key) += 1;
    StartEligibleJobs();
}

void FDWCEditorWorkerJobScheduler::CancelDomain(const EDWCEditorAuthoringDomain Domain)
{
    check(IsInGameThread());
    for (int32 Index = QueuedJobs.Num() - 1; Index >= 0; --Index)
    {
        const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& Job = QueuedJobs[Index];
        if (Job->Descriptor.Domain == Domain)
        {
            Job->CancellationToken->Cancel();
            GenerationByKey.FindOrAdd(Job->Descriptor.Key) += 1;
            ReleaseReservation(Job);
            QueuedJobs.RemoveAt(Index, 1, EAllowShrinking::No);
            if (Job->Finished)
            {
                Job->Finished(Job->Ticket, EDWCEditorWorkerJobCompletion::Canceled, FString());
            }
        }
    }
    for (const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& Job : ActiveJobs)
    {
        if (Job->Descriptor.Domain == Domain)
        {
            Job->CancellationToken->Cancel();
            GenerationByKey.FindOrAdd(Job->Descriptor.Key) += 1;
        }
    }
    StartEligibleJobs();
}

void FDWCEditorWorkerJobScheduler::Shutdown()
{
    if (bShuttingDown)
    {
        return;
    }
    check(IsInGameThread());
    bShuttingDown = true;
    for (const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& Job : QueuedJobs)
    {
        Job->CancellationToken->Cancel();
        ReleaseReservation(Job);
    }
    QueuedJobs.Reset();
    for (const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& Job : ActiveJobs)
    {
        Job->CancellationToken->Cancel();
    }
    DomainRevisionProvider = nullptr;
}

int32 FDWCEditorWorkerJobScheduler::GetQueuedJobCount() const
{
    check(IsInGameThread());
    return QueuedJobs.Num();
}

int32 FDWCEditorWorkerJobScheduler::GetActiveJobCount() const
{
    check(IsInGameThread());
    return ActiveJobs.Num();
}

uint64 FDWCEditorWorkerJobScheduler::GetReservedBytes() const
{
    check(IsInGameThread());
    return ReservedBytes;
}

uint64 FDWCEditorWorkerJobScheduler::GetCurrentDomainRevision(
    const EDWCEditorAuthoringDomain Domain) const
{
    check(IsInGameThread());
    return DomainRevisionProvider ? DomainRevisionProvider(Domain) : 0;
}

void FDWCEditorWorkerJobScheduler::StartEligibleJobs()
{
    check(IsInGameThread());
    while (!bShuttingDown && ActiveJobs.Num() < MaxActiveJobs && !QueuedJobs.IsEmpty())
    {
        int32 EligibleIndex = INDEX_NONE;
        for (int32 Index = 0; Index < QueuedJobs.Num(); ++Index)
        {
            const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& Candidate = QueuedJobs[Index];
            if (Candidate->CancellationToken->IsCanceled())
            {
                continue;
            }
            if (Candidate->bReservationAcquired)
            {
                EligibleIndex = Index;
                break;
            }
        }
        if (EligibleIndex == INDEX_NONE)
        {
            break;
        }

        TSharedRef<FQueuedJob, ESPMode::ThreadSafe> Job = QueuedJobs[EligibleIndex];
        QueuedJobs.RemoveAt(EligibleIndex, 1, EAllowShrinking::No);
        if (Job->CancellationToken->IsCanceled())
        {
            continue;
        }

        ActiveJobs.Add(Job);
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
                    });
            });
    }
}

void FDWCEditorWorkerJobScheduler::HandleWorkerFinished(
    const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& Job,
    TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe> Result)
{
    check(IsInGameThread());
    ActiveJobs.RemoveSingle(Job);
    ReleaseReservation(Job);

    const FString ResultError = Result.IsValid() ? Result->Error : FString();

    const bool bDomainRevisionCurrent = Job->Ticket.Domain == EDWCEditorAuthoringDomain::None ||
        !DomainRevisionProvider ||
        DomainRevisionProvider(Job->Ticket.Domain) == Job->Ticket.DomainRevision;
    EDWCEditorWorkerJobCompletion Completion = EDWCEditorWorkerJobCompletion::Failed;
    if (bShuttingDown || Job->CancellationToken->IsCanceled())
    {
        Completion = EDWCEditorWorkerJobCompletion::Canceled;
    }
    else if (!IsCurrentGeneration(Job->Ticket))
    {
        Completion = EDWCEditorWorkerJobCompletion::Superseded;
    }
    else if (!bDomainRevisionCurrent)
    {
        Completion = EDWCEditorWorkerJobCompletion::Stale;
    }
    else if (Result.IsValid() && Result->bSucceeded)
    {
        Job->Apply(Job->Ticket, MoveTemp(Result));
        Completion = EDWCEditorWorkerJobCompletion::Applied;
    }
    if (Job->Finished)
    {
        Job->Finished(Job->Ticket, Completion, ResultError);
    }
    StartEligibleJobs();
}

bool FDWCEditorWorkerJobScheduler::IsCurrentGeneration(const FDWCEditorWorkerJobTicket& Ticket) const
{
    const uint64* CurrentGeneration = GenerationByKey.Find(Ticket.Key);
    return CurrentGeneration != nullptr && *CurrentGeneration == Ticket.Generation;
}

void FDWCEditorWorkerJobScheduler::CancelSupersededJobs(const FDWCEditorWorkerJobKey& Key)
{
    for (int32 Index = QueuedJobs.Num() - 1; Index >= 0; --Index)
    {
        const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& Job = QueuedJobs[Index];
        if (Job->Descriptor.Key == Key)
        {
            Job->CancellationToken->Cancel();
            ReleaseReservation(Job);
            QueuedJobs.RemoveAt(Index, 1, EAllowShrinking::No);
            if (Job->Finished)
            {
                Job->Finished(Job->Ticket, EDWCEditorWorkerJobCompletion::Superseded, FString());
            }
        }
    }
    for (const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& Job : ActiveJobs)
    {
        if (Job->Descriptor.Key == Key)
        {
            Job->CancellationToken->Cancel();
        }
    }
}

void FDWCEditorWorkerJobScheduler::ReleaseReservation(
    const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& Job)
{
    if (Job->bReservationAcquired && !Job->bReservationReleased)
    {
        ReservedBytes = ReservedBytes >= Job->Descriptor.EstimatedBytes
            ? ReservedBytes - Job->Descriptor.EstimatedBytes
            : 0;
        Job->bReservationReleased = true;
    }
}
