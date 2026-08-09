//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Foundation/TextureWorkspace/DWCEditorRenderUploadQueue.h"

#include "Engine/Texture2D.h"
#include "Misc/ScopeLock.h"
#include "Rendering/Texture2DResource.h"
#include "WetClothing/Foundation/Preview/Diagnostics/DWCEditorPreviewDiagnostics.h"

class FDWCEditorTextureUploadTelemetryState final
{
  public:
    FDWCEditorTextureUploadTelemetryState()
    {
        Timing.QueuedSeconds = FPlatformTime::Seconds();
    }

    void RecordRequest(
        const FIntRect& Region,
        const FIntPoint& TextureSize,
        const EDWCEditorTextureUploadPriority Priority,
        const bool bCoalesced)
    {
        FScopeLock Lock(&Mutex);
        ++Timing.RequestedRegionCount;
        Timing.RequestedPixels += static_cast<uint64>(Region.Width()) * Region.Height();
        Timing.Priority = static_cast<uint8>(Priority) > static_cast<uint8>(Timing.Priority)
            ? Priority
            : Timing.Priority;
        Timing.CoalescedRequestCount += bCoalesced ? 1u : 0u;
        Timing.bFullTextureUpload |= Region.Min == FIntPoint::ZeroValue && Region.Max == TextureSize;
    }

    void MarkFullTextureUpload()
    {
        FScopeLock Lock(&Mutex);
        Timing.bFullTextureUpload = true;
    }

    void MarkSelected(const uint32 QueueDepth)
    {
        FScopeLock Lock(&Mutex);
        if (Timing.SelectedSeconds <= 0.0)
        {
            Timing.SelectedSeconds = FPlatformTime::Seconds();
            Timing.QueueDepthAtSelection = QueueDepth;
        }
    }

    void AddStagingCopy(const double Milliseconds)
    {
        FScopeLock Lock(&Mutex);
        Timing.StagingCopyMs += Milliseconds;
    }

    void RecordPreparedPayload(const uint64 PayloadBytes)
    {
        FScopeLock Lock(&Mutex);
        Timing.PreparedPayloadBytes = PayloadBytes;
        Timing.bUsedPreparedPayload = true;
    }

    void RecordRegionScheduled(const uint64 UploadBytes)
    {
        FScopeLock Lock(&Mutex);
        ++Timing.SubmittedRegionCount;
        Timing.SubmittedBytes += UploadBytes;
    }

    void RecordPreparedRegionScheduled(const uint64 UploadBytes)
    {
        FScopeLock Lock(&Mutex);
        ++Timing.SubmittedRegionCount;
        Timing.SubmittedBytes += UploadBytes;
        Timing.AvoidedStagingCopyBytes += UploadBytes;
    }

    void AddSubmitCall(const double Milliseconds)
    {
        FScopeLock Lock(&Mutex);
        Timing.SubmitCallMs += Milliseconds;
    }

    void RecordRenderCallback()
    {
        FScopeLock Lock(&Mutex);
        ++Timing.CompletedRegionCount;
        if (Timing.CompletedRegionCount >= Timing.SubmittedRegionCount)
        {
            Timing.RenderCallbackSeconds = FPlatformTime::Seconds();
        }
    }

    void MarkSubmitted()
    {
        FScopeLock Lock(&Mutex);
        if (Timing.SubmittedSeconds <= 0.0)
        {
            Timing.SubmittedSeconds = FPlatformTime::Seconds();
        }
    }

    void MarkObserved()
    {
        FScopeLock Lock(&Mutex);
        if (Timing.ObservedSeconds <= 0.0)
        {
            Timing.ObservedSeconds = FPlatformTime::Seconds();
        }
    }

    void MarkStale(const bool bCanceled)
    {
        FScopeLock Lock(&Mutex);
        Timing.bStale = true;
        Timing.bCanceled |= bCanceled;
    }

    FDWCEditorTextureUploadTiming Snapshot() const
    {
        FScopeLock Lock(&Mutex);
        FDWCEditorTextureUploadTiming Result = Timing;
        Result.QueueWaitMs = Result.QueuedSeconds > 0.0 && Result.SelectedSeconds >= Result.QueuedSeconds
            ? (Result.SelectedSeconds - Result.QueuedSeconds) * 1000.0
            : 0.0;
        const double SelectedToSubmittedMs = Result.SelectedSeconds > 0.0 &&
                Result.SubmittedSeconds >= Result.SelectedSeconds
            ? (Result.SubmittedSeconds - Result.SelectedSeconds) * 1000.0
            : 0.0;
        Result.SliceDelayMs = FMath::Max(
            SelectedToSubmittedMs - Result.StagingCopyMs - Result.SubmitCallMs,
            0.0);
        Result.SubmittedToObservedMs = Result.SubmittedSeconds > 0.0 &&
                Result.ObservedSeconds >= Result.SubmittedSeconds
            ? (Result.ObservedSeconds - Result.SubmittedSeconds) * 1000.0
            : 0.0;
        Result.RenderCallbackLatencyMs = Result.SubmittedSeconds > 0.0 &&
                Result.RenderCallbackSeconds >= Result.SubmittedSeconds
            ? (Result.RenderCallbackSeconds - Result.SubmittedSeconds) * 1000.0
            : 0.0;
        return Result;
    }

  private:
    mutable FCriticalSection Mutex;
    FDWCEditorTextureUploadTiming Timing;
};

class FDWCEditorTextureUploadState final
{
  private:
    friend class FDWCEditorRenderUploadQueue;

    EDWCEditorTextureUploadStatus Status = EDWCEditorTextureUploadStatus::Queued;
    TMap<uint64, FDWCEditorRenderUploadQueue::FUploadStateObserver> Observers;
};

FDWCEditorRenderUploadQueue::FDWCEditorRenderUploadQueue(
    const uint64 InStagingBudgetBytes,
    const uint64 InPerFlushBudgetBytes)
    : StagingBudgetBytes(FMath::Max<uint64>(InStagingBudgetBytes, 1))
    , PerFlushBudgetBytes(FMath::Max<uint64>(InPerFlushBudgetBytes, 1))
    , StagingState(MakeShared<FStagingState, ESPMode::ThreadSafe>())
{
}

