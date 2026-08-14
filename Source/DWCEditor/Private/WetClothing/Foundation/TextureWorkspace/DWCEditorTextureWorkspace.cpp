//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Foundation/TextureWorkspace/DWCEditorTextureWorkspace.h"

#include "Engine/Texture2D.h"
#include "RenderingThread.h"
#include "WetClothing/Foundation/Preview/Diagnostics/DWCEditorPreviewDiagnostics.h"
#include "WetClothing/Foundation/TextureWorkspace/DWCEditorRenderUploadQueue.h"

namespace
{
    bool IsRegionRectValid(const FIntRect& Rect, const FIntPoint& Size)
    {
        return !Rect.IsEmpty() && Rect.Min.X >= 0 && Rect.Min.Y >= 0 &&
            Rect.Max.X <= Size.X && Rect.Max.Y <= Size.Y;
    }

    int64 GetRegionPixelCount(const FIntRect& Rect)
    {
        return static_cast<int64>(Rect.Width()) * static_cast<int64>(Rect.Height());
    }

    bool HasOverlappingRects(const TArray<FIntRect>& Rects)
    {
        for (int32 RectIndex = 0; RectIndex < Rects.Num(); ++RectIndex)
        {
            for (int32 OtherIndex = RectIndex + 1; OtherIndex < Rects.Num(); ++OtherIndex)
            {
                const FIntRect& A = Rects[RectIndex];
                const FIntRect& B = Rects[OtherIndex];
                if (FMath::Max(A.Min.X, B.Min.X) < FMath::Min(A.Max.X, B.Max.X) &&
                    FMath::Max(A.Min.Y, B.Min.Y) < FMath::Min(A.Max.Y, B.Max.Y))
                {
                    return true;
                }
            }
        }
        return false;
    }

    template <typename ElementType>
    void CopyRegionRows(
        const TArray<ElementType>& Source,
        const FIntRect& DestinationRect,
        const int32 DestinationWidth,
        TArray<ElementType>& Destination)
    {
        const int32 RegionWidth = DestinationRect.Width();
        for (int32 RowIndex = 0; RowIndex < DestinationRect.Height(); ++RowIndex)
        {
            const ElementType* SourceRow = Source.GetData() + RowIndex * RegionWidth;
            ElementType* DestinationRow = Destination.GetData() +
                (DestinationRect.Min.Y + RowIndex) * DestinationWidth + DestinationRect.Min.X;
            FMemory::Memcpy(DestinationRow, SourceRow, RegionWidth * sizeof(ElementType));
        }
    }
}

FDWCEditorTextureWorkspace::FDWCEditorTextureWorkspace(
    TSharedRef<FDWCEditorRenderUploadQueue> InUploadQueue,
    const uint64 InCPUBudgetBytes,
    const uint64 InGPUBudgetBytes)
    : UploadQueue(MoveTemp(InUploadQueue))
    , LeaseState(MakeShared<FDWCEditorTextureLeaseState>())
    , CPUBudgetBytes(FMath::Max<uint64>(InCPUBudgetBytes, 1))
    , GPUBudgetBytes(FMath::Max<uint64>(InGPUBudgetBytes, 1))
{
    LeaseState->ReleaseCallback = [this](const FDWCEditorTextureHandle& Entry, const uint64 LeaseId)
    {
        ReleaseTextureLease(Entry, LeaseId);
    };
}

FDWCEditorTextureWorkspace::FDWCEditorTextureWorkspace(
    TSharedRef<FDWCEditorRenderUploadQueue> InUploadQueue,
    TSharedRef<FDWCEditorResourceGovernor> InResourceGovernor,
    const FGuid& InSessionEpoch,
    const uint64 InCPUBudgetBytes,
    const uint64 InGPUBudgetBytes)
    : UploadQueue(MoveTemp(InUploadQueue))
    , ResourceGovernor(MoveTemp(InResourceGovernor))
    , LeaseState(MakeShared<FDWCEditorTextureLeaseState>())
    , CPUBudgetBytes(FMath::Max<uint64>(InCPUBudgetBytes, 1))
    , GPUBudgetBytes(FMath::Max<uint64>(InGPUBudgetBytes, 1))
{
    const FGuid SessionEpoch = InSessionEpoch.IsValid() ? InSessionEpoch : FGuid::NewGuid();
    CPUMemoryOwner.Key.Namespace = TEXT("DWC.TextureWorkspace.CPU");
    CPUMemoryOwner.SessionEpoch = SessionEpoch;
    CPUMemoryOwner.OperationId = 1;
    CPUMemoryOwner.Generation = 1;
    GPUMemoryOwner.Key.Namespace = TEXT("DWC.TextureWorkspace.GPU");
    GPUMemoryOwner.SessionEpoch = SessionEpoch;
    GPUMemoryOwner.OperationId = 2;
    GPUMemoryOwner.Generation = 1;
    LeaseState->ReleaseCallback = [this](const FDWCEditorTextureHandle& Entry, const uint64 LeaseId)
    {
        ReleaseTextureLease(Entry, LeaseId);
    };
}

FDWCEditorTextureWorkspace::~FDWCEditorTextureWorkspace()
{
    check(IsInGameThread());
    Shutdown();
}

void FDWCEditorTextureWorkspace::Shutdown()
{
    check(IsInGameThread());
    if (bShuttingDown)
    {
        return;
    }
    bShuttingDown = true;
    LeaseState->bAcceptReleases = false;
    LeaseState->ReleaseCallback = nullptr;
    Reset();

    // Render callbacks own their entry until UpdateTextureRegions has consumed
    // the payload. Drain those callbacks before issuing ReleaseResource, then
    // drain the release fences. This is the only blocking workspace boundary.
    if (!RetiredEntries.IsEmpty())
    {
        FlushRenderingCommands();
        ProcessRetiredGPUResources();
    }
    if (!RetiredEntries.IsEmpty())
    {
        FlushRenderingCommands();
        ProcessRetiredGPUResources();
    }
    ensureMsgf(
        RetiredEntries.IsEmpty(),
        TEXT("DWC texture workspace shutdown left %d retired entries."),
        RetiredEntries.Num());
}

FDWCEditorTextureHandle FDWCEditorTextureWorkspace::Acquire(
    const FDWCEditorTextureKey& Key,
    const FDWCEditorTextureDescriptor& Descriptor)
{
    check(IsInGameThread());
    return FindOrCreateEntry(Key, Descriptor, true);
}

FDWCEditorTextureLease FDWCEditorTextureWorkspace::AcquireExistingLease(
    const FDWCEditorTextureKey& Key,
    const FDWCEditorTextureDescriptor& Descriptor)
{
    check(IsInGameThread());
    FDWCEditorTextureHandle* Existing = Entries.Find(Key);
    if (Existing == nullptr || !Existing->IsValid() || (*Existing)->Descriptor != Descriptor ||
        (*Existing)->DataRevision == 0)
    {
        return FDWCEditorTextureLease();
    }

    ++AcquireHitCount;
    (*Existing)->LastUsedSerial = ++UseSerial;
    return AcquireLease(*Existing);
}

FDWCEditorTextureHandle FDWCEditorTextureWorkspace::FindOrCreateEntry(
    const FDWCEditorTextureKey& Key,
    const FDWCEditorTextureDescriptor& Descriptor,
    const bool bInitializeBuffers)
{
    if (!Descriptor.IsValid())
    {
        return nullptr;
    }

    FDWCEditorTextureHandle* Existing = Entries.Find(Key);
    if (Existing != nullptr && Existing->IsValid() && (*Existing)->Descriptor == Descriptor)
    {
        ++AcquireHitCount;
        (*Existing)->LastUsedSerial = ++UseSerial;
        return *Existing;
    }

    if (Existing != nullptr)
    {
        ++TextureRecreateCount;
        RetireEntry(Key);
    }
    else
    {
        ++AcquireMissCount;
    }

    // Make room before publishing a new entry. Trimming after insertion can
    // evict the just-created, not-yet-leased entry and leave the caller with
    // an untracked transient resource.
    TrimToBudget(FDWCEditorTextureHandle());

    FDWCEditorTextureHandle Entry = MakeShared<FDWCEditorTextureWorkspaceEntry>();
    Entry->Key = Key;
    Entry->Descriptor = Descriptor;
    Entry->LastUsedSerial = ++UseSerial;
    if (bInitializeBuffers)
    {
        InitializeBuffers(Entry);
        if (!SyncEntryCPUReservation(Entry))
        {
            ReleaseEntryCPUStorage(Entry);
            return nullptr;
        }
    }
    Entries.Add(Key, Entry);
    return Entry;
}

FDWCEditorTextureLease FDWCEditorTextureWorkspace::AcquireLease(const FDWCEditorTextureHandle& Entry)
{
    check(IsInGameThread());
    FDWCEditorTextureLease Lease;
    if (!Entry.IsValid())
    {
        return Lease;
    }

    bool bDeferredInitialUpload = false;
    if (!EnsureTexture(Entry, true, &bDeferredInitialUpload))
    {
        return Lease;
    }
    if (bDeferredInitialUpload)
    {
        UploadQueue->Enqueue(
            Entry,
            FIntRect(0, 0, Entry->Descriptor.Size.X, Entry->Descriptor.Size.Y),
            false,
            EDWCEditorTextureUploadPriority::Interactive);
    }

    ++Entry->ActiveLeaseCount;
    Lease.State = LeaseState;
    Lease.Entry = Entry;
    Lease.LeaseId = NextLeaseId++;
    return Lease;
}

FDWCEditorTextureLease FDWCEditorTextureWorkspace::TransferBGRA8AndAcquireLease(
    const FDWCEditorTextureKey& Key,
    const FDWCEditorTextureDescriptor& Descriptor,
    TArray<FColor>&& Pixels,
    const EDWCEditorTextureUploadPriority Priority)
{
    check(IsInGameThread());
    return AcquireLease(PublishBGRA8(Key, Descriptor, MoveTemp(Pixels), Priority));
}

FDWCEditorTextureLease FDWCEditorTextureWorkspace::TransferNormalBGRA8AndAcquireLease(
    const FDWCEditorTextureKey& Key,
    const FDWCEditorTextureDescriptor& Descriptor,
    TArray<FColor>&& Pixels,
    FDWCEditorNormalRasterSurface&& WorkingSurface,
    const EDWCEditorTextureUploadPriority Priority)
{
    check(IsInGameThread());
    return AcquireLease(PublishNormalBGRA8(
        Key,
        Descriptor,
        MoveTemp(Pixels),
        MoveTemp(WorkingSurface),
        Priority));
}

