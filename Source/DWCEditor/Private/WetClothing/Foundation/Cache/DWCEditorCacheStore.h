//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "HAL/ThreadSafeCounter.h"
#include "UObject/ObjectKey.h"
#include "WetClothing/Foundation/Async/DWCEditorResourceGovernor.h"

struct FDWCEditorPreviewMemoryBucket;

class IDWCEditorCacheValue
{
  public:
    virtual ~IDWCEditorCacheValue() = default;

    virtual FName GetCacheTypeName() const = 0;
    virtual uint64 GetAllocatedSizeBytes() const = 0;
};

struct FDWCEditorCacheKey
{
    FName Namespace;
    FObjectKey Owner;
    const void* ResourceIdentity = nullptr;
    int32 LODIndex = 0;
    int32 UVChannelIndex = INDEX_NONE;
    int32 MaterialSlotIndex = INDEX_NONE;
    FString Signature;

    bool operator==(const FDWCEditorCacheKey& Other) const
    {
        return Namespace == Other.Namespace &&
            Owner == Other.Owner &&
            ResourceIdentity == Other.ResourceIdentity &&
            LODIndex == Other.LODIndex &&
            UVChannelIndex == Other.UVChannelIndex &&
            MaterialSlotIndex == Other.MaterialSlotIndex &&
            Signature == Other.Signature;
    }

    friend uint32 GetTypeHash(const FDWCEditorCacheKey& Key)
    {
        uint32 Hash = GetTypeHash(Key.Namespace);
        Hash = HashCombine(Hash, GetTypeHash(Key.Owner));
        Hash = HashCombine(Hash, GetTypeHash(Key.ResourceIdentity));
        Hash = HashCombine(Hash, GetTypeHash(Key.LODIndex));
        Hash = HashCombine(Hash, GetTypeHash(Key.UVChannelIndex));
        Hash = HashCombine(Hash, GetTypeHash(Key.MaterialSlotIndex));
        return HashCombine(Hash, GetTypeHash(Key.Signature));
    }
};

/**
 * Shared ownership unit for a cache payload.
 *
 * The cache map may drop its reference during invalidation or eviction, but an
 * active lease keeps this entry, and therefore its immutable payload, alive.
 */
struct FDWCEditorCacheEntry final
{
    TSharedPtr<const IDWCEditorCacheValue, ESPMode::ThreadSafe> Value;
    uint64 PayloadBytes = 0;
    uint64 ResidentBytes = 0;
    uint64 LastUsedSerial = 0;
    FThreadSafeCounter ActiveLeaseCount;
    FDWCEditorMemoryLease MemoryLease;
};

/** Move-only lifetime token for an actively used cache entry. */
class FDWCEditorCacheLease final
{
  public:
    FDWCEditorCacheLease() = default;
    ~FDWCEditorCacheLease() { Reset(); }

    FDWCEditorCacheLease(const FDWCEditorCacheLease&) = delete;
    FDWCEditorCacheLease& operator=(const FDWCEditorCacheLease&) = delete;

    FDWCEditorCacheLease(FDWCEditorCacheLease&& Other) noexcept
        : Entry(MoveTemp(Other.Entry))
    {
    }

    FDWCEditorCacheLease& operator=(FDWCEditorCacheLease&& Other) noexcept
    {
        if (this != &Other)
        {
            Reset();
            Entry = MoveTemp(Other.Entry);
        }
        return *this;
    }

    bool IsValid() const { return Entry.IsValid() && Entry->Value.IsValid(); }
    explicit operator bool() const { return IsValid(); }

    const IDWCEditorCacheValue* GetValue() const
    {
        return Entry.IsValid() ? Entry->Value.Get() : nullptr;
    }

    TSharedPtr<const IDWCEditorCacheValue, ESPMode::ThreadSafe> GetSharedValue() const
    {
        return Entry.IsValid() ? Entry->Value : nullptr;
    }

    template <typename TValue>
    const TValue* GetAs() const
    {
        const IDWCEditorCacheValue* BaseValue = GetValue();
        return BaseValue != nullptr &&
                BaseValue->GetCacheTypeName() == TValue::StaticCacheTypeName()
            ? static_cast<const TValue*>(BaseValue)
            : nullptr;
    }

    uint64 GetAllocatedSizeBytes() const
    {
        return Entry.IsValid() ? Entry->ResidentBytes : 0;
    }

    uint32 GetActiveLeaseCount() const
    {
        return Entry.IsValid()
            ? static_cast<uint32>(FMath::Max(Entry->ActiveLeaseCount.GetValue(), 0))
            : 0;
    }