FDWCEditorRenderUploadQueue::FDWCEditorRenderUploadQueue(
    TSharedRef<FDWCEditorResourceGovernor> InResourceGovernor,
    const FGuid& InSessionEpoch,
    const uint64 InStagingBudgetBytes,
    const uint64 InPerFlushBudgetBytes)
    : ResourceGovernor(MoveTemp(InResourceGovernor))
    , StagingBudgetBytes(FMath::Max<uint64>(InStagingBudgetBytes, 1))
    , PerFlushBudgetBytes(FMath::Max<uint64>(InPerFlushBudgetBytes, 1))
    , StagingState(MakeShared<FStagingState, ESPMode::ThreadSafe>())
{
    StagingMemoryOwner.Key.Namespace = TEXT("DWC.UploadStaging");
    StagingMemoryOwner.SessionEpoch = InSessionEpoch.IsValid() ? InSessionEpoch : FGuid::NewGuid();
    StagingMemoryOwner.OperationId = 1;
    StagingMemoryOwner.Generation = 1;
}

void FDWCEditorRenderUploadQueue::Enqueue(
    const FDWCEditorTextureHandle& Entry,
    const FIntRect& DirtyRect,
    const bool bWrap,
    const EDWCEditorTextureUploadPriority Priority)
{
    check(IsInGameThread());
    if (bShuttingDown || !Entry.IsValid() || !Entry->CanAcceptUploads() || DirtyRect.IsEmpty())
    {
        return;
    }

    FPendingUpload* Existing = PendingUploads.Find(Entry->GetKey());
    const bool bSameRevision = Existing != nullptr &&
        Existing->ResourceGeneration == Entry->GetResourceGeneration() &&
        Existing->ContentRevision == Entry->GetContentRevision();
    if (Existing != nullptr)
    {
        ++CoalescedRequestCount;
        // A newer CPU revision supersedes any unsent slices. Fold those slices
        // back into the dirty set so they are uploaded from the latest pixels.
        for (const FIntRect& Region : Existing->RemainingRegions)
        {
            Existing->DirtyRegions.Add(Region, Entry->GetDescriptor().Size, false);
        }
        Existing->RemainingRegions.Reset();
        Existing->PreparedPayload.Reset();
        Existing->PreparedRegionIndex = 0;
        Existing->PreparedRowOffset = 0;
    }
    FPendingUpload& Pending = Existing != nullptr ? *Existing : PendingUploads.Add(Entry->GetKey());
    if (!bSameRevision)
    {
        if (Pending.Telemetry.IsValid())
        {
            Pending.Telemetry->MarkStale(false);
        }
        TransitionState(Pending.State, EDWCEditorTextureUploadStatus::Stale);
        Pending.Telemetry = MakeShared<FDWCEditorTextureUploadTelemetryState, ESPMode::ThreadSafe>();
        Pending.State = MakeShared<FDWCEditorTextureUploadState, ESPMode::ThreadSafe>();
    }
    else if (!Pending.State.IsValid())
    {
        Pending.State = MakeShared<FDWCEditorTextureUploadState, ESPMode::ThreadSafe>();
    }
    if (FRenderEnqueuedRevision* Submitted = RenderEnqueuedRevisions.Find(Entry->GetKey());
        Submitted != nullptr &&
        (Submitted->ResourceGeneration != Entry->GetResourceGeneration() ||
         Submitted->ContentRevision < Entry->GetContentRevision()))
    {
        if (Submitted->Telemetry.IsValid())
        {
            Submitted->Telemetry->MarkStale(false);
        }
        TransitionState(Submitted->State, EDWCEditorTextureUploadStatus::Stale);
        RenderEnqueuedRevisions.Remove(Entry->GetKey());
    }
    Pending.Entry = Entry;
    Pending.ResourceGeneration = Entry->GetResourceGeneration();
    Pending.ContentRevision = Entry->GetContentRevision();
    Pending.QueuedSerial = ++QueuedSerial;
    Pending.Priority = Existing == nullptr ||
        static_cast<uint8>(Priority) > static_cast<uint8>(Pending.Priority)
        ? Priority
        : Pending.Priority;
    Pending.Telemetry->RecordRequest(
        DirtyRect,
        Entry->GetDescriptor().Size,
        Priority,
        bSameRevision);
    Pending.DirtyRegions.Add(DirtyRect, Entry->GetDescriptor().Size, bWrap);

    const uint64 FullArea = static_cast<uint64>(Entry->GetDescriptor().Size.X) * Entry->GetDescriptor().Size.Y;
    if (FullArea > 0 && Pending.DirtyRegions.GetArea() * 2 >= FullArea)
    {
        Pending.DirtyRegions.Reset();
        Pending.DirtyRegions.Add(
            FIntRect(0, 0, Entry->GetDescriptor().Size.X, Entry->GetDescriptor().Size.Y),
            Entry->GetDescriptor().Size,
            false);
        Pending.Telemetry->MarkFullTextureUpload();
    }
    DispatchNotifications();
}

