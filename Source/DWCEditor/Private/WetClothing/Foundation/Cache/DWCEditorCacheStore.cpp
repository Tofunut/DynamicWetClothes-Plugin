//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Foundation/Cache/DWCEditorCacheStore.h"

#include "WetClothing/Foundation/Preview/Diagnostics/DWCEditorPreviewDiagnostics.h"

FDWCEditorCacheStore::FDWCEditorCacheStore(const uint64 InBudgetBytes)
    : BudgetBytes(FMath::Max<uint64>(InBudgetBytes, 1))
{
}

FDWCEditorCacheStore::FDWCEditorCacheStore(
    TSharedRef<FDWCEditorResourceGovernor> InResourceGovernor,
    const FGuid& InSessionEpoch,
    const uint64 InBudgetBytes)
    : ResourceGovernor(MoveTemp(InResourceGovernor))
    , BudgetBytes(FMath::Max<uint64>(InBudgetBytes, 1))
{
    MemoryOwner.Key.Namespace = TEXT("DWC.SharedCache");
    MemoryOwner.SessionEpoch = InSessionEpoch.IsValid() ? InSessionEpoch : FGuid::NewGuid();
    MemoryOwner.OperationId = 1;
    MemoryOwner.Generation = 1;
}

FDWCEditorCacheLease FDWCEditorCacheStore::AcquireLease(const FDWCEditorCacheKey& Key)
{
    check(IsInGameThread());
    CleanupRetiredEntries();

    TSharedPtr<FDWCEditorCacheEntry>* EntryPtr = Entries.Find(Key);
    if (EntryPtr == nullptr || !EntryPtr->IsValid() || !(*EntryPtr)->Value.IsValid())
    {
        ++MissCount;
        return FDWCEditorCacheLease();
    }

    TSharedPtr<FDWCEditorCacheEntry> Entry = *EntryPtr;
    Entry->LastUsedSerial = ++UseSerial;
    ++HitCount;
    return FDWCEditorCacheLease(MoveTemp(Entry));
}

bool FDWCEditorCacheStore::Put(
    const FDWCEditorCacheKey& Key,
    TSharedRef<const IDWCEditorCacheValue, ESPMode::ThreadSafe> Value)
{
    check(IsInGameThread());
    CleanupRetiredEntries();

    TSharedPtr<FDWCEditorCacheEntry> Entry = MakeShared<FDWCEditorCacheEntry>();
    Entry->Value = Value;
    Entry->PayloadBytes = Value->GetAllocatedSizeBytes();
    Entry->ResidentBytes =
        static_cast<uint64>(sizeof(FDWCEditorCacheEntry)) +
        static_cast<uint64>(sizeof(FDWCEditorCacheKey)) +
        static_cast<uint64>(Key.Signature.GetAllocatedSize()) +
        Entry->PayloadBytes;

    const TSharedPtr<FDWCEditorCacheEntry> Existing = Entries.FindRef(Key);
    const bool bExistingCanRetireImmediately =
        Existing.IsValid() && Existing->ActiveLeaseCount.GetValue() <= 0;
    const auto GetProjectedBytes = [this, &Entry, &Existing, bExistingCanRetireImmediately]()
    {
        const uint64 ReplacedBytes = bExistingCanRetireImmediately
            ? Existing->ResidentBytes
            : 0;
        const uint64 TrackedBytes = GetTrackedBytes();
        return TrackedBytes >= ReplacedBytes
            ? TrackedBytes - ReplacedBytes + Entry->ResidentBytes
            : Entry->ResidentBytes;
    };

    while (GetProjectedBytes() > BudgetBytes && EvictOldestUnleased(&Key))
    {
    }
    if (GetProjectedBytes() > BudgetBytes)
    {
        ++AdmissionRejectCount;
        return false;
    }

    if (ResourceGovernor.IsValid())
    {
        if (bExistingCanRetireImmediately && Existing->MemoryLease.IsValid())
        {
            FString ResizeError;
            while (!Existing->MemoryLease.TryResize(Entry->ResidentBytes, &ResizeError) &&
                   EvictOldestUnleased(&Key))
            {
                ResizeError.Reset();
            }
            if (Existing->MemoryLease.GetReservedBytes() != Entry->ResidentBytes)
            {
                ++AdmissionRejectCount;
                return false;
            }
            Entry->MemoryLease = MoveTemp(Existing->MemoryLease);
        }
        else
        {
            FDWCEditorResourceReservationRequest Request;
            Request.Pool = EDWCEditorResourcePool::SharedCacheCPU;
            Request.Bytes = Entry->ResidentBytes;
            Request.Owner = MemoryOwner;
            Request.DebugName = FString::Printf(TEXT("Shared cache: %s"), *Key.Namespace.ToString());
            Entry->MemoryLease = ResourceGovernor->TryAcquire(Request);
            while (!Entry->MemoryLease.IsValid() && EvictOldestUnleased(&Key))
            {
                Entry->MemoryLease = ResourceGovernor->TryAcquire(Request);
            }
            if (!Entry->MemoryLease.IsValid())
            {
                ++AdmissionRejectCount;
                return false;
            }
        }
    }

    if (Existing.IsValid())
    {
        UsedBytes -= Existing->ResidentBytes;
        Entries.Remove(Key);
        TrackRetiredEntry(Existing);
    }
    Entry->LastUsedSerial = ++UseSerial;
    Entries.Add(Key, Entry);
    UsedBytes += Entry->ResidentBytes;
    TrimToBudget();
    return true;
}