FDWCEditorTextureLease FDWCEditorTextureWorkspace::InitializeNormalBGRA8AndAcquireLease(
    const FDWCEditorTextureKey& Key,
    const FDWCEditorTextureDescriptor& Descriptor,
    const bool bWithCoverage,
    const EDWCEditorTextureUploadPriority Priority)
{
    check(IsInGameThread());
    if (Descriptor.PixelFormat != PF_B8G8R8A8 ||
        Descriptor.WorkingSize.X <= 0 || Descriptor.WorkingSize.Y <= 0)
    {
        return FDWCEditorTextureLease();
    }

    const FDWCEditorTextureHandle Entry = FindOrCreateEntry(Key, Descriptor, false);
    if (!Entry.IsValid())
    {
        return FDWCEditorTextureLease();
    }

    const bool bRefreshResidentTexture = Entry->DataRevision > 0 && Entry->IsGPUResident();
    Entry->BGRA8Pixels.Init(
        Descriptor.InitialBGRA8,
        Descriptor.Size.X * Descriptor.Size.Y);
    Entry->G8Pixels.Reset();
    if (!Entry->WorkingNormalSurface.Initialize(Descriptor.WorkingSize, bWithCoverage) ||
        !SyncEntryCPUReservation(Entry))
    {
        RetireEntry(Entry);
        return FDWCEditorTextureLease();
    }

    ++Entry->DataRevision;
    ++Entry->ContentRevision;
    Entry->LastUsedSerial = ++UseSerial;
    TrimToBudget(Entry);

    FDWCEditorTextureLease Lease = AcquireLease(Entry);
    if (!Lease.IsValid())
    {
        RetireEntry(Entry);
        return FDWCEditorTextureLease();
    }
    if (bRefreshResidentTexture)
    {
        UploadQueue->Enqueue(
            Entry,
            FIntRect(0, 0, Descriptor.Size.X, Descriptor.Size.Y),
            false,
            Priority);
    }
    return Lease;
}

FDWCEditorTextureLease FDWCEditorTextureWorkspace::TransferG8AndAcquireLease(
    const FDWCEditorTextureKey& Key,
    const FDWCEditorTextureDescriptor& Descriptor,
    TArray<uint8>&& Pixels,
    const EDWCEditorTextureUploadPriority Priority)
{
    check(IsInGameThread());
    return AcquireLease(PublishG8(Key, Descriptor, MoveTemp(Pixels), Priority));
}

FDWCEditorTextureLease FDWCEditorTextureWorkspace::TransferR32FAndAcquireLease(
    const FDWCEditorTextureKey& Key,
    const FDWCEditorTextureDescriptor& Descriptor,
    TArray<float>&& Pixels,
    const EDWCEditorTextureUploadPriority Priority)
{
    check(IsInGameThread());
    return AcquireLease(PublishR32F(Key, Descriptor, MoveTemp(Pixels), Priority));
}

FDWCEditorTextureHandle FDWCEditorTextureWorkspace::PublishBGRA8(
    const FDWCEditorTextureKey& Key,
    const FDWCEditorTextureDescriptor& Descriptor,
    TArray<FColor>&& Pixels,
    const EDWCEditorTextureUploadPriority Priority)
{
    check(IsInGameThread());
    if (Descriptor.PixelFormat != PF_B8G8R8A8 ||
        Pixels.Num() != Descriptor.Size.X * Descriptor.Size.Y)
    {
        return nullptr;
    }

    // The completed worker result already owns the complete pixel array. Do
    // not allocate and clear a same-sized temporary workspace buffer first.
    const FDWCEditorTextureHandle Entry = FindOrCreateEntry(Key, Descriptor, false);
    if (!Entry.IsValid())
    {
        return nullptr;
    }
    Entry->BGRA8Pixels = MoveTemp(Pixels);
    Entry->WorkingNormalSurface = FDWCEditorNormalRasterSurface();
    if (!SyncEntryCPUReservation(Entry))
    {
        RetireEntry(Entry);
        return nullptr;
    }
    ++Entry->DataRevision;
    ++Entry->ContentRevision;
    TrimToBudget(Entry);
    bool bDeferredInitialUpload = false;
    if (!EnsureTexture(Entry, true, &bDeferredInitialUpload))
    {
        return nullptr;
    }
    if (Entry->ContentRevision > 1 || bDeferredInitialUpload)
    {
        UploadQueue->Enqueue(
            Entry,
            FIntRect(0, 0, Descriptor.Size.X, Descriptor.Size.Y),
            false,
            Priority);
    }
    else
    {
        UploadQueue->CaptureSubmittedTicket(Entry);
    }
    return Entry;
}

FDWCEditorTextureHandle FDWCEditorTextureWorkspace::PublishG8(
    const FDWCEditorTextureKey& Key,
    const FDWCEditorTextureDescriptor& Descriptor,
    TArray<uint8>&& Pixels,
    const EDWCEditorTextureUploadPriority Priority)
{
    check(IsInGameThread());
    if (Descriptor.PixelFormat != PF_G8 || Pixels.Num() != Descriptor.Size.X * Descriptor.Size.Y)
    {
        return nullptr;
    }

    // The completed worker result already owns the complete pixel array. Do
    // not allocate and clear a same-sized temporary workspace buffer first.
    const FDWCEditorTextureHandle Entry = FindOrCreateEntry(Key, Descriptor, false);
    if (!Entry.IsValid())
    {
        return nullptr;
    }
    Entry->G8Pixels = MoveTemp(Pixels);
    Entry->WorkingNormalSurface = FDWCEditorNormalRasterSurface();
    if (!SyncEntryCPUReservation(Entry))
    {
        RetireEntry(Entry);
        return nullptr;
    }
    ++Entry->DataRevision;
    ++Entry->ContentRevision;
    TrimToBudget(Entry);
    bool bDeferredInitialUpload = false;
    if (!EnsureTexture(Entry, true, &bDeferredInitialUpload))
    {
        return nullptr;
    }
    if (Entry->ContentRevision > 1 || bDeferredInitialUpload)
    {
        UploadQueue->Enqueue(
            Entry,
            FIntRect(0, 0, Descriptor.Size.X, Descriptor.Size.Y),
            false,
            Priority);
    }
    else
    {
        UploadQueue->CaptureSubmittedTicket(Entry);
    }
    return Entry;
}

FDWCEditorTextureHandle FDWCEditorTextureWorkspace::PublishR32F(
    const FDWCEditorTextureKey& Key,
    const FDWCEditorTextureDescriptor& Descriptor,
    TArray<float>&& Pixels,
    const EDWCEditorTextureUploadPriority Priority)
{
    check(IsInGameThread());
    if (Descriptor.PixelFormat != PF_R32_FLOAT || Pixels.Num() != Descriptor.Size.X * Descriptor.Size.Y)
    {
        return nullptr;
    }

    const FDWCEditorTextureHandle Entry = FindOrCreateEntry(Key, Descriptor, false);
    if (!Entry.IsValid())
    {
        return nullptr;
    }
    Entry->R32FPixels = MoveTemp(Pixels);
    Entry->BGRA8Pixels.Reset();
    Entry->G8Pixels.Reset();
    Entry->WorkingNormalSurface = FDWCEditorNormalRasterSurface();
    if (!SyncEntryCPUReservation(Entry))
    {
        RetireEntry(Entry);
        return nullptr;
    }
    ++Entry->DataRevision;
    ++Entry->ContentRevision;
    TrimToBudget(Entry);
    bool bDeferredInitialUpload = false;
    if (!EnsureTexture(Entry, true, &bDeferredInitialUpload))
    {
        return nullptr;
    }
    if (Entry->ContentRevision > 1 || bDeferredInitialUpload)
    {
        UploadQueue->Enqueue(
            Entry,
            FIntRect(0, 0, Descriptor.Size.X, Descriptor.Size.Y),
            false,
            Priority);
    }
    else
    {
        UploadQueue->CaptureSubmittedTicket(Entry);
    }
    return Entry;
}

FDWCEditorTextureHandle FDWCEditorTextureWorkspace::PublishNormalBGRA8(
    const FDWCEditorTextureKey& Key,
    const FDWCEditorTextureDescriptor& Descriptor,
    TArray<FColor>&& Pixels,
    FDWCEditorNormalRasterSurface&& WorkingSurface,
    const EDWCEditorTextureUploadPriority Priority)
{
    check(IsInGameThread());
    if (Descriptor.PixelFormat != PF_B8G8R8A8 ||
        Pixels.Num() != Descriptor.Size.X * Descriptor.Size.Y ||
        !WorkingSurface.IsValid() || WorkingSurface.Size != Descriptor.WorkingSize)
    {
        return nullptr;
    }

    // The completed worker result already owns the complete pixel array. Do
    // not allocate and clear a same-sized temporary workspace buffer first.
    const FDWCEditorTextureHandle Entry = FindOrCreateEntry(Key, Descriptor, false);
    if (!Entry.IsValid())
    {
        return nullptr;
    }
    Entry->BGRA8Pixels = MoveTemp(Pixels);
    Entry->WorkingNormalSurface = MoveTemp(WorkingSurface);
    if (!SyncEntryCPUReservation(Entry))
    {
        RetireEntry(Entry);
        return nullptr;
    }
    ++Entry->DataRevision;
    ++Entry->ContentRevision;
    TrimToBudget(Entry);
    bool bDeferredInitialUpload = false;
    if (!EnsureTexture(Entry, true, &bDeferredInitialUpload))
    {
        return nullptr;
    }
    if (Entry->ContentRevision > 1 || bDeferredInitialUpload)
    {
        UploadQueue->Enqueue(
            Entry,
            FIntRect(0, 0, Descriptor.Size.X, Descriptor.Size.Y),
            false,
            Priority);
    }
    else
    {
        UploadQueue->CaptureSubmittedTicket(Entry);
    }
    return Entry;
}

void FDWCEditorTextureWorkspace::MarkDirty(
    const FDWCEditorTextureHandle& Entry,
    const FIntRect& DirtyRect,
    const bool bWrap,
    const EDWCEditorTextureUploadPriority Priority)
{
    check(IsInGameThread());
    bool bDeferredInitialUpload = false;
    if (!Entry.IsValid() || DirtyRect.IsEmpty() ||
        !EnsureTexture(Entry, true, &bDeferredInitialUpload))
    {
        return;
    }
    ++Entry->DataRevision;
    ++Entry->ContentRevision;
    Entry->LastUsedSerial = ++UseSerial;
    UploadQueue->Enqueue(
        Entry,
        bDeferredInitialUpload ? FIntRect(0, 0, Entry->Descriptor.Size.X, Entry->Descriptor.Size.Y) : DirtyRect,
        bDeferredInitialUpload ? false : bWrap,
        Priority);
}

