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
        Config.SpatialCacheCPUBytes = 100;
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

#endif
