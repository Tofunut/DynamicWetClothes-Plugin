// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "WetClothing/Foundation/TextureWorkspace/DWCEditorRenderUploadQueue.h"

#include "Engine/Texture2D.h"
#include "Rendering/Texture2DResource.h"
#include "WetClothing/Foundation/Preview/Diagnostics/DWCEditorPreviewDiagnostics.h"

FDWCEditorRenderUploadQueue::FDWCEditorRenderUploadQueue(
    const uint64 InStagingBudgetBytes,
    const uint64 InPerFlushBudgetBytes)
    : StagingBudgetBytes(FMath::Max<uint64>(InStagingBudgetBytes, 1)), PerFlushBudgetBytes(FMath::Max<uint64>(InPerFlushBudgetBytes, 1)), StagingState(MakeShared<FStagingState, ESPMode::ThreadSafe>())
{
}

void FDWCEditorRenderUploadQueue::Enqueue(
    const FDWCEditorTextureHandle&        Entry,
    const FIntRect&                       DirtyRect,
    const bool                            bWrap,
    const EDWCEditorTextureUploadPriority Priority)
{
    check(IsInGameThread());
    if (bShuttingDown || !Entry.IsValid() || !Entry->CanAcceptUploads() || DirtyRect.IsEmpty())
    {
        return;
    }

    FPendingUpload* Existing = PendingUploads.Find(Entry->GetKey());
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
    }
    FPendingUpload& Pending = Existing != nullptr ? *Existing : PendingUploads.Add(Entry->GetKey());
    Pending.Entry = Entry;
    Pending.ResourceGeneration = Entry->GetResourceGeneration();
    Pending.ContentRevision = Entry->GetContentRevision();
    Pending.QueuedSerial = ++QueuedSerial;
    Pending.Priority = Existing == nullptr ||
                               static_cast<uint8>(Priority) > static_cast<uint8>(Pending.Priority)
                           ? Priority
                           : Pending.Priority;
    Pending.DirtyRegions.Add(DirtyRect, Entry->GetDescriptor().Size, bWrap);

    const uint64 FullArea = static_cast<uint64>(Entry->GetDescriptor().Size.X) * Entry->GetDescriptor().Size.Y;
    if (FullArea > 0 && Pending.DirtyRegions.GetArea() * 2 >= FullArea)
    {
        Pending.DirtyRegions.Reset();
        Pending.DirtyRegions.Add(
            FIntRect(0, 0, Entry->GetDescriptor().Size.X, Entry->GetDescriptor().Size.Y),
            Entry->GetDescriptor().Size,
            false);
    }
}

void FDWCEditorRenderUploadQueue::Cancel(const FDWCEditorTextureKey& Key)
{
    check(IsInGameThread());
    PendingUploads.Remove(Key);
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
            It.RemoveCurrent();
        }
    }
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
        FPendingUpload* Pending = PendingUploads.Find(Key);
        if (Pending == nullptr)
        {
            continue;
        }
        const FDWCEditorTextureHandle Entry = Pending->Entry.Pin();
        if (!Entry.IsValid() || !Entry->CanAcceptUploads() ||
            Entry->GetResourceGeneration() != Pending->ResourceGeneration ||
            Entry->GetContentRevision() < Pending->ContentRevision)
        {
            ++DroppedStaleRequestCount;
            PendingUploads.Remove(Key);
            continue;
        }

        if (Pending->RemainingRegions.IsEmpty() && !Pending->DirtyRegions.IsEmpty())
        {
            Pending->RemainingRegions = Pending->DirtyRegions.GetRegions();
            Pending->DirtyRegions.Reset();
        }
        if (Pending->RemainingRegions.IsEmpty())
        {
            PendingUploads.Remove(Key);
            continue;
        }

        const uint64 RemainingFlushBytes = PerFlushBudgetBytes > SubmittedThisFlush
                                               ? PerFlushBudgetBytes - SubmittedThisFlush
                                               : 0;
        if (RemainingFlushBytes == 0)
        {
            break;
        }

        const int32  BytesPerPixel = Entry->GetDescriptor().GetBytesPerPixel();
        const uint64 ResourceGeneration = Pending->ResourceGeneration;
        const uint64 ContentRevision = Pending->ContentRevision;
        bool         bDeferredByStaging = false;
        while (!Pending->RemainingRegions.IsEmpty())
        {
            const uint64 AvailableBytes = PerFlushBudgetBytes > SubmittedThisFlush
                                              ? PerFlushBudgetBytes - SubmittedThisFlush
                                              : 0;
            if (AvailableBytes == 0)
            {
                break;
            }

            FIntRect&    RemainingRegion = Pending->RemainingRegions[0];
            const uint64 RowBytes = static_cast<uint64>(RemainingRegion.Width()) * BytesPerPixel;
            if (RowBytes == 0 || RowBytes > AvailableBytes || RowBytes > StagingBudgetBytes)
            {
                bDeferredByStaging = true;
                break;
            }
            const int32    RowsToUpload = FMath::Max(1, static_cast<int32>(
                                                         FMath::Min<uint64>(RemainingRegion.Height(), AvailableBytes / RowBytes)));
            const FIntRect UploadRegion(
                RemainingRegion.Min.X,
                RemainingRegion.Min.Y,
                RemainingRegion.Max.X,
                RemainingRegion.Min.Y + RowsToUpload);
            const uint64 UploadBytes = RowBytes * RowsToUpload;
            if (!TryReserveStagingBytes(UploadBytes))
            {
                bDeferredByStaging = true;
                break;
            }
            if (!SubmitRegion(Entry, UploadRegion, ResourceGeneration, ContentRevision))
            {
                ReleaseStagingBytes(UploadBytes);
                Pending->RemainingRegions.Reset();
                break;
            }

            SubmittedThisFlush += UploadBytes;
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
        if (Pending->RemainingRegions.IsEmpty() && Pending->DirtyRegions.IsEmpty())
        {
            PendingUploads.Remove(Key);
        }
    }
}