bool FDWCEditorRenderUploadQueue::EnqueuePreparedBGRA8(
    const FDWCEditorTextureHandle& Entry,
    TArray<FDWCEditorPreparedBGRA8Region>&& Regions,
    const EDWCEditorTextureUploadPriority Priority)
{
    check(IsInGameThread());
    if (bShuttingDown || !Entry.IsValid() || !Entry->CanAcceptUploads() ||
        Entry->GetDescriptor().PixelFormat != PF_B8G8R8A8 || Regions.IsEmpty())
    {
        ++PreparedPayloadRejectCount;
        return false;
    }

    uint64 TotalBytes = 0;
    for (const FDWCEditorPreparedBGRA8Region& Region : Regions)
    {
        const int64 PixelCount = static_cast<int64>(Region.Rect.Width()) * Region.Rect.Height();
        const bool bValidRect = !Region.Rect.IsEmpty() && Region.Rect.Min.X >= 0 && Region.Rect.Min.Y >= 0 &&
            Region.Rect.Max.X <= Entry->GetDescriptor().Size.X &&
            Region.Rect.Max.Y <= Entry->GetDescriptor().Size.Y;
        const uint64 RegionBytes = PixelCount > 0
            ? static_cast<uint64>(PixelCount) * sizeof(FColor)
            : 0;
        if (!bValidRect || PixelCount != Region.Pixels.Num() ||
            RegionBytes > MAX_uint64 - TotalBytes)
        {
            ++PreparedPayloadRejectCount;
            return false;
        }
        TotalBytes += RegionBytes;
    }

    // The texture key is a latest-only mailbox. Release the queue's ownership
    // of an unsent older payload before reserving the replacement.
    if (FPendingUpload* Existing = PendingUploads.Find(Entry->GetKey()))
    {
        ++CoalescedRequestCount;
        if (Existing->PreparedPayload.IsValid())
        {
            ++PreparedMailboxReplacementCount;
            PreparedMailboxReplacementBytes = PreparedMailboxReplacementBytes <=
                    MAX_uint64 - Existing->PreparedPayload->ReservedBytes
                ? PreparedMailboxReplacementBytes + Existing->PreparedPayload->ReservedBytes
                : MAX_uint64;
        }
        if (Existing->Telemetry.IsValid())
        {
            Existing->Telemetry->MarkStale(false);
        }
        TransitionState(Existing->State, EDWCEditorTextureUploadStatus::Stale);
        PendingUploads.Remove(Entry->GetKey());
    }

    TSharedPtr<FDWCEditorMemoryLease, ESPMode::ThreadSafe> StagingLease;
    if (!TryReserveStagingBytes(TotalBytes, StagingLease))
    {
        ++PreparedPayloadRejectCount;
        DispatchNotifications();
        return false;
    }

    TSharedPtr<FPreparedUploadPayload, ESPMode::ThreadSafe> Payload =
        MakeShared<FPreparedUploadPayload, ESPMode::ThreadSafe>(
            MoveTemp(Regions),
            TotalBytes,
            StagingState,
            MoveTemp(StagingLease));
    if (FRenderEnqueuedRevision* Submitted = RenderEnqueuedRevisions.Find(Entry->GetKey());
        Submitted != nullptr &&
        (Submitted->ResourceGeneration != Entry->GetResourceGeneration() ||
         Submitted->ContentRevision < Entry->GetContentRevision()))
    {
        if (Submitted->Telemetry.IsValid())
        {
            Submitted->Telemetry->MarkStale(false);
        }
        TransitionState(Submitted->State, EDWCEditorTextureUploadStatus::Stale);
        RenderEnqueuedRevisions.Remove(Entry->GetKey());
    }

    FPendingUpload& Pending = PendingUploads.Add(Entry->GetKey());
    Pending.Entry = Entry;
    Pending.ResourceGeneration = Entry->GetResourceGeneration();
    Pending.ContentRevision = Entry->GetContentRevision();
    Pending.QueuedSerial = ++QueuedSerial;
    Pending.Priority = Priority;
    Pending.Telemetry = MakeShared<FDWCEditorTextureUploadTelemetryState, ESPMode::ThreadSafe>();
    Pending.State = MakeShared<FDWCEditorTextureUploadState, ESPMode::ThreadSafe>();
    Pending.PreparedPayload = MoveTemp(Payload);
    Pending.Telemetry->RecordPreparedPayload(TotalBytes);
    for (const FDWCEditorPreparedBGRA8Region& Region : Pending.PreparedPayload->Regions)
    {
        Pending.Telemetry->RecordRequest(Region.Rect, Entry->GetDescriptor().Size, Priority, false);
    }

    ++PreparedPayloadCount;
    PreparedPayloadBytes += TotalBytes;
    DispatchNotifications();
    return true;
}

void FDWCEditorRenderUploadQueue::Cancel(const FDWCEditorTextureKey& Key)
{
    check(IsInGameThread());
    if (const FPendingUpload* Pending = PendingUploads.Find(Key); Pending != nullptr && Pending->Telemetry.IsValid())
    {
        Pending->Telemetry->MarkStale(true);
        TransitionState(Pending->State, EDWCEditorTextureUploadStatus::Stale);
    }
    if (const FRenderEnqueuedRevision* Submitted = RenderEnqueuedRevisions.Find(Key); Submitted != nullptr)
    {
        if (Submitted->Telemetry.IsValid())
        {
            Submitted->Telemetry->MarkStale(true);
        }
        TransitionState(Submitted->State, EDWCEditorTextureUploadStatus::Stale);
    }
    PendingUploads.Remove(Key);
    RenderEnqueuedRevisions.Remove(Key);
    DispatchNotifications();
}

void FDWCEditorRenderUploadQueue::CancelOwner(const UObject* Owner)
{
    check(IsInGameThread());
    if (Owner == nullptr)
    {
        return;
    }
    const FObjectKey OwnerKey(Owner);
    for (auto It = PendingUploads.CreateIterator(); It; ++It)
    {
        if (It.Key().Owner == OwnerKey)
        {
            if (It.Value().Telemetry.IsValid())
            {
                It.Value().Telemetry->MarkStale(true);
            }
            TransitionState(It.Value().State, EDWCEditorTextureUploadStatus::Stale);
            RenderEnqueuedRevisions.Remove(It.Key());
            It.RemoveCurrent();
        }
    }
    for (auto It = RenderEnqueuedRevisions.CreateIterator(); It; ++It)
    {
        if (It.Key().Owner == OwnerKey)
        {
            if (It.Value().Telemetry.IsValid())
            {
                It.Value().Telemetry->MarkStale(true);
            }
            TransitionState(It.Value().State, EDWCEditorTextureUploadStatus::Stale);
            It.RemoveCurrent();
        }
    }
    DispatchNotifications();
}

void FDWCEditorRenderUploadQueue::Flush()
{
    check(IsInGameThread());
    if (bShuttingDown || PendingUploads.IsEmpty())
    {
        return;
    }

    TArray<FDWCEditorTextureKey> Keys;
    PendingUploads.GenerateKeyArray(Keys);
    Keys.Sort(
        [this](const FDWCEditorTextureKey& A, const FDWCEditorTextureKey& B)
        {
            const FPendingUpload& UploadA = PendingUploads.FindChecked(A);
            const FPendingUpload& UploadB = PendingUploads.FindChecked(B);
            if (UploadA.Priority != UploadB.Priority)
            {
                return static_cast<uint8>(UploadA.Priority) > static_cast<uint8>(UploadB.Priority);
            }
            return UploadA.QueuedSerial < UploadB.QueuedSerial;
        });

    uint64 SubmittedThisFlush = 0;
    for (const FDWCEditorTextureKey& Key : Keys)
    {
        if (SubmittedThisFlush >= PerFlushBudgetBytes)
        {
            break;
        }
        ProcessPendingUpload(
            Key,
            SubmittedThisFlush,
            PerFlushBudgetBytes,
            0.0,
            static_cast<uint32>(PendingUploads.Num()));
    }
    DispatchNotifications();
}

