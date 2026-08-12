// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "WetClothing/Foundation/Diagnostics/DWCEditorMemoryDiagnostics.h"

#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMemory.h"
#include "HAL/PlatformTime.h"
#include "Misc/ScopeLock.h"
#include "WetClothing/Foundation/Async/DWCEditorAsyncOperationContract.h"
#include "WetClothing/Foundation/Async/DWCEditorResourceGovernor.h"
#include "WetClothing/Foundation/Preview/Diagnostics/DWCEditorPreviewDiagnostics.h"
#include "WetClothing/Foundation/Resources/DWCEditorResourceBroker.h"

DEFINE_LOG_CATEGORY_STATIC(LogDWCEditorMemory, Log, All);

namespace DWCEditorMemoryDiagnosticsPrivate
{
    struct FState
    {
        FCriticalSection Mutex;
        TMap<uint64, FDWCEditorMemoryOwnerRecord> Owners;
        TMap<FName, FDWCEditorMemoryCollector> Collectors;
        TMap<FString, uint64> OwnerPeaks;
        uint64 NextOwnerId = 1;
        uint64 PeakResidentCPUBytes = 0;
        uint64 PeakResidentGPUBytes = 0;
        uint64 PeakReservedCPUBytes = 0;
        uint64 PeakReservedGPUBytes = 0;
        TMap<FString, double> AdmissionFailureLogSeconds;
        FString ActiveBuildName;
        FString ActiveBuildAsset;
        FString ActiveBuildPhase;
    };

    FState& GetState()
    {
        // Intentionally process-lived. Static diagnostic registrars may unregister during module shutdown.
        static FState* State = new FState();
        return *State;
    }

    bool IsGPUCategory(const EDWCEditorMemoryCategory Category)
    {
        return Category == EDWCEditorMemoryCategory::PreviewGPU;
    }

    FString MakePeakKey(const FDWCEditorMemoryOwnerRecord& Record)
    {
        return FString::Printf(
            TEXT("%s|%s|%s|%u|%u"),
            *Record.Identifier,
            *Record.Subsystem.ToString(),
            *Record.Resource.ToString(),
            static_cast<uint8>(Record.Category),
            static_cast<uint8>(Record.Accounting));
    }

    void Accumulate(FDWCEditorMemorySnapshot& Snapshot, const FDWCEditorMemoryOwnerRecord& Record)
    {
        const bool bGPU = IsGPUCategory(Record.Category);
        switch (Record.Accounting)
        {
        case EDWCEditorMemoryAccounting::Resident:
            (bGPU ? Snapshot.ResidentGPUBytes : Snapshot.ResidentCPUBytes) += Record.CurrentBytes;
            break;
        case EDWCEditorMemoryAccounting::Reservation:
            (bGPU ? Snapshot.ReservedGPUBytes : Snapshot.ReservedCPUBytes) += Record.CurrentBytes;
            break;
        case EDWCEditorMemoryAccounting::TransientEstimate:
            if (!bGPU)
            {
                Snapshot.TransientEstimatedCPUBytes += Record.CurrentBytes;
            }
            break;
        default:
            break;
        }
    }

    FString FormatBytes(const uint64 Bytes)
    {
        return FDWCEditorPreviewDiagnostics::FormatBytes(Bytes);
    }

    FString FormatDelta(const uint64 Before, const uint64 After)
    {
        const double DeltaMiB = static_cast<double>(After) / FDWCEditorResourceBudgetConfig::MiB -
            static_cast<double>(Before) / FDWCEditorResourceBudgetConfig::MiB;
        return FString::Printf(TEXT("%+.2f MiB"), DeltaMiB);
    }

    TAutoConsoleVariable<int32> CVarBuildMemoryDiagnostics(
        TEXT("dwc.Editor.Memory.BuildDiagnostics"),
        1,
        TEXT("Controls WCA build memory diagnostics: 0=admission failures only, 1=phase summaries, 2=phase owner rows."),
        ECVF_Default);