void FDWCEditorRenderUploadQueue::Shutdown()
{
    check(IsInGameThread());
    bShuttingDown = true;
    PendingUploads.Reset();
}

bool FDWCEditorRenderUploadQueue::SubmitRegion(
    const FDWCEditorTextureHandle& Entry,
    const FIntRect&                Region,
    const uint64                   ResourceGeneration,
    const uint64                   ContentRevision)
{
    UTexture2D*                        Texture = Entry->GetTexture();
    const FDWCEditorTextureDescriptor& Descriptor = Entry->GetDescriptor();
    if (!Entry->CanAcceptUploads() || Texture == nullptr || Texture->GetResource() == nullptr || Region.IsEmpty() ||
        Entry->GetResourceGeneration() != ResourceGeneration ||
        Entry->GetContentRevision() < ContentRevision)
    {
        return false;
    }

    const int32  BytesPerPixel = Descriptor.GetBytesPerPixel();
    const uint32 Pitch = static_cast<uint32>(Region.Width() * BytesPerPixel);
    const uint64 UploadBytes = static_cast<uint64>(Pitch) * Region.Height();

    TSharedRef<TArray<uint8>, ESPMode::ThreadSafe> Staging =
        MakeShared<TArray<uint8>, ESPMode::ThreadSafe>();
    Staging->SetNumUninitialized(static_cast<int32>(UploadBytes));
    const uint8* Source = Entry->GetPixelData();
    const int32  SourcePitch = Descriptor.Size.X * BytesPerPixel;
    for (int32 Row = 0; Row < Region.Height(); ++Row)
    {
        FMemory::Memcpy(
            Staging->GetData() + static_cast<SIZE_T>(Row) * Pitch,
            Source + static_cast<SIZE_T>(Region.Min.Y + Row) * SourcePitch +
                static_cast<SIZE_T>(Region.Min.X) * BytesPerPixel,
            Pitch);
    }

    FUpdateTextureRegion2D* UpdateRegion = new FUpdateTextureRegion2D(
        Region.Min.X,
        Region.Min.Y,
        0,
        0,
        Region.Width(),
        Region.Height());
    TSharedPtr<TArray<uint8>, ESPMode::ThreadSafe>       KeepAlive = Staging;
    const TSharedRef<FStagingState, ESPMode::ThreadSafe> KeepStagingState = StagingState;
    Texture->UpdateTextureRegions(
        0,
        1,
        UpdateRegion,
        Pitch,
        BytesPerPixel,
        Staging->GetData(),
        [KeepAlive = MoveTemp(KeepAlive), KeepStagingState, UploadBytes](
            uint8*,
            const FUpdateTextureRegion2D* Regions)
        {
            (void)KeepAlive;
            KeepStagingState->InFlightBytes.SubExchange(UploadBytes);
            delete Regions;
        });

    ++SubmittedUploadCount;
    SubmittedUploadBytes += UploadBytes;
    return true;
}

bool FDWCEditorRenderUploadQueue::TryReserveStagingBytes(const uint64 UploadBytes)
{
    if (UploadBytes == 0 || UploadBytes > StagingBudgetBytes)
    {
        return false;
    }

    uint64 CurrentBytes = StagingState->InFlightBytes.Load();
    while (CurrentBytes <= StagingBudgetBytes - UploadBytes)
    {
        if (StagingState->InFlightBytes.CompareExchange(CurrentBytes, CurrentBytes + UploadBytes))
        {
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

    struct FPendingPriorityStats
    {
        uint64 Count = 0;
        uint64 EstimatedBytes = 0;
    };
    FPendingPriorityStats PendingStats[3];
    for (const TPair<FDWCEditorTextureKey, FPendingUpload>& Pair : PendingUploads)
    {
        const FPendingUpload& Pending = Pair.Value;
        const int32           PriorityIndex = static_cast<int32>(Pending.Priority);
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
        if (!Pending.RemainingRegions.IsEmpty())
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
                                       const TCHAR*                          Name)
    {
        const FPendingPriorityStats&       Stats = PendingStats[static_cast<int32>(Priority)];
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
}
