// Copyright 2026 Team Tofunut. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "WetClothing/Foundation/Async/DWCEditorResourceGovernor.h"
#include "WetClothing/Foundation/Diagnostics/DWCEditorMemoryDiagnostics.h"

namespace
{
    const FDWCEditorMemoryOwnerRecord* FindOwner(
        const FDWCEditorMemorySnapshot& Snapshot,
        const FString& Identifier)
    {
        return Snapshot.Owners.FindByPredicate(
            [&Identifier](const FDWCEditorMemoryOwnerRecord& Owner)
            {
                return Owner.Identifier == Identifier;
            });
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorMemoryOwnerLifetimeTest,
    "DWC.Editor.Foundation.Diagnostics.MemoryOwnerLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorMemoryOwnerLifetimeTest::RunTest(const FString&)
{
    const FString Identifier = FString::Printf(TEXT("MemoryOwnerTest.%s"), *FGuid::NewGuid().ToString());
    FDWCEditorMemoryOwnerRecord Record;
    Record.Identifier = Identifier;
    Record.Subsystem = TEXT("Test");
    Record.Resource = TEXT("ResidentBuffer");
    Record.Category = EDWCEditorMemoryCategory::PersistentEditorCPU;
    Record.Accounting = EDWCEditorMemoryAccounting::Resident;
    Record.CurrentBytes = 64;
    Record.EntryCount = 1;

    FDWCEditorMemoryOwner Owner(Record);
    const FDWCEditorMemoryOwnerRecord* Captured = FindOwner(
        FDWCEditorMemoryDiagnostics::CaptureSnapshot(),
        Identifier);
    TestNotNull(TEXT("Registered owner appears in the snapshot"), Captured);
    if (Captured != nullptr)
    {
        TestEqual(TEXT("Initial resident bytes are preserved"), Captured->CurrentBytes, 64ull);
    }

    Owner.Update(160, 4);
    const FDWCEditorMemorySnapshot UpdatedSnapshot = FDWCEditorMemoryDiagnostics::CaptureSnapshot();
    Captured = FindOwner(UpdatedSnapshot, Identifier);
    TestNotNull(TEXT("Updated owner remains visible"), Captured);
    if (Captured != nullptr)
    {
        TestEqual(TEXT("Owner update changes current bytes"), Captured->CurrentBytes, 160ull);
        TestEqual(TEXT("Owner update changes entry count"), Captured->EntryCount, 4);
        TestTrue(TEXT("Owner high-water follows the largest value"), Captured->PeakBytes >= 160ull);
    }

    FDWCEditorMemoryOwner MovedOwner(MoveTemp(Owner));
    TestFalse(TEXT("Move clears the source owner token"), Owner.IsValid());
    TestTrue(TEXT("Move preserves the destination owner token"), MovedOwner.IsValid());
    MovedOwner.Reset();
    TestNull(
        TEXT("Reset removes the owner from subsequent snapshots"),
        FindOwner(FDWCEditorMemoryDiagnostics::CaptureSnapshot(), Identifier));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorMemoryCollectorAccountingTest,
    "DWC.Editor.Foundation.Diagnostics.MemoryCollectorAccounting",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorMemoryCollectorAccountingTest::RunTest(const FString&)
{
    const FName CollectorName(*FString::Printf(TEXT("MemoryCollectorTest_%s"), *FGuid::NewGuid().ToString()));
    const FString ResidentIdentifier = CollectorName.ToString() + TEXT(".Resident");
    const FString ReservationIdentifier = CollectorName.ToString() + TEXT(".Reservation");
    FDWCEditorMemoryDiagnostics::RegisterCollector(
        CollectorName,
        [ResidentIdentifier, ReservationIdentifier](TArray<FDWCEditorMemoryOwnerRecord>& OutOwners)
        {
            FDWCEditorMemoryOwnerRecord& Resident = OutOwners.AddDefaulted_GetRef();
            Resident.Identifier = ResidentIdentifier;
            Resident.Subsystem = TEXT("Test");
            Resident.Resource = TEXT("ResidentGPU");
            Resident.Category = EDWCEditorMemoryCategory::PreviewGPU;
            Resident.Accounting = EDWCEditorMemoryAccounting::Resident;
            Resident.CurrentBytes = 80;

            FDWCEditorMemoryOwnerRecord& Reservation = OutOwners.AddDefaulted_GetRef();
            Reservation.Identifier = ReservationIdentifier;
            Reservation.Subsystem = TEXT("Test");
            Reservation.Resource = TEXT("ReservedGPU");
            Reservation.Category = EDWCEditorMemoryCategory::PreviewGPU;
            Reservation.Accounting = EDWCEditorMemoryAccounting::Reservation;
            Reservation.CurrentBytes = 120;
        });

    const FDWCEditorMemorySnapshot Snapshot = FDWCEditorMemoryDiagnostics::CaptureSnapshot();
    TestNotNull(TEXT("Collector resident record is visible"), FindOwner(Snapshot, ResidentIdentifier));
    TestNotNull(TEXT("Collector reservation record is visible"), FindOwner(Snapshot, ReservationIdentifier));
    TestTrue(TEXT("Resident GPU total includes collector bytes"), Snapshot.ResidentGPUBytes >= 80ull);
    TestTrue(TEXT("Reserved GPU total is kept separate"), Snapshot.ReservedGPUBytes >= 120ull);

    FDWCEditorMemoryDiagnostics::UnregisterCollector(CollectorName);
    const FDWCEditorMemorySnapshot RemovedSnapshot = FDWCEditorMemoryDiagnostics::CaptureSnapshot();
    TestNull(TEXT("Unregistered collector no longer contributes resident bytes"), FindOwner(RemovedSnapshot, ResidentIdentifier));
    TestNull(TEXT("Unregistered collector no longer contributes reservations"), FindOwner(RemovedSnapshot, ReservationIdentifier));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FDWCEditorMemoryGovernorStateLifetimeTest,
    "DWC.Editor.Foundation.Diagnostics.GovernorStateLifetime",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCEditorMemoryGovernorStateLifetimeTest::RunTest(const FString&)
{
    const uint64 BaselineReservedCPU =
        FDWCEditorMemoryDiagnostics::CaptureSnapshot().ReservedCPUBytes;
    FDWCEditorMemoryLease Lease;
    {
        FDWCEditorResourceBudgetConfig Config;
        Config.GlobalEditorCPUBytes = 1024;
        Config.WorkerPrivateCPUBytes = 1024;
        FDWCEditorResourceGovernor Governor(Config);

        FDWCEditorResourceReservationRequest Request;
        Request.Pool = EDWCEditorResourcePool::WorkerPrivateCPU;
        Request.Bytes = 96;
        Request.DebugName = TEXT("Diagnostic state lifetime test");
        Lease = Governor.TryAcquire(Request);
        TestTrue(TEXT("Test reservation is admitted"), Lease.IsValid());
        TestTrue(
            TEXT("Live governor reservation appears in the global snapshot"),
            FDWCEditorMemoryDiagnostics::CaptureSnapshot().ReservedCPUBytes >=
                BaselineReservedCPU + 96);
    }

    TestTrue(TEXT("Lease keeps governor state alive"), Lease.IsValid());
    TestTrue(
        TEXT("Reservation remains diagnosed after the governor wrapper is destroyed"),
        FDWCEditorMemoryDiagnostics::CaptureSnapshot().ReservedCPUBytes >=
            BaselineReservedCPU + 96);
    Lease.Reset();
    TestEqual(
        TEXT("Releasing the last lease removes the diagnosed reservation"),
        FDWCEditorMemoryDiagnostics::CaptureSnapshot().ReservedCPUBytes,
        BaselineReservedCPU);
    return true;
}

#endif