    FAutoConsoleCommand DumpMemoryCommand(
        TEXT("dwc.Editor.Memory.Dump"),
        TEXT("Dumps WCA editor resident, reservation, and transient memory with owner rows."),
        FConsoleCommandDelegate::CreateStatic([]()
        {
            FDWCEditorMemoryDiagnostics::DumpSnapshot(true);
        }));

    FAutoConsoleCommand DumpMemorySummaryCommand(
        TEXT("dwc.Editor.Memory.Summary"),
        TEXT("Dumps only the WCA editor memory totals."),
        FConsoleCommandDelegate::CreateStatic([]()
        {
            FDWCEditorMemoryDiagnostics::DumpSnapshot(false);
        }));

    FAutoConsoleCommand ResetMemoryPeaksCommand(
        TEXT("dwc.Editor.Memory.ResetPeaks"),
        TEXT("Resets WCA editor memory diagnostic high-water marks to current usage."),
        FConsoleCommandDelegate::CreateStatic(&FDWCEditorMemoryDiagnostics::ResetPeaks));

}

using namespace DWCEditorMemoryDiagnosticsPrivate;

uint64 FDWCEditorMemoryDiagnostics::RegisterOwner(const FDWCEditorMemoryOwnerRecord& Record)
{
    FState& State = GetState();
    FScopeLock Lock(&State.Mutex);
    const uint64 OwnerId = State.NextOwnerId++;
    FDWCEditorMemoryOwnerRecord Stored = Record;
    Stored.PeakBytes = Stored.CurrentBytes;
    State.Owners.Add(OwnerId, MoveTemp(Stored));
    return OwnerId;
}

void FDWCEditorMemoryDiagnostics::UpdateOwner(
    const uint64 OwnerId,
    const uint64 CurrentBytes,
    const int32 EntryCount)
{
    if (OwnerId == 0)
    {
        return;
    }
    FState& State = GetState();
    FScopeLock Lock(&State.Mutex);
    if (FDWCEditorMemoryOwnerRecord* Record = State.Owners.Find(OwnerId))
    {
        Record->CurrentBytes = CurrentBytes;
        Record->PeakBytes = FMath::Max(Record->PeakBytes, CurrentBytes);
        Record->EntryCount = EntryCount;
    }
}

void FDWCEditorMemoryDiagnostics::UnregisterOwner(const uint64 OwnerId)
{
    if (OwnerId == 0)
    {
        return;
    }
    FState& State = GetState();
    FScopeLock Lock(&State.Mutex);
    State.Owners.Remove(OwnerId);
}

void FDWCEditorMemoryDiagnostics::RegisterCollector(
    const FName CollectorName,
    FDWCEditorMemoryCollector Collector)
{
    if (CollectorName.IsNone() || !Collector)
    {
        return;
    }
    FState& State = GetState();
    FScopeLock Lock(&State.Mutex);
    State.Collectors.Add(CollectorName, MoveTemp(Collector));
}

void FDWCEditorMemoryDiagnostics::UnregisterCollector(const FName CollectorName)
{
    FState& State = GetState();
    FScopeLock Lock(&State.Mutex);
    State.Collectors.Remove(CollectorName);
}