EDWCEditorTextureUploadStatus FDWCEditorRenderUploadQueue::TrySubmitInteractive(
    const FDWCEditorTextureUploadTicket& Ticket,
    const uint64 ByteBudget,
    const double TimeBudgetMs)
{
    check(IsInGameThread());
    if (bShuttingDown || !Ticket.IsValid())
    {
        return bShuttingDown
            ? EDWCEditorTextureUploadStatus::Stale
            : EDWCEditorTextureUploadStatus::Invalid;
    }

    FPendingUpload* Pending = PendingUploads.Find(Ticket.Key);
    if (Pending == nullptr || Pending->State != Ticket.State ||
        Pending->ResourceGeneration != Ticket.ResourceGeneration ||
        Pending->ContentRevision != Ticket.ContentRevision)
    {
        return GetStatus(Ticket);
    }
    if (Pending->Priority != EDWCEditorTextureUploadPriority::Interactive)
    {
        return EDWCEditorTextureUploadStatus::Queued;
    }

    const uint64 EffectiveByteBudget = FMath::Max<uint64>(ByteBudget, 1);
    const double DeadlineSeconds = TimeBudgetMs > 0.0
        ? FPlatformTime::Seconds() + TimeBudgetMs / 1000.0
        : 0.0;
    uint64 SubmittedBytes = 0;
    ProcessPendingUpload(
        Ticket.Key,
        SubmittedBytes,
        EffectiveByteBudget,
        DeadlineSeconds,
        static_cast<uint32>(PendingUploads.Num()));
    DispatchNotifications();

    const EDWCEditorTextureUploadStatus Status = GetStatus(Ticket);
    if (Status == EDWCEditorTextureUploadStatus::RenderEnqueued)
    {
        ++ImmediateInteractiveSubmitCount;
    }
    else if (Status == EDWCEditorTextureUploadStatus::Queued)
    {
        ++DeferredInteractiveSubmitCount;
    }
    return Status;
}

void FDWCEditorRenderUploadQueue::ProcessPendingUpload(
    const FDWCEditorTextureKey& Key,
    uint64& InOutSubmittedBytes,
    const uint64 ByteBudget,
    const double DeadlineSeconds,
    const uint32 QueueDepth)
{
    FPendingUpload* Pending = PendingUploads.Find(Key);
    if (Pending == nullptr)
    {
        return;
    }
    const FDWCEditorTextureHandle Entry = Pending->Entry.Pin();
    if (!Entry.IsValid() || !Entry->CanAcceptUploads() ||
        Entry->GetResourceGeneration() != Pending->ResourceGeneration ||
        Entry->GetContentRevision() < Pending->ContentRevision)
    {
        ++DroppedStaleRequestCount;
        if (Pending->Telemetry.IsValid())
        {
            Pending->Telemetry->MarkStale(false);
        }
        TransitionState(Pending->State, EDWCEditorTextureUploadStatus::Stale);
        PendingUploads.Remove(Key);
        return;
    }

    if (Pending->Telemetry.IsValid())
    {
        Pending->Telemetry->MarkSelected(QueueDepth);
    }

    auto FinishPendingUpload = [&]()
    {
        if (Pending->Telemetry.IsValid())
        {
            Pending->Telemetry->MarkSubmitted();
        }
        if (FRenderEnqueuedRevision* Previous = RenderEnqueuedRevisions.Find(Key);
            Previous != nullptr && Previous->State != Pending->State)
        {
            TransitionState(Previous->State, EDWCEditorTextureUploadStatus::Stale);
        }
        FRenderEnqueuedRevision& Submitted = RenderEnqueuedRevisions.FindOrAdd(Key);
        Submitted.ResourceGeneration = Pending->ResourceGeneration;
        Submitted.ContentRevision = Pending->ContentRevision;
        Submitted.Entry = Entry;
        Submitted.Telemetry = Pending->Telemetry;
        Submitted.State = Pending->State;
        TransitionState(Pending->State, EDWCEditorTextureUploadStatus::RenderEnqueued);
        PendingUploads.Remove(Key);
    };

    if (Pending->PreparedPayload.IsValid())
    {
        if (Entry->GetContentRevision() != Pending->ContentRevision)
        {
            ++DroppedStaleRequestCount;
            Pending->Telemetry->MarkStale(false);
            TransitionState(Pending->State, EDWCEditorTextureUploadStatus::Stale);
            PendingUploads.Remove(Key);
            return;
        }
        if (Entry->GetTexture() == nullptr || Entry->GetTexture()->GetResource() == nullptr)
        {
            return;
        }

        while (Pending->PreparedRegionIndex < Pending->PreparedPayload->Regions.Num())
        {
            if ((DeadlineSeconds > 0.0 && FPlatformTime::Seconds() >= DeadlineSeconds) ||
                InOutSubmittedBytes >= ByteBudget)
            {
                return;
            }

            const FDWCEditorPreparedBGRA8Region& Region =
                Pending->PreparedPayload->Regions[Pending->PreparedRegionIndex];
            const uint64 RowBytes = static_cast<uint64>(Region.Rect.Width()) * sizeof(FColor);
            const uint64 AvailableBytes = ByteBudget - InOutSubmittedBytes;
            if (RowBytes == 0 || RowBytes > AvailableBytes)
            {
                return;
            }
            const int32 RemainingRows = Region.Rect.Height() - Pending->PreparedRowOffset;
            const int32 RowsToUpload = FMath::Max(1, static_cast<int32>(
                FMath::Min<uint64>(RemainingRows, AvailableBytes / RowBytes)));
            if (!SubmitPreparedRegion(
                    Entry,
                    Region,
                    Pending->PreparedRowOffset,
                    RowsToUpload,
                    Pending->ResourceGeneration,
                    Pending->ContentRevision,
                    Pending->Telemetry,
                    Pending->PreparedPayload))
            {
                ++PreparedPayloadRejectCount;
                Pending->Telemetry->MarkStale(false);
                TransitionState(Pending->State, EDWCEditorTextureUploadStatus::Stale);
                PendingUploads.Remove(Key);
                return;
            }

            const uint64 UploadBytes = RowBytes * RowsToUpload;
            InOutSubmittedBytes += UploadBytes;
            AvoidedStagingCopyBytes += UploadBytes;
            Pending->PreparedRowOffset += RowsToUpload;
            if (Pending->PreparedRowOffset >= Region.Rect.Height())
            {
                ++Pending->PreparedRegionIndex;
                Pending->PreparedRowOffset = 0;
            }
        }

        FinishPendingUpload();
        return;
    }

    if (Pending->RemainingRegions.IsEmpty() && !Pending->DirtyRegions.IsEmpty())
    {
        Pending->RemainingRegions = Pending->DirtyRegions.GetRegions();
        Pending->DirtyRegions.Reset();
    }
    if (Pending->RemainingRegions.IsEmpty())
    {
        TransitionState(Pending->State, EDWCEditorTextureUploadStatus::Stale);
        PendingUploads.Remove(Key);
        return;
    }

    const int32 BytesPerPixel = Entry->GetDescriptor().GetBytesPerPixel();
    const uint64 ResourceGeneration = Pending->ResourceGeneration;
    const uint64 ContentRevision = Pending->ContentRevision;
    bool bDeferredByStaging = false;
    while (!Pending->RemainingRegions.IsEmpty())
    {
        if ((DeadlineSeconds > 0.0 && FPlatformTime::Seconds() >= DeadlineSeconds) ||
            InOutSubmittedBytes >= ByteBudget)
        {
            break;
        }

        const uint64 AvailableBytes = ByteBudget - InOutSubmittedBytes;
        FIntRect& RemainingRegion = Pending->RemainingRegions[0];
        const uint64 RowBytes = static_cast<uint64>(RemainingRegion.Width()) * BytesPerPixel;
        if (RowBytes == 0 || RowBytes > AvailableBytes || RowBytes > StagingBudgetBytes)
        {
            bDeferredByStaging = RowBytes > StagingBudgetBytes;
            break;
        }
        const int32 RowsToUpload = FMath::Max(1, static_cast<int32>(
            FMath::Min<uint64>(RemainingRegion.Height(), AvailableBytes / RowBytes)));
        const FIntRect UploadRegion(
            RemainingRegion.Min.X,
            RemainingRegion.Min.Y,
            RemainingRegion.Max.X,
            RemainingRegion.Min.Y + RowsToUpload);
        const uint64 UploadBytes = RowBytes * RowsToUpload;
        TSharedPtr<FDWCEditorMemoryLease, ESPMode::ThreadSafe> StagingLease;
        if (!TryReserveStagingBytes(UploadBytes, StagingLease))
        {
            bDeferredByStaging = true;
            break;
        }
        if (!SubmitRegion(
                Entry,
                UploadRegion,
                ResourceGeneration,
                ContentRevision,
                Pending->Telemetry,
                MoveTemp(StagingLease)))
        {
            ReleaseStagingBytes(UploadBytes);
            bDeferredByStaging = true;
            break;
        }

        InOutSubmittedBytes += UploadBytes;
        RemainingRegion.Min.Y += RowsToUpload;
        if (RemainingRegion.Min.Y >= RemainingRegion.Max.Y)
        {
            Pending->RemainingRegions.RemoveAt(0, 1, EAllowShrinking::No);
        }
    }
    if (bDeferredByStaging)
    {
        ++DeferredByStagingBudgetCount;
    }
    if (!Pending->RemainingRegions.IsEmpty() || !Pending->DirtyRegions.IsEmpty())
    {
        return;
    }

    FinishPendingUpload();
}

