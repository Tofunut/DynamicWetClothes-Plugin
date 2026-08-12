// Copyright 2026 Team Tofunut. All Rights Reserved.
#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "WetClothing/Foundation/Resources/DWCEditorResourceBroker.h"

namespace
{
    FDWCEditorResourceBudgetConfig MakeBrokerTestBudget()
    {
        FDWCEditorResourceBudgetConfig Config;
        Config.GlobalEditorCPUBytes = 100;
        Config.WorkerPrivateCPUBytes = 30;
        Config.PreviewWorkspaceCPUBytes = 30;
        Config.SharedCacheCPUBytes = 30;
        Config.UploadStagingCPUBytes = 30;
        Config.PreviewGPUBytes = 100;
        Config.bAllowCPUPoolBorrowing = true;
        return Config;
    }

    FDWCEditorResourceReservationRequest MakeBrokerRequest(
        const EDWCEditorResourcePool Pool,
        const uint64 Bytes,
        const FName OwnerNamespace,
        const uint64 OperationId)
    {
        FDWCEditorResourceReservationRequest Request;
        Request.Pool = Pool;
        Request.Bytes = Bytes;
        Request.Owner.Key.Namespace = OwnerNamespace;
        Request.Owner.SessionEpoch = FGuid::NewGuid();
        Request.Owner.OperationId = OperationId;
        Request.Owner.Generation = 1;
        Request.DebugName = OwnerNamespace.ToString();
        return Request;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorResourceBrokerCrossPoolReclaimTest,
    "DWC.Editor.Foundation.Resources.Broker.CrossPoolPressureReclaim",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorResourceBrokerCrossPoolReclaimTest::RunTest(const FString&)
{
    const TSharedRef<FDWCEditorResourceBroker> Broker =
        FDWCEditorResourceBroker::Create(MakeBrokerTestBudget());
    const TSharedRef<FDWCEditorResourceGovernor> Governor = Broker->GetResourceGovernor();
    const FGuid CacheSession = Broker->OpenSession(TEXT("Cache session"));

    TSharedPtr<FDWCEditorMemoryLease> CacheLease = MakeShared<FDWCEditorMemoryLease>(
        Governor->TryAcquire(MakeBrokerRequest(
            EDWCEditorResourcePool::SharedCacheCPU, 80, TEXT("Test.Cache"), 1)));
    TestTrue(TEXT("CPU pools may borrow unused global capacity"), CacheLease->IsValid());

    FDWCEditorReclaimParticipantDescriptor Participant;
    Participant.Name = TEXT("Test cache");
    Participant.ReservationOwnerNamespace = TEXT("Test.Cache");
    Participant.SessionId = CacheSession;
    Participant.Pool = EDWCEditorResourcePool::SharedCacheCPU;
    Participant.Priority = EDWCEditorReclaimPriority::SharedCache;
    Participant.QueryReclaimableBytes = [CacheLease]
    {
        return CacheLease->GetReservedBytes();
    };
    Participant.Reclaim = [CacheLease](const FDWCEditorResourceReclaimRequest&)
    {
        FDWCEditorResourceReclaimResult Result;
        Result.ImmediateBytes = CacheLease->GetReservedBytes();
        CacheLease->Reset();
        return Result;
    };
    const uint64 ParticipantId = Broker->RegisterParticipant(MoveTemp(Participant));
    TestTrue(TEXT("The cache participant is registered"), ParticipantId != 0);

    FDWCEditorMemoryLease WorkerLease = Governor->TryAcquire(MakeBrokerRequest(
        EDWCEditorResourcePool::WorkerPrivateCPU, 50, TEXT("Test.Worker"), 2));
    TestTrue(TEXT("Admission pressure reclaims an unleased cross-pool owner"), WorkerLease.IsValid());
    TestFalse(TEXT("The reclaimed cache lease is released"), CacheLease->IsValid());

    const FDWCEditorResourceBrokerDiagnostics Diagnostics = Broker->GetDiagnostics();
    TestEqual(TEXT("One pressure request was coordinated"), Diagnostics.PressureRequestCount, 1ull);
    TestEqual(TEXT("The pressure request completed after reclaim"), Diagnostics.SuccessfulReclaimCount, 1ull);
    TestEqual(TEXT("The last pressure request records its pool"),
        Diagnostics.LastRequestedPool, EDWCEditorResourcePool::WorkerPrivateCPU);
    TestEqual(TEXT("The last pressure request records requested bytes"),
        Diagnostics.LastRequestedBytes, 50ull);
    TestEqual(TEXT("The last pressure request records the required reclaim target"),
        Diagnostics.LastTargetBytes, 30ull);
    TestEqual(TEXT("The last pressure request records immediately released bytes"),
        Diagnostics.LastImmediateReclaimedBytes, 80ull);
    TestEqual(TEXT("The reclaimable participant is diagnosed"),
        Diagnostics.LastReclaimableParticipantCount, 1);
    TestEqual(TEXT("The governor retains only the admitted worker bytes"),
        Governor->GetDiagnostics().GlobalCPUUsedBytes, 50ull);

    Broker->CloseSession(CacheSession);
    TestEqual(TEXT("Closing a session unregisters its participants"),
        Broker->GetDiagnostics().ParticipantCount, 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorResourceBrokerOwnerRecursionGuardTest,
    "DWC.Editor.Foundation.Resources.Broker.OwnerRecursionGuard",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorResourceBrokerOwnerRecursionGuardTest::RunTest(const FString&)
{
    const TSharedRef<FDWCEditorResourceBroker> Broker =
        FDWCEditorResourceBroker::Create(MakeBrokerTestBudget());
    const TSharedRef<FDWCEditorResourceGovernor> Governor = Broker->GetResourceGovernor();
    const FGuid Session = Broker->OpenSession(TEXT("Owner guard"));

    FDWCEditorResourceReservationRequest InitialRequest = MakeBrokerRequest(
        EDWCEditorResourcePool::SharedCacheCPU, 100, TEXT("Test.SameOwner"), 10);
    FDWCEditorMemoryLease CacheLease = Governor->TryAcquire(InitialRequest);
    TestTrue(TEXT("The initial owner fills the CPU budget"), CacheLease.IsValid());

    int32 ReclaimCallCount = 0;
    FDWCEditorReclaimParticipantDescriptor Participant;
    Participant.Name = TEXT("Same owner participant");
    Participant.ReservationOwnerNamespace = TEXT("Test.SameOwner");
    Participant.ReservationSessionEpoch = InitialRequest.Owner.SessionEpoch;
    Participant.SessionId = Session;
    Participant.Pool = EDWCEditorResourcePool::SharedCacheCPU;
    Participant.QueryReclaimableBytes = [] { return 100ull; };
    Participant.Reclaim = [&ReclaimCallCount](const FDWCEditorResourceReclaimRequest&)
    {
        ++ReclaimCallCount;
        return FDWCEditorResourceReclaimResult();
    };
    Broker->RegisterParticipant(MoveTemp(Participant));

    FDWCEditorResourceReservationRequest RejectedRequest = MakeBrokerRequest(
        EDWCEditorResourcePool::SharedCacheCPU, 1, TEXT("Test.SameOwner"), 11);
    RejectedRequest.Owner.SessionEpoch = InitialRequest.Owner.SessionEpoch;
    FDWCEditorMemoryLease Rejected = Governor->TryAcquire(RejectedRequest);
    TestFalse(TEXT("An impossible same-owner request remains deferred"), Rejected.IsValid());
    TestEqual(TEXT("The broker does not re-enter the participant that triggered admission"),
        ReclaimCallCount, 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorResourceBrokerExclusiveBuildScopeTest,
    "DWC.Editor.Foundation.Resources.Broker.ExclusiveBuildScope",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorResourceBrokerExclusiveBuildScopeTest::RunTest(const FString&)
{
    const TSharedRef<FDWCEditorResourceBroker> Broker =
        FDWCEditorResourceBroker::Create(MakeBrokerTestBudget());
    const FGuid OwnerSession = Broker->OpenSession(TEXT("Build owner"));
    const FGuid OtherSession = Broker->OpenSession(TEXT("Other editor"));
    int32 OwnerSuspendCount = 0;
    int32 OtherSuspendCount = 0;
    int32 ResumeCount = 0;
    bool bOtherSessionHasInteractiveWork = true;
    Broker->SetSessionBuildBarrierHooks(
        OwnerSession,
        [&OwnerSuspendCount, &ResumeCount](const bool bActive)
        {
            bActive ? ++OwnerSuspendCount : ++ResumeCount;
        },
        []() { return false; });
    Broker->SetSessionBuildBarrierHooks(
        OtherSession,
        [&OtherSuspendCount, &ResumeCount](const bool bActive)
        {
            bActive ? ++OtherSuspendCount : ++ResumeCount;
        },
        [&bOtherSessionHasInteractiveWork]()
        {
            return bOtherSessionHasInteractiveWork;
        });

    FDWCEditorExclusiveBuildRequest Request;
    Request.SessionId = OwnerSession;
    Request.AssetPath = TEXT("/Game/Test/WCA_Test");
    Request.DebugName = TEXT("Build All Required");
    FString Error;
    TUniquePtr<FDWCEditorExclusiveBuildLease> Lease =
        Broker->TryBeginExclusiveBuild(Request, &Error);
    TestTrue(TEXT("The first exclusive Build acquires the process-wide scope"), Lease.IsValid());
    TestTrue(TEXT("The acquired scope is diagnosed as active"),
        Broker->GetExclusiveBuildSnapshot().IsActive());
    TestEqual(TEXT("The owner preview session is suspended"), OwnerSuspendCount, 1);
    TestEqual(TEXT("Other preview sessions are also suspended"), OtherSuspendCount, 1);
    TestTrue(TEXT("The broker observes interactive work across sessions"),
        Broker->HasOutstandingInteractiveWork());
    bOtherSessionHasInteractiveWork = false;
    TestFalse(TEXT("The global drain completes after all sessions retire"),
        Broker->HasOutstandingInteractiveWork());

    FString PreviewReason;
    TestFalse(TEXT("Preview work is blocked while Build owns the scope"),
        Broker->CanAdmitWork(
            OwnerSession,
            EDWCEditorWorkClass::InteractivePreview,
            FGuid(),
            &PreviewReason));
    TestFalse(TEXT("An unrelated editor Build is blocked"),
        Broker->CanAdmitWork(
            OtherSession,
            EDWCEditorWorkClass::UserBuild,
            FGuid(),
            nullptr));
    TestTrue(TEXT("The scope owner may admit explicitly-scoped Build work"),
        Broker->CanAdmitWork(
            OwnerSession,
            EDWCEditorWorkClass::ExclusiveBuild,
            Lease->GetScopeId(),
            nullptr));

    FDWCEditorExclusiveBuildRequest ConflictingRequest = Request;
    ConflictingRequest.SessionId = OtherSession;
    TUniquePtr<FDWCEditorExclusiveBuildLease> ConflictingLease =
        Broker->TryBeginExclusiveBuild(ConflictingRequest, &Error);
    TestFalse(TEXT("A second exclusive Build cannot overlap"), ConflictingLease.IsValid());

    Lease.Reset();
    TestFalse(TEXT("Releasing the lease clears the scope"),
        Broker->GetExclusiveBuildSnapshot().IsActive());
    TestEqual(TEXT("Every registered preview session is resumed"), ResumeCount, 2);
    TestTrue(TEXT("Preview admission resumes after scope retirement"),
        Broker->CanAdmitWork(
            OtherSession,
            EDWCEditorWorkClass::InteractivePreview,
            FGuid(),
            nullptr));
    return true;
}

#endif