void FDWCEditorCacheStore::InvalidateOwner(const UObject* Owner)
{
    check(IsInGameThread());
    CleanupRetiredEntries();
    if (Owner == nullptr)
    {
        return;
    }

    const FObjectKey OwnerKey(Owner);
    TArray<FDWCEditorCacheKey> KeysToRemove;
    for (const TPair<FDWCEditorCacheKey, TSharedPtr<FDWCEditorCacheEntry>>& Pair : Entries)
    {
        if (Pair.Key.Owner == OwnerKey)
        {
            KeysToRemove.Add(Pair.Key);
        }
    }
    for (const FDWCEditorCacheKey& Key : KeysToRemove)
    {
        RemoveEntry(Key, false);
    }
}

void FDWCEditorCacheStore::InvalidateOwnerNamespace(
    const UObject* Owner,
    const FName Namespace,
    const int32 MaterialSlotIndex)
{
    check(IsInGameThread());
    CleanupRetiredEntries();
    if (Owner == nullptr || Namespace.IsNone())
    {
        return;
    }

    const FObjectKey OwnerKey(Owner);
    TArray<FDWCEditorCacheKey> KeysToRemove;
    for (const TPair<FDWCEditorCacheKey, TSharedPtr<FDWCEditorCacheEntry>>& Pair : Entries)
    {
        if (Pair.Key.Owner == OwnerKey && Pair.Key.Namespace == Namespace &&
            (MaterialSlotIndex == INDEX_NONE ||
             Pair.Key.MaterialSlotIndex == MaterialSlotIndex))
        {
            KeysToRemove.Add(Pair.Key);
        }
    }
    for (const FDWCEditorCacheKey& Key : KeysToRemove)
    {
        RemoveEntry(Key, false);
    }
}

void FDWCEditorCacheStore::InvalidateNamespace(const FName Namespace)
{
    check(IsInGameThread());
    CleanupRetiredEntries();

    TArray<FDWCEditorCacheKey> KeysToRemove;
    for (const TPair<FDWCEditorCacheKey, TSharedPtr<FDWCEditorCacheEntry>>& Pair : Entries)
    {
        if (Pair.Key.Namespace == Namespace)
        {
            KeysToRemove.Add(Pair.Key);
        }
    }
    for (const FDWCEditorCacheKey& Key : KeysToRemove)
    {
        RemoveEntry(Key, false);
    }
}

