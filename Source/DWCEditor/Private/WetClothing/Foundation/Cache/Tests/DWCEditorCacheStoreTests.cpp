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

    TestTrue(TEXT("Inserted value is reused"), Store.Find<FTestCacheValue>(Key).IsValid());
    TestTrue(TEXT("Store tracks payload and entry memory"), Store.GetUsedBytes() >= static_cast<uint64>(128));
    Store.InvalidateOwner(Owner);
    TestFalse(TEXT("Owner invalidation removes the value"), Store.Find<FTestCacheValue>(Key).IsValid());
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
    TSharedRef<FTestCacheValue, ESPMode::ThreadSafe> PinnedValue =
        MakeShared<FTestCacheValue, ESPMode::ThreadSafe>(PayloadBytes);
    TSharedRef<const IDWCEditorCacheValue, ESPMode::ThreadSafe> BasePinnedValue = PinnedValue;
    Store.Put(FirstKey, BasePinnedValue);
    FDWCEditorCacheLease Lease = Store.FindLease<FTestCacheValue>(FirstKey);
    TestTrue(TEXT("Lease pins the first cache entry"), Lease.IsValid());
    TestEqual(TEXT("Cache tracks the active lease"), Store.GetActiveLeaseCount(), 1);

    Store.Put(SecondKey, MakeShared<FTestCacheValue, ESPMode::ThreadSafe>(PayloadBytes));

    TestTrue(TEXT("The cache map stays within budget after evicting an unleased entry"),
        Store.GetUsedBytes() <= Store.GetBudgetBytes());
    TestTrue(TEXT("The actively leased entry remains cached"), Store.Find<FTestCacheValue>(FirstKey).IsValid());
    TestFalse(TEXT("The unleased entry is evicted when the pinned entry cannot be removed"),
        Store.Find<FTestCacheValue>(SecondKey).IsValid());
    TestEqual(TEXT("The caller's leased payload remains valid"), PinnedValue->Bytes, PayloadBytes);

    Lease.Reset();
    TestEqual(TEXT("Releasing the lease updates the cache count"), Store.GetActiveLeaseCount(), 0);
    Store.Put(SecondKey, MakeShared<FTestCacheValue, ESPMode::ThreadSafe>(PayloadBytes));
    TestFalse(TEXT("The former entry becomes evictable after lease release"),
        Store.Find<FTestCacheValue>(FirstKey).IsValid());
    TestTrue(TEXT("The new entry remains cached after release"), Store.Find<FTestCacheValue>(SecondKey).IsValid());
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
        Store.Find<FTestCacheValue>(Key).IsValid());
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

#endif
