// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/GCObject.h"
#include "WetClothing/Foundation/Preview/Region/DWCEditorPreviewRegionTypes.h"
#include "WetClothing/Foundation/TextureWorkspace/DWCEditorTextureWorkspaceTypes.h"

class FDWCEditorRenderUploadQueue;
struct FDWCEditorPreviewMemoryBucket;
struct FDWCEditorPreviewOperationCounter;

/** WCA editor-instance owner for mutable CPU preview pixels and transient GPU textures. */
class FDWCEditorTextureWorkspace final : public FGCObject
{
  public:
    // CPU data covers pixels, normal working surfaces, and retained scratch.
    // GPU data is a separate transient texture residency budget.
    static constexpr uint64 DefaultCPUBudgetBytes = 640ull * 1024ull * 1024ull;
    static constexpr uint64 DefaultGPUBudgetBytes = 384ull * 1024ull * 1024ull;

    explicit FDWCEditorTextureWorkspace(
        TSharedRef<FDWCEditorRenderUploadQueue> InUploadQueue,
        uint64                                  InCPUBudgetBytes = DefaultCPUBudgetBytes,
        uint64                                  InGPUBudgetBytes = DefaultGPUBudgetBytes);
    virtual ~FDWCEditorTextureWorkspace() override;

    FDWCEditorTextureHandle Acquire(
        const FDWCEditorTextureKey&        Key,
        const FDWCEditorTextureDescriptor& Descriptor);
    FDWCEditorTextureLease AcquireLease(const FDWCEditorTextureHandle& Entry);
    FDWCEditorTextureLease TransferBGRA8AndAcquireLease(
        const FDWCEditorTextureKey&        Key,
        const FDWCEditorTextureDescriptor& Descriptor,
        TArray<FColor>&&                   Pixels,
        EDWCEditorTextureUploadPriority    Priority = EDWCEditorTextureUploadPriority::Normal);
    FDWCEditorTextureLease TransferNormalBGRA8AndAcquireLease(
        const FDWCEditorTextureKey&        Key,
        const FDWCEditorTextureDescriptor& Descriptor,
        TArray<FColor>&&                   Pixels,
        FDWCEditorNormalRasterSurface&&    WorkingSurface,
        EDWCEditorTextureUploadPriority    Priority = EDWCEditorTextureUploadPriority::Normal);
    FDWCEditorTextureLease TransferG8AndAcquireLease(
        const FDWCEditorTextureKey&        Key,
        const FDWCEditorTextureDescriptor& Descriptor,
        TArray<uint8>&&                    Pixels,
        EDWCEditorTextureUploadPriority    Priority = EDWCEditorTextureUploadPriority::Normal);
    FDWCEditorTextureHandle PublishBGRA8(
        const FDWCEditorTextureKey&        Key,
        const FDWCEditorTextureDescriptor& Descriptor,
        TArray<FColor>&&                   Pixels,
        EDWCEditorTextureUploadPriority    Priority = EDWCEditorTextureUploadPriority::Normal);
    FDWCEditorTextureHandle PublishNormalBGRA8(
        const FDWCEditorTextureKey&        Key,
        const FDWCEditorTextureDescriptor& Descriptor,
        TArray<FColor>&&                   Pixels,
        FDWCEditorNormalRasterSurface&&    WorkingSurface,
        EDWCEditorTextureUploadPriority    Priority = EDWCEditorTextureUploadPriority::Normal);
    FDWCEditorTextureHandle PublishG8(
        const FDWCEditorTextureKey&        Key,
        const FDWCEditorTextureDescriptor& Descriptor,
        TArray<uint8>&&                    Pixels,
        EDWCEditorTextureUploadPriority    Priority = EDWCEditorTextureUploadPriority::Normal);
    FDWCEditorPreviewRegionCommitOutcome CommitBGRA8Regions(
        const FDWCEditorTextureLease&               Lease,
        const FDWCEditorPreviewRegionTarget&        Target,
        const TArray<FDWCEditorBGRA8RegionPayload>& Regions,
        EDWCEditorTextureUploadPriority             Priority = EDWCEditorTextureUploadPriority::Normal);
    FDWCEditorPreviewRegionCommitOutcome CommitG8Regions(
        const FDWCEditorTextureLease&            Lease,
        const FDWCEditorPreviewRegionTarget&     Target,
        const TArray<FDWCEditorG8RegionPayload>& Regions,
        EDWCEditorTextureUploadPriority          Priority = EDWCEditorTextureUploadPriority::Normal);
    FDWCEditorPreviewRegionCommitOutcome CommitNormalRegions(
        const FDWCEditorTextureLease&                Lease,
        const FDWCEditorPreviewRegionTarget&         Target,
        const TArray<FDWCEditorNormalRegionPayload>& Regions,
        EDWCEditorTextureUploadPriority              Priority = EDWCEditorTextureUploadPriority::Normal);
    void MarkDirty(
        const FDWCEditorTextureHandle&  Entry,
        const FIntRect&                 DirtyRect,
        bool                            bWrap,
        EDWCEditorTextureUploadPriority Priority = EDWCEditorTextureUploadPriority::Interactive);
    void MarkDirty(
        const FDWCEditorTextureLease&   Lease,
        const FIntRect&                 DirtyRect,
        bool                            bWrap,
        EDWCEditorTextureUploadPriority Priority = EDWCEditorTextureUploadPriority::Interactive);
    /** Queues presentation-only pixels without advancing persistent DataRevision. */
    void MarkPresentationDirty(
        const FDWCEditorTextureLease&   Lease,
        const FIntRect&                 DirtyRect,
        bool                            bWrap,
        EDWCEditorTextureUploadPriority Priority = EDWCEditorTextureUploadPriority::Interactive);
    void RecreateWithAddressMode(
        const FDWCEditorTextureHandle& Entry,
        TextureAddress                 AddressX,
        TextureAddress                 AddressY);
    void RecreateWithAddressMode(
        const FDWCEditorTextureLease& Lease,
        TextureAddress                AddressX,
        TextureAddress                AddressY);
    /**
     * Removes a short-lived entry from the retained LRU cache while preserving
     * any active lease until its owner releases it.
     */
    void Discard(const FDWCEditorTextureLease& Lease);
    void Invalidate(const FDWCEditorTextureKey& Key);
    void InvalidateOwner(const UObject* Owner);
    void Reset();
    void TrimToBudget();
    /** Polls render fences and releases transient UTexture2D references after GPU retire completes. */
    void ProcessRetiredGPUResources();

