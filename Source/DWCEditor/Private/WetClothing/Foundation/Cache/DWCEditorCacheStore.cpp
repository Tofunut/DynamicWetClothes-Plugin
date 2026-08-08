// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "WetClothing/Foundation/Cache/DWCEditorCacheStore.h"

#include "WetClothing/Foundation/Preview/Diagnostics/DWCEditorPreviewDiagnostics.h"

FDWCEditorCacheStore::FDWCEditorCacheStore(const uint64 InBudgetBytes)
    : BudgetBytes(FMath::Max<uint64>(InBudgetBytes, 1))
{
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

void FDWCEditorCacheStore::Put(
    const FDWCEditorCacheKey&                                   Key,
    TSharedRef<const IDWCEditorCacheValue, ESPMode::ThreadSafe> Value)
{
    check(IsInGameThread());
    CleanupRetiredEntries();

    if (const TSharedPtr<FDWCEditorCacheEntry>* Existing = Entries.Find(Key))
    {
        TrackRetiredEntry(*Existing);
        UsedBytes -= (*Existing)->ResidentBytes;
    }

    TSharedPtr<FDWCEditorCacheEntry> Entry = MakeShared<FDWCEditorCacheEntry>();
    Entry->Value = Value;
    Entry->PayloadBytes = Value->GetAllocatedSizeBytes();
    Entry->ResidentBytes =
        static_cast<uint64>(sizeof(FDWCEditorCacheEntry)) +
        static_cast<uint64>(sizeof(FDWCEditorCacheKey)) +
        static_cast<uint64>(Key.Signature.GetAllocatedSize()) +
        Entry->PayloadBytes;
    Entry->LastUsedSerial = ++UseSerial;
    Entries.Add(Key, Entry);
    UsedBytes += Entry->ResidentBytes;
    TrimToBudget();
}

void FDWCEditorCacheStore::InvalidateOwner(const UObject* Owner)
{
    check(IsInGameThread());
    CleanupRetiredEntries();
    if (Owner == nullptr)
    {
        return;
    }

    const FObjectKey           OwnerKey(Owner);
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
        uint64                    OldestSerial = TNumericLimits<uint64>::Max();
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

uint64 FDWCEditorCacheStore::GetPayloadBytes() const
{
    uint64 PayloadBytes = 0;
    for (const TPair<FDWCEditorCacheKey, TSharedPtr<FDWCEditorCacheEntry>>& Pair : Entries)
    {
        if (Pair.Value.IsValid())
        {
            PayloadBytes += Pair.Value->PayloadBytes;
        }
    }
    for (const TWeakPtr<FDWCEditorCacheEntry>& RetiredEntry : RetiredEntries)
    {
        if (const TSharedPtr<FDWCEditorCacheEntry> Entry = RetiredEntry.Pin())
        {
            PayloadBytes += Entry->PayloadBytes;
        }
    }
    return PayloadBytes;
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
}

void FDWCEditorCacheStore::ResetDiagnosticCounters()
{
    HitCount = 0;
    MissCount = 0;
    EvictionCount = 0;
}

void FDWCEditorCacheStore::RemoveEntry(
    const FDWCEditorCacheKey& Key,
    const bool                bCountEviction)
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
        return !Entry.IsValid() || Entry->ActiveLeaseCount.GetValue() <= 0; });
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