void FDWCEditorRenderUploadQueue::Shutdown()
{
    check(IsInGameThread());
    bShuttingDown = true;
    for (const TPair<FDWCEditorTextureKey, FPendingUpload>& Pair : PendingUploads)
    {
        if (Pair.Value.Telemetry.IsValid())
        {
            Pair.Value.Telemetry->MarkStale(true);
        }
        TransitionState(Pair.Value.State, EDWCEditorTextureUploadStatus::Stale);
    }
    for (const TPair<FDWCEditorTextureKey, FRenderEnqueuedRevision>& Pair : RenderEnqueuedRevisions)
    {
        if (Pair.Value.Telemetry.IsValid())
        {
            Pair.Value.Telemetry->MarkStale(true);
        }
        TransitionState(Pair.Value.State, EDWCEditorTextureUploadStatus::Stale);
    }
    PendingUploads.Reset();
    RenderEnqueuedRevisions.Reset();
    DispatchNotifications();
}

FDWCEditorTextureUploadTicket FDWCEditorRenderUploadQueue::CaptureTicket(
    const FDWCEditorTextureHandle& Entry) const
{
    check(IsInGameThread());
    FDWCEditorTextureUploadTicket Ticket;
    if (!bShuttingDown && Entry.IsValid() && Entry->CanAcceptUploads())
    {
        Ticket.Key = Entry->GetKey();
        Ticket.Entry = Entry;
        Ticket.ResourceGeneration = Entry->GetResourceGeneration();
        Ticket.ContentRevision = Entry->GetContentRevision();
        if (const FPendingUpload* Pending = PendingUploads.Find(Ticket.Key);
            Pending != nullptr && Pending->ResourceGeneration == Ticket.ResourceGeneration &&
            Pending->ContentRevision >= Ticket.ContentRevision)
        {
            Ticket.Telemetry = Pending->Telemetry;
            Ticket.State = Pending->State;
        }
        else if (const FRenderEnqueuedRevision* Submitted = RenderEnqueuedRevisions.Find(Ticket.Key);
                 Submitted != nullptr && Submitted->ResourceGeneration == Ticket.ResourceGeneration &&
                 Submitted->ContentRevision >= Ticket.ContentRevision)
        {
            Ticket.Telemetry = Submitted->Telemetry;
            Ticket.State = Submitted->State;
        }
    }
    return Ticket;
}

FDWCEditorTextureUploadTicket FDWCEditorRenderUploadQueue::CaptureSubmittedTicket(
    const FDWCEditorTextureHandle& Entry)
{
    check(IsInGameThread());
    FDWCEditorTextureUploadTicket Ticket = CaptureTicket(Entry);
    if (!Ticket.IsValid())
    {
        Ticket.Key = Entry->GetKey();
        Ticket.Entry = Entry;
        Ticket.ResourceGeneration = Entry->GetResourceGeneration();
        Ticket.ContentRevision = Entry->GetContentRevision();
    }
    FRenderEnqueuedRevision& Submitted = RenderEnqueuedRevisions.FindOrAdd(Ticket.Key);
    if (Submitted.ResourceGeneration != Ticket.ResourceGeneration)
    {
        TransitionState(Submitted.State, EDWCEditorTextureUploadStatus::Stale);
        Submitted = {};
        Submitted.ResourceGeneration = Ticket.ResourceGeneration;
    }
    Submitted.ContentRevision = FMath::Max(Submitted.ContentRevision, Ticket.ContentRevision);
    Submitted.Entry = Entry;
    if (!Ticket.State.IsValid())
    {
        Ticket.State = MakeShared<FDWCEditorTextureUploadState, ESPMode::ThreadSafe>();
    }
    Ticket.State->Status = EDWCEditorTextureUploadStatus::RenderEnqueued;
    Submitted.Telemetry = Ticket.Telemetry;
    Submitted.State = Ticket.State;
    return Ticket;
}