void FDWCEditorTextureWorkspace::MarkDirty(
    const FDWCEditorTextureLease& Lease,
    const FIntRect& DirtyRect,
    const bool bWrap,
    const EDWCEditorTextureUploadPriority Priority)
{
    MarkDirty(Lease.GetHandle(), DirtyRect, bWrap, Priority);
}

void FDWCEditorTextureWorkspace::MarkPresentationDirty(
    const FDWCEditorTextureLease& Lease,
    const FIntRect& DirtyRect,
    const bool bWrap,
    const EDWCEditorTextureUploadPriority Priority)
{
    check(IsInGameThread());
    const FDWCEditorTextureHandle& Entry = Lease.GetHandle();
    bool bDeferredInitialUpload = false;
    if (!Entry.IsValid() || Entries.FindRef(Entry->Key) != Entry ||
        Entry->GetActiveLeaseCount() == 0 || DirtyRect.IsEmpty() ||
        !EnsureTexture(Entry, true, &bDeferredInitialUpload))
    {
        return;
    }
    ++Entry->ContentRevision;
    Entry->LastUsedSerial = ++UseSerial;
    UploadQueue->Enqueue(
        Entry,
        bDeferredInitialUpload
            ? FIntRect(0, 0, Entry->Descriptor.Size.X, Entry->Descriptor.Size.Y)
            : DirtyRect,
        bDeferredInitialUpload ? false : bWrap,
        Priority);
}

EDWCEditorPreviewRegionCommitResult FDWCEditorTextureWorkspace::ValidateRegionTarget(
    const FDWCEditorTextureLease& Lease,
    const FDWCEditorPreviewRegionTarget& Target,
    FDWCEditorTextureHandle& OutEntry) const
{
    OutEntry = Lease.GetHandle();
    const FDWCEditorTextureHandle RetainedEntry = Entries.FindRef(Target.Key);
    if (!OutEntry.IsValid() || RetainedEntry != OutEntry || OutEntry->GetActiveLeaseCount() == 0)
    {
        return EDWCEditorPreviewRegionCommitResult::WorkspaceEntryMissing;
    }
    if (!(OutEntry->GetDescriptor() == Target.Descriptor))
    {
        return EDWCEditorPreviewRegionCommitResult::DescriptorMismatch;
    }
    if (OutEntry->GetDataRevision() != Target.ExpectedDataRevision)
    {
        return EDWCEditorPreviewRegionCommitResult::DataRevisionMismatch;
    }
    if (OutEntry->GetResourceGeneration() != Target.ExpectedResourceGeneration)
    {
        return EDWCEditorPreviewRegionCommitResult::ResourceGenerationMismatch;
    }
    return EDWCEditorPreviewRegionCommitResult::Applied;
}

FDWCEditorTextureUploadTicket FDWCEditorTextureWorkspace::QueueCommittedRegions(
    const FDWCEditorTextureHandle& Entry,
    const TArray<FIntRect>& DirtyRegions,
    const EDWCEditorTextureUploadPriority Priority,
    const bool bDeferredInitialUpload)
{
    if (bDeferredInitialUpload)
    {
        UploadQueue->Enqueue(
            Entry,
            FIntRect(0, 0, Entry->Descriptor.Size.X, Entry->Descriptor.Size.Y),
            false,
            Priority);
        return UploadQueue->CaptureTicket(Entry);
    }
    for (const FIntRect& DirtyRegion : DirtyRegions)
    {
        UploadQueue->Enqueue(Entry, DirtyRegion, false, Priority);
    }
    return UploadQueue->CaptureTicket(Entry);
}

FDWCEditorPreviewRegionCommitOutcome FDWCEditorTextureWorkspace::CommitBGRA8Regions(
    const FDWCEditorTextureLease& Lease,
    const FDWCEditorPreviewRegionTarget& Target,
    const TArray<FDWCEditorBGRA8RegionPayload>& Regions,
    const EDWCEditorTextureUploadPriority Priority)
{
    check(IsInGameThread());
    ++RegionCommitRequestCount;
    FDWCEditorPreviewRegionCommitOutcome Outcome;
    FDWCEditorTextureHandle Entry;
    Outcome.Result = ValidateRegionTarget(Lease, Target, Entry);
    if (Outcome.Result != EDWCEditorPreviewRegionCommitResult::Applied)
    {
        if (Outcome.Result == EDWCEditorPreviewRegionCommitResult::WorkspaceEntryMissing) ++RegionCommitEntryMissingCount;
        if (Outcome.Result == EDWCEditorPreviewRegionCommitResult::DataRevisionMismatch) ++RegionCommitDataRevisionMismatchCount;
        if (Outcome.Result == EDWCEditorPreviewRegionCommitResult::ResourceGenerationMismatch) ++RegionCommitResourceGenerationMismatchCount;
        if (Outcome.Result == EDWCEditorPreviewRegionCommitResult::DescriptorMismatch) ++RegionCommitDescriptorMismatchCount;
        return Outcome;
    }

    TArray<FIntRect> DirtyRegions;
    FDWCEditorPreviewRegionMemoryEstimate MemoryEstimate;
    if (Target.Descriptor.PixelFormat != PF_B8G8R8A8 || Regions.IsEmpty() ||
        Entry->BGRA8Pixels.Num() != Target.Descriptor.Size.X * Target.Descriptor.Size.Y ||
        !FDWCEditorPreviewRegionMemory::TryEstimateBGRA8(Regions, MemoryEstimate))
    {
        Outcome.Result = EDWCEditorPreviewRegionCommitResult::InvalidPayload;
    }
    for (const FDWCEditorBGRA8RegionPayload& Region : Regions)
    {
        if (Outcome.Result != EDWCEditorPreviewRegionCommitResult::Applied ||
            !IsRegionRectValid(Region.Rect, Target.Descriptor.Size))
        {
            Outcome.Result = EDWCEditorPreviewRegionCommitResult::InvalidPayload;
            break;
        }
        DirtyRegions.Add(Region.Rect);
    }
    if (Outcome.Result == EDWCEditorPreviewRegionCommitResult::Applied && HasOverlappingRects(DirtyRegions))
    {
        Outcome.Result = EDWCEditorPreviewRegionCommitResult::InvalidPayload;
    }
    if (Outcome.Result != EDWCEditorPreviewRegionCommitResult::Applied)
    {
        ++RegionCommitInvalidPayloadCount;
        return Outcome;
    }

    bool bDeferredInitialUpload = false;
    if (!EnsureTexture(Entry, true, &bDeferredInitialUpload))
    {
        Outcome.Result = EDWCEditorPreviewRegionCommitResult::WorkspaceRejected;
        ++RegionCommitWorkspaceRejectedCount;
        return Outcome;
    }
    for (const FDWCEditorBGRA8RegionPayload& Region : Regions)
    {
        CopyRegionRows(Region.Pixels, Region.Rect, Target.Descriptor.Size.X, Entry->BGRA8Pixels);
        Outcome.CommittedPixelCount += static_cast<uint64>(GetRegionPixelCount(Region.Rect));
    }
    Outcome.CommittedBytes = MemoryEstimate.ResultBytes;
    ++Entry->DataRevision;
    ++Entry->ContentRevision;
    Entry->LastUsedSerial = ++UseSerial;
    Outcome.UploadTicket = QueueCommittedRegions(Entry, DirtyRegions, Priority, bDeferredInitialUpload);
    Outcome.Result = EDWCEditorPreviewRegionCommitResult::Applied;
    Outcome.NewDataRevision = Entry->DataRevision;
    Outcome.NewContentRevision = Entry->ContentRevision;
    ++RegionCommitAppliedCount;
    RegionCommitPixelCount += Outcome.CommittedPixelCount;
    RegionCommitBytes += Outcome.CommittedBytes;
    return Outcome;
}

FDWCEditorPreviewRegionCommitOutcome FDWCEditorTextureWorkspace::CommitG8Regions(
    const FDWCEditorTextureLease& Lease,
    const FDWCEditorPreviewRegionTarget& Target,
    const TArray<FDWCEditorG8RegionPayload>& Regions,
    const EDWCEditorTextureUploadPriority Priority)
{
    check(IsInGameThread());
    ++RegionCommitRequestCount;
    FDWCEditorPreviewRegionCommitOutcome Outcome;
    FDWCEditorTextureHandle Entry;
    Outcome.Result = ValidateRegionTarget(Lease, Target, Entry);
    if (Outcome.Result != EDWCEditorPreviewRegionCommitResult::Applied)
    {
        if (Outcome.Result == EDWCEditorPreviewRegionCommitResult::WorkspaceEntryMissing) ++RegionCommitEntryMissingCount;
        if (Outcome.Result == EDWCEditorPreviewRegionCommitResult::DataRevisionMismatch) ++RegionCommitDataRevisionMismatchCount;
        if (Outcome.Result == EDWCEditorPreviewRegionCommitResult::ResourceGenerationMismatch) ++RegionCommitResourceGenerationMismatchCount;
        if (Outcome.Result == EDWCEditorPreviewRegionCommitResult::DescriptorMismatch) ++RegionCommitDescriptorMismatchCount;
        return Outcome;
    }

    TArray<FIntRect> DirtyRegions;
    FDWCEditorPreviewRegionMemoryEstimate MemoryEstimate;
    if (Target.Descriptor.PixelFormat != PF_G8 || Regions.IsEmpty() ||
        Entry->G8Pixels.Num() != Target.Descriptor.Size.X * Target.Descriptor.Size.Y ||
        !FDWCEditorPreviewRegionMemory::TryEstimateG8(Regions, MemoryEstimate))
    {
        Outcome.Result = EDWCEditorPreviewRegionCommitResult::InvalidPayload;
    }
    for (const FDWCEditorG8RegionPayload& Region : Regions)
    {
        if (Outcome.Result != EDWCEditorPreviewRegionCommitResult::Applied ||
            !IsRegionRectValid(Region.Rect, Target.Descriptor.Size))
        {
            Outcome.Result = EDWCEditorPreviewRegionCommitResult::InvalidPayload;
            break;
        }
        DirtyRegions.Add(Region.Rect);
    }
    if (Outcome.Result == EDWCEditorPreviewRegionCommitResult::Applied && HasOverlappingRects(DirtyRegions))
    {
        Outcome.Result = EDWCEditorPreviewRegionCommitResult::InvalidPayload;
    }
    if (Outcome.Result != EDWCEditorPreviewRegionCommitResult::Applied)
    {
        ++RegionCommitInvalidPayloadCount;
        return Outcome;
    }

    bool bDeferredInitialUpload = false;
    if (!EnsureTexture(Entry, true, &bDeferredInitialUpload))
    {
        Outcome.Result = EDWCEditorPreviewRegionCommitResult::WorkspaceRejected;
        ++RegionCommitWorkspaceRejectedCount;
        return Outcome;
    }
    for (const FDWCEditorG8RegionPayload& Region : Regions)
    {
        CopyRegionRows(Region.Pixels, Region.Rect, Target.Descriptor.Size.X, Entry->G8Pixels);
        Outcome.CommittedPixelCount += static_cast<uint64>(GetRegionPixelCount(Region.Rect));
    }
    Outcome.CommittedBytes = MemoryEstimate.ResultBytes;
    ++Entry->DataRevision;
    ++Entry->ContentRevision;
    Entry->LastUsedSerial = ++UseSerial;
    Outcome.UploadTicket = QueueCommittedRegions(Entry, DirtyRegions, Priority, bDeferredInitialUpload);
    Outcome.Result = EDWCEditorPreviewRegionCommitResult::Applied;
    Outcome.NewDataRevision = Entry->DataRevision;
    Outcome.NewContentRevision = Entry->ContentRevision;
    ++RegionCommitAppliedCount;
    RegionCommitPixelCount += Outcome.CommittedPixelCount;
    RegionCommitBytes += Outcome.CommittedBytes;
    return Outcome;
}