FDWCEditorMemorySnapshot FDWCEditorMemoryDiagnostics::CaptureSnapshot()
{
    FState& State = GetState();
    FDWCEditorMemorySnapshot Snapshot;
    TArray<FDWCEditorMemoryCollector> Collectors;
    {
        FScopeLock Lock(&State.Mutex);
        State.Owners.GenerateValueArray(Snapshot.Owners);
        State.Collectors.GenerateValueArray(Collectors);
    }

    for (const FDWCEditorMemoryCollector& Collector : Collectors)
    {
        if (Collector)
        {
            Collector(Snapshot.Owners);
        }
    }

    FDWCEditorPreviewDiagnostics::AppendGlobalMemoryOwners(Snapshot.Owners);

    {
        FScopeLock Lock(&State.Mutex);
        for (FDWCEditorMemoryOwnerRecord& Record : Snapshot.Owners)
        {
            uint64& Peak = State.OwnerPeaks.FindOrAdd(MakePeakKey(Record));
            Peak = FMath::Max(Peak, Record.CurrentBytes);
            Record.PeakBytes = FMath::Max(Record.PeakBytes, Peak);
            Accumulate(Snapshot, Record);
        }
        State.PeakResidentCPUBytes = FMath::Max(State.PeakResidentCPUBytes, Snapshot.ResidentCPUBytes);
        State.PeakResidentGPUBytes = FMath::Max(State.PeakResidentGPUBytes, Snapshot.ResidentGPUBytes);
        State.PeakReservedCPUBytes = FMath::Max(State.PeakReservedCPUBytes, Snapshot.ReservedCPUBytes);
        State.PeakReservedGPUBytes = FMath::Max(State.PeakReservedGPUBytes, Snapshot.ReservedGPUBytes);
        Snapshot.PeakResidentCPUBytes = State.PeakResidentCPUBytes;
        Snapshot.PeakResidentGPUBytes = State.PeakResidentGPUBytes;
        Snapshot.PeakReservedCPUBytes = State.PeakReservedCPUBytes;
        Snapshot.PeakReservedGPUBytes = State.PeakReservedGPUBytes;
    }

    Snapshot.Owners.Sort([](const FDWCEditorMemoryOwnerRecord& A, const FDWCEditorMemoryOwnerRecord& B)
    {
        if (A.CurrentBytes != B.CurrentBytes)
        {
            return A.CurrentBytes > B.CurrentBytes;
        }
        return A.Identifier < B.Identifier;
    });
    return Snapshot;
}

void FDWCEditorMemoryDiagnostics::LogSnapshotSummary(
    const FDWCEditorMemorySnapshot& Snapshot,
    const FString& Prefix)
{
    UE_LOG(
        LogDWCEditorMemory,
        Display,
        TEXT("%sresidentCPU=%s, residentGPU=%s, reservedCPU=%s, reservedGPU=%s, transientEstimateCPU=%s, owners=%d."),
        *Prefix,
        *FormatBytes(Snapshot.ResidentCPUBytes),
        *FormatBytes(Snapshot.ResidentGPUBytes),
        *FormatBytes(Snapshot.ReservedCPUBytes),
        *FormatBytes(Snapshot.ReservedGPUBytes),
        *FormatBytes(Snapshot.TransientEstimatedCPUBytes),
        Snapshot.Owners.Num());
}

void FDWCEditorMemoryDiagnostics::LogTopOwners(
    const FDWCEditorMemorySnapshot& Snapshot,
    const int32 MaxOwnerCount,
    const FString& Prefix)
{
    int32 LoggedOwnerCount = 0;
    for (const FDWCEditorMemoryOwnerRecord& Owner : Snapshot.Owners)
    {
        if (Owner.CurrentBytes == 0 || LoggedOwnerCount >= FMath::Max(MaxOwnerCount, 0))
        {
            continue;
        }
        UE_LOG(
            LogDWCEditorMemory,
            Display,
            TEXT("%s[%s/%s] %s :: %s current=%s entries=%d context='%s'."),
            *Prefix,
            LexToString(Owner.Accounting),
            LexToString(Owner.Category),
            *Owner.Subsystem.ToString(),
            *Owner.Resource.ToString(),
            *FormatBytes(Owner.CurrentBytes),
            Owner.EntryCount,
            *Owner.Context);
        ++LoggedOwnerCount;
    }
}

