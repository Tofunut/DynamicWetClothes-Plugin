//Copyright 2026 Team Tofunut. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS

#include "Async/Async.h"
#include "HAL/Event.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"

#include "WetClothing/Foundation/Async/DWCEditorResourceGovernor.h"

namespace
{
    FDWCEditorAsyncOperationIdentity MakeGovernorTestIdentity(const uint64 OperationId)
    {
        FDWCEditorAsyncOperationIdentity Identity;
        Identity.Key.Namespace = TEXT("GovernorTest");
        Identity.SessionEpoch = FGuid::NewGuid();
        Identity.OperationId = OperationId;
        Identity.Generation = 1;
        return Identity;
    }

    FDWCEditorResourceReservationRequest MakeRequest(
        const EDWCEditorResourcePool Pool,
        const uint64 Bytes,
        const uint64 OperationId)
    {
        FDWCEditorResourceReservationRequest Request;
        Request.Pool = Pool;
        Request.Bytes = Bytes;
        Request.Owner = MakeGovernorTestIdentity(OperationId);
        Request.DebugName = FString::Printf(TEXT("GovernorTest_%llu"), OperationId);
        return Request;
    }

    FDWCEditorResourceBudgetConfig MakeSmallBudgetConfig()
    {
        FDWCEditorResourceBudgetConfig Config;
        Config.GlobalEditorCPUBytes = 100;
        Config.WorkerPrivateCPUBytes = 100;
        Config.PreviewWorkspaceCPUBytes = 100;
        Config.SharedCacheCPUBytes = 100;
        Config.UploadStagingCPUBytes = 100;
        Config.PreviewGPUBytes = 100;
        return Config;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorResourceGovernorBudgetTest,
    "DWC.Editor.Foundation.Async.ResourceGovernor.BudgetAdmission",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorResourceGovernorBudgetTest::RunTest(const FString&)
{
    FDWCEditorResourceGovernor Governor(MakeSmallBudgetConfig());
    FDWCEditorMemoryLease WorkerLease = Governor.TryAcquire(
        MakeRequest(EDWCEditorResourcePool::WorkerPrivateCPU, 60, 1));
    TestTrue(TEXT("First CPU reservation is admitted"), WorkerLease.IsValid());

    FString Error;
    FDWCEditorMemoryLease RejectedCPU = Governor.TryAcquire(
        MakeRequest(EDWCEditorResourcePool::PreviewWorkspaceCPU, 50, 2),
        &Error);
    TestFalse(TEXT("Global CPU budget rejects overlapping reservations"), RejectedCPU.IsValid());
    TestFalse(TEXT("Global rejection reports a reason"), Error.IsEmpty());

    FDWCEditorMemoryLease GPULease = Governor.TryAcquire(
        MakeRequest(EDWCEditorResourcePool::PreviewGPU, 80, 3));
    TestTrue(TEXT("GPU reservation is independent from the CPU hard cap"), GPULease.IsValid());

    const FDWCEditorResourceGovernorDiagnostics Diagnostics = Governor.GetDiagnostics();
    TestEqual(TEXT("Governor reports the active CPU reservation"), Diagnostics.GlobalCPUUsedBytes, 60ull);
    TestEqual(TEXT("Governor reports active reservations"), Diagnostics.Reservations.Num(), 2);
    TestEqual(TEXT("Governor records global CPU rejections"), Diagnostics.GlobalCPURejectionCount, 1ull);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorResourceGovernorDeferredAdmissionTest,
    "DWC.Editor.Foundation.Async.ResourceGovernor.DeferredAdmission",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorResourceGovernorDeferredAdmissionTest::RunTest(const FString&)
{
    FDWCEditorResourceGovernor Governor(MakeSmallBudgetConfig());
    FDWCEditorMemoryLease Owner = Governor.TryAcquire(
        MakeRequest(EDWCEditorResourcePool::WorkerPrivateCPU, 100, 10));
    TestTrue(TEXT("The budget owner is admitted"), Owner.IsValid());

    EDWCEditorResourceAdmissionResult AdmissionResult =
        EDWCEditorResourceAdmissionResult::InvalidRequest;
    FString Error;
    FDWCEditorMemoryLease Deferred = Governor.TryAcquireForAdmission(
        MakeRequest(EDWCEditorResourcePool::WorkerPrivateCPU, 1, 11),
        AdmissionResult,
        &Error);
    TestFalse(TEXT("Admission waits while the pool is full"), Deferred.IsValid());
    TestEqual(
        TEXT("Temporary pressure is distinguished from an invalid request"),
        AdmissionResult,
        EDWCEditorResourceAdmissionResult::TemporarilyUnavailable);
    TestFalse(TEXT("Temporary pressure still provides a diagnostic"), Error.IsEmpty());
    const FDWCEditorResourceGovernorDiagnostics DeferredDiagnostics = Governor.GetDiagnostics();
    const FDWCEditorResourcePoolDiagnostics* WorkerPool =
        DeferredDiagnostics.Pools.FindByPredicate(
            [](const FDWCEditorResourcePoolDiagnostics& Pool)
            {
                return Pool.Pool == EDWCEditorResourcePool::WorkerPrivateCPU;
            });
    TestNotNull(TEXT("Worker pool diagnostics are available"), WorkerPool);
    if (WorkerPool != nullptr)
    {
        TestEqual(
            TEXT("A deferred admission does not increment hard rejection diagnostics"),
            WorkerPool->RejectionCount,
            0ull);
    }

    Owner.Reset();
    Deferred = Governor.TryAcquireForAdmission(
        MakeRequest(EDWCEditorResourcePool::WorkerPrivateCPU, 1, 11),
        AdmissionResult,
        &Error);
    TestTrue(TEXT("Admission succeeds after the owner releases memory"), Deferred.IsValid());
    TestEqual(
        TEXT("Successful admission is reported explicitly"),
        AdmissionResult,
        EDWCEditorResourceAdmissionResult::Admitted);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorResourceGovernorLeaseTest,
    "DWC.Editor.Foundation.Async.ResourceGovernor.LeaseAndGrowth",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorResourceGovernorLeaseTest::RunTest(const FString&)
{
    FDWCEditorResourceGovernor Governor(MakeSmallBudgetConfig());
    FDWCEditorMemoryLease Lease = Governor.TryAcquire(
        MakeRequest(EDWCEditorResourcePool::WorkerPrivateCPU, 40, 4));
    TestTrue(TEXT("Lease starts valid"), Lease.IsValid());
    TestTrue(TEXT("Lease can grow within budget"), Lease.TryGrow(40));
    TestEqual(TEXT("Lease reports its grown reservation"), Lease.GetReservedBytes(), 80ull);

    FString Error;
    TestFalse(TEXT("Lease growth is rejected past the budget"), Lease.TryGrow(30, &Error));
    TestEqual(TEXT("Failed growth preserves the original reservation"), Lease.GetReservedBytes(), 80ull);

    FDWCEditorMemoryLease MovedLease = MoveTemp(Lease);
    TestFalse(TEXT("Moved-from lease releases ownership"), Lease.IsValid());
    TestTrue(TEXT("Moved-to lease owns the reservation"), MovedLease.IsValid());
    MovedLease.Reset();
    MovedLease.Reset();
    TestEqual(TEXT("Reset is idempotent"), Governor.GetDiagnostics().GlobalCPUUsedBytes, 0ull);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorResourceGovernorAsyncLeaseTest,
    "DWC.Editor.Foundation.Async.ResourceGovernor.AsyncLeaseLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorResourceGovernorAsyncLeaseTest::RunTest(const FString&)
{
    FDWCEditorResourceGovernor Governor(MakeSmallBudgetConfig());
    FDWCEditorMemoryLease Lease = Governor.TryAcquire(
        MakeRequest(EDWCEditorResourcePool::WorkerPrivateCPU, 75, 5));
    TestTrue(TEXT("Worker lease is admitted"), Lease.IsValid());

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

    TestTrue(TEXT("Worker starts while owning the lease"), WorkerStarted->Wait(5000));
    TestEqual(TEXT("Cross-thread lease remains accounted"),
        Governor.GetDiagnostics().GlobalCPUUsedBytes, 75ull);
    AllowWorkerRelease->Trigger();
    Worker.Wait();
    TestEqual(TEXT("Cross-thread release returns the reservation"),
        Governor.GetDiagnostics().GlobalCPUUsedBytes, 0ull);

    FPlatformProcess::ReturnSynchEventToPool(WorkerStarted);
    FPlatformProcess::ReturnSynchEventToPool(AllowWorkerRelease);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorResourceGovernorResizeAndPartialReleaseTest,
    "DWC.Editor.Foundation.Async.ResourceGovernor.ResizeAndPartialRelease",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorResourceGovernorResizeAndPartialReleaseTest::RunTest(const FString&)
{
    FDWCEditorResourceGovernor Governor(MakeSmallBudgetConfig());
    FDWCEditorMemoryLease Lease = Governor.TryAcquire(
        MakeRequest(EDWCEditorResourcePool::PreviewWorkspaceCPU, 80, 20));
    TestTrue(TEXT("The initial workspace reservation is admitted"), Lease.IsValid());

    TestTrue(TEXT("A lease can shrink to its live allocation"), Lease.TryResize(50));
    TestEqual(TEXT("Shrink updates the lease size"), Lease.GetReservedBytes(), 50ull);
    TestEqual(TEXT("Shrink immediately returns global CPU budget"),
        Governor.GetDiagnostics().GlobalCPUUsedBytes, 50ull);

    TestTrue(TEXT("A lease can partially release unused capacity"), Lease.ReleaseBytes(20));
    TestEqual(TEXT("Partial release updates the lease size"), Lease.GetReservedBytes(), 30ull);

    FString Error;
    TestFalse(TEXT("Resize beyond the pool budget is rejected"), Lease.TryResize(101, &Error));
    TestFalse(TEXT("Rejected resize reports a reason"), Error.IsEmpty());
    TestEqual(TEXT("Rejected resize preserves the reservation"), Lease.GetReservedBytes(), 30ull);

    Error.Reset();
    TestFalse(TEXT("A lease cannot release more than it owns"), Lease.ReleaseBytes(31, &Error));
    TestEqual(TEXT("Rejected release preserves the reservation"), Lease.GetReservedBytes(), 30ull);

    TestTrue(TEXT("Resize to zero releases the reservation"), Lease.TryResize(0));
    TestFalse(TEXT("A zero-sized lease no longer owns a reservation"), Lease.IsValid());
    const FDWCEditorResourceGovernorDiagnostics Diagnostics = Governor.GetDiagnostics();
    TestEqual(TEXT("All CPU bytes are returned"), Diagnostics.GlobalCPUUsedBytes, 0ull);
    TestEqual(TEXT("No reservation remains after zero resize"), Diagnostics.Reservations.Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorResourceGovernorCrossPoolBudgetTest,
    "DWC.Editor.Foundation.Async.ResourceGovernor.CrossPoolGlobalBudget",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorResourceGovernorCrossPoolBudgetTest::RunTest(const FString&)
{
    FDWCEditorResourceGovernor Governor(MakeSmallBudgetConfig());
    FDWCEditorMemoryLease CacheLease = Governor.TryAcquire(
        MakeRequest(EDWCEditorResourcePool::SharedCacheCPU, 40, 30));
    FDWCEditorMemoryLease WorkspaceLease = Governor.TryAcquire(
        MakeRequest(EDWCEditorResourcePool::PreviewWorkspaceCPU, 35, 31));
    FDWCEditorMemoryLease WorkerLease = Governor.TryAcquire(
        MakeRequest(EDWCEditorResourcePool::WorkerPrivateCPU, 25, 32));
    TestTrue(TEXT("Independent CPU pools can fill the shared global budget"),
        CacheLease.IsValid() && WorkspaceLease.IsValid() && WorkerLease.IsValid());

    FDWCEditorMemoryLease UploadLease = Governor.TryAcquire(
        MakeRequest(EDWCEditorResourcePool::UploadStagingCPU, 1, 33));
    TestFalse(TEXT("The global CPU cap rejects another pool at full occupancy"), UploadLease.IsValid());

    FDWCEditorMemoryLease GPULease = Governor.TryAcquire(
        MakeRequest(EDWCEditorResourcePool::PreviewGPU, 100, 34));
    TestTrue(TEXT("GPU residency remains independent of the global CPU cap"), GPULease.IsValid());

    CacheLease.Reset();
    UploadLease = Governor.TryAcquire(
        MakeRequest(EDWCEditorResourcePool::UploadStagingCPU, 20, 35));
    TestTrue(TEXT("Releasing one CPU owner admits another pool"), UploadLease.IsValid());
    TestEqual(TEXT("Cross-pool accounting reflects the replacement owner"),
        Governor.GetDiagnostics().GlobalCPUUsedBytes, 80ull);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorResourceGovernorDefaultBudgetPolicyTest,
    "DWC.Editor.Foundation.Async.ResourceGovernor.DefaultBudgetPolicy",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorResourceGovernorDefaultBudgetPolicyTest::RunTest(const FString&)
{
    const FDWCEditorResourceBudgetConfig Config;
    TestEqual(
        TEXT("The process-wide editor CPU budget permits large authoring builds"),
        Config.GlobalEditorCPUBytes,
        1536ull * FDWCEditorResourceBudgetConfig::MiB);
    TestEqual(
        TEXT("Preview workspace retention has bounded headroom"),
        Config.PreviewWorkspaceCPUBytes,
        768ull * FDWCEditorResourceBudgetConfig::MiB);
    TestEqual(
        TEXT("Shared cache retention remains bounded"),
        Config.SharedCacheCPUBytes,
        256ull * FDWCEditorResourceBudgetConfig::MiB);
    TestEqual(
        TEXT("Worker private memory remains capped independently"),
        Config.WorkerPrivateCPUBytes,
        512ull * FDWCEditorResourceBudgetConfig::MiB);
    TestEqual(
        TEXT("Preview GPU residency is not expanded by the CPU policy change"),
        Config.PreviewGPUBytes,
        384ull * FDWCEditorResourceBudgetConfig::MiB);
    TestFalse(
        TEXT("Standalone governors do not emit process-wide admission snapshots"),
        Config.bEnableAdmissionFailureDiagnostics);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorResourceGovernorLeaseBundleTest,
    "DWC.Editor.Foundation.Async.ResourceGovernor.LeaseBundleAtomicity",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorResourceGovernorLeaseBundleTest::RunTest(const FString&)
{
    FDWCEditorResourceGovernor Governor(MakeSmallBudgetConfig());
    const FDWCEditorAsyncOperationIdentity Owner = MakeGovernorTestIdentity(40);

    TArray<FDWCEditorResourceReservationRequest> Requests;
    FDWCEditorResourceReservationRequest& Worker = Requests.AddDefaulted_GetRef();
    Worker.Pool = EDWCEditorResourcePool::WorkerPrivateCPU;
    Worker.Bytes = 40;
    Worker.Owner = Owner;
    Worker.DebugName = TEXT("Bundle worker");
    FDWCEditorResourceReservationRequest& Cache = Requests.AddDefaulted_GetRef();
    Cache.Pool = EDWCEditorResourcePool::SharedCacheCPU;
    Cache.Bytes = 30;
    Cache.Owner = Owner;
    Cache.DebugName = TEXT("Bundle cache");

    EDWCEditorResourceAdmissionResult Admission = EDWCEditorResourceAdmissionResult::InvalidRequest;
    FDWCEditorMemoryLeaseSet Bundle = Governor.TryAcquireBundleForAdmission(Requests, Admission);
    TestTrue(TEXT("A cross-pool bundle is admitted as one operation"), Bundle.IsValid());
    TestEqual(TEXT("The bundle owns one merged lease per pool"), Bundle.Num(), 2);
    TestEqual(TEXT("The bundle reports its complete ownership"), Bundle.GetReservedBytes(), 70ull);
    FDWCEditorMemoryLease WorkerLease =
        Bundle.TakeLease(EDWCEditorResourcePool::WorkerPrivateCPU);
    TestTrue(TEXT("A pool lease can transfer out of the bundle"), WorkerLease.IsValid());
    TestEqual(TEXT("Transferred ownership leaves the other pool in the bundle"), Bundle.Num(), 1);
    TestEqual(TEXT("The remaining bundle reports only its retained pool"),
        Bundle.GetReservedBytes(), 30ull);
    WorkerLease.Reset();
    Bundle.Reset();

    FDWCEditorMemoryLease Blocker = Governor.TryAcquire(
        MakeRequest(EDWCEditorResourcePool::SharedCacheCPU, 90, 41));
    TestTrue(TEXT("A cache blocker is admitted"), Blocker.IsValid());
    Bundle = Governor.TryAcquireBundleForAdmission(Requests, Admission);
    TestFalse(TEXT("A partially satisfiable bundle is rejected"), Bundle.IsValid());
    TestEqual(TEXT("Failed bundle admission rolls back its earlier pool lease"),
        Governor.GetDiagnostics().GlobalCPUUsedBytes, 90ull);
    return true;
}

#endif