void FDWCEditorCacheStore::Reset()
{
    check(IsInGameThread());
    CleanupRetiredEntries();
    for (const TPair<FDWCEditorCacheKey, TSharedPtr<FDWCEditorCacheEntry>>& Pair : Entries)
    {
        TrackRetiredEntry(Pair.Value);
    }
    Entries.Reset();
    UsedBytes = 0;
    UseSerial = 0;
}

void FDWCEditorCacheStore::TrimToBudget()
{
    check(IsInGameThread());
    CleanupRetiredEntries();

    while (GetTrackedBytes() > BudgetBytes && !Entries.IsEmpty())
    {
        const FDWCEditorCacheKey* OldestKey = nullptr;
        uint64 OldestSerial = TNumericLimits<uint64>::Max();
        for (const TPair<FDWCEditorCacheKey, TSharedPtr<FDWCEditorCacheEntry>>& Pair : Entries)
        {
            if (!Pair.Value.IsValid() || Pair.Value->ActiveLeaseCount.GetValue() > 0)
            {
                continue;
            }

            if (Pair.Value->LastUsedSerial < OldestSerial)
            {
                OldestSerial = Pair.Value->LastUsedSerial;
                OldestKey = &Pair.Key;
            }
        }

        if (OldestKey == nullptr)
        {
            break;
        }
        const FDWCEditorCacheKey KeyCopy = *OldestKey;
        RemoveEntry(KeyCopy, true);
    }
}

int32 FDWCEditorCacheStore::GetActiveLeaseCount() const
{
    int32 ActiveLeaseCount = 0;
    for (const TPair<FDWCEditorCacheKey, TSharedPtr<FDWCEditorCacheEntry>>& Pair : Entries)
    {
        if (Pair.Value.IsValid())
        {
            ActiveLeaseCount += FMath::Max(Pair.Value->ActiveLeaseCount.GetValue(), 0);
        }
    }
    for (const TWeakPtr<FDWCEditorCacheEntry>& RetiredEntry : RetiredEntries)
    {
        if (const TSharedPtr<FDWCEditorCacheEntry> Entry = RetiredEntry.Pin())
        {
            ActiveLeaseCount += FMath::Max(Entry->ActiveLeaseCount.GetValue(), 0);
        }
    }
    return ActiveLeaseCount;
}

uint64 FDWCEditorCacheStore::GetRetiredBytes() const
{
    uint64 RetiredBytes = 0;
    for (const TWeakPtr<FDWCEditorCacheEntry>& RetiredEntry : RetiredEntries)
    {
        if (const TSharedPtr<FDWCEditorCacheEntry> Entry = RetiredEntry.Pin();
            Entry.IsValid() && Entry->ActiveLeaseCount.GetValue() > 0)
        {
            RetiredBytes += Entry->ResidentBytes;
        }
    }
    return RetiredBytes;
}

int32 FDWCEditorCacheStore::GetRetiredEntryCount() const
{
    int32 RetiredCount = 0;
    for (const TWeakPtr<FDWCEditorCacheEntry>& RetiredEntry : RetiredEntries)
    {
        if (const TSharedPtr<FDWCEditorCacheEntry> Entry = RetiredEntry.Pin();
            Entry.IsValid() && Entry->ActiveLeaseCount.GetValue() > 0)
        {
            ++RetiredCount;
        }
    }
    return RetiredCount;
}

void FDWCEditorCacheStore::AppendDiagnosticMemoryBucket(
    TArray<FDWCEditorPreviewMemoryBucket>& OutBuckets) const
{
    FDWCEditorPreviewMemoryBucket& Bucket = OutBuckets.AddDefaulted_GetRef();
    Bucket.Name = TEXT("Shared WCA editor cache");
    Bucket.UsedBytes = GetUsedBytes();
    Bucket.BudgetBytes = BudgetBytes;
    Bucket.EntryCount = Entries.Num();
    Bucket.ActiveLeaseCount = GetActiveLeaseCount();
    Bucket.RetiredEntryCount = GetRetiredEntryCount();
    Bucket.HitCount = HitCount;
    Bucket.MissCount = MissCount;
    Bucket.EvictionCount = EvictionCount;
    Bucket.GlobalOwnerIdentifier = FString::Printf(TEXT("CacheStore/%p"), static_cast<const void*>(this));
    Bucket.GlobalCategory = EDWCEditorMemoryCategory::SharedCacheCPU;
    Bucket.bIncludeInGlobalSnapshot = true;
}

