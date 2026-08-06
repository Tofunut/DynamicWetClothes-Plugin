#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/TextureWorkspace/DWCEditorTextureWorkspaceTypes.h"

struct FDWCEditorPreviewMemoryBucket;
struct FDWCEditorPreviewOperationCounter;

/** Game-thread queue that coalesces CPU dirty regions before submitting render uploads. */
class FDWCEditorRenderUploadQueue final
{
  public:
    static constexpr uint64 DefaultStagingBudgetBytes = 64ull * 1024ull * 1024ull;
    static constexpr uint64 DefaultPerFlushBudgetBytes = 32ull * 1024ull * 1024ull;

    explicit FDWCEditorRenderUploadQueue(
        uint64 InStagingBudgetBytes = DefaultStagingBudgetBytes,
        uint64 InPerFlushBudgetBytes = DefaultPerFlushBudgetBytes);

    void Enqueue(
        const FDWCEditorTextureHandle& Entry,
        const FIntRect& DirtyRect,
        bool bWrap,
        EDWCEditorTextureUploadPriority Priority = EDWCEditorTextureUploadPriority::Normal);
    void Cancel(const FDWCEditorTextureKey& Key);
    void CancelOwner(const UObject* Owner);
    void Flush();
    void Shutdown();

    void AppendDiagnosticMemoryBucket(TArray<FDWCEditorPreviewMemoryBucket>& OutBuckets) const;
    void AppendDiagnosticOperationCounters(TArray<FDWCEditorPreviewOperationCounter>& OutCounters) const;
    void ResetDiagnosticCounters();

  private:
    struct FStagingState
    {
        TAtomic<uint64> InFlightBytes{0};
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
    };

    bool SubmitRegion(
        const FDWCEditorTextureHandle& Entry,
        const FIntRect& Region,
        uint64 ResourceGeneration,
        uint64 ContentRevision);
    bool TryReserveStagingBytes(uint64 UploadBytes);
    void ReleaseStagingBytes(uint64 UploadBytes);

    TMap<FDWCEditorTextureKey, FPendingUpload> PendingUploads;
    uint64 StagingBudgetBytes = DefaultStagingBudgetBytes;
    uint64 PerFlushBudgetBytes = DefaultPerFlushBudgetBytes;
    TSharedRef<FStagingState, ESPMode::ThreadSafe> StagingState;
    uint64 QueuedSerial = 0;
    uint64 SubmittedUploadCount = 0;
    uint64 SubmittedUploadBytes = 0;
    uint64 CoalescedRequestCount = 0;
    uint64 DroppedStaleRequestCount = 0;
    uint64 DeferredByStagingBudgetCount = 0;
    bool bShuttingDown = false;
};