void FDWCEditorMemoryDiagnostics::ReportAdmissionFailure(
    const FDWCEditorResourceReservationRequest& Request,
    const FDWCEditorResourceGovernorDiagnostics& GovernorDiagnostics,
    const FString& ErrorMessage)
{
    const FString FailureKey = FString::Printf(
        TEXT("%s|%d|%u|%s"),
        *Request.Owner.Key.Namespace.ToString(),
        Request.Owner.Key.MaterialSlotIndex,
        static_cast<uint8>(Request.Pool),
        *Request.DebugName);
    const double NowSeconds = FPlatformTime::Seconds();
    bool bLogOwners = false;
    FString ActiveBuildContext;
    {
        FState& State = GetState();
        FScopeLock Lock(&State.Mutex);
        double& LastLogSeconds = State.AdmissionFailureLogSeconds.FindOrAdd(FailureKey);
        bLogOwners = LastLogSeconds <= 0.0 || NowSeconds - LastLogSeconds >= 5.0;
        if (bLogOwners)
        {
            LastLogSeconds = NowSeconds;
        }
        if (!State.ActiveBuildName.IsEmpty())
        {
            ActiveBuildContext = FString::Printf(
                TEXT(" build='%s' phase='%s' asset='%s'"),
                *State.ActiveBuildName,
                *State.ActiveBuildPhase,
                *State.ActiveBuildAsset);
        }
    }

    UE_LOG(
        LogDWCEditorMemory,
        Warning,
        TEXT("WCA resource admission failed:%s pool=%s request=%.2f MiB globalCPU=%.2f/%.2f MiB owner='%s' slot=%d operation=%llu generation=%llu debug='%s'. %s"),
        *ActiveBuildContext,
        FDWCEditorAsyncOperationContract::LexToString(Request.Pool),
        static_cast<double>(Request.Bytes) / FDWCEditorResourceBudgetConfig::MiB,
        static_cast<double>(GovernorDiagnostics.GlobalCPUUsedBytes) / FDWCEditorResourceBudgetConfig::MiB,
        static_cast<double>(GovernorDiagnostics.GlobalCPUBudgetBytes) / FDWCEditorResourceBudgetConfig::MiB,
        *Request.Owner.Key.Namespace.ToString(),
        Request.Owner.Key.MaterialSlotIndex,
        Request.Owner.OperationId,
        Request.Owner.Generation,
        *Request.DebugName,
        *ErrorMessage);

    if (bLogOwners && GetBuildDiagnosticsVerbosity() >= 1 && IsInGameThread())
    {
        const FDWCEditorMemorySnapshot Snapshot = CaptureSnapshot();
        LogSnapshotSummary(Snapshot, TEXT("  admission snapshot: "));
        LogTopOwners(Snapshot, 10, TEXT("  owner: "));
    }
}

void FDWCEditorMemoryDiagnostics::DumpSnapshot(const bool bVerboseOwners)
{
    const FDWCEditorMemorySnapshot Snapshot = CaptureSnapshot();
    const FPlatformMemoryStats PlatformStats = FPlatformMemory::GetStats();
    UE_LOG(
        LogDWCEditorMemory,
        Display,
        TEXT("WCA editor memory: residentCPU=%s (peak=%s), residentGPU=%s (peak=%s), reservedCPU=%s (peak=%s), reservedGPU=%s (peak=%s), transientEstimateCPU=%s, owners=%d."),
        *FormatBytes(Snapshot.ResidentCPUBytes),
        *FormatBytes(Snapshot.PeakResidentCPUBytes),
        *FormatBytes(Snapshot.ResidentGPUBytes),
        *FormatBytes(Snapshot.PeakResidentGPUBytes),
        *FormatBytes(Snapshot.ReservedCPUBytes),
        *FormatBytes(Snapshot.PeakReservedCPUBytes),
        *FormatBytes(Snapshot.ReservedGPUBytes),
        *FormatBytes(Snapshot.PeakReservedGPUBytes),
        *FormatBytes(Snapshot.TransientEstimatedCPUBytes),
        Snapshot.Owners.Num());
    UE_LOG(
        LogDWCEditorMemory,
        Display,
        TEXT("Platform memory at snapshot: usedPhysical=%s, peakUsedPhysical=%s, usedVirtual=%s, peakUsedVirtual=%s."),
        *FormatBytes(PlatformStats.UsedPhysical),
        *FormatBytes(PlatformStats.PeakUsedPhysical),
        *FormatBytes(PlatformStats.UsedVirtual),
        *FormatBytes(PlatformStats.PeakUsedVirtual));

    if (bVerboseOwners)
    {
        for (const FDWCEditorMemoryOwnerRecord& Owner : Snapshot.Owners)
        {
            UE_LOG(
                LogDWCEditorMemory,
                Display,
                TEXT("  [%s/%s] %s :: %s current=%s peak=%s entries=%d context='%s'."),
                LexToString(Owner.Accounting),
                LexToString(Owner.Category),
                *Owner.Subsystem.ToString(),
                *Owner.Resource.ToString(),
                *FormatBytes(Owner.CurrentBytes),
                *FormatBytes(Owner.PeakBytes),
                Owner.EntryCount,
                *Owner.Context);
        }
    }

    FDWCEditorPreviewDiagnostics::DumpAllSessions();
}