EDWCEditorTextureUploadStatus FDWCEditorRenderUploadQueue::GetStatus(
    const FDWCEditorTextureUploadTicket& Ticket) const
{
    check(IsInGameThread());
    if (!Ticket.IsValid() || !Ticket.State.IsValid())
    {
        return EDWCEditorTextureUploadStatus::Invalid;
    }
    if (bShuttingDown)
    {
        if (Ticket.Telemetry.IsValid())
        {
            Ticket.Telemetry->MarkStale(true);
        }
        return EDWCEditorTextureUploadStatus::Stale;
    }
    const FDWCEditorTextureHandle Entry = Ticket.Entry.Pin();
    if (!Entry.IsValid() || !Entry->CanAcceptUploads() || !(Entry->GetKey() == Ticket.Key) ||
        Entry->GetResourceGeneration() != Ticket.ResourceGeneration ||
        Entry->GetContentRevision() < Ticket.ContentRevision)
    {
        if (Ticket.Telemetry.IsValid())
        {
            Ticket.Telemetry->MarkStale(false);
        }
        return EDWCEditorTextureUploadStatus::Stale;
    }
    const EDWCEditorTextureUploadStatus Status = Ticket.State->Status;
    if (Status == EDWCEditorTextureUploadStatus::RenderEnqueued && Ticket.Telemetry.IsValid())
    {
        Ticket.Telemetry->MarkObserved();
    }
    return Status;
}

FDWCEditorTextureUploadObserverHandle FDWCEditorRenderUploadQueue::Observe(
    const FDWCEditorTextureUploadTicket& Ticket,
    FUploadStateObserver Observer)
{
    check(IsInGameThread());
    FDWCEditorTextureUploadObserverHandle Handle;
    if (!Ticket.IsValid() || !Ticket.State.IsValid() || !Observer)
    {
        return Handle;
    }

    if (Ticket.State->Status != EDWCEditorTextureUploadStatus::Queued)
    {
        ++ObserverNotificationCount;
        Observer(Ticket.State->Status);
        return Handle;
    }

    Handle.ObserverId = NextObserverId++;
    Handle.State = Ticket.State;
    Ticket.State->Observers.Add(Handle.ObserverId, MoveTemp(Observer));
    return Handle;
}

void FDWCEditorRenderUploadQueue::RemoveObserver(FDWCEditorTextureUploadObserverHandle& Handle)
{
    check(IsInGameThread());
    if (const TSharedPtr<FDWCEditorTextureUploadState, ESPMode::ThreadSafe> State = Handle.State.Pin())
    {
        State->Observers.Remove(Handle.ObserverId);
    }
    Handle.Reset();
}

void FDWCEditorRenderUploadQueue::TransitionState(
    const TSharedPtr<FDWCEditorTextureUploadState, ESPMode::ThreadSafe>& State,
    const EDWCEditorTextureUploadStatus NewStatus)
{
    if (!State.IsValid() || State->Status == NewStatus)
    {
        return;
    }
    State->Status = NewStatus;
    if (NewStatus != EDWCEditorTextureUploadStatus::RenderEnqueued &&
        NewStatus != EDWCEditorTextureUploadStatus::Stale)
    {
        return;
    }

    for (TPair<uint64, FUploadStateObserver>& Pair : State->Observers)
    {
        FUploadStateObserver Observer = MoveTemp(Pair.Value);
        DeferredNotifications.Add(
            [Observer = MoveTemp(Observer), NewStatus]() mutable
            {
                Observer(NewStatus);
            });
    }
    State->Observers.Reset();
}

void FDWCEditorRenderUploadQueue::DispatchNotifications()
{
    if (bDispatchingNotifications)
    {
        return;
    }
    TGuardValue<bool> DispatchGuard(bDispatchingNotifications, true);
    while (!DeferredNotifications.IsEmpty())
    {
        TArray<TFunction<void()>> Notifications = MoveTemp(DeferredNotifications);
        DeferredNotifications.Reset();
        ObserverNotificationCount += Notifications.Num();
        for (TFunction<void()>& Notification : Notifications)
        {
            Notification();
        }
    }
}

bool FDWCEditorRenderUploadQueue::GetTiming(
    const FDWCEditorTextureUploadTicket& Ticket,
    FDWCEditorTextureUploadTiming& OutTiming) const
{
    if (!Ticket.IsValid() || !Ticket.Telemetry.IsValid())
    {
        OutTiming = {};
        return false;
    }
    OutTiming = Ticket.Telemetry->Snapshot();
    return true;
}

bool FDWCEditorRenderUploadQueue::SubmitRegion(
    const FDWCEditorTextureHandle& Entry,
    const FIntRect& Region,
    const uint64 ResourceGeneration,
    const uint64 ContentRevision,
    const TSharedPtr<FDWCEditorTextureUploadTelemetryState, ESPMode::ThreadSafe>& Telemetry,
    TSharedPtr<FDWCEditorMemoryLease, ESPMode::ThreadSafe> StagingLease)
{
    UTexture2D* Texture = Entry->GetTexture();
    const FDWCEditorTextureDescriptor& Descriptor = Entry->GetDescriptor();
    if (!Entry->CanAcceptUploads() || Texture == nullptr || Texture->GetResource() == nullptr || Region.IsEmpty() ||
        Entry->GetResourceGeneration() != ResourceGeneration ||
        Entry->GetContentRevision() < ContentRevision)
    {
        return false;
    }

    const int32 BytesPerPixel = Descriptor.GetBytesPerPixel();
    const uint32 Pitch = static_cast<uint32>(Region.Width() * BytesPerPixel);
    const uint64 UploadBytes = static_cast<uint64>(Pitch) * Region.Height();

    TSharedRef<TArray<uint8>, ESPMode::ThreadSafe> Staging =
        MakeShared<TArray<uint8>, ESPMode::ThreadSafe>();
    const double StagingStartSeconds = FPlatformTime::Seconds();
    Staging->SetNumUninitialized(static_cast<int32>(UploadBytes));
    const uint8* Source = Entry->GetPixelData();
    const int32 SourcePitch = Descriptor.Size.X * BytesPerPixel;
    for (int32 Row = 0; Row < Region.Height(); ++Row)
    {
        FMemory::Memcpy(
            Staging->GetData() + static_cast<SIZE_T>(Row) * Pitch,
            Source + static_cast<SIZE_T>(Region.Min.Y + Row) * SourcePitch +
                static_cast<SIZE_T>(Region.Min.X) * BytesPerPixel,
            Pitch);
    }
    if (Telemetry.IsValid())
    {
        Telemetry->AddStagingCopy((FPlatformTime::Seconds() - StagingStartSeconds) * 1000.0);
        Telemetry->RecordRegionScheduled(UploadBytes);
    }

    FUpdateTextureRegion2D* UpdateRegion = new FUpdateTextureRegion2D(
        Region.Min.X,
        Region.Min.Y,
        0,
        0,
        Region.Width(),
        Region.Height());
    TSharedPtr<TArray<uint8>, ESPMode::ThreadSafe> KeepAlive = Staging;
    const TSharedRef<FStagingState, ESPMode::ThreadSafe> KeepStagingState = StagingState;
    const double SubmitStartSeconds = FPlatformTime::Seconds();
    Texture->UpdateTextureRegions(
        0,
        1,
        UpdateRegion,
        Pitch,
        BytesPerPixel,
        Staging->GetData(),
        [KeepAlive = MoveTemp(KeepAlive), KeepStagingState, UploadBytes, Telemetry,
         StagingLease = MoveTemp(StagingLease)](
            uint8*,
            const FUpdateTextureRegion2D* Regions)
        {
            (void)KeepAlive;
            if (StagingLease.IsValid())
            {
                StagingLease->Reset();
            }
            KeepStagingState->InFlightBytes.SubExchange(UploadBytes);
            if (Telemetry.IsValid())
            {
                Telemetry->RecordRenderCallback();
            }
            delete Regions;
        });
    if (Telemetry.IsValid())
    {
        Telemetry->AddSubmitCall((FPlatformTime::Seconds() - SubmitStartSeconds) * 1000.0);
    }

    ++SubmittedUploadCount;
    SubmittedUploadBytes += UploadBytes;
    return true;
}