    void AppendDiagnosticMemoryBucket(TArray<FDWCEditorPreviewMemoryBucket>& OutBuckets) const;
    void AppendDiagnosticOperationCounters(TArray<FDWCEditorPreviewOperationCounter>& OutCounters) const;
    void ResetDiagnosticCounters();

    virtual void    AddReferencedObjects(FReferenceCollector& Collector) override;
    virtual FString GetReferencerName() const override { return TEXT("FDWCEditorTextureWorkspace"); }

  private:
    struct FGPUResidencyStats
    {
        uint64 ResidentBytes = 0;
        uint64 RetiringBytes = 0;
        int32  ResidentEntryCount = 0;
        int32  RetiringEntryCount = 0;
        int32  CPUOnlyEntryCount = 0;
        int32  ResidentLeaseCount = 0;
        int32  RetiringLeaseCount = 0;
    };

    FDWCEditorTextureHandle FindOrCreateEntry(
        const FDWCEditorTextureKey&        Key,
        const FDWCEditorTextureDescriptor& Descriptor,
        bool                               bInitializeBuffers);
    bool EnsureTexture(
        const FDWCEditorTextureHandle& Entry,
        bool                           bDeferLargeInitialUpload = false,
        bool*                          bOutDeferredInitialUpload = nullptr);
    void                                InitializeBuffers(const FDWCEditorTextureHandle& Entry);
    EDWCEditorPreviewRegionCommitResult ValidateRegionTarget(
        const FDWCEditorTextureLease&        Lease,
        const FDWCEditorPreviewRegionTarget& Target,
        FDWCEditorTextureHandle&             OutEntry) const;
    void QueueCommittedRegions(
        const FDWCEditorTextureHandle&  Entry,
        const TArray<FIntRect>&         DirtyRegions,
        EDWCEditorTextureUploadPriority Priority,
        bool                            bDeferredInitialUpload);
    void               RetireEntry(const FDWCEditorTextureKey& Key);
    void               RetireEntry(const FDWCEditorTextureHandle& Entry);
    bool               BeginGPUResourceRetire(const FDWCEditorTextureHandle& Entry);
    void               ReleaseEntryCPUStorage(const FDWCEditorTextureHandle& Entry);
    void               RemoveCompletedRetiredEntries();
    void               ReleaseTextureLease(const FDWCEditorTextureHandle& Entry, uint64 LeaseId);
    void               RemoveEntry(const FDWCEditorTextureKey& Key, bool bCountEviction);
    void               TrimToBudget(const FDWCEditorTextureHandle& ProtectedEntry);
    uint64             CalculateCPUUsedBytes() const;
    uint64             CalculateGPUUsedBytes() const;
    FGPUResidencyStats CollectGPUResidencyStats() const;
    void               UpdateGPUHighWaterMark();

    TSharedRef<FDWCEditorRenderUploadQueue>             UploadQueue;
    TMap<FDWCEditorTextureKey, FDWCEditorTextureHandle> Entries;
    TArray<FDWCEditorTextureHandle>                     RetiredEntries;
    TSharedRef<FDWCEditorTextureLeaseState>             LeaseState;
    uint64                                              CPUBudgetBytes = DefaultCPUBudgetBytes;
    uint64                                              GPUBudgetBytes = DefaultGPUBudgetBytes;
    uint64                                              UseSerial = 0;
    uint64                                              NextLeaseId = 1;
    uint64                                              AcquireHitCount = 0;
    uint64                                              AcquireMissCount = 0;
    uint64                                              EvictionCount = 0;
    uint64                                              TextureCreateCount = 0;
    uint64                                              TextureRecreateCount = 0;
    uint64                                              GPUResourceRetireCount = 0;
    uint64                                              GPUResourceReleaseCompleteCount = 0;
    uint64                                              GPUBudgetRejectCount = 0;
    uint64                                              GPUHighWaterBytes = 0;
    uint64                                              RegionCommitRequestCount = 0;
    uint64                                              RegionCommitAppliedCount = 0;
    uint64                                              RegionCommitPixelCount = 0;
    uint64                                              RegionCommitBytes = 0;
    uint64                                              RegionCommitInvalidPayloadCount = 0;
    uint64                                              RegionCommitDataRevisionMismatchCount = 0;
    uint64                                              RegionCommitResourceGenerationMismatchCount = 0;
    uint64                                              RegionCommitDescriptorMismatchCount = 0;
    uint64                                              RegionCommitEntryMissingCount = 0;
    uint64                                              RegionCommitWorkspaceRejectedCount = 0;
};
