#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Jobs/DWCEditorCancellationToken.h"
#include "WetClothing/Foundation/Jobs/DWCEditorWorkerJobTypes.h"

class FDWCEditorResourceGovernor;

/**
 * Shared game-thread scheduler for immutable editor preview jobs.
 *
 * Two-phase jobs reserve memory before Prepare captures immutable snapshots.
 * Work only touches plain data. Apply always runs on the game thread after
 * generation and authoring revision checks.
 */
class FDWCEditorWorkerJobScheduler final
    : public TSharedFromThis<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe>
{
  public:
    using FWork = TFunction<TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>(
        const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>&)>;
    struct FPreparedWorkerJob
    {
        FWork Work;
        uint64 ActualEstimatedBytes = 0;
        FDWCEditorWorkerMemoryEstimate ActualMemoryEstimate;

        uint64 GetReservedBytes() const
        {
            const uint64 CategorizedBytes = ActualMemoryEstimate.GetTotalBytes();
            return ActualEstimatedBytes > 0 ? ActualEstimatedBytes : CategorizedBytes;
        }
    };
    using FPrepare = TFunction<bool(
        const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>&,
        FPreparedWorkerJob&,
        FString&)>;
    using FApply = TFunction<void(
        const FDWCEditorWorkerJobTicket&,
        TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>)>;
    using FFinished = TFunction<void(
        const FDWCEditorWorkerJobTicket&,
        EDWCEditorWorkerJobCompletion,
        const FString&)>;
    using FDomainRevisionProvider = TFunction<uint64(EDWCEditorAuthoringDomain)>;

    static constexpr int32 DefaultMaxActiveJobs = 2;
    // Pending requests retain only lightweight prepare closures. Prepared jobs
    // retain immutable authoring snapshots under a governor lease.
    static constexpr int32 DefaultMaxQueuedJobs = 64;
    static constexpr uint64 DefaultTotalMemoryBudgetBytes = 512ull * 1024ull * 1024ull;
    // A 4096 wrinkle preview needs one packed working surface plus its final
    // encoded output and ridge scratch space. The total budget still limits
    // high-resolution work to a bounded set of active jobs.
    static constexpr uint64 DefaultPerJobMemoryBudgetBytes = 512ull * 1024ull * 1024ull;

    explicit FDWCEditorWorkerJobScheduler(
        int32 InMaxActiveJobs = DefaultMaxActiveJobs,
        uint64 InTotalMemoryBudgetBytes = DefaultTotalMemoryBudgetBytes,
        uint64 InPerJobMemoryBudgetBytes = DefaultPerJobMemoryBudgetBytes,
        int32 InMaxQueuedJobs = DefaultMaxQueuedJobs);
    explicit FDWCEditorWorkerJobScheduler(
        TSharedRef<FDWCEditorResourceGovernor> InResourceGovernor,
        int32 InMaxActiveJobs = DefaultMaxActiveJobs,
        uint64 InTotalMemoryBudgetBytes = DefaultTotalMemoryBudgetBytes,
        uint64 InPerJobMemoryBudgetBytes = DefaultPerJobMemoryBudgetBytes,
        int32 InMaxQueuedJobs = DefaultMaxQueuedJobs);
    ~FDWCEditorWorkerJobScheduler();

    void SetDomainRevisionProvider(FDomainRevisionProvider InProvider);
    FDWCEditorWorkerJobTicket Submit(
        const FDWCEditorWorkerJobDescriptor& Descriptor,
        FWork Work,
        FApply Apply,
        FString* OutError = nullptr,
        FFinished Finished = nullptr);
    FDWCEditorWorkerJobTicket SubmitTwoPhase(
        const FDWCEditorWorkerJobDescriptor& Descriptor,
        FPrepare Prepare,
        FApply Apply,
        FString* OutError = nullptr,
        FFinished Finished = nullptr);
    void Cancel(const FDWCEditorWorkerJobKey& Key);
    void CancelDomain(EDWCEditorAuthoringDomain Domain);
    void Shutdown();

    int32 GetQueuedJobCount() const;
    int32 GetPendingAdmissionCount() const;
    int32 GetActiveJobCount() const;
    uint64 GetReservedBytes() const;
    uint64 GetCurrentDomainRevision(EDWCEditorAuthoringDomain Domain) const;
    const FGuid& GetSessionEpoch() const { return SessionEpoch; }
    FDWCEditorWorkerSchedulerDiagnostics GetDiagnostics() const;
    void ResetDiagnosticCounters();

  private:
    struct FQueuedJob;

    FDWCEditorWorkerJobTicket SubmitInternal(
        const FDWCEditorWorkerJobDescriptor& Descriptor,
        FPrepare Prepare,
        FApply Apply,
        FString* OutError,
        FFinished Finished);
    void PumpAdmissions();
    bool TryAdmitPendingJob(const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& Job);
    void StartEligibleJobs();
    void HandleWorkerFinished(
        const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& Job,
        TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe> Result);
    bool IsCurrentGeneration(const FDWCEditorWorkerJobTicket& Ticket) const;
    bool HasOutstandingJobForKey(const FDWCEditorWorkerJobKey& Key) const;
    bool HasOlderFIFOJob(const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& Job) const;
    bool HasActiveJobForKey(const FDWCEditorWorkerJobKey& Key) const;
    int32 GetOutstandingQueueCount() const;
    void SupersedeLatestJobs(const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& Replacement);
    void CancelJobsByKey(const FDWCEditorWorkerJobKey& Key);
    void FinalizeNonRunningJob(
        const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& Job,
        EDWCEditorWorkerJobCompletion Completion,
        const FString& Error);
    static void FinalizeDetachedJob(
        const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& Job,
        const FString& Error);
    static void NotifyFinished(
        const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& Job,
        EDWCEditorWorkerJobCompletion Completion,
        const FString& Error);
    static bool TransitionJob(
        const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& Job,
        EDWCEditorAsyncOperationState NewState);
    static void RequestJobCancellation(
        const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& Job);
    void MarkCompleted(
        const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& Job,
        EDWCEditorWorkerJobCompletion Completion);
    FString BuildBudgetFailureDiagnostic(
        const FDWCEditorWorkerJobDescriptor& Descriptor,
        bool bPerJobLimit,
        const FString& GovernorError = FString()) const;
    uint64 CalculateReservedBytes() const;

    TArray<TSharedRef<FQueuedJob, ESPMode::ThreadSafe>> PendingAdmissionJobs;
    TArray<TSharedRef<FQueuedJob, ESPMode::ThreadSafe>> PreparingJobs;
    TArray<TSharedRef<FQueuedJob, ESPMode::ThreadSafe>> ReadyJobs;
    TArray<TSharedRef<FQueuedJob, ESPMode::ThreadSafe>> ActiveJobs;
    TMap<FDWCEditorWorkerJobKey, uint64> GenerationByKey;
    FDomainRevisionProvider DomainRevisionProvider;
    TSharedPtr<FDWCEditorResourceGovernor> ResourceGovernor;
    FGuid SessionEpoch;
    uint64 NextJobId = 1;
    int32 MaxActiveJobs = DefaultMaxActiveJobs;
    int32 MaxQueuedJobs = DefaultMaxQueuedJobs;
    uint64 TotalMemoryBudgetBytes = DefaultTotalMemoryBudgetBytes;
    uint64 PerJobMemoryBudgetBytes = DefaultPerJobMemoryBudgetBytes;
    uint64 HighWaterReservedBytes = 0;
    uint64 BudgetRejectionCount = 0;
    uint64 QueueRejectionCount = 0;
    uint64 MailboxReplacementCount = 0;
    uint64 AdmissionDeferredCount = 0;
    uint64 SingletonRejectionCount = 0;
    uint64 CompletedJobCount = 0;
    double TotalQueueSeconds = 0.0;
    double TotalWorkerSeconds = 0.0;
    double MaxQueueSeconds = 0.0;
    double MaxWorkerSeconds = 0.0;
    bool bShuttingDown = false;
    bool bPumpingAdmissions = false;
    bool bAdmissionPumpRequested = false;
};