bool FDWCEditorRenderUploadQueue::SubmitPreparedRegion(
    const FDWCEditorTextureHandle& Entry,
    const FDWCEditorPreparedBGRA8Region& Region,
    const int32 RowOffset,
    const int32 RowCount,
    const uint64 ResourceGeneration,
    const uint64 ContentRevision,
    const TSharedPtr<FDWCEditorTextureUploadTelemetryState, ESPMode::ThreadSafe>& Telemetry,
    const TSharedPtr<FPreparedUploadPayload, ESPMode::ThreadSafe>& Payload)
{
    UTexture2D* Texture = Entry->GetTexture();
    if (!Payload.IsValid() || !Entry->CanAcceptUploads() || Texture == nullptr ||
        Texture->GetResource() == nullptr || Region.Rect.IsEmpty() || RowOffset < 0 || RowCount <= 0 ||
        RowOffset + RowCount > Region.Rect.Height() ||
        Entry->GetResourceGeneration() != ResourceGeneration ||
        Entry->GetContentRevision() != ContentRevision)
    {
        return false;
    }

    const uint32 Pitch = static_cast<uint32>(Region.Rect.Width() * sizeof(FColor));
    const uint64 UploadBytes = static_cast<uint64>(Pitch) * RowCount;
    const uint8* Source = reinterpret_cast<const uint8*>(Region.Pixels.GetData()) +
        static_cast<SIZE_T>(RowOffset) * Pitch;
    FUpdateTextureRegion2D* UpdateRegion = new FUpdateTextureRegion2D(
        Region.Rect.Min.X,
        Region.Rect.Min.Y + RowOffset,
        0,
        0,
        Region.Rect.Width(),
        RowCount);

    if (Telemetry.IsValid())
    {
        Telemetry->RecordPreparedRegionScheduled(UploadBytes);
    }
    const double SubmitStartSeconds = FPlatformTime::Seconds();
    Texture->UpdateTextureRegions(
        0,
        1,
        UpdateRegion,
        Pitch,
        sizeof(FColor),
        const_cast<uint8*>(Source),
        [KeepAlive = Payload, Telemetry](uint8*, const FUpdateTextureRegion2D* Regions)
        {
            (void)KeepAlive;
            if (Telemetry.IsValid())
            {
                Telemetry->RecordRenderCallback();
            }
            delete Regions;
        });
    if (Telemetry.IsValid())
    {
        Telemetry->AddSubmitCall((FPlatformTime::Seconds() - SubmitStartSeconds) * 1000.0);
    }

    ++SubmittedUploadCount;
    SubmittedUploadBytes += UploadBytes;
    return true;
}

bool FDWCEditorRenderUploadQueue::TryReserveStagingBytes(
    const uint64 UploadBytes,
    TSharedPtr<FDWCEditorMemoryLease, ESPMode::ThreadSafe>& OutLease)
{
    OutLease.Reset();
    if (UploadBytes == 0 || UploadBytes > StagingBudgetBytes)
    {
        return false;
    }

    uint64 CurrentBytes = StagingState->InFlightBytes.Load();
    while (CurrentBytes <= StagingBudgetBytes - UploadBytes)
    {
        if (StagingState->InFlightBytes.CompareExchange(CurrentBytes, CurrentBytes + UploadBytes))
        {
            if (ResourceGovernor.IsValid())
            {
                FDWCEditorResourceReservationRequest Request;
                Request.Pool = EDWCEditorResourcePool::UploadStagingCPU;
                Request.Bytes = UploadBytes;
                Request.Owner = StagingMemoryOwner;
                Request.DebugName = TEXT("Render upload staging slice");
                FDWCEditorMemoryLease Lease = ResourceGovernor->TryAcquire(Request);
                if (!Lease.IsValid())
                {
                    StagingState->InFlightBytes.SubExchange(UploadBytes);
                    return false;
                }
                OutLease = MakeShared<FDWCEditorMemoryLease, ESPMode::ThreadSafe>(MoveTemp(Lease));
            }
            return true;
        }
    }
    return false;
}

void FDWCEditorRenderUploadQueue::ReleaseStagingBytes(const uint64 UploadBytes)
{
    if (UploadBytes > 0)
    {
        StagingState->InFlightBytes.SubExchange(UploadBytes);
    }
}

void FDWCEditorRenderUploadQueue::AppendDiagnosticMemoryBucket(
    TArray<FDWCEditorPreviewMemoryBucket>& OutBuckets) const
{
    FDWCEditorPreviewMemoryBucket& Bucket = OutBuckets.AddDefaulted_GetRef();
    Bucket.Name = TEXT("Render upload staging (in-flight)");
    Bucket.UsedBytes = StagingState->InFlightBytes.Load();
    Bucket.BudgetBytes = StagingBudgetBytes;
    Bucket.EntryCount = PendingUploads.Num();
}