FDWCEditorPreviewRegionCommitOutcome FDWCEditorTextureWorkspace::CommitNormalRegions(
    const FDWCEditorTextureLease& Lease,
    const FDWCEditorPreviewRegionTarget& Target,
    const TArray<FDWCEditorNormalRegionPayload>& Regions,
    const EDWCEditorTextureUploadPriority Priority)
{
    return CommitNormalRegionsInternal(Lease, Target, Regions, nullptr, Priority);
}

FDWCEditorPreviewRegionCommitOutcome FDWCEditorTextureWorkspace::CommitInteractiveNormalRegions(
    const FDWCEditorTextureLease& Lease,
    const FDWCEditorPreviewRegionTarget& Target,
    TArray<FDWCEditorNormalRegionPayload>&& Regions)
{
    return CommitNormalRegionsInternal(
        Lease,
        Target,
        Regions,
        &Regions,
        EDWCEditorTextureUploadPriority::Interactive);
}

FDWCEditorPreviewRegionCommitOutcome FDWCEditorTextureWorkspace::CommitNormalRegionsInternal(
    const FDWCEditorTextureLease& Lease,
    const FDWCEditorPreviewRegionTarget& Target,
    const TArray<FDWCEditorNormalRegionPayload>& Regions,
    TArray<FDWCEditorNormalRegionPayload>* OwnedRegions,
    const EDWCEditorTextureUploadPriority Priority)
{
    check(IsInGameThread());
    ++RegionCommitRequestCount;
    FDWCEditorPreviewRegionCommitOutcome Outcome;
    FDWCEditorTextureHandle Entry;
    Outcome.Result = ValidateRegionTarget(Lease, Target, Entry);
    if (Outcome.Result != EDWCEditorPreviewRegionCommitResult::Applied)
    {
        if (Outcome.Result == EDWCEditorPreviewRegionCommitResult::WorkspaceEntryMissing) ++RegionCommitEntryMissingCount;
        if (Outcome.Result == EDWCEditorPreviewRegionCommitResult::DataRevisionMismatch) ++RegionCommitDataRevisionMismatchCount;
        if (Outcome.Result == EDWCEditorPreviewRegionCommitResult::ResourceGenerationMismatch) ++RegionCommitResourceGenerationMismatchCount;
        if (Outcome.Result == EDWCEditorPreviewRegionCommitResult::DescriptorMismatch) ++RegionCommitDescriptorMismatchCount;
        return Outcome;
    }

    FDWCEditorNormalRasterSurface& WorkingSurface = Entry->WorkingNormalSurface;
    TArray<FIntRect> WorkingRects;
    TArray<FIntRect> OutputRects;
    FDWCEditorPreviewRegionMemoryEstimate MemoryEstimate;
    if (Target.Descriptor.PixelFormat != PF_B8G8R8A8 || Regions.IsEmpty() ||
        !WorkingSurface.IsValid() || WorkingSurface.Size != Target.Descriptor.WorkingSize ||
        Entry->BGRA8Pixels.Num() != Target.Descriptor.Size.X * Target.Descriptor.Size.Y ||
        !FDWCEditorPreviewRegionMemory::TryEstimateNormal(Regions, MemoryEstimate))
    {
        Outcome.Result = EDWCEditorPreviewRegionCommitResult::InvalidPayload;
    }
    for (const FDWCEditorNormalRegionPayload& Region : Regions)
    {
        const bool bCoverageMatches = WorkingSurface.HasCoverage()
            ? Region.Coverage.Num() == GetRegionPixelCount(Region.WorkingRect)
            : Region.Coverage.IsEmpty();
        if (Outcome.Result != EDWCEditorPreviewRegionCommitResult::Applied ||
            !IsRegionRectValid(Region.WorkingRect, Target.Descriptor.WorkingSize) ||
            !IsRegionRectValid(Region.OutputRect, Target.Descriptor.Size) ||
            !bCoverageMatches)
        {
            Outcome.Result = EDWCEditorPreviewRegionCommitResult::InvalidPayload;
            break;
        }
        WorkingRects.Add(Region.WorkingRect);
        OutputRects.Add(Region.OutputRect);
    }
    if (Outcome.Result == EDWCEditorPreviewRegionCommitResult::Applied &&
        (HasOverlappingRects(WorkingRects) || HasOverlappingRects(OutputRects)))
    {
        Outcome.Result = EDWCEditorPreviewRegionCommitResult::InvalidPayload;
    }
    if (Outcome.Result != EDWCEditorPreviewRegionCommitResult::Applied)
    {
        ++RegionCommitInvalidPayloadCount;
        return Outcome;
    }

    bool bDeferredInitialUpload = false;
    if (!EnsureTexture(Entry, true, &bDeferredInitialUpload))
    {
        Outcome.Result = EDWCEditorPreviewRegionCommitResult::WorkspaceRejected;
        ++RegionCommitWorkspaceRejectedCount;
        return Outcome;
    }
    for (const FDWCEditorNormalRegionPayload& Region : Regions)
    {
        CopyRegionRows(
            Region.PackedNormalXY,
            Region.WorkingRect,
            Target.Descriptor.WorkingSize.X,
            WorkingSurface.PackedNormalXY);
        if (WorkingSurface.HasCoverage())
        {
            CopyRegionRows(
                Region.Coverage,
                Region.WorkingRect,
                Target.Descriptor.WorkingSize.X,
                WorkingSurface.Coverage);
        }
        CopyRegionRows(
            Region.EncodedPixels,
            Region.OutputRect,
            Target.Descriptor.Size.X,
            Entry->BGRA8Pixels);
        Outcome.CommittedPixelCount += static_cast<uint64>(GetRegionPixelCount(Region.OutputRect));
    }
    Outcome.CommittedBytes = MemoryEstimate.ResultBytes;
    ++Entry->DataRevision;
    ++Entry->ContentRevision;
    Entry->LastUsedSerial = ++UseSerial;
    bool bPreparedUploadQueued = false;
    if (OwnedRegions != nullptr && !bDeferredInitialUpload)
    {
        TArray<FDWCEditorPreparedBGRA8Region> PreparedRegions;
        PreparedRegions.Reserve(OwnedRegions->Num());
        for (FDWCEditorNormalRegionPayload& Region : *OwnedRegions)
        {
            FDWCEditorPreparedBGRA8Region& Prepared = PreparedRegions.AddDefaulted_GetRef();
            Prepared.Rect = Region.OutputRect;
            Prepared.Pixels = MoveTemp(Region.EncodedPixels);
        }
        bPreparedUploadQueued = UploadQueue->EnqueuePreparedBGRA8(
            Entry,
            MoveTemp(PreparedRegions),
            Priority);
    }
    Outcome.UploadTicket = bPreparedUploadQueued
        ? UploadQueue->CaptureTicket(Entry)
        : QueueCommittedRegions(Entry, OutputRects, Priority, bDeferredInitialUpload);
    Outcome.Result = EDWCEditorPreviewRegionCommitResult::Applied;
    Outcome.NewDataRevision = Entry->DataRevision;
    Outcome.NewContentRevision = Entry->ContentRevision;
    ++RegionCommitAppliedCount;
    RegionCommitPixelCount += Outcome.CommittedPixelCount;
    RegionCommitBytes += Outcome.CommittedBytes;
    return Outcome;
}

void FDWCEditorTextureWorkspace::RecreateWithAddressMode(
    const FDWCEditorTextureHandle& Entry,
    const TextureAddress AddressX,
    const TextureAddress AddressY)
{
    check(IsInGameThread());
    if (!Entry.IsValid() ||
        (Entry->Descriptor.AddressX == AddressX && Entry->Descriptor.AddressY == AddressY))
    {
        return;
    }
    UploadQueue->Cancel(Entry->Key);
    Entry->Descriptor.AddressX = AddressX;
    Entry->Descriptor.AddressY = AddressY;
    ++TextureRecreateCount;

    // Keep the UTexture2D object stable. Preview MIDs already point at this
    // object, so replacing it would require every consumer to rebind after the
    // release fence. UpdateResource recreates its RHI resource in place; the
    // full queued upload below restores the current CPU pixels afterwards.
    if (Entry->IsGPUResident() && Entry->Texture != nullptr)
    {
        Entry->Texture->AddressX = AddressX;
        Entry->Texture->AddressY = AddressY;
        Entry->Texture->UpdateResource();
        ++Entry->ResourceGeneration;
        UploadQueue->Enqueue(
            Entry,
            FIntRect(0, 0, Entry->Descriptor.Size.X, Entry->Descriptor.Size.Y),
            false,
            EDWCEditorTextureUploadPriority::Interactive);
        return;
    }

    bool bDeferredInitialUpload = false;
    if (EnsureTexture(Entry, true, &bDeferredInitialUpload) && bDeferredInitialUpload)
    {
        UploadQueue->Enqueue(
            Entry,
            FIntRect(0, 0, Entry->Descriptor.Size.X, Entry->Descriptor.Size.Y),
            false,
            EDWCEditorTextureUploadPriority::Interactive);
    }
}

