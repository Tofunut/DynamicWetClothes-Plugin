//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Async/DWCEditorResourceGovernor.h"
#include "WetClothing/Foundation/TextureWorkspace/DWCEditorTextureWorkspaceTypes.h"

struct FDWCEditorPreviewMemoryBucket;
struct FDWCEditorPreviewOperationCounter;

/** Game-thread queue that coalesces CPU dirty regions before submitting render uploads. */
class FDWCEditorRenderUploadQueue final
{
  public:
    static constexpr uint64 DefaultStagingBudgetBytes = 64ull * 1024ull * 1024ull;
    static constexpr uint64 DefaultPerFlushBudgetBytes = 32ull * 1024ull * 1024ull;
    static constexpr uint64 DefaultInteractiveSubmitBudgetBytes = 16ull * 1024ull * 1024ull;
    static constexpr double DefaultInteractiveSubmitTimeBudgetMs = 2.0;

    using FUploadStateObserver = TFunction<void(EDWCEditorTextureUploadStatus)>;

    explicit FDWCEditorRenderUploadQueue(
        uint64 InStagingBudgetBytes = DefaultStagingBudgetBytes,
        uint64 InPerFlushBudgetBytes = DefaultPerFlushBudgetBytes);
    FDWCEditorRenderUploadQueue(
        TSharedRef<FDWCEditorResourceGovernor> InResourceGovernor,
        const FGuid& InSessionEpoch,
        uint64 InStagingBudgetBytes = DefaultStagingBudgetBytes,
        uint64 InPerFlushBudgetBytes = DefaultPerFlushBudgetBytes);

    void Enqueue(
        const FDWCEditorTextureHandle& Entry,
        const FIntRect& DirtyRect,
        bool bWrap,
        EDWCEditorTextureUploadPriority Priority = EDWCEditorTextureUploadPriority::Normal);
    bool EnqueuePreparedBGRA8(
        const FDWCEditorTextureHandle& Entry,
        TArray<FDWCEditorPreparedBGRA8Region>&& Regions,
        EDWCEditorTextureUploadPriority Priority = EDWCEditorTextureUploadPriority::Interactive);
    void Cancel(const FDWCEditorTextureKey& Key);
    void CancelOwner(const UObject* Owner);
    void Flush();
    EDWCEditorTextureUploadStatus TrySubmitInteractive(
        const FDWCEditorTextureUploadTicket& Ticket,
        uint64 ByteBudget = DefaultInteractiveSubmitBudgetBytes,
        double TimeBudgetMs = DefaultInteractiveSubmitTimeBudgetMs);
    void Shutdown();

    FDWCEditorTextureUploadTicket CaptureTicket(const FDWCEditorTextureHandle& Entry) const;
    FDWCEditorTextureUploadTicket CaptureSubmittedTicket(const FDWCEditorTextureHandle& Entry);
    EDWCEditorTextureUploadStatus GetStatus(const FDWCEditorTextureUploadTicket& Ticket) const;
    bool GetTiming(
        const FDWCEditorTextureUploadTicket& Ticket,
        FDWCEditorTextureUploadTiming& OutTiming) const;
    FDWCEditorTextureUploadObserverHandle Observe(
        const FDWCEditorTextureUploadTicket& Ticket,
        FUploadStateObserver Observer);
    void RemoveObserver(FDWCEditorTextureUploadObserverHandle& Handle);

    void AppendDiagnosticMemoryBucket(TArray<FDWCEditorPreviewMemoryBucket>& OutBuckets) const;
    void AppendDiagnosticOperationCounters(TArray<FDWCEditorPreviewOperationCounter>& OutCounters) const;
    void ResetDiagnosticCounters();

  private:
    struct FStagingState
    {
        TAtomic<uint64> InFlightBytes{0};
    };

    struct FPreparedUploadPayload
    {
        FPreparedUploadPayload(
            TArray<FDWCEditorPreparedBGRA8Region>&& InRegions,
            uint64 InReservedBytes,
            TSharedRef<FStagingState, ESPMode::ThreadSafe> InStagingState,
            TSharedPtr<FDWCEditorMemoryLease, ESPMode::ThreadSafe> InStagingLease)
            : Regions(MoveTemp(InRegions))
            , ReservedBytes(InReservedBytes)
            , StagingState(MoveTemp(InStagingState))
            , StagingLease(MoveTemp(InStagingLease))
        {
        }

        ~FPreparedUploadPayload()
        {
            if (StagingLease.IsValid())
            {
                StagingLease->Reset();
            }
            if (ReservedBytes > 0)
            {
                StagingState->InFlightBytes.SubExchange(ReservedBytes);
            }
        }

        TArray<FDWCEditorPreparedBGRA8Region> Regions;
        uint64 ReservedBytes = 0;
        TSharedRef<FStagingState, ESPMode::ThreadSafe> StagingState;
        TSharedPtr<FDWCEditorMemoryLease, ESPMode::ThreadSafe> StagingLease;
    };