void FDWCEditorRenderUploadQueue::AppendDiagnosticOperationCounters(
    TArray<FDWCEditorPreviewOperationCounter>& OutCounters) const
{
    FDWCEditorPreviewOperationCounter& Uploads = OutCounters.AddDefaulted_GetRef();
    Uploads.Name = TEXT("Render texture region uploads");
    Uploads.Count = SubmittedUploadCount;
    Uploads.Bytes = SubmittedUploadBytes;

    FDWCEditorPreviewOperationCounter& Coalesced = OutCounters.AddDefaulted_GetRef();
    Coalesced.Name = TEXT("Coalesced texture upload requests");
    Coalesced.Count = CoalescedRequestCount;

    FDWCEditorPreviewOperationCounter& Dropped = OutCounters.AddDefaulted_GetRef();
    Dropped.Name = TEXT("Dropped stale texture uploads");
    Dropped.Count = DroppedStaleRequestCount;

    FDWCEditorPreviewOperationCounter& Deferred = OutCounters.AddDefaulted_GetRef();
    Deferred.Name = TEXT("Deferred texture uploads (staging budget)");
    Deferred.Count = DeferredByStagingBudgetCount;

    FDWCEditorPreviewOperationCounter& ImmediateInteractive = OutCounters.AddDefaulted_GetRef();
    ImmediateInteractive.Name = TEXT("Immediate interactive texture uploads");
    ImmediateInteractive.Count = ImmediateInteractiveSubmitCount;

    FDWCEditorPreviewOperationCounter& DeferredInteractive = OutCounters.AddDefaulted_GetRef();
    DeferredInteractive.Name = TEXT("Deferred interactive texture uploads");
    DeferredInteractive.Count = DeferredInteractiveSubmitCount;

    FDWCEditorPreviewOperationCounter& ObserverNotifications = OutCounters.AddDefaulted_GetRef();
    ObserverNotifications.Name = TEXT("Texture upload observer notifications");
    ObserverNotifications.Count = ObserverNotificationCount;

    FDWCEditorPreviewOperationCounter& Prepared = OutCounters.AddDefaulted_GetRef();
    Prepared.Name = TEXT("Prepared zero-copy texture payloads");
    Prepared.Count = PreparedPayloadCount;
    Prepared.Bytes = PreparedPayloadBytes;

    FDWCEditorPreviewOperationCounter& AvoidedCopies = OutCounters.AddDefaulted_GetRef();
    AvoidedCopies.Name = TEXT("Avoided texture staging copies");
    AvoidedCopies.Count = PreparedPayloadCount;
    AvoidedCopies.Bytes = AvoidedStagingCopyBytes;

    FDWCEditorPreviewOperationCounter& PreparedRejects = OutCounters.AddDefaulted_GetRef();
    PreparedRejects.Name = TEXT("Prepared texture payload fallbacks");
    PreparedRejects.Count = PreparedPayloadRejectCount;

    FDWCEditorPreviewOperationCounter& MailboxReplacements = OutCounters.AddDefaulted_GetRef();
    MailboxReplacements.Name = TEXT("Superseded prepared upload payloads");
    MailboxReplacements.Count = PreparedMailboxReplacementCount;
    MailboxReplacements.Bytes = PreparedMailboxReplacementBytes;

    struct FPendingPriorityStats
    {
        uint64 Count = 0;
        uint64 EstimatedBytes = 0;
    };
    FPendingPriorityStats PendingStats[3];
    for (const TPair<FDWCEditorTextureKey, FPendingUpload>& Pair : PendingUploads)
    {
        const FPendingUpload& Pending = Pair.Value;
        const int32 PriorityIndex = static_cast<int32>(Pending.Priority);
        if (PriorityIndex < 0 || PriorityIndex >= UE_ARRAY_COUNT(PendingStats))
        {
            continue;
        }
        ++PendingStats[PriorityIndex].Count;

        const FDWCEditorTextureHandle Entry = Pending.Entry.Pin();
        if (!Entry.IsValid())
        {
            continue;
        }

        uint64 PixelCount = 0;
        if (Pending.PreparedPayload.IsValid())
        {
            for (int32 RegionIndex = Pending.PreparedRegionIndex;
                 RegionIndex < Pending.PreparedPayload->Regions.Num(); ++RegionIndex)
            {
                const FDWCEditorPreparedBGRA8Region& Region = Pending.PreparedPayload->Regions[RegionIndex];
                const int32 FirstRow = RegionIndex == Pending.PreparedRegionIndex
                    ? Pending.PreparedRowOffset
                    : 0;
                PixelCount += static_cast<uint64>(Region.Rect.Width()) *
                    static_cast<uint64>(Region.Rect.Height() - FirstRow);
            }
        }
        else if (!Pending.RemainingRegions.IsEmpty())
        {
            for (const FIntRect& Region : Pending.RemainingRegions)
            {
                PixelCount += static_cast<uint64>(Region.Width()) * static_cast<uint64>(Region.Height());
            }
        }
        else
        {
            PixelCount = Pending.DirtyRegions.GetArea();
        }
        PendingStats[PriorityIndex].EstimatedBytes += PixelCount *
            static_cast<uint64>(Entry->GetDescriptor().GetBytesPerPixel());
    }

    const auto AddPendingCounter = [&OutCounters, &PendingStats](
                                       const EDWCEditorTextureUploadPriority Priority,
                                       const TCHAR* Name)
    {
        const FPendingPriorityStats& Stats = PendingStats[static_cast<int32>(Priority)];
        FDWCEditorPreviewOperationCounter& Counter = OutCounters.AddDefaulted_GetRef();
        Counter.Name = Name;
        Counter.Count = Stats.Count;
        Counter.Bytes = Stats.EstimatedBytes;
    };
    AddPendingCounter(EDWCEditorTextureUploadPriority::Interactive, TEXT("Pending interactive texture uploads"));
    AddPendingCounter(EDWCEditorTextureUploadPriority::Normal, TEXT("Pending normal texture uploads"));
    AddPendingCounter(EDWCEditorTextureUploadPriority::Background, TEXT("Pending background texture uploads"));
}

void FDWCEditorRenderUploadQueue::ResetDiagnosticCounters()
{
    SubmittedUploadCount = 0;
    SubmittedUploadBytes = 0;
    CoalescedRequestCount = 0;
    DroppedStaleRequestCount = 0;
    DeferredByStagingBudgetCount = 0;
    ImmediateInteractiveSubmitCount = 0;
    DeferredInteractiveSubmitCount = 0;
    ObserverNotificationCount = 0;
    PreparedPayloadCount = 0;
    PreparedPayloadBytes = 0;
    AvoidedStagingCopyBytes = 0;
    PreparedPayloadRejectCount = 0;
    PreparedMailboxReplacementCount = 0;
    PreparedMailboxReplacementBytes = 0;
}