    void Reset()
    {
        if (Entry.IsValid())
        {
            Entry->ActiveLeaseCount.Decrement();
            Entry.Reset();
        }
    }

  private:
    friend class FDWCEditorCacheStore;

    explicit FDWCEditorCacheLease(TSharedPtr<FDWCEditorCacheEntry> InEntry)
        : Entry(MoveTemp(InEntry))
    {
        if (Entry.IsValid())
        {
            Entry->ActiveLeaseCount.Increment();
        }
    }

    TSharedPtr<FDWCEditorCacheEntry> Entry;
};

/**
 * WCA editor-instance cache with one byte budget and LRU policy.
 * Values are immutable after insertion and may remain alive through client handles after eviction.
 */
class FDWCEditorCacheStore final
{
  public:
    static constexpr uint64 DefaultBudgetBytes = 64ull * 1024ull * 1024ull;

    explicit FDWCEditorCacheStore(uint64 InBudgetBytes = DefaultBudgetBytes);
    FDWCEditorCacheStore(
        TSharedRef<FDWCEditorResourceGovernor> InResourceGovernor,
        const FGuid& InSessionEpoch,
        uint64 InBudgetBytes = DefaultBudgetBytes);

    template <typename TValue>
    TSharedPtr<const TValue, ESPMode::ThreadSafe> Find(const FDWCEditorCacheKey& Key)
    {
        check(IsInGameThread());
        CleanupRetiredEntries();

        const TSharedPtr<FDWCEditorCacheEntry>* EntryPtr = Entries.Find(Key);
        if (EntryPtr == nullptr || !EntryPtr->IsValid() ||
            !(*EntryPtr)->Value.IsValid() ||
            (*EntryPtr)->Value->GetCacheTypeName() != TValue::StaticCacheTypeName())
        {
            ++MissCount;
            return nullptr;
        }

        ++HitCount;
        (*EntryPtr)->LastUsedSerial = ++UseSerial;
        return StaticCastSharedPtr<const TValue>((*EntryPtr)->Value);
    }

    /** Acquires an active lifetime lease for an existing cache entry. */
    FDWCEditorCacheLease AcquireLease(const FDWCEditorCacheKey& Key);

    template <typename TValue>
    FDWCEditorCacheLease FindLease(const FDWCEditorCacheKey& Key)
    {
        FDWCEditorCacheLease Lease = AcquireLease(Key);
        if (!Lease.IsValid() || Lease.GetAs<TValue>() == nullptr)
        {
            Lease.Reset();
        }
        return Lease;
    }

    bool Put(
        const FDWCEditorCacheKey& Key,
        TSharedRef<const IDWCEditorCacheValue, ESPMode::ThreadSafe> Value);
    void InvalidateOwner(const UObject* Owner);
    void InvalidateOwnerNamespace(
        const UObject* Owner,
        FName Namespace,
        int32 MaterialSlotIndex = INDEX_NONE);
    void InvalidateNamespace(FName Namespace);
    void Reset();
    void TrimToBudget();

    /** Tracked cache memory, including active retired entries held by leases. */
    uint64 GetUsedBytes() const { return UsedBytes + GetRetiredBytes(); }
    uint64 GetRetiredBytes() const;
    uint64 GetBudgetBytes() const { return BudgetBytes; }
    int32 GetEntryCount() const { return Entries.Num(); }
    int32 GetRetiredEntryCount() const;
    int32 GetActiveLeaseCount() const;
    void AppendDiagnosticMemoryBucket(TArray<FDWCEditorPreviewMemoryBucket>& OutBuckets) const;
    void ResetDiagnosticCounters();

  private:
    void CleanupRetiredEntries();
    void TrackRetiredEntry(const TSharedPtr<FDWCEditorCacheEntry>& Entry);
    bool EvictOldestUnleased();
    uint64 GetTrackedBytes() const { return UsedBytes + GetRetiredBytes(); }
    void RemoveEntry(const FDWCEditorCacheKey& Key, bool bCountEviction);

    TMap<FDWCEditorCacheKey, TSharedPtr<FDWCEditorCacheEntry>> Entries;
    TArray<TWeakPtr<FDWCEditorCacheEntry>> RetiredEntries;
    TSharedPtr<FDWCEditorResourceGovernor> ResourceGovernor;
    FDWCEditorAsyncOperationIdentity MemoryOwner;
    uint64 BudgetBytes = DefaultBudgetBytes;
    uint64 UsedBytes = 0;
    uint64 UseSerial = 0;
    uint64 HitCount = 0;
    uint64 MissCount = 0;
    uint64 EvictionCount = 0;
    uint64 AdmissionRejectCount = 0;
};
