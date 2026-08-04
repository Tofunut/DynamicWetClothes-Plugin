#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Jobs/DWCEditorCancellationToken.h"
#include "WetClothing/Foundation/Jobs/DWCEditorWorkerJobTypes.h"

/**
 * Shared game-thread scheduler for immutable editor preview jobs.
 *
 * Snapshots are captured before Submit. Work must only touch plain data. Apply
 * always runs on the game thread after generation and authoring revision checks.
 */
class FDWCEditorWorkerJobScheduler final
    : public TSharedFromThis<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe>
{
  public:
    using FWork = TFunction<TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>(
        const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>&)>;
    using FApply = TFunction<void(
        const FDWCEditorWorkerJobTicket&,
        TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>)>;
    using FFinished = TFunction<void(
        const FDWCEditorWorkerJobTicket&,
        EDWCEditorWorkerJobCompletion,
        const FString&)>;
    using FDomainRevisionProvider = TFunction<uint64(EDWCEditorAuthoringDomain)>;

    static constexpr int32 DefaultMaxActiveJobs = 2;
    // Queued jobs retain immutable authoring snapshots. Their estimate therefore
    // reserves budget at submission time, not only after a worker starts them.
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
    ~FDWCEditorWorkerJobScheduler();

    void SetDomainRevisionProvider(FDomainRevisionProvider InProvider);
    FDWCEditorWorkerJobTicket Submit(
        const FDWCEditorWorkerJobDescriptor& Descriptor,
        FWork Work,
        FApply Apply,
        FString* OutError = nullptr,
        FFinished Finished = nullptr);
    void Cancel(const FDWCEditorWorkerJobKey& Key);
    void CancelDomain(EDWCEditorAuthoringDomain Domain);
    void Shutdown();

    int32 GetQueuedJobCount() const;
    int32 GetActiveJobCount() const;
    uint64 GetReservedBytes() const;
    uint64 GetCurrentDomainRevision(EDWCEditorAuthoringDomain Domain) const;

  private:
    struct FQueuedJob;

    void StartEligibleJobs();
    void HandleWorkerFinished(
        const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& Job,
        TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe> Result);
    bool IsCurrentGeneration(const FDWCEditorWorkerJobTicket& Ticket) const;
    void CancelSupersededJobs(const FDWCEditorWorkerJobKey& Key);
    void ReleaseReservation(const TSharedRef<FQueuedJob, ESPMode::ThreadSafe>& Job);

    TArray<TSharedRef<FQueuedJob, ESPMode::ThreadSafe>> QueuedJobs;
    TArray<TSharedRef<FQueuedJob, ESPMode::ThreadSafe>> ActiveJobs;
    TMap<FDWCEditorWorkerJobKey, uint64> GenerationByKey;
    FDomainRevisionProvider DomainRevisionProvider;
    uint64 NextJobId = 1;
    uint64 ReservedBytes = 0;
    int32 MaxActiveJobs = DefaultMaxActiveJobs;
    int32 MaxQueuedJobs = DefaultMaxQueuedJobs;
    uint64 TotalMemoryBudgetBytes = DefaultTotalMemoryBudgetBytes;
    uint64 PerJobMemoryBudgetBytes = DefaultPerJobMemoryBudgetBytes;
    bool bShuttingDown = false;
};
