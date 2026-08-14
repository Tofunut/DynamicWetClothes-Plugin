//Copyright 2026 Team Tofunut. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS

#include "Async/Async.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"

#include "Engine/Texture2D.h"
#include "WetClothing/Foundation/Cache/DWCEditorCacheStore.h"
#include "WetClothing/Foundation/Preview/Diagnostics/DWCEditorPreviewDiagnostics.h"

namespace
{
    struct FTestCacheValue final : IDWCEditorCacheValue
    {
        explicit FTestCacheValue(const uint64 InBytes) : Bytes(InBytes) {}

        static FName StaticCacheTypeName() { return TEXT("DWCEditorTestCacheValue"); }
        virtual FName GetCacheTypeName() const override { return StaticCacheTypeName(); }
        virtual uint64 GetAllocatedSizeBytes() const override { return Bytes; }

        uint64 Bytes = 0;
    };

    FDWCEditorResourceBudgetConfig MakeCacheGovernorBudget(const uint64 Bytes)
    {
        FDWCEditorResourceBudgetConfig Config;
        Config.GlobalEditorCPUBytes = Bytes;
        Config.WorkerPrivateCPUBytes = Bytes;
        Config.PreviewWorkspaceCPUBytes = Bytes;
        Config.SharedCacheCPUBytes = Bytes;
        Config.UploadStagingCPUBytes = Bytes;
        Config.PreviewGPUBytes = Bytes;
        return Config;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorCacheStoreReuseTest,
    "DWC.Editor.Foundation.Cache.ReuseAndInvalidation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorCacheStoreReuseTest::RunTest(const FString& Parameters)
{
    FDWCEditorCacheStore Store(1024);
    UTexture2D* Owner = NewObject<UTexture2D>();
    FDWCEditorCacheKey Key;
    Key.Namespace = TEXT("Test");
    Key.Owner = FObjectKey(Owner);
    Key.MaterialSlotIndex = 2;

    TSharedRef<FTestCacheValue, ESPMode::ThreadSafe> Value =
        MakeShared<FTestCacheValue, ESPMode::ThreadSafe>(128);
    TSharedRef<const IDWCEditorCacheValue, ESPMode::ThreadSafe> BaseValue = Value;
    Store.Put(Key, BaseValue);

    TestTrue(TEXT("Inserted value is reused"), Store.Contains<FTestCacheValue>(Key));
    TestTrue(TEXT("Store tracks payload and entry memory"), Store.GetUsedBytes() >= static_cast<uint64>(128));
    Store.InvalidateOwner(Owner);
    TestFalse(TEXT("Owner invalidation removes the value"), Store.Contains<FTestCacheValue>(Key));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorCacheStoreScopedInvalidationTest,
    "DWC.Editor.Foundation.Cache.ScopedOwnerNamespaceInvalidation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorCacheStoreScopedInvalidationTest::RunTest(const FString& Parameters)
{
    FDWCEditorCacheStore Store(1024 * 1024);
    UTexture2D* Owner = NewObject<UTexture2D>();
    FDWCEditorCacheKey SlotOne;
    SlotOne.Namespace = TEXT("WrinkleCoverage");
    SlotOne.Owner = FObjectKey(Owner);
    SlotOne.MaterialSlotIndex = 1;
    FDWCEditorCacheKey SlotTwo = SlotOne;
    SlotTwo.MaterialSlotIndex = 2;
    FDWCEditorCacheKey OtherNamespace = SlotOne;
    OtherNamespace.Namespace = TEXT("Spatial");

    Store.Put(SlotOne, MakeShared<FTestCacheValue, ESPMode::ThreadSafe>(64));
    Store.Put(SlotTwo, MakeShared<FTestCacheValue, ESPMode::ThreadSafe>(64));
    Store.Put(OtherNamespace, MakeShared<FTestCacheValue, ESPMode::ThreadSafe>(64));
    Store.InvalidateOwnerNamespace(Owner, SlotOne.Namespace, SlotOne.MaterialSlotIndex);

    TestFalse(TEXT("The requested slot is invalidated."),
        Store.Contains<FTestCacheValue>(SlotOne));
    TestTrue(TEXT("Another slot in the same namespace remains cached."),
        Store.Contains<FTestCacheValue>(SlotTwo));
    TestTrue(TEXT("Another namespace for the same owner remains cached."),
        Store.Contains<FTestCacheValue>(OtherNamespace));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorCacheStoreResourceIdentityInvalidationTest,
    "DWC.Editor.Foundation.Cache.ResourceIdentityInvalidation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorCacheStoreResourceIdentityInvalidationTest::RunTest(const FString& Parameters)
{
    FDWCEditorCacheStore Store(1024 * 1024);
    UTexture2D* Owner = NewObject<UTexture2D>();
    UTexture2D* FirstResource = NewObject<UTexture2D>();
    UTexture2D* SecondResource = NewObject<UTexture2D>();

    FDWCEditorCacheKey FirstKey;
    FirstKey.Namespace = TEXT("UVTopology");
    FirstKey.Owner = FObjectKey(Owner);
    FirstKey.ResourceIdentity = FirstResource;
    FirstKey.MaterialSlotIndex = 1;

    FDWCEditorCacheKey SecondKey = FirstKey;
    SecondKey.ResourceIdentity = SecondResource;
    SecondKey.MaterialSlotIndex = 2;

    Store.Put(FirstKey, MakeShared<FTestCacheValue, ESPMode::ThreadSafe>(64));
    Store.Put(SecondKey, MakeShared<FTestCacheValue, ESPMode::ThreadSafe>(64));
    FDWCEditorCacheLease FirstLease = Store.FindLease<FTestCacheValue>(FirstKey);

    Store.InvalidateResourceIdentity(FirstResource, FirstKey.Namespace);

    TestFalse(TEXT("The matching resource entry is removed from the cache index."),
        Store.Contains<FTestCacheValue>(FirstKey));
    TestTrue(TEXT("A different resource remains cached."),
        Store.Contains<FTestCacheValue>(SecondKey));
    TestTrue(TEXT("An active lease keeps an invalidated resource payload alive."),
        FirstLease.IsValid());
    TestEqual(TEXT("The leased resource is tracked as retired."),
        Store.GetRetiredEntryCount(), 1);

    FirstLease.Reset();
    TestEqual(TEXT("Releasing the lease clears retired resource accounting."),
        Store.GetRetiredEntryCount(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorCacheStorePinnedEntryEvictionTest,
    "DWC.Editor.Foundation.Cache.PinnedEntryEviction",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorCacheStorePinnedEntryEvictionTest::RunTest(const FString& Parameters)
{
    constexpr uint64 PayloadBytes = 512ull * 1024ull;
    FDWCEditorCacheStore Store(1024ull * 1024ull);
    UTexture2D* Owner = NewObject<UTexture2D>();
    FDWCEditorCacheKey FirstKey;
    FirstKey.Namespace = TEXT("Test");
    FirstKey.Owner = FObjectKey(Owner);
    FirstKey.MaterialSlotIndex = 1;

    FDWCEditorCacheKey SecondKey = FirstKey;
    SecondKey.MaterialSlotIndex = 2;

    // Keep an active lease alive while inserting a newer entry. The cache must
    // not evict the entry that is actively used by a spatial query.
    Store.Put(FirstKey, MakeShared<FTestCacheValue, ESPMode::ThreadSafe>(PayloadBytes));
    FDWCEditorCacheLease Lease = Store.FindLease<FTestCacheValue>(FirstKey);
    TestTrue(TEXT("Lease pins the first cache entry"), Lease.IsValid());
    TestEqual(TEXT("Cache tracks the active lease"), Store.GetActiveLeaseCount(), 1);

    Store.Put(SecondKey, MakeShared<FTestCacheValue, ESPMode::ThreadSafe>(PayloadBytes));

    TestTrue(TEXT("The cache map stays within budget after evicting an unleased entry"),
        Store.GetUsedBytes() <= Store.GetBudgetBytes());
    TestTrue(TEXT("The actively leased entry remains cached"), Store.Contains<FTestCacheValue>(FirstKey));
    TestFalse(TEXT("The unleased entry is evicted when the pinned entry cannot be removed"),
        Store.Contains<FTestCacheValue>(SecondKey));
    const FTestCacheValue* LeasedValue = Lease.GetAs<FTestCacheValue>();
    TestNotNull(TEXT("The caller's leased payload remains valid"), LeasedValue);
    if (LeasedValue != nullptr)
    {
        TestEqual(TEXT("The leased payload preserves its data"), LeasedValue->Bytes, PayloadBytes);
    }

    Lease.Reset();
    TestEqual(TEXT("Releasing the lease updates the cache count"), Store.GetActiveLeaseCount(), 0);
    Store.Put(SecondKey, MakeShared<FTestCacheValue, ESPMode::ThreadSafe>(PayloadBytes));
    TestFalse(TEXT("The former entry becomes evictable after lease release"),
        Store.Contains<FTestCacheValue>(FirstKey));
    TestTrue(TEXT("The new entry remains cached after release"), Store.Contains<FTestCacheValue>(SecondKey));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorCacheStoreInvalidationLeaseTest,
    "DWC.Editor.Foundation.Cache.InvalidationLeaseLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorCacheStoreInvalidationLeaseTest::RunTest(const FString& Parameters)
{
    FDWCEditorCacheStore Store(1024ull * 1024ull);
    UTexture2D* Owner = NewObject<UTexture2D>();
    FDWCEditorCacheKey Key;
    Key.Namespace = TEXT("Test");
    Key.Owner = FObjectKey(Owner);
    Key.MaterialSlotIndex = 3;

    constexpr uint64 PayloadBytes = 256ull * 1024ull;
    TSharedRef<FTestCacheValue, ESPMode::ThreadSafe> Value =
        MakeShared<FTestCacheValue, ESPMode::ThreadSafe>(PayloadBytes);
    Store.Put(Key, Value);

    FDWCEditorCacheLease Lease = Store.FindLease<FTestCacheValue>(Key);
    TestTrue(TEXT("Invalidation test acquires a lease"), Lease.IsValid());

    Store.InvalidateOwner(Owner);
    TestFalse(TEXT("Invalidation removes the entry from the cache index"),
        Store.Contains<FTestCacheValue>(Key));
    TestTrue(TEXT("The active lease keeps the invalidated payload alive"), Lease.IsValid());
    TestEqual(TEXT("Invalidated entry is reported as retired"), Store.GetRetiredEntryCount(), 1);
    TestTrue(TEXT("Retired payload remains included in the memory estimate"),
        Store.GetRetiredBytes() >= PayloadBytes);

    Lease.Reset();
    TestEqual(TEXT("Released invalidated entry is no longer retired"), Store.GetRetiredEntryCount(), 0);
    TestEqual(TEXT("Released invalidated entry no longer contributes memory"), Store.GetRetiredBytes(), 0ull);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorCacheStoreAsyncLeaseLifetimeTest,
    "DWC.Editor.Foundation.Cache.AsyncLeaseLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorCacheStoreAsyncLeaseLifetimeTest::RunTest(const FString& Parameters)
{
    FDWCEditorCacheStore Store(1024ull * 1024ull);
    UTexture2D* Owner = NewObject<UTexture2D>();
    FDWCEditorCacheKey Key;
    Key.Namespace = TEXT("Test");
    Key.Owner = FObjectKey(Owner);
    Key.MaterialSlotIndex = 4;

    TSharedRef<FTestCacheValue, ESPMode::ThreadSafe> Value =
        MakeShared<FTestCacheValue, ESPMode::ThreadSafe>(256ull * 1024ull);
    Store.Put(Key, Value);

    FDWCEditorCacheLease Lease = Store.FindLease<FTestCacheValue>(Key);
    TestTrue(TEXT("Async test acquires a lease"), Lease.IsValid());
    TSharedPtr<const IDWCEditorCacheValue, ESPMode::ThreadSafe> Payload = Lease.GetSharedValue();

    FEvent* WorkerStarted = FPlatformProcess::GetSynchEventFromPool(false);
    FEvent* AllowWorkerRelease = FPlatformProcess::GetSynchEventFromPool(false);
    TFuture<void> Worker = Async(
        EAsyncExecution::ThreadPool,
        [WorkerLease = MoveTemp(Lease), WorkerStarted, AllowWorkerRelease]() mutable
        {
            WorkerStarted->Trigger();
            AllowWorkerRelease->Wait();
            WorkerLease.Reset();
        });

    const bool bWorkerStarted = WorkerStarted->Wait(5000);
    TestTrue(TEXT("Async worker starts while holding the lease"), bWorkerStarted);

    // Simulate preview/session teardown while the worker still owns the
    // immutable spatial payload.
    Store.Reset();
    TestEqual(TEXT("Reset removes resident cache entries"), Store.GetEntryCount(), 0);
    TestEqual(TEXT("The retired entry remains pinned by the worker"), Store.GetActiveLeaseCount(), 1);
    TestTrue(TEXT("Worker-owned payload remains accounted while retired"), Store.GetUsedBytes() > 0);
    TestTrue(TEXT("The payload object remains valid after cache reset"), Payload.IsValid());

    AllowWorkerRelease->Trigger();
    Worker.Wait();
    TestEqual(TEXT("Worker lease release clears the active lease count"), Store.GetActiveLeaseCount(), 0);
    TestEqual(TEXT("Worker lease release clears retired memory"), Store.GetRetiredBytes(), 0ull);

    FPlatformProcess::ReturnSynchEventToPool(WorkerStarted);
    FPlatformProcess::ReturnSynchEventToPool(AllowWorkerRelease);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorCacheStoreGovernorLeaseLifetimeTest,
    "DWC.Editor.Foundation.Cache.ResourceGovernor.InvalidationLeaseLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorCacheStoreGovernorLeaseLifetimeTest::RunTest(const FString&)
{
    constexpr uint64 BudgetBytes = 4096;
    const TSharedRef<FDWCEditorResourceGovernor> Governor =
        MakeShared<FDWCEditorResourceGovernor>(MakeCacheGovernorBudget(BudgetBytes));
    FDWCEditorCacheStore Store(Governor, FGuid::NewGuid(), BudgetBytes);
    UTexture2D* Owner = NewObject<UTexture2D>();
    FDWCEditorCacheKey Key;
    Key.Namespace = TEXT("GovernorLifetime");
    Key.Owner = FObjectKey(Owner);
    Key.MaterialSlotIndex = 5;

    TestTrue(TEXT("Governor-backed cache entry is admitted"),
        Store.Put(Key, MakeShared<FTestCacheValue, ESPMode::ThreadSafe>(128)));
    const uint64 ResidentBytes = Store.GetUsedBytes();
    TestTrue(TEXT("The cache reports resident bytes"), ResidentBytes > 128);
    TestEqual(TEXT("The governor owns the same resident cache bytes"),
        Governor->GetDiagnostics().GlobalCPUUsedBytes, ResidentBytes);

    FDWCEditorCacheLease Lease = Store.FindLease<FTestCacheValue>(Key);
    TestTrue(TEXT("The cache payload can be leased"), Lease.IsValid());
    Store.InvalidateOwner(Owner);
    TestFalse(TEXT("Invalidation removes the cache lookup"), Store.Contains<FTestCacheValue>(Key));
    TestEqual(TEXT("A retired leased entry keeps its governor reservation"),
        Governor->GetDiagnostics().GlobalCPUUsedBytes, ResidentBytes);

    Lease.Reset();
    Store.TrimToBudget();
    const FDWCEditorResourceGovernorDiagnostics Diagnostics = Governor->GetDiagnostics();
    TestEqual(TEXT("The last lease returns retired cache memory"), Diagnostics.GlobalCPUUsedBytes, 0ull);
    TestEqual(TEXT("No cache reservation remains after lease release"), Diagnostics.Reservations.Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorCacheStoreGovernorLRUTest,
    "DWC.Editor.Foundation.Cache.ResourceGovernor.LRUEviction",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorCacheStoreGovernorLRUTest::RunTest(const FString&)
{
    constexpr uint64 LocalBudgetBytes = 700;
    constexpr uint64 GovernorBudgetBytes = 4096;
    const TSharedRef<FDWCEditorResourceGovernor> Governor =
        MakeShared<FDWCEditorResourceGovernor>(MakeCacheGovernorBudget(GovernorBudgetBytes));
    FDWCEditorCacheStore Store(Governor, FGuid::NewGuid(), LocalBudgetBytes);
    UTexture2D* Owner = NewObject<UTexture2D>();
    FDWCEditorCacheKey FirstKey;
    FirstKey.Namespace = TEXT("GovernorLRU");
    FirstKey.Owner = FObjectKey(Owner);
    FirstKey.MaterialSlotIndex = 1;
    FDWCEditorCacheKey SecondKey = FirstKey;
    SecondKey.MaterialSlotIndex = 2;

    TestTrue(TEXT("First LRU entry is admitted"),
        Store.Put(FirstKey, MakeShared<FTestCacheValue, ESPMode::ThreadSafe>(256)));
    TestTrue(TEXT("Second LRU entry is admitted after evicting the first"),
        Store.Put(SecondKey, MakeShared<FTestCacheValue, ESPMode::ThreadSafe>(256)));
    TestFalse(TEXT("The least recently used entry was evicted"),
        Store.Contains<FTestCacheValue>(FirstKey));
    TestTrue(TEXT("The newest entry remains resident"),
        Store.Contains<FTestCacheValue>(SecondKey));
    TestTrue(TEXT("The local cache remains within its byte budget"),
        Store.GetUsedBytes() <= LocalBudgetBytes);
    TestEqual(TEXT("Governor accounting matches the remaining cache entry"),
        Governor->GetDiagnostics().GlobalCPUUsedBytes, Store.GetUsedBytes());

    Store.Reset();
    TestEqual(TEXT("Reset returns the final cache reservation"),
        Governor->GetDiagnostics().GlobalCPUUsedBytes, 0ull);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorCacheStoreAtomicReplacementTest,
    "DWC.Editor.Foundation.Cache.AtomicReplacement",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorCacheStoreAtomicReplacementTest::RunTest(const FString&)
{
    constexpr uint64 BudgetBytes = 1024;
    const TSharedRef<FDWCEditorResourceGovernor> Governor =
        MakeShared<FDWCEditorResourceGovernor>(MakeCacheGovernorBudget(BudgetBytes));
    FDWCEditorCacheStore Store(Governor, FGuid::NewGuid(), BudgetBytes);
    UTexture2D* Owner = NewObject<UTexture2D>();
    FDWCEditorCacheKey Key;
    Key.Namespace = TEXT("AtomicReplacement");
    Key.Owner = FObjectKey(Owner);

    TestTrue(TEXT("Baseline cache entry is admitted"),
        Store.Put(Key, MakeShared<FTestCacheValue, ESPMode::ThreadSafe>(128)));
    FDWCEditorCacheLease OriginalLease = Store.FindLease<FTestCacheValue>(Key);
    const FTestCacheValue* OriginalValue = OriginalLease.GetAs<FTestCacheValue>();
    TestNotNull(TEXT("Baseline value is leased"), OriginalValue);

    TestFalse(TEXT("Oversized replacement is rejected"),
        Store.Put(Key, MakeShared<FTestCacheValue, ESPMode::ThreadSafe>(2048)));
    FDWCEditorCacheLease PreservedLease = Store.FindLease<FTestCacheValue>(Key);
    const FTestCacheValue* PreservedValue = PreservedLease.GetAs<FTestCacheValue>();
    TestNotNull(TEXT("Rejected replacement preserves the indexed entry"), PreservedValue);
    if (PreservedValue != nullptr)
    {
        TestEqual(TEXT("Rejected replacement preserves the original payload"),
            PreservedValue->Bytes, 128ull);
    }
    TestEqual(TEXT("Rejected replacement is diagnosed once"), Store.GetAdmissionRejectCount(), 1ull);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorCacheStoreLeaseBackedAccessTest,
    "DWC.Editor.Foundation.Cache.LeaseBackedAccess",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorCacheStoreLeaseBackedAccessTest::RunTest(const FString&)
{
    FDWCEditorCacheStore Store(1024 * 1024);
    UTexture2D* Owner = NewObject<UTexture2D>();
    FDWCEditorCacheKey Key;
    Key.Namespace = TEXT("LeaseBackedAccess");
    Key.Owner = FObjectKey(Owner);

    TestTrue(TEXT("Cache entry is inserted"),
        Store.Put(Key, MakeShared<FTestCacheValue, ESPMode::ThreadSafe>(256)));
    TestTrue(TEXT("Presence can be queried without exposing payload ownership"),
        Store.Contains<FTestCacheValue>(Key));
    FDWCEditorCacheLease Lease = Store.FindLease<FTestCacheValue>(Key);
    TestTrue(TEXT("Payload access is backed by an active entry lease"), Lease.IsValid());
    TestEqual(TEXT("One active payload owner is tracked"), Store.GetActiveLeaseCount(), 1);
    return true;
}

#endif