void FDWCEditorMemoryDiagnostics::ResetPeaks()
{
    FState& State = GetState();
    {
        FScopeLock Lock(&State.Mutex);
        State.OwnerPeaks.Reset();
        State.PeakResidentCPUBytes = 0;
        State.PeakResidentGPUBytes = 0;
        State.PeakReservedCPUBytes = 0;
        State.PeakReservedGPUBytes = 0;
        for (TPair<uint64, FDWCEditorMemoryOwnerRecord>& Pair : State.Owners)
        {
            Pair.Value.PeakBytes = Pair.Value.CurrentBytes;
        }
    }
    CaptureSnapshot();
    FDWCEditorPreviewDiagnostics::ResetAllCounters();
    UE_LOG(LogDWCEditorMemory, Display, TEXT("Reset WCA editor memory high-water marks."));
}

int32 FDWCEditorMemoryDiagnostics::GetBuildDiagnosticsVerbosity()
{
    return FMath::Clamp(CVarBuildMemoryDiagnostics.GetValueOnAnyThread(), 0, 2);
}

FDWCEditorBuildMemoryTrace::FDWCEditorBuildMemoryTrace(
    FString InBuildName,
    FString InAssetPath,
    const FString* InFailureMessage)
    : BuildName(MoveTemp(InBuildName))
    , AssetPath(MoveTemp(InAssetPath))
    , FailureMessage(InFailureMessage)
    , BuildStartSeconds(FPlatformTime::Seconds())
    , bEnabled(FDWCEditorMemoryDiagnostics::GetBuildDiagnosticsVerbosity() >= 1)
{
    if (!bEnabled)
    {
        return;
    }

    BuildStartSnapshot = FDWCEditorMemoryDiagnostics::CaptureSnapshot();
    {
        FState& State = GetState();
        FScopeLock Lock(&State.Mutex);
        State.ActiveBuildName = BuildName;
        State.ActiveBuildAsset = AssetPath;
        State.ActiveBuildPhase.Reset();
    }
    const TSharedRef<FDWCEditorResourceBroker> Broker = FDWCEditorResourceBroker::Get();
    const FDWCEditorResourceBrokerDiagnostics BrokerDiagnostics = Broker->GetDiagnostics();
    const FDWCEditorResourceGovernorDiagnostics GovernorDiagnostics =
        Broker->GetResourceGovernor()->GetDiagnostics();
    StartPressureRequestCount = BrokerDiagnostics.PressureRequestCount;
    StartSuccessfulReclaimCount = BrokerDiagnostics.SuccessfulReclaimCount;
    StartReclaimedBytes = BrokerDiagnostics.ImmediateReclaimedBytes;
    StartCPURejectionCount = GovernorDiagnostics.GlobalCPURejectionCount;

    UE_LOG(
        LogDWCEditorMemory,
        Display,
        TEXT("WCA build memory trace started: build='%s' asset='%s' reservedCPU=%s residentCPU=%s globalCPU=%.2f/%.2f MiB."),
        *BuildName,
        *AssetPath,
        *FormatBytes(BuildStartSnapshot.ReservedCPUBytes),
        *FormatBytes(BuildStartSnapshot.ResidentCPUBytes),
        static_cast<double>(GovernorDiagnostics.GlobalCPUUsedBytes) / FDWCEditorResourceBudgetConfig::MiB,
        static_cast<double>(GovernorDiagnostics.GlobalCPUBudgetBytes) / FDWCEditorResourceBudgetConfig::MiB);
}