void FDWCEditorTextureWorkspace::RecreateWithAddressMode(
    const FDWCEditorTextureLease& Lease,
    const TextureAddress AddressX,
    const TextureAddress AddressY)
{
    RecreateWithAddressMode(Lease.GetHandle(), AddressX, AddressY);
}

void FDWCEditorTextureWorkspace::Invalidate(const FDWCEditorTextureKey& Key)
{
    check(IsInGameThread());
    RetireEntry(Key);
}

void FDWCEditorTextureWorkspace::Discard(const FDWCEditorTextureLease& Lease)
{
    check(IsInGameThread());
    if (!Lease.IsValid())
    {
        return;
    }

    // Retire by entry identity rather than by key. A new preview may already
    // have reused the same key while an older lease is being released.
    RetireEntry(Lease.GetHandle());
}

void FDWCEditorTextureWorkspace::InvalidateOwner(const UObject* Owner)
{
    check(IsInGameThread());
    if (Owner == nullptr)
    {
        return;
    }
    const FObjectKey OwnerKey(Owner);
    UploadQueue->CancelOwner(Owner);
    TArray<FDWCEditorTextureKey> KeysToRetire;
    for (const TPair<FDWCEditorTextureKey, FDWCEditorTextureHandle>& Pair : Entries)
    {
        if (Pair.Key.Owner == OwnerKey)
        {
            KeysToRetire.Add(Pair.Key);
        }
    }
    for (const FDWCEditorTextureKey& Key : KeysToRetire)
    {
        RetireEntry(Key);
    }
}

void FDWCEditorTextureWorkspace::Reset()
{
    check(IsInGameThread());
    for (const TPair<FDWCEditorTextureKey, FDWCEditorTextureHandle>& Pair : Entries)
    {
        UploadQueue->Cancel(Pair.Key);
    }
    TArray<FDWCEditorTextureKey> Keys;
    Entries.GetKeys(Keys);
    for (const FDWCEditorTextureKey& Key : Keys)
    {
        RetireEntry(Key);
    }
    ProcessRetiredGPUResources();
}

void FDWCEditorTextureWorkspace::TrimToBudget()
{
    check(IsInGameThread());
    TrimToBudget(FDWCEditorTextureHandle());
}

void FDWCEditorTextureWorkspace::TrimToBudget(const FDWCEditorTextureHandle& ProtectedEntry)
{
    check(IsInGameThread());
    ProcessRetiredGPUResources();

    uint64 UsedGPUBytes = CalculateGPUUsedBytes();
    while (UsedGPUBytes > GPUBudgetBytes)
    {
        const FDWCEditorTextureKey* OldestKey = nullptr;
        uint64 OldestSerial = MAX_uint64;
        for (const TPair<FDWCEditorTextureKey, FDWCEditorTextureHandle>& Pair : Entries)
        {
            if (Pair.Value.IsValid() && Pair.Value != ProtectedEntry &&
                Pair.Value->ActiveLeaseCount == 0 && Pair.Value->IsGPUResident() &&
                Pair.Value->LastUsedSerial < OldestSerial)
            {
                OldestSerial = Pair.Value->LastUsedSerial;
                OldestKey = &Pair.Key;
            }
        }
        if (OldestKey == nullptr)
        {
            break;
        }
        const FDWCEditorTextureHandle Entry = Entries.FindRef(*OldestKey);
        if (!BeginGPUResourceRetire(Entry))
        {
            break;
        }
        UsedGPUBytes = CalculateGPUUsedBytes();
    }

    uint64 UsedCPUBytes = CalculateCPUUsedBytes();
    while (UsedCPUBytes > CPUBudgetBytes)
    {
        const FDWCEditorTextureKey* OldestKey = nullptr;
        uint64 OldestSerial = MAX_uint64;
        for (const TPair<FDWCEditorTextureKey, FDWCEditorTextureHandle>& Pair : Entries)
        {
            if (Pair.Value.IsValid() && Pair.Value != ProtectedEntry &&
                Pair.Value->ActiveLeaseCount == 0 && Pair.Value->LastUsedSerial < OldestSerial)
            {
                OldestSerial = Pair.Value->LastUsedSerial;
                OldestKey = &Pair.Key;
            }
        }
        if (OldestKey == nullptr)
        {
            break;
        }
        RemoveEntry(*OldestKey, true);
        UsedCPUBytes = CalculateCPUUsedBytes();
    }
}

uint64 FDWCEditorTextureWorkspace::GetReclaimableCPUBytes() const
{
    check(IsInGameThread());
    uint64 ReclaimableBytes = 0;
    for (const TPair<FDWCEditorTextureKey, FDWCEditorTextureHandle>& Pair : Entries)
    {
        if (Pair.Value.IsValid() && Pair.Value->ActiveLeaseCount == 0)
        {
            ReclaimableBytes += Pair.Value->GetAllocatedSizeBytes();
        }
    }
    return ReclaimableBytes;
}

uint64 FDWCEditorTextureWorkspace::GetReclaimableCPUBytesForPurposes(
    const TConstArrayView<EDWCEditorTexturePurpose> Purposes) const
{
    check(IsInGameThread());
    if (Purposes.IsEmpty())
    {
        return 0;
    }

    TSet<EDWCEditorTexturePurpose> PurposeSet;
    PurposeSet.Reserve(Purposes.Num());
    for (const EDWCEditorTexturePurpose Purpose : Purposes)
    {
        PurposeSet.Add(Purpose);
    }

    uint64 ReclaimableBytes = 0;
    for (const TPair<FDWCEditorTextureKey, FDWCEditorTextureHandle>& Pair : Entries)
    {
        if (PurposeSet.Contains(Pair.Key.Purpose) && Pair.Value.IsValid() &&
            Pair.Value->ActiveLeaseCount == 0)
        {
            ReclaimableBytes += Pair.Value->GetAllocatedSizeBytes();
        }
    }
    return ReclaimableBytes;
}

uint64 FDWCEditorTextureWorkspace::GetReclaimableGPUBytes() const
{
    check(IsInGameThread());
    uint64 ReclaimableBytes = 0;
    for (const TPair<FDWCEditorTextureKey, FDWCEditorTextureHandle>& Pair : Entries)
    {
        if (Pair.Value.IsValid() && Pair.Value->ActiveLeaseCount == 0 &&
            Pair.Value->IsGPUResident())
        {
            ReclaimableBytes += Pair.Value->GetEstimatedGPUBytes();
        }
    }
    return ReclaimableBytes;
}

uint64 FDWCEditorTextureWorkspace::ReclaimUnleasedCPUBytes(
    const uint64 TargetBytes,
    uint64* const OutRetiringGPUBytes)
{
    check(IsInGameThread());
    if (OutRetiringGPUBytes != nullptr)
    {
        *OutRetiringGPUBytes = 0;
    }

    const uint64 BeforeBytes = CalculateCPUUsedBytes();
    while (BeforeBytes >= CalculateCPUUsedBytes() &&
           BeforeBytes - CalculateCPUUsedBytes() < TargetBytes)
    {
        const FDWCEditorTextureKey* OldestKey = nullptr;
        uint64 OldestSerial = MAX_uint64;
        for (const TPair<FDWCEditorTextureKey, FDWCEditorTextureHandle>& Pair : Entries)
        {
            if (Pair.Value.IsValid() && Pair.Value->ActiveLeaseCount == 0 &&
                Pair.Value->LastUsedSerial < OldestSerial)
            {
                OldestKey = &Pair.Key;
                OldestSerial = Pair.Value->LastUsedSerial;
            }
        }
        if (OldestKey == nullptr)
        {
            break;
        }

        const FDWCEditorTextureHandle Entry = Entries.FindRef(*OldestKey);
        if (OutRetiringGPUBytes != nullptr && Entry.IsValid() && Entry->IsGPUResident())
        {
            *OutRetiringGPUBytes += Entry->GetEstimatedGPUBytes();
        }
        RemoveEntry(*OldestKey, true);
    }
    const uint64 AfterBytes = CalculateCPUUsedBytes();
    return BeforeBytes >= AfterBytes ? BeforeBytes - AfterBytes : 0;
}

uint64 FDWCEditorTextureWorkspace::ReclaimUnleasedCPUBytesForPurposes(
    const TConstArrayView<EDWCEditorTexturePurpose> Purposes,
    const uint64 TargetBytes,
    uint64* const OutRetiringGPUBytes)
{
    check(IsInGameThread());
    if (OutRetiringGPUBytes != nullptr)
    {
        *OutRetiringGPUBytes = 0;
    }
    if (Purposes.IsEmpty() || TargetBytes == 0)
    {
        return 0;
    }

    TSet<EDWCEditorTexturePurpose> PurposeSet;
    PurposeSet.Reserve(Purposes.Num());
    for (const EDWCEditorTexturePurpose Purpose : Purposes)
    {
        PurposeSet.Add(Purpose);
    }

    uint64 ReclaimedBytes = 0;
    while (ReclaimedBytes < TargetBytes)
    {
        const FDWCEditorTextureKey* OldestKey = nullptr;
        FDWCEditorTextureHandle OldestEntry;
        uint64 OldestSerial = MAX_uint64;
        for (const TPair<FDWCEditorTextureKey, FDWCEditorTextureHandle>& Pair : Entries)
        {
            if (PurposeSet.Contains(Pair.Key.Purpose) && Pair.Value.IsValid() &&
                Pair.Value->ActiveLeaseCount == 0 && Pair.Value->LastUsedSerial < OldestSerial)
            {
                OldestKey = &Pair.Key;
                OldestEntry = Pair.Value;
                OldestSerial = Pair.Value->LastUsedSerial;
            }
        }
        if (OldestKey == nullptr || !OldestEntry.IsValid())
        {
            break;
        }

        const uint64 EntryCPUBytes = OldestEntry->GetAllocatedSizeBytes();
        if (OutRetiringGPUBytes != nullptr && OldestEntry->IsGPUResident())
        {
            *OutRetiringGPUBytes += OldestEntry->GetEstimatedGPUBytes();
        }
        RemoveEntry(*OldestKey, true);
        ReclaimedBytes = ReclaimedBytes <= MAX_uint64 - EntryCPUBytes
            ? ReclaimedBytes + EntryCPUBytes
            : MAX_uint64;
    }
    return ReclaimedBytes;
}