    struct FPendingUpload
    {
        TWeakPtr<FDWCEditorTextureWorkspaceEntry> Entry;
        FDWCEditorDirtyRegionSet DirtyRegions;
        // Regions are moved here when a flush begins. Keeping the unfinished
        // tail lets a large 4K upload respect the per-frame byte budget.
        TArray<FIntRect, TInlineAllocator<FDWCEditorDirtyRegionSet::MaxRegions>> RemainingRegions;
        uint64 ResourceGeneration = 0;
        uint64 ContentRevision = 0;
        uint64 QueuedSerial = 0;
        EDWCEditorTextureUploadPriority Priority = EDWCEditorTextureUploadPriority::Normal;
        TSharedPtr<FDWCEditorTextureUploadTelemetryState, ESPMode::ThreadSafe> Telemetry;
        TSharedPtr<FDWCEditorTextureUploadState, ESPMode::ThreadSafe> State;
        TSharedPtr<FPreparedUploadPayload, ESPMode::ThreadSafe> PreparedPayload;
        int32 PreparedRegionIndex = 0;
        int32 PreparedRowOffset = 0;
    };

    struct FRenderEnqueuedRevision
    {
        uint64 ResourceGeneration = 0;
        uint64 ContentRevision = 0;
        TWeakPtr<FDWCEditorTextureWorkspaceEntry> Entry;
        TSharedPtr<FDWCEditorTextureUploadTelemetryState, ESPMode::ThreadSafe> Telemetry;
        TSharedPtr<FDWCEditorTextureUploadState, ESPMode::ThreadSafe> State;
    };

    void ProcessPendingUpload(
        const FDWCEditorTextureKey& Key,
        uint64& InOutSubmittedBytes,
        uint64 ByteBudget,
        double DeadlineSeconds,
        uint32 QueueDepth);
    void TransitionState(
        const TSharedPtr<FDWCEditorTextureUploadState, ESPMode::ThreadSafe>& State,
        EDWCEditorTextureUploadStatus NewStatus);
    void PromoteCompletedUploads();
    void DispatchNotifications();

    bool SubmitRegion(
        const FDWCEditorTextureHandle& Entry,
        const FIntRect& Region,
        uint64 ResourceGeneration,
        uint64 ContentRevision,
        const TSharedPtr<FDWCEditorTextureUploadTelemetryState, ESPMode::ThreadSafe>& Telemetry,
        TSharedPtr<FDWCEditorMemoryLease, ESPMode::ThreadSafe> StagingLease);
    bool SubmitPreparedRegion(
        const FDWCEditorTextureHandle& Entry,
        const FDWCEditorPreparedBGRA8Region& Region,
        int32 RowOffset,
        int32 RowCount,
        uint64 ResourceGeneration,
        uint64 ContentRevision,
        const TSharedPtr<FDWCEditorTextureUploadTelemetryState, ESPMode::ThreadSafe>& Telemetry,
        const TSharedPtr<FPreparedUploadPayload, ESPMode::ThreadSafe>& Payload);
    bool TryReserveStagingBytes(
        uint64 UploadBytes,
        TSharedPtr<FDWCEditorMemoryLease, ESPMode::ThreadSafe>& OutLease);
    void ReleaseStagingBytes(uint64 UploadBytes);

    TMap<FDWCEditorTextureKey, FPendingUpload> PendingUploads;
    TMap<FDWCEditorTextureKey, FRenderEnqueuedRevision> RenderEnqueuedRevisions;
    TSharedPtr<FDWCEditorResourceGovernor> ResourceGovernor;
    FDWCEditorAsyncOperationIdentity StagingMemoryOwner;
    uint64 StagingBudgetBytes = DefaultStagingBudgetBytes;
    uint64 PerFlushBudgetBytes = DefaultPerFlushBudgetBytes;
    TSharedRef<FStagingState, ESPMode::ThreadSafe> StagingState;
    uint64 QueuedSerial = 0;
    uint64 SubmittedUploadCount = 0;
    uint64 SubmittedUploadBytes = 0;
    uint64 CoalescedRequestCount = 0;
    uint64 DroppedStaleRequestCount = 0;
    uint64 DeferredByStagingBudgetCount = 0;
    uint64 ImmediateInteractiveSubmitCount = 0;
    uint64 DeferredInteractiveSubmitCount = 0;
    uint64 ObserverNotificationCount = 0;
    uint64 PreparedPayloadCount = 0;
    uint64 PreparedPayloadBytes = 0;
    uint64 AvoidedStagingCopyBytes = 0;
    uint64 PreparedPayloadRejectCount = 0;
    uint64 PreparedMailboxReplacementCount = 0;
    uint64 PreparedMailboxReplacementBytes = 0;
    TArray<TFunction<void()>> DeferredNotifications;
    uint64 NextObserverId = 1;
    bool bDispatchingNotifications = false;
    bool bShuttingDown = false;
};