uint64 FDWCEditorCacheStore::GetReclaimableBytes() const
{
    check(IsInGameThread());
    uint64 ReclaimableBytes = 0;
    for (const TPair<FDWCEditorCacheKey, TSharedPtr<FDWCEditorCacheEntry>>& Pair : Entries)
    {
        if (Pair.Value.IsValid() && Pair.Value->ActiveLeaseCount.GetValue() <= 0)
        {
            ReclaimableBytes += Pair.Value->ResidentBytes;
        }
    }
    return ReclaimableBytes;
}

uint64 FDWCEditorCacheStore::ReclaimUnleasedBytes(const uint64 TargetBytes)
{
    check(IsInGameThread());
    CleanupRetiredEntries();
    const uint64 BeforeBytes = GetTrackedBytes();
    while (BeforeBytes >= GetTrackedBytes() &&
           BeforeBytes - GetTrackedBytes() < TargetBytes &&
           EvictOldestUnleased())
    {
    }
    return BeforeBytes >= GetTrackedBytes() ? BeforeBytes - GetTrackedBytes() : 0;
}

void FDWCEditorCacheStore::ResetDiagnosticCounters()
{
    HitCount = 0;
    MissCount = 0;
    EvictionCount = 0;
    AdmissionRejectCount = 0;
}

bool FDWCEditorCacheStore::EvictOldestUnleased(const FDWCEditorCacheKey* ExcludedKey)
{
    const FDWCEditorCacheKey* OldestKey = nullptr;
    uint64 OldestSerial = TNumericLimits<uint64>::Max();
    for (const TPair<FDWCEditorCacheKey, TSharedPtr<FDWCEditorCacheEntry>>& Pair : Entries)
    {
        if (ExcludedKey != nullptr && Pair.Key == *ExcludedKey)
        {
            continue;
        }
        if (!Pair.Value.IsValid() || Pair.Value->ActiveLeaseCount.GetValue() > 0)
        {
            continue;
        }
        if (Pair.Value->LastUsedSerial < OldestSerial)
        {
            OldestSerial = Pair.Value->LastUsedSerial;
            OldestKey = &Pair.Key;
        }
    }
    if (OldestKey == nullptr)
    {
        return false;
    }
    const FDWCEditorCacheKey KeyCopy = *OldestKey;
    RemoveEntry(KeyCopy, true);
    return true;
}

void FDWCEditorCacheStore::RemoveEntry(
    const FDWCEditorCacheKey& Key,
    const bool bCountEviction)
{
    if (const TSharedPtr<FDWCEditorCacheEntry>* Entry = Entries.Find(Key))
    {
        TrackRetiredEntry(*Entry);
        UsedBytes -= (*Entry)->ResidentBytes;
        Entries.Remove(Key);
        if (bCountEviction)
        {
            ++EvictionCount;
        }
    }
}

void FDWCEditorCacheStore::CleanupRetiredEntries()
{
    RetiredEntries.RemoveAll([](const TWeakPtr<FDWCEditorCacheEntry>& RetiredEntry)
    {
        const TSharedPtr<FDWCEditorCacheEntry> Entry = RetiredEntry.Pin();
        return !Entry.IsValid() || Entry->ActiveLeaseCount.GetValue() <= 0;
    });
}

void FDWCEditorCacheStore::TrackRetiredEntry(const TSharedPtr<FDWCEditorCacheEntry>& Entry)
{
    if (!Entry.IsValid() || Entry->ActiveLeaseCount.GetValue() <= 0)
    {
        return;
    }

    for (const TWeakPtr<FDWCEditorCacheEntry>& Existing : RetiredEntries)
    {
        if (Existing.Pin() == Entry)
        {
            return;
        }
    }
    RetiredEntries.Add(Entry);
}