uint64 FDWCEditorTextureWorkspace::RetireUnleasedGPUBytes(const uint64 TargetBytes)
{
    check(IsInGameThread());
    ProcessRetiredGPUResources();
    uint64 RetiringBytes = 0;
    while (RetiringBytes < TargetBytes)
    {
        FDWCEditorTextureHandle OldestEntry;
        uint64 OldestSerial = MAX_uint64;
        for (const TPair<FDWCEditorTextureKey, FDWCEditorTextureHandle>& Pair : Entries)
        {
            if (Pair.Value.IsValid() && Pair.Value->ActiveLeaseCount == 0 &&
                Pair.Value->IsGPUResident() && Pair.Value->LastUsedSerial < OldestSerial)
            {
                OldestEntry = Pair.Value;
                OldestSerial = Pair.Value->LastUsedSerial;
            }
        }
        if (!OldestEntry.IsValid())
        {
            break;
        }

        const uint64 EntryBytes = OldestEntry->GetEstimatedGPUBytes();
        if (!BeginGPUResourceRetire(OldestEntry))
        {
            break;
        }
        RetiringBytes += EntryBytes;
    }
    return RetiringBytes;
}

uint64 FDWCEditorTextureWorkspace::RetireUnleasedPurposes(
    const TConstArrayView<EDWCEditorTexturePurpose> Purposes)
{
    check(IsInGameThread());
    ProcessRetiredGPUResources();
    if (Purposes.IsEmpty())
    {
        return 0;
    }

    TSet<EDWCEditorTexturePurpose> PurposeSet;
    PurposeSet.Reserve(Purposes.Num());
    for (const EDWCEditorTexturePurpose Purpose : Purposes)
    {
        PurposeSet.Add(Purpose);
    }

    uint64 RetiringBytes = 0;
    for (const TPair<FDWCEditorTextureKey, FDWCEditorTextureHandle>& Pair : Entries)
    {
        const FDWCEditorTextureHandle& Entry = Pair.Value;
        if (PurposeSet.Contains(Pair.Key.Purpose) && Entry.IsValid() &&
            Entry->ActiveLeaseCount == 0 && Entry->IsGPUResident())
        {
            const uint64 EntryBytes = Entry->GetEstimatedGPUBytes();
            if (BeginGPUResourceRetire(Entry))
            {
                RetiringBytes += EntryBytes;
            }
        }
    }
    return RetiringBytes;
}

bool FDWCEditorTextureWorkspace::HasRetiringGPUResources() const
{
    check(IsInGameThread());
    for (const TPair<FDWCEditorTextureKey, FDWCEditorTextureHandle>& Pair : Entries)
    {
        if (Pair.Value.IsValid() && Pair.Value->IsGPURetiring())
        {
            return true;
        }
    }
    for (const FDWCEditorTextureHandle& Entry : RetiredEntries)
    {
        if (Entry.IsValid() && Entry->IsGPURetiring())
        {
            return true;
        }
    }
    return false;
}

void FDWCEditorTextureWorkspace::SetMaintenanceRequiredCallback(
    FMaintenanceRequiredCallback Callback)
{
    check(IsInGameThread());
    MaintenanceRequiredCallback = MoveTemp(Callback);
    if (HasRetiringGPUResources())
    {
        NotifyMaintenanceRequired();
    }
}

void FDWCEditorTextureWorkspace::GetGPUResidencySnapshot(
    TArray<FDWCEditorTextureGPUResidencyRecord>& OutRecords) const
{
    check(IsInGameThread());
    OutRecords.Reset();
    OutRecords.Reserve(Entries.Num() + RetiredEntries.Num());
    const auto AddRecord = [&OutRecords](const FDWCEditorTextureHandle& Entry)
    {
        if (!Entry.IsValid())
        {
            return;
        }
        FDWCEditorTextureGPUResidencyRecord& Record = OutRecords.AddDefaulted_GetRef();
        Record.Key = Entry->Key;
        Record.Size = Entry->Descriptor.Size;
        Record.PixelFormat = Entry->Descriptor.PixelFormat;
        Record.State = Entry->GPUState;
        Record.ResourceGeneration = Entry->ResourceGeneration;
        Record.EstimatedGPUBytes = Entry->GetEstimatedGPUBytes();
        Record.ActiveLeaseCount = Entry->ActiveLeaseCount;
    };
    for (const TPair<FDWCEditorTextureKey, FDWCEditorTextureHandle>& Pair : Entries)
    {
        AddRecord(Pair.Value);
    }
    for (const FDWCEditorTextureHandle& Entry : RetiredEntries)
    {
        AddRecord(Entry);
    }
}