FDWCEditorBuildMemoryTrace::~FDWCEditorBuildMemoryTrace()
{
    if (!bEnabled)
    {
        return;
    }

    EndCurrentPhase();
    const FDWCEditorMemorySnapshot FinalSnapshot = FDWCEditorMemoryDiagnostics::CaptureSnapshot();
    const TSharedRef<FDWCEditorResourceBroker> Broker = FDWCEditorResourceBroker::Get();
    const FDWCEditorResourceBrokerDiagnostics BrokerDiagnostics = Broker->GetDiagnostics();
    const FDWCEditorResourceGovernorDiagnostics GovernorDiagnostics =
        Broker->GetResourceGovernor()->GetDiagnostics();
    const FString Failure = FailureMessage != nullptr ? *FailureMessage : FString();
    const TCHAR* Status = bComplete ? TEXT("succeeded") : Failure.IsEmpty() ? TEXT("aborted") : TEXT("failed");
    const auto CounterDelta = [](const uint64 EndValue, const uint64 StartValue)
    {
        return EndValue >= StartValue ? EndValue - StartValue : 0;
    };

    const FString Summary = FString::Printf(
        TEXT("WCA build memory trace %s: build='%s' asset='%s' duration=%.2fs reservedCPU=%s (%s) residentCPU=%s (%s) highWater=%.2f MiB pressure=%llu successful=%llu reclaimed=%.2f MiB CPURejects=%llu%s."),
        Status,
        *BuildName,
        *AssetPath,
        FPlatformTime::Seconds() - BuildStartSeconds,
        *FormatBytes(FinalSnapshot.ReservedCPUBytes),
        *FormatDelta(BuildStartSnapshot.ReservedCPUBytes, FinalSnapshot.ReservedCPUBytes),
        *FormatBytes(FinalSnapshot.ResidentCPUBytes),
        *FormatDelta(BuildStartSnapshot.ResidentCPUBytes, FinalSnapshot.ResidentCPUBytes),
        static_cast<double>(GovernorDiagnostics.GlobalCPUHighWaterBytes) / FDWCEditorResourceBudgetConfig::MiB,
        CounterDelta(BrokerDiagnostics.PressureRequestCount, StartPressureRequestCount),
        CounterDelta(BrokerDiagnostics.SuccessfulReclaimCount, StartSuccessfulReclaimCount),
        static_cast<double>(CounterDelta(BrokerDiagnostics.ImmediateReclaimedBytes, StartReclaimedBytes)) /
            FDWCEditorResourceBudgetConfig::MiB,
        CounterDelta(GovernorDiagnostics.GlobalCPURejectionCount, StartCPURejectionCount),
        Failure.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(" error='%s'"), *Failure));
    if (bComplete)
    {
        UE_LOG(LogDWCEditorMemory, Display, TEXT("%s"), *Summary);
    }
    else
    {
        UE_LOG(LogDWCEditorMemory, Warning, TEXT("%s"), *Summary);
    }

    if (!bComplete)
    {
        FDWCEditorMemoryDiagnostics::LogTopOwners(FinalSnapshot, 10, TEXT("  build failure owner: "));
    }
    {
        FState& State = GetState();
        FScopeLock Lock(&State.Mutex);
        if (State.ActiveBuildName == BuildName && State.ActiveBuildAsset == AssetPath)
        {
            State.ActiveBuildName.Reset();
            State.ActiveBuildAsset.Reset();
            State.ActiveBuildPhase.Reset();
        }
    }
}

void FDWCEditorBuildMemoryTrace::BeginPhase(const TCHAR* PhaseName)
{
    if (!bEnabled)
    {
        return;
    }
    EndCurrentPhase();
    CurrentPhase = PhaseName != nullptr ? PhaseName : TEXT("Unnamed");
    {
        FState& State = GetState();
        FScopeLock Lock(&State.Mutex);
        if (State.ActiveBuildName == BuildName && State.ActiveBuildAsset == AssetPath)
        {
            State.ActiveBuildPhase = CurrentPhase;
        }
    }
    PhaseStartSnapshot = FDWCEditorMemoryDiagnostics::CaptureSnapshot();
    PhaseStartSeconds = FPlatformTime::Seconds();
}

void FDWCEditorBuildMemoryTrace::Complete()
{
    if (bEnabled)
    {
        EndCurrentPhase();
    }
    bComplete = true;
}