bool FDWCEditorTextureWorkspace::EnsureTexture(
    const FDWCEditorTextureHandle& Entry,
    const bool bDeferLargeInitialUpload,
    bool* const bOutDeferredInitialUpload)
{
    check(IsInGameThread());
    if (bOutDeferredInitialUpload != nullptr)
    {
        *bOutDeferredInitialUpload = false;
    }
    if (!Entry.IsValid() || !Entry->Descriptor.IsValid())
    {
        return false;
    }
    if (Entry->IsGPUResident())
    {
        return true;
    }
    if (Entry->IsGPURetiring())
    {
        return false;
    }

    TrimToBudget(Entry);
    const uint64 NewResourceBytes = static_cast<uint64>(Entry->Descriptor.Size.X) *
        static_cast<uint64>(Entry->Descriptor.Size.Y) *
        static_cast<uint64>(Entry->Descriptor.GetBytesPerPixel());
    if (CalculateGPUUsedBytes() + NewResourceBytes > GPUBudgetBytes)
    {
        ++GPUBudgetRejectCount;
        return false;
    }
    if (!AcquireEntryGPUReservation(Entry, NewResourceBytes))
    {
        ++GPUBudgetRejectCount;
        return false;
    }

    Entry->Texture = UTexture2D::CreateTransient(
        Entry->Descriptor.Size.X,
        Entry->Descriptor.Size.Y,
        Entry->Descriptor.PixelFormat);
    if (Entry->Texture == nullptr || Entry->Texture->GetPlatformData() == nullptr ||
        !Entry->Texture->GetPlatformData()->Mips.IsValidIndex(0))
    {
        Entry->Texture = nullptr;
        Entry->GPUResourceLease.Reset();
        return false;
    }

    UTexture2D* Texture = Entry->Texture;
    Texture->SRGB = Entry->Descriptor.bSRGB;
    Texture->CompressionSettings = Entry->Descriptor.CompressionSettings;
    Texture->MipGenSettings = Entry->Descriptor.MipGenSettings;
    Texture->Filter = Entry->Descriptor.Filter;
    Texture->AddressX = Entry->Descriptor.AddressX;
    Texture->AddressY = Entry->Descriptor.AddressY;
    Texture->LODGroup = Entry->Descriptor.LODGroup;
    Texture->NeverStream = true;

    FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
    void* MipData = Mip.BulkData.Lock(LOCK_READ_WRITE);
    const int64 ExpectedBytes = static_cast<int64>(Entry->Descriptor.Size.X) *
        Entry->Descriptor.Size.Y * Entry->Descriptor.GetBytesPerPixel();
    if (MipData == nullptr || Entry->GetPixelDataBytes() != ExpectedBytes)
    {
        Mip.BulkData.Unlock();
        Entry->Texture = nullptr;
        Entry->GPUResourceLease.Reset();
        return false;
    }
    const bool bShouldDeferUpload = bDeferLargeInitialUpload &&
        ExpectedBytes > static_cast<int64>(FDWCEditorRenderUploadQueue::DefaultPerFlushBudgetBytes);
    if (bShouldDeferUpload)
    {
        // Resource creation must happen on the game thread, but copying a 4K
        // CPU image into its initial mip does not. Initialize a descriptor-
        // appropriate neutral resource and let the budgeted upload queue fill
        // it in rows. A zeroed normal map is not a flat tangent normal.
        if (Entry->Descriptor.PixelFormat == PF_B8G8R8A8)
        {
            FColor* InitialPixels = static_cast<FColor*>(MipData);
            const int64 PixelCount = static_cast<int64>(Entry->Descriptor.Size.X) * Entry->Descriptor.Size.Y;
            for (int64 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
            {
                InitialPixels[PixelIndex] = Entry->Descriptor.InitialBGRA8;
            }
        }
        else if (Entry->Descriptor.PixelFormat == PF_R32_FLOAT)
        {
            float* InitialPixels = static_cast<float*>(MipData);
            const int64 PixelCount = static_cast<int64>(Entry->Descriptor.Size.X) * Entry->Descriptor.Size.Y;
            for (int64 PixelIndex = 0; PixelIndex < PixelCount; ++PixelIndex)
            {
                InitialPixels[PixelIndex] = Entry->Descriptor.InitialR32F;
            }
        }
        else
        {
            FMemory::Memset(MipData, Entry->Descriptor.InitialG8, ExpectedBytes);
        }
        if (bOutDeferredInitialUpload != nullptr)
        {
            *bOutDeferredInitialUpload = true;
        }
    }
    else
    {
        FMemory::Memcpy(MipData, Entry->GetPixelData(), ExpectedBytes);
    }
    Mip.BulkData.Unlock();
    Texture->UpdateResource();
    Entry->GPUState = EDWCEditorTextureGPUState::Resident;
    ++Entry->ResourceGeneration;
    ++TextureCreateCount;
    UpdateGPUHighWaterMark();
    return true;
}

void FDWCEditorTextureWorkspace::InitializeBuffers(const FDWCEditorTextureHandle& Entry)
{
    Entry->WorkingNormalSurface = FDWCEditorNormalRasterSurface();
    const int32 PixelCount = Entry->Descriptor.Size.X * Entry->Descriptor.Size.Y;
    if (Entry->Descriptor.PixelFormat == PF_G8)
    {
        Entry->G8Pixels.Init(Entry->Descriptor.InitialG8, PixelCount);
    }
    else if (Entry->Descriptor.PixelFormat == PF_R32_FLOAT)
    {
        Entry->R32FPixels.Init(Entry->Descriptor.InitialR32F, PixelCount);
    }
    else
    {
        Entry->BGRA8Pixels.Init(Entry->Descriptor.InitialBGRA8, PixelCount);
    }
}

bool FDWCEditorTextureWorkspace::SyncEntryCPUReservation(const FDWCEditorTextureHandle& Entry)
{
    if (!Entry.IsValid())
    {
        return false;
    }
    const uint64 Bytes = Entry->GetAllocatedSizeBytes();
    if (!ResourceGovernor.IsValid())
    {
        return true;
    }
    if (Bytes == 0)
    {
        Entry->CPUResourceLease.Reset();
        return true;
    }
    if (Entry->CPUResourceLease.IsValid())
    {
        return Entry->CPUResourceLease.TryResize(Bytes);
    }

    FDWCEditorResourceReservationRequest Request;
    Request.Pool = EDWCEditorResourcePool::PreviewWorkspaceCPU;
    Request.Bytes = Bytes;
    Request.Owner = CPUMemoryOwner;
    Request.DebugName = TEXT("Preview texture CPU storage");
    Entry->CPUResourceLease = ResourceGovernor->TryAcquire(Request);
    return Entry->CPUResourceLease.IsValid();
}

bool FDWCEditorTextureWorkspace::AcquireEntryGPUReservation(
    const FDWCEditorTextureHandle& Entry,
    const uint64 Bytes)
{
    if (!Entry.IsValid() || Bytes == 0)
    {
        return false;
    }
    if (!ResourceGovernor.IsValid())
    {
        return true;
    }
    if (Entry->GPUResourceLease.IsValid())
    {
        return Entry->GPUResourceLease.TryResize(Bytes);
    }

    FDWCEditorResourceReservationRequest Request;
    Request.Pool = EDWCEditorResourcePool::PreviewGPU;
    Request.Bytes = Bytes;
    Request.Owner = GPUMemoryOwner;
    Request.DebugName = TEXT("Preview texture GPU resource");
    Entry->GPUResourceLease = ResourceGovernor->TryAcquire(Request);
    return Entry->GPUResourceLease.IsValid();
}

void FDWCEditorTextureWorkspace::RemoveEntry(
    const FDWCEditorTextureKey& Key,
    const bool bCountEviction)
{
    const FDWCEditorTextureHandle Entry = Entries.FindRef(Key);
    if (!Entry.IsValid())
    {
        return;
    }

    RetireEntry(Entry);
    if (bCountEviction)
    {
        ++EvictionCount;
    }
}

void FDWCEditorTextureWorkspace::RetireEntry(const FDWCEditorTextureKey& Key)
{
    // Take a strong local copy before RetireEntry(handle) erases the map key.
    // Passing the TMap value by reference here would leave that reference
    // dangling as soon as Entries.Remove() destroys the map element.
    const FDWCEditorTextureHandle Entry = Entries.FindRef(Key);
    if (!Entry.IsValid())
    {
        return;
    }

    RetireEntry(Entry);
}

void FDWCEditorTextureWorkspace::RetireEntry(const FDWCEditorTextureHandle& Entry)
{
    if (!Entry.IsValid() || Entry->bRetired)
    {
        return;
    }

    const FDWCEditorTextureHandle* CurrentEntry = Entries.Find(Entry->Key);
    if (CurrentEntry != nullptr && *CurrentEntry == Entry)
    {
        UploadQueue->Cancel(Entry->Key);
        Entries.Remove(Entry->Key);
    }

    Entry->bRetired = true;
    if (Entry->ActiveLeaseCount == 0)
    {
        ReleaseEntryCPUStorage(Entry);
    }
    BeginGPUResourceRetire(Entry);
    if (Entry->ActiveLeaseCount > 0 || Entry->IsGPURetiring())
    {
        RetiredEntries.AddUnique(Entry);
    }
}

bool FDWCEditorTextureWorkspace::BeginGPUResourceRetire(const FDWCEditorTextureHandle& Entry)
{
    check(IsInGameThread());
    if (!Entry.IsValid() || Entry->IsGPURetiring())
    {
        return false;
    }
    if (!Entry->IsGPUResident() || Entry->Texture == nullptr)
    {
        Entry->GPUState = EDWCEditorTextureGPUState::CPUOnly;
        Entry->GPUResourceLease.Reset();
        return false;
    }

    UploadQueue->Cancel(Entry->Key);
    Entry->GPUState = EDWCEditorTextureGPUState::Retiring;
    ++GPUResourceRetireCount;
    TryIssueGPUResourceRelease(Entry);
    NotifyMaintenanceRequired();
    return true;
}

bool FDWCEditorTextureWorkspace::TryIssueGPUResourceRelease(
    const FDWCEditorTextureHandle& Entry)
{
    check(IsInGameThread());
    if (!Entry.IsValid() || !Entry->IsGPURetiring() || Entry->GPUReleaseFence.IsValid() ||
        Entry->HasInFlightRenderUploads() || Entry->Texture == nullptr)
    {
        return false;
    }

    Entry->GPUReleaseFence = MakeUnique<FRenderCommandFence>();
    Entry->Texture->ReleaseResource();
    Entry->GPUReleaseFence->BeginFence();
    return true;
}

void FDWCEditorTextureWorkspace::NotifyMaintenanceRequired()
{
    if (MaintenanceRequiredCallback)
    {
        MaintenanceRequiredCallback();
    }
}

void FDWCEditorTextureWorkspace::ReleaseEntryCPUStorage(const FDWCEditorTextureHandle& Entry)
{
    if (!Entry.IsValid())
    {
        return;
    }

    Entry->BGRA8Pixels.Empty();
    Entry->G8Pixels.Empty();
    Entry->R32FPixels.Empty();
    Entry->WorkingNormalSurface = FDWCEditorNormalRasterSurface();
    Entry->CPUResourceLease.Reset();
}

void FDWCEditorTextureWorkspace::ProcessRetiredGPUResources()
{
    check(IsInGameThread());
    const auto ProcessEntry = [this](const FDWCEditorTextureHandle& Entry)
    {
        if (!Entry.IsValid() || !Entry->IsGPURetiring())
        {
            return;
        }
        if (!Entry->GPUReleaseFence.IsValid())
        {
            TryIssueGPUResourceRelease(Entry);
        }
        if (!Entry->GPUReleaseFence.IsValid() || !Entry->GPUReleaseFence->IsFenceComplete())
        {
            return;
        }

        Entry->Texture = nullptr;
        Entry->GPUReleaseFence.Reset();
        Entry->GPUState = EDWCEditorTextureGPUState::CPUOnly;
        Entry->GPUResourceLease.Reset();
        ++GPUResourceReleaseCompleteCount;
    };

    for (const TPair<FDWCEditorTextureKey, FDWCEditorTextureHandle>& Pair : Entries)
    {
        ProcessEntry(Pair.Value);
    }
    for (const FDWCEditorTextureHandle& Entry : RetiredEntries)
    {
        ProcessEntry(Entry);
    }
    RemoveCompletedRetiredEntries();
}

void FDWCEditorTextureWorkspace::RemoveCompletedRetiredEntries()
{
    RetiredEntries.RemoveAllSwap(
        [](const FDWCEditorTextureHandle& Entry)
        {
            return !Entry.IsValid() ||
                (Entry->bRetired && Entry->ActiveLeaseCount == 0 && !Entry->IsGPURetiring());
        },
        EAllowShrinking::No);
}

void FDWCEditorTextureWorkspace::ReleaseTextureLease(
    const FDWCEditorTextureHandle& Entry,
    const uint64 LeaseId)
{
    check(IsInGameThread());
    (void)LeaseId;
    if (!Entry.IsValid() || Entry->ActiveLeaseCount == 0)
    {
        return;
    }

    --Entry->ActiveLeaseCount;
    if (Entry->ActiveLeaseCount == 0 && Entry->bRetired)
    {
        ReleaseEntryCPUStorage(Entry);
        RemoveCompletedRetiredEntries();
    }
    TrimToBudget();
}

uint64 FDWCEditorTextureWorkspace::CalculateCPUUsedBytes() const
{
    uint64 UsedBytes = 0;
    for (const TPair<FDWCEditorTextureKey, FDWCEditorTextureHandle>& Pair : Entries)
    {
        if (Pair.Value.IsValid())
        {
            UsedBytes += Pair.Value->GetAllocatedSizeBytes();
        }
    }
    for (const FDWCEditorTextureHandle& Entry : RetiredEntries)
    {
        if (Entry.IsValid())
        {
            UsedBytes += Entry->GetAllocatedSizeBytes();
        }
    }
    return UsedBytes;
}

uint64 FDWCEditorTextureWorkspace::CalculateGPUUsedBytes() const
{
    uint64 UsedBytes = 0;
    for (const TPair<FDWCEditorTextureKey, FDWCEditorTextureHandle>& Pair : Entries)
    {
        if (Pair.Value.IsValid())
        {
            UsedBytes += Pair.Value->GetEstimatedGPUBytes();
        }
    }
    for (const FDWCEditorTextureHandle& Entry : RetiredEntries)
    {
        if (Entry.IsValid())
        {
            UsedBytes += Entry->GetEstimatedGPUBytes();
        }
    }
    return UsedBytes;
}

FDWCEditorTextureWorkspace::FGPUResidencyStats FDWCEditorTextureWorkspace::CollectGPUResidencyStats() const
{
    FGPUResidencyStats Stats;
    const auto AccumulateEntry = [&Stats](const FDWCEditorTextureHandle& Entry)
    {
        if (!Entry.IsValid())
        {
            return;
        }

        switch (Entry->GetGPUState())
        {
        case EDWCEditorTextureGPUState::Resident:
            ++Stats.ResidentEntryCount;
            Stats.ResidentBytes += Entry->GetEstimatedGPUBytes();
            Stats.ResidentLeaseCount += static_cast<int32>(Entry->GetActiveLeaseCount());
            break;
        case EDWCEditorTextureGPUState::Retiring:
            ++Stats.RetiringEntryCount;
            Stats.RetiringBytes += Entry->GetEstimatedGPUBytes();
            Stats.RetiringLeaseCount += static_cast<int32>(Entry->GetActiveLeaseCount());
            break;
        case EDWCEditorTextureGPUState::CPUOnly:
        default:
            ++Stats.CPUOnlyEntryCount;
            break;
        }
    };

    for (const TPair<FDWCEditorTextureKey, FDWCEditorTextureHandle>& Pair : Entries)
    {
        AccumulateEntry(Pair.Value);
    }
    for (const FDWCEditorTextureHandle& Entry : RetiredEntries)
    {
        AccumulateEntry(Entry);
    }
    return Stats;
}

void FDWCEditorTextureWorkspace::UpdateGPUHighWaterMark()
{
    GPUHighWaterBytes = FMath::Max(GPUHighWaterBytes, CalculateGPUUsedBytes());
}

void FDWCEditorTextureWorkspace::AppendDiagnosticMemoryBucket(
    TArray<FDWCEditorPreviewMemoryBucket>& OutBuckets) const
{
    auto AddBucket = [this, &OutBuckets](
                         const FString& Name,
                         const uint64 UsedBytes,
                         const uint64 BudgetBytes,
                         const int32 EntryCount,
                         const int32 ActiveLeaseCount,
                         const int32 RetiredEntryCount,
                         const EDWCEditorMemoryCategory GlobalCategory = EDWCEditorMemoryCategory::PersistentEditorCPU,
                         const bool bIncludeInGlobalSnapshot = false,
                         const TCHAR* GlobalOwnerSuffix = nullptr)
    {
        FDWCEditorPreviewMemoryBucket& Bucket = OutBuckets.AddDefaulted_GetRef();
        Bucket.Name = Name;
        Bucket.UsedBytes = UsedBytes;
        Bucket.BudgetBytes = BudgetBytes;
        Bucket.EntryCount = EntryCount;
        Bucket.ActiveLeaseCount = ActiveLeaseCount;
        Bucket.RetiredEntryCount = RetiredEntryCount;
        Bucket.HitCount = AcquireHitCount;
        Bucket.MissCount = AcquireMissCount;
        Bucket.EvictionCount = EvictionCount;
        Bucket.GlobalCategory = GlobalCategory;
        Bucket.bIncludeInGlobalSnapshot = bIncludeInGlobalSnapshot;
        if (GlobalOwnerSuffix != nullptr)
        {
            Bucket.GlobalOwnerIdentifier = FString::Printf(
                TEXT("TextureWorkspace/%p/%s"),
                static_cast<const void*>(this),
                GlobalOwnerSuffix);
        }
    };

    const int32 TotalEntryCount = Entries.Num() + RetiredEntries.Num();
    int32 TotalLeaseCount = 0;
    for (const TPair<FDWCEditorTextureKey, FDWCEditorTextureHandle>& Pair : Entries)
    {
        if (Pair.Value.IsValid())
        {
            TotalLeaseCount += static_cast<int32>(Pair.Value->GetActiveLeaseCount());
        }
    }
    for (const FDWCEditorTextureHandle& Entry : RetiredEntries)
    {
        if (Entry.IsValid())
        {
            TotalLeaseCount += static_cast<int32>(Entry->GetActiveLeaseCount());
        }
    }

    const FGPUResidencyStats GPUStats = CollectGPUResidencyStats();
    const uint64 CPUUsedBytes = CalculateCPUUsedBytes();
    const uint64 GPUUsedBytes = GPUStats.ResidentBytes + GPUStats.RetiringBytes;

    // Keep aggregate buckets for existing diagnostics consumers, then add the
    // GPU lifecycle split used to investigate VRAM pressure after PIE.
    AddBucket(
        TEXT("Editor texture workspace"),
        CPUUsedBytes + GPUUsedBytes,
        CPUBudgetBytes + GPUBudgetBytes,
        TotalEntryCount,
        TotalLeaseCount,
        RetiredEntries.Num());
    AddBucket(
        TEXT("Editor texture workspace CPU"),
        CPUUsedBytes,
        CPUBudgetBytes,
        TotalEntryCount,
        TotalLeaseCount,
        RetiredEntries.Num(),
        EDWCEditorMemoryCategory::PersistentEditorCPU,
        true,
        TEXT("CPU"));
    AddBucket(
        TEXT("Editor texture workspace GPU"),
        GPUUsedBytes,
        GPUBudgetBytes,
        TotalEntryCount,
        TotalLeaseCount,
        GPUStats.RetiringEntryCount,
        EDWCEditorMemoryCategory::PreviewGPU,
        true,
        TEXT("GPU"));
    AddBucket(
        TEXT("Preview GPU resident resources"),
        GPUStats.ResidentBytes,
        GPUBudgetBytes,
        GPUStats.ResidentEntryCount,
        GPUStats.ResidentLeaseCount,
        0);
    AddBucket(
        TEXT("Preview GPU retiring resources"),
        GPUStats.RetiringBytes,
        GPUBudgetBytes,
        GPUStats.RetiringEntryCount,
        GPUStats.RetiringLeaseCount,
        GPUStats.RetiringEntryCount);
    AddBucket(
        TEXT("Preview GPU CPU-only workspace entries"),
        0,
        0,
        GPUStats.CPUOnlyEntryCount,
        0,
        0);
    AddBucket(TEXT("Preview GPU residency high-water"), GPUHighWaterBytes, GPUBudgetBytes, 0, 0, 0);
}

void FDWCEditorTextureWorkspace::AppendDiagnosticOperationCounters(
    TArray<FDWCEditorPreviewOperationCounter>& OutCounters) const
{
    FDWCEditorPreviewOperationCounter& Creates = OutCounters.AddDefaulted_GetRef();
    Creates.Name = TEXT("Transient preview texture creates");
    Creates.Count = TextureCreateCount;

    FDWCEditorPreviewOperationCounter& Recreates = OutCounters.AddDefaulted_GetRef();
    Recreates.Name = TEXT("Transient preview texture recreates");
    Recreates.Count = TextureRecreateCount;

    FDWCEditorPreviewOperationCounter& Retires = OutCounters.AddDefaulted_GetRef();
    Retires.Name = TEXT("Transient GPU resource retires");
    Retires.Count = GPUResourceRetireCount;

    FDWCEditorPreviewOperationCounter& RetireCompletions = OutCounters.AddDefaulted_GetRef();
    RetireCompletions.Name = TEXT("Transient GPU resource retire completions");
    RetireCompletions.Count = GPUResourceReleaseCompleteCount;

    FDWCEditorPreviewOperationCounter& GPURejects = OutCounters.AddDefaulted_GetRef();
    GPURejects.Name = TEXT("Preview GPU budget rejects");
    GPURejects.Count = GPUBudgetRejectCount;

    const FGPUResidencyStats GPUStats = CollectGPUResidencyStats();
    FDWCEditorPreviewOperationCounter& ResidentEntries = OutCounters.AddDefaulted_GetRef();
    ResidentEntries.Name = TEXT("Preview GPU resident entries");
    ResidentEntries.Count = GPUStats.ResidentEntryCount;
    ResidentEntries.Bytes = GPUStats.ResidentBytes;

    FDWCEditorPreviewOperationCounter& RetiringEntries = OutCounters.AddDefaulted_GetRef();
    RetiringEntries.Name = TEXT("Preview GPU retiring entries");
    RetiringEntries.Count = GPUStats.RetiringEntryCount;
    RetiringEntries.Bytes = GPUStats.RetiringBytes;

    FDWCEditorPreviewOperationCounter& HighWater = OutCounters.AddDefaulted_GetRef();
    HighWater.Name = TEXT("Preview GPU residency high-water");
    HighWater.Count = 1;
    HighWater.Bytes = GPUHighWaterBytes;

    FDWCEditorPreviewOperationCounter& RegionRequests = OutCounters.AddDefaulted_GetRef();
    RegionRequests.Name = TEXT("Preview region commit requests");
    RegionRequests.Count = RegionCommitRequestCount;

    FDWCEditorPreviewOperationCounter& RegionApplied = OutCounters.AddDefaulted_GetRef();
    RegionApplied.Name = TEXT("Applied preview region commits");
    RegionApplied.Count = RegionCommitAppliedCount;
    RegionApplied.Bytes = RegionCommitBytes;

    FDWCEditorPreviewOperationCounter& RegionPixels = OutCounters.AddDefaulted_GetRef();
    RegionPixels.Name = TEXT("Preview region committed pixels");
    RegionPixels.Count = RegionCommitPixelCount;

    FDWCEditorPreviewOperationCounter& InvalidRegionPayloads = OutCounters.AddDefaulted_GetRef();
    InvalidRegionPayloads.Name = TEXT("Rejected preview region invalid payloads");
    InvalidRegionPayloads.Count = RegionCommitInvalidPayloadCount;

    FDWCEditorPreviewOperationCounter& RegionDataMismatches = OutCounters.AddDefaulted_GetRef();
    RegionDataMismatches.Name = TEXT("Rejected preview region data revisions");
    RegionDataMismatches.Count = RegionCommitDataRevisionMismatchCount;

    FDWCEditorPreviewOperationCounter& RegionResourceMismatches = OutCounters.AddDefaulted_GetRef();
    RegionResourceMismatches.Name = TEXT("Rejected preview region resource generations");
    RegionResourceMismatches.Count = RegionCommitResourceGenerationMismatchCount;

    FDWCEditorPreviewOperationCounter& RegionDescriptorMismatches = OutCounters.AddDefaulted_GetRef();
    RegionDescriptorMismatches.Name = TEXT("Rejected preview region descriptors");
    RegionDescriptorMismatches.Count = RegionCommitDescriptorMismatchCount;

    FDWCEditorPreviewOperationCounter& MissingRegionEntries = OutCounters.AddDefaulted_GetRef();
    MissingRegionEntries.Name = TEXT("Rejected preview region missing entries");
    MissingRegionEntries.Count = RegionCommitEntryMissingCount;

    FDWCEditorPreviewOperationCounter& WorkspaceRejectedRegions = OutCounters.AddDefaulted_GetRef();
    WorkspaceRejectedRegions.Name = TEXT("Rejected preview regions by workspace");
    WorkspaceRejectedRegions.Count = RegionCommitWorkspaceRejectedCount;
}

void FDWCEditorTextureWorkspace::ResetDiagnosticCounters()
{
    AcquireHitCount = 0;
    AcquireMissCount = 0;
    EvictionCount = 0;
    TextureCreateCount = 0;
    TextureRecreateCount = 0;
    GPUResourceRetireCount = 0;
    GPUResourceReleaseCompleteCount = 0;
    GPUBudgetRejectCount = 0;
    GPUHighWaterBytes = CalculateGPUUsedBytes();
    RegionCommitRequestCount = 0;
    RegionCommitAppliedCount = 0;
    RegionCommitPixelCount = 0;
    RegionCommitBytes = 0;
    RegionCommitInvalidPayloadCount = 0;
    RegionCommitDataRevisionMismatchCount = 0;
    RegionCommitResourceGenerationMismatchCount = 0;
    RegionCommitDescriptorMismatchCount = 0;
    RegionCommitEntryMissingCount = 0;
    RegionCommitWorkspaceRejectedCount = 0;
}

void FDWCEditorTextureWorkspace::AddReferencedObjects(FReferenceCollector& Collector)
{
    for (TPair<FDWCEditorTextureKey, FDWCEditorTextureHandle>& Pair : Entries)
    {
        if (Pair.Value.IsValid())
        {
            Collector.AddReferencedObject(Pair.Value->Texture);
        }
    }
    for (FDWCEditorTextureHandle& Entry : RetiredEntries)
    {
        if (Entry.IsValid())
        {
            Collector.AddReferencedObject(Entry->Texture);
        }
    }
}