void FDWCEditorBuildMemoryTrace::EndCurrentPhase()
{
    if (!bEnabled || CurrentPhase.IsEmpty())
    {
        return;
    }

    const FDWCEditorMemorySnapshot EndSnapshot = FDWCEditorMemoryDiagnostics::CaptureSnapshot();
    UE_LOG(
        LogDWCEditorMemory,
        Display,
        TEXT("WCA build memory phase '%s': duration=%.2fs reservedCPU=%s (%s) residentCPU=%s (%s) reservedGPU=%s."),
        *CurrentPhase,
        FPlatformTime::Seconds() - PhaseStartSeconds,
        *FormatBytes(EndSnapshot.ReservedCPUBytes),
        *FormatDelta(PhaseStartSnapshot.ReservedCPUBytes, EndSnapshot.ReservedCPUBytes),
        *FormatBytes(EndSnapshot.ResidentCPUBytes),
        *FormatDelta(PhaseStartSnapshot.ResidentCPUBytes, EndSnapshot.ResidentCPUBytes),
        *FormatBytes(EndSnapshot.ReservedGPUBytes));

    if (FDWCEditorMemoryDiagnostics::GetBuildDiagnosticsVerbosity() >= 2)
    {
        FDWCEditorMemoryDiagnostics::LogTopOwners(EndSnapshot, 10, TEXT("  phase owner: "));
    }
    CurrentPhase.Reset();
}

const TCHAR* FDWCEditorMemoryDiagnostics::LexToString(const EDWCEditorMemoryCategory Category)
{
    switch (Category)
    {
    case EDWCEditorMemoryCategory::PersistentEditorCPU: return TEXT("PersistentEditorCPU");
    case EDWCEditorMemoryCategory::SharedCacheCPU: return TEXT("SharedCacheCPU");
    case EDWCEditorMemoryCategory::OperationPrivateCPU: return TEXT("OperationPrivateCPU");
    case EDWCEditorMemoryCategory::UploadStagingCPU: return TEXT("UploadStagingCPU");
    case EDWCEditorMemoryCategory::PreviewGPU: return TEXT("PreviewGPU");
    case EDWCEditorMemoryCategory::AssetBuildCPU: return TEXT("AssetBuildCPU");
    default: return TEXT("Unknown");
    }
}

const TCHAR* FDWCEditorMemoryDiagnostics::LexToString(const EDWCEditorMemoryAccounting Accounting)
{
    switch (Accounting)
    {
    case EDWCEditorMemoryAccounting::Resident: return TEXT("Resident");
    case EDWCEditorMemoryAccounting::Reservation: return TEXT("Reservation");
    case EDWCEditorMemoryAccounting::TransientEstimate: return TEXT("TransientEstimate");
    default: return TEXT("Unknown");
    }
}

FDWCEditorMemoryOwner::FDWCEditorMemoryOwner(const FDWCEditorMemoryOwnerRecord& Record)
    : OwnerId(FDWCEditorMemoryDiagnostics::RegisterOwner(Record))
{
}

FDWCEditorMemoryOwner::~FDWCEditorMemoryOwner()
{
    Reset();
}

FDWCEditorMemoryOwner::FDWCEditorMemoryOwner(FDWCEditorMemoryOwner&& Other) noexcept
    : OwnerId(Other.OwnerId)
{
    Other.OwnerId = 0;
}

FDWCEditorMemoryOwner& FDWCEditorMemoryOwner::operator=(FDWCEditorMemoryOwner&& Other) noexcept
{
    if (this != &Other)
    {
        Reset();
        OwnerId = Other.OwnerId;
        Other.OwnerId = 0;
    }
    return *this;
}

void FDWCEditorMemoryOwner::Update(const uint64 CurrentBytes, const int32 EntryCount) const
{
    FDWCEditorMemoryDiagnostics::UpdateOwner(OwnerId, CurrentBytes, EntryCount);
}

void FDWCEditorMemoryOwner::Reset()
{
    FDWCEditorMemoryDiagnostics::UnregisterOwner(OwnerId);
    OwnerId = 0;
}
