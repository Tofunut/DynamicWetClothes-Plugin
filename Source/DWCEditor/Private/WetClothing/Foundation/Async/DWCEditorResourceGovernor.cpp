//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Foundation/Async/DWCEditorResourceGovernor.h"

#include "HAL/PlatformTime.h"
#include "Misc/ScopeLock.h"
#include "WetClothing/Foundation/Async/DWCEditorAsyncOperationContract.h"
#include "WetClothing/Foundation/Diagnostics/DWCEditorMemoryDiagnostics.h"

namespace
{
    constexpr EDWCEditorResourcePool ResourcePools[] =
    {
        EDWCEditorResourcePool::WorkerPrivateCPU,
        EDWCEditorResourcePool::AssetCommitCPU,
        EDWCEditorResourcePool::PreviewWorkspaceCPU,
        EDWCEditorResourcePool::SharedCacheCPU,
        EDWCEditorResourcePool::UploadStagingCPU,
        EDWCEditorResourcePool::PreviewGPU
    };

    bool FitsWithinBudget(const uint64 UsedBytes, const uint64 AddedBytes, const uint64 BudgetBytes)
    {
        return UsedBytes <= BudgetBytes && AddedBytes <= BudgetBytes - UsedBytes;
    }

    FString FormatBudgetFailure(
        const EDWCEditorResourcePool Pool,
        const uint64 RequestedBytes,
        const uint64 UsedBytes,
        const uint64 BudgetBytes)
    {
        return FString::Printf(
            TEXT("Resource pool %s cannot reserve %.2f MiB (used %.2f MiB, budget %.2f MiB)."),
            FDWCEditorAsyncOperationContract::LexToString(Pool),
            static_cast<double>(RequestedBytes) / FDWCEditorResourceBudgetConfig::MiB,
            static_cast<double>(UsedBytes) / FDWCEditorResourceBudgetConfig::MiB,
            static_cast<double>(BudgetBytes) / FDWCEditorResourceBudgetConfig::MiB);
    }

    EDWCEditorMemoryCategory GetDiagnosticCategory(const EDWCEditorResourcePool Pool)
    {
        switch (Pool)
        {
        case EDWCEditorResourcePool::SharedCacheCPU:
            return EDWCEditorMemoryCategory::SharedCacheCPU;
        case EDWCEditorResourcePool::UploadStagingCPU:
            return EDWCEditorMemoryCategory::UploadStagingCPU;
        case EDWCEditorResourcePool::PreviewGPU:
            return EDWCEditorMemoryCategory::PreviewGPU;
        case EDWCEditorResourcePool::PreviewWorkspaceCPU:
            return EDWCEditorMemoryCategory::PersistentEditorCPU;
        case EDWCEditorResourcePool::WorkerPrivateCPU:
        case EDWCEditorResourcePool::AssetCommitCPU:
        default:
            return EDWCEditorMemoryCategory::OperationPrivateCPU;
        }
    }
}

class FDWCEditorResourceGovernorState final
{
public:
    explicit FDWCEditorResourceGovernorState(const FDWCEditorResourceBudgetConfig& InConfig)
        : Config(InConfig)
    {
        Config.GlobalEditorCPUBytes = FMath::Max<uint64>(Config.GlobalEditorCPUBytes, 1);
        Config.WorkerPrivateCPUBytes = FMath::Max<uint64>(Config.WorkerPrivateCPUBytes, 1);
        Config.PreviewWorkspaceCPUBytes = FMath::Max<uint64>(Config.PreviewWorkspaceCPUBytes, 1);
        Config.SharedCacheCPUBytes = FMath::Max<uint64>(Config.SharedCacheCPUBytes, 1);
        Config.UploadStagingCPUBytes = FMath::Max<uint64>(Config.UploadStagingCPUBytes, 1);
        Config.PreviewGPUBytes = FMath::Max<uint64>(Config.PreviewGPUBytes, 1);

        for (const EDWCEditorResourcePool Pool : ResourcePools)
        {
            FPoolState& PoolState = Pools.Add(Pool);
            PoolState.BudgetBytes = Config.GetPoolBudgetBytes(Pool);
        }
    }

    ~FDWCEditorResourceGovernorState()
    {
        FDWCEditorMemoryDiagnostics::UnregisterCollector(MemoryDiagnosticCollectorName);
    }

    void RegisterMemoryDiagnostics(
        const TSharedRef<FDWCEditorResourceGovernorState, ESPMode::ThreadSafe>& SharedState)
    {
        MemoryDiagnosticCollectorName = FName(
            *FString::Printf(TEXT("DWCResourceGovernor_%p"), this));
        const TWeakPtr<FDWCEditorResourceGovernorState, ESPMode::ThreadSafe> WeakState = SharedState;
        const FString CollectorIdentity = MemoryDiagnosticCollectorName.ToString();
        FDWCEditorMemoryDiagnostics::RegisterCollector(
            MemoryDiagnosticCollectorName,
            [WeakState, CollectorIdentity](TArray<FDWCEditorMemoryOwnerRecord>& OutOwners)
            {
                const TSharedPtr<FDWCEditorResourceGovernorState, ESPMode::ThreadSafe> PinnedState =
                    WeakState.Pin();
                if (!PinnedState.IsValid())
                {
                    return;
                }

                const FDWCEditorResourceGovernorDiagnostics Diagnostics = PinnedState->GetDiagnostics();
                for (const FDWCEditorResourceReservationDiagnostic& Reservation : Diagnostics.Reservations)
                {
                    FDWCEditorMemoryOwnerRecord& Owner = OutOwners.AddDefaulted_GetRef();
                    Owner.Identifier = FString::Printf(
                        TEXT("%s:%llu"),
                        *CollectorIdentity,
                        Reservation.ReservationId);
                    Owner.Subsystem = TEXT("ResourceGovernor");
                    Owner.Resource = FName(
                        FDWCEditorAsyncOperationContract::LexToString(Reservation.Pool));
                    Owner.Category = GetDiagnosticCategory(Reservation.Pool);
                    Owner.Accounting = EDWCEditorMemoryAccounting::Reservation;
                    Owner.CurrentBytes = Reservation.ReservedBytes;
                    Owner.EntryCount = 1;
                    Owner.Context = FString::Printf(
                        TEXT("%s; operation=%s; slot=%d; id=%llu; generation=%llu"),
                        *Reservation.DebugName,
                        *Reservation.Owner.Key.Namespace.ToString(),
                        Reservation.Owner.Key.MaterialSlotIndex,
                        Reservation.Owner.OperationId,
                        Reservation.Owner.Generation);
                }
            });
    }

    uint64 TryAcquire(
        const FDWCEditorResourceReservationRequest& Request,
        EDWCEditorResourceAdmissionResult& OutResult,
        const bool bRecordTemporaryRejection,
        FString* OutError)
    {
        FScopeLock Lock(&Mutex);
        OutResult = EDWCEditorResourceAdmissionResult::InvalidRequest;
        if (OutError != nullptr)
        {
            OutError->Reset();
        }
        if (Request.Bytes == 0)
        {
            if (OutError != nullptr)
            {
                *OutError = TEXT("A memory reservation must request at least one byte.");
            }
            return 0;
        }

        FPoolState* PoolState = Pools.Find(Request.Pool);
        if (PoolState == nullptr)
        {
            if (OutError != nullptr)
            {
                *OutError = TEXT("The requested resource pool is not configured.");
            }
            return 0;
        }
        const bool bEnforcePoolBudget =
            Request.Pool == EDWCEditorResourcePool::PreviewGPU ||
            !Config.bAllowCPUPoolBorrowing;
        if (bEnforcePoolBudget &&
            !FitsWithinBudget(PoolState->UsedBytes, Request.Bytes, PoolState->BudgetBytes))
        {
            OutResult = EDWCEditorResourceAdmissionResult::TemporarilyUnavailable;
            if (bRecordTemporaryRejection)
            {
                ++PoolState->RejectionCount;
            }
            if (OutError != nullptr)
            {
                *OutError = FormatBudgetFailure(
                    Request.Pool,
                    Request.Bytes,
                    PoolState->UsedBytes,
                    PoolState->BudgetBytes);
            }
            return 0;
        }

        if (FDWCEditorAsyncOperationContract::IsCPUResourcePool(Request.Pool) &&
            !FitsWithinBudget(GlobalCPUUsedBytes, Request.Bytes, Config.GlobalEditorCPUBytes))
        {
            OutResult = EDWCEditorResourceAdmissionResult::TemporarilyUnavailable;
            if (bRecordTemporaryRejection)
            {
                ++GlobalCPURejectionCount;
            }
            if (OutError != nullptr)
            {
                *OutError = FString::Printf(
                    TEXT("The editor CPU resource budget cannot reserve %.2f MiB (used %.2f MiB, budget %.2f MiB)."),
                    static_cast<double>(Request.Bytes) / FDWCEditorResourceBudgetConfig::MiB,
                    static_cast<double>(GlobalCPUUsedBytes) / FDWCEditorResourceBudgetConfig::MiB,
                    static_cast<double>(Config.GlobalEditorCPUBytes) / FDWCEditorResourceBudgetConfig::MiB);
            }
            return 0;
        }

        const uint64 ReservationId = NextReservationId++;
        FReservation& Reservation = Reservations.Add(ReservationId);
        Reservation.Diagnostic.ReservationId = ReservationId;
        Reservation.Diagnostic.Pool = Request.Pool;
        Reservation.Diagnostic.ReservedBytes = Request.Bytes;
        Reservation.Diagnostic.Owner = Request.Owner;
        Reservation.Diagnostic.DebugName = Request.DebugName;
        Reservation.Diagnostic.AcquiredSeconds = FPlatformTime::Seconds();

        PoolState->UsedBytes += Request.Bytes;
        PoolState->HighWaterBytes = FMath::Max(PoolState->HighWaterBytes, PoolState->UsedBytes);
        if (FDWCEditorAsyncOperationContract::IsCPUResourcePool(Request.Pool))
        {
            GlobalCPUUsedBytes += Request.Bytes;
            GlobalCPUHighWaterBytes = FMath::Max(GlobalCPUHighWaterBytes, GlobalCPUUsedBytes);
        }
        OutResult = EDWCEditorResourceAdmissionResult::Admitted;
        return ReservationId;
    }

    bool TryGrow(const uint64 ReservationId, const uint64 AdditionalBytes, FString* OutError)
    {
        FScopeLock Lock(&Mutex);
        if (OutError != nullptr)
        {
            OutError->Reset();
        }
        if (AdditionalBytes == 0)
        {
            return Reservations.Contains(ReservationId);
        }

        FReservation* Reservation = Reservations.Find(ReservationId);
        if (Reservation == nullptr)
        {
            if (OutError != nullptr)
            {
                *OutError = TEXT("The memory reservation is no longer active.");
            }
            return false;
        }

        FPoolState& PoolState = Pools.FindChecked(Reservation->Diagnostic.Pool);
        const bool bEnforcePoolBudget =
            Reservation->Diagnostic.Pool == EDWCEditorResourcePool::PreviewGPU ||
            !Config.bAllowCPUPoolBorrowing;
        if (bEnforcePoolBudget &&
            !FitsWithinBudget(PoolState.UsedBytes, AdditionalBytes, PoolState.BudgetBytes))
        {
            ++PoolState.RejectionCount;
            if (OutError != nullptr)
            {
                *OutError = FormatBudgetFailure(
                    Reservation->Diagnostic.Pool,
                    AdditionalBytes,
                    PoolState.UsedBytes,
                    PoolState.BudgetBytes);
            }
            return false;
        }
        if (FDWCEditorAsyncOperationContract::IsCPUResourcePool(Reservation->Diagnostic.Pool) &&
            !FitsWithinBudget(GlobalCPUUsedBytes, AdditionalBytes, Config.GlobalEditorCPUBytes))
        {
            ++GlobalCPURejectionCount;
            if (OutError != nullptr)
            {
                *OutError = TEXT("Growing the reservation would exceed the editor CPU resource budget.");
            }
            return false;
        }
        if (AdditionalBytes > MAX_uint64 - Reservation->Diagnostic.ReservedBytes)
        {
            if (OutError != nullptr)
            {
                *OutError = TEXT("Growing the reservation would overflow its byte count.");
            }
            return false;
        }

        Reservation->Diagnostic.ReservedBytes += AdditionalBytes;
        PoolState.UsedBytes += AdditionalBytes;
        PoolState.HighWaterBytes = FMath::Max(PoolState.HighWaterBytes, PoolState.UsedBytes);
        if (FDWCEditorAsyncOperationContract::IsCPUResourcePool(Reservation->Diagnostic.Pool))
        {
            GlobalCPUUsedBytes += AdditionalBytes;
            GlobalCPUHighWaterBytes = FMath::Max(GlobalCPUHighWaterBytes, GlobalCPUUsedBytes);
        }
        return true;
    }

    bool TryResize(const uint64 ReservationId, const uint64 NewBytes, FString* OutError)
    {
        FScopeLock Lock(&Mutex);
        if (OutError != nullptr)
        {
            OutError->Reset();
        }

        FReservation* Reservation = Reservations.Find(ReservationId);
        if (Reservation == nullptr)
        {
            if (OutError != nullptr)
            {
                *OutError = TEXT("The memory reservation is no longer active.");
            }
            return false;
        }

        const uint64 CurrentBytes = Reservation->Diagnostic.ReservedBytes;
        if (NewBytes == CurrentBytes)
        {
            return true;
        }

        FPoolState& PoolState = Pools.FindChecked(Reservation->Diagnostic.Pool);
        if (NewBytes < CurrentBytes)
        {
            const uint64 ReleasedBytes = CurrentBytes - NewBytes;
            Reservation->Diagnostic.ReservedBytes = NewBytes;
            PoolState.UsedBytes = PoolState.UsedBytes >= ReleasedBytes
                ? PoolState.UsedBytes - ReleasedBytes
                : 0;
            if (FDWCEditorAsyncOperationContract::IsCPUResourcePool(Reservation->Diagnostic.Pool))
            {
                GlobalCPUUsedBytes = GlobalCPUUsedBytes >= ReleasedBytes
                    ? GlobalCPUUsedBytes - ReleasedBytes
                    : 0;
            }
            return true;
        }

        const uint64 AdditionalBytes = NewBytes - CurrentBytes;
        const bool bEnforcePoolBudget =
            Reservation->Diagnostic.Pool == EDWCEditorResourcePool::PreviewGPU ||
            !Config.bAllowCPUPoolBorrowing;
        if (bEnforcePoolBudget &&
            !FitsWithinBudget(PoolState.UsedBytes, AdditionalBytes, PoolState.BudgetBytes))
        {
            ++PoolState.RejectionCount;
            if (OutError != nullptr)
            {
                *OutError = FormatBudgetFailure(
                    Reservation->Diagnostic.Pool,
                    AdditionalBytes,
                    PoolState.UsedBytes,
                    PoolState.BudgetBytes);
            }
            return false;
        }
        if (FDWCEditorAsyncOperationContract::IsCPUResourcePool(Reservation->Diagnostic.Pool) &&
            !FitsWithinBudget(GlobalCPUUsedBytes, AdditionalBytes, Config.GlobalEditorCPUBytes))
        {
            ++GlobalCPURejectionCount;
            if (OutError != nullptr)
            {
                *OutError = TEXT("Resizing the reservation would exceed the editor CPU resource budget.");
            }
            return false;
        }

        Reservation->Diagnostic.ReservedBytes = NewBytes;
        PoolState.UsedBytes += AdditionalBytes;
        PoolState.HighWaterBytes = FMath::Max(PoolState.HighWaterBytes, PoolState.UsedBytes);
        if (FDWCEditorAsyncOperationContract::IsCPUResourcePool(Reservation->Diagnostic.Pool))
        {
            GlobalCPUUsedBytes += AdditionalBytes;
            GlobalCPUHighWaterBytes = FMath::Max(GlobalCPUHighWaterBytes, GlobalCPUUsedBytes);
        }
        return true;
    }

    void Release(const uint64 ReservationId)
    {
        FScopeLock Lock(&Mutex);
        FReservation Reservation;
        if (!Reservations.RemoveAndCopyValue(ReservationId, Reservation))
        {
            return;
        }

        FPoolState& PoolState = Pools.FindChecked(Reservation.Diagnostic.Pool);
        const uint64 Bytes = Reservation.Diagnostic.ReservedBytes;
        PoolState.UsedBytes = PoolState.UsedBytes >= Bytes ? PoolState.UsedBytes - Bytes : 0;
        if (FDWCEditorAsyncOperationContract::IsCPUResourcePool(Reservation.Diagnostic.Pool))
        {
            GlobalCPUUsedBytes = GlobalCPUUsedBytes >= Bytes ? GlobalCPUUsedBytes - Bytes : 0;
        }
    }

    uint64 GetReservedBytes(const uint64 ReservationId) const
    {
        FScopeLock Lock(&Mutex);
        const FReservation* Reservation = Reservations.Find(ReservationId);
        return Reservation != nullptr ? Reservation->Diagnostic.ReservedBytes : 0;
    }

    EDWCEditorResourcePool GetPool(const uint64 ReservationId) const
    {
        FScopeLock Lock(&Mutex);
        const FReservation* Reservation = Reservations.Find(ReservationId);
        return Reservation != nullptr
            ? Reservation->Diagnostic.Pool
            : EDWCEditorResourcePool::WorkerPrivateCPU;
    }

    FDWCEditorResourceGovernorDiagnostics GetDiagnostics() const
    {
        FScopeLock Lock(&Mutex);
        FDWCEditorResourceGovernorDiagnostics Diagnostics;
        Diagnostics.GlobalCPUUsedBytes = GlobalCPUUsedBytes;
        Diagnostics.GlobalCPUBudgetBytes = Config.GlobalEditorCPUBytes;
        Diagnostics.GlobalCPUHighWaterBytes = GlobalCPUHighWaterBytes;
        Diagnostics.GlobalCPURejectionCount = GlobalCPURejectionCount;

        for (const EDWCEditorResourcePool Pool : ResourcePools)
        {
            const FPoolState& PoolState = Pools.FindChecked(Pool);
            FDWCEditorResourcePoolDiagnostics& PoolDiagnostic = Diagnostics.Pools.AddDefaulted_GetRef();
            PoolDiagnostic.Pool = Pool;
            PoolDiagnostic.UsedBytes = PoolState.UsedBytes;
            PoolDiagnostic.BudgetBytes = PoolState.BudgetBytes;
            PoolDiagnostic.HighWaterBytes = PoolState.HighWaterBytes;
            PoolDiagnostic.RejectionCount = PoolState.RejectionCount;
        }
        for (const TPair<uint64, FReservation>& Pair : Reservations)
        {
            Diagnostics.Reservations.Add(Pair.Value.Diagnostic);
        }
        Diagnostics.Reservations.Sort(
            [](const FDWCEditorResourceReservationDiagnostic& A,
               const FDWCEditorResourceReservationDiagnostic& B)
            {
                return A.ReservationId < B.ReservationId;
            });
        return Diagnostics;
    }

    bool ShouldReportAdmissionFailures() const
    {
        return Config.bEnableAdmissionFailureDiagnostics;
    }

    void ResetDiagnosticCounters()
    {
        FScopeLock Lock(&Mutex);
        GlobalCPUHighWaterBytes = GlobalCPUUsedBytes;
        GlobalCPURejectionCount = 0;
        for (TPair<EDWCEditorResourcePool, FPoolState>& Pair : Pools)
        {
            Pair.Value.HighWaterBytes = Pair.Value.UsedBytes;
            Pair.Value.RejectionCount = 0;
        }
    }

private:
    struct FPoolState
    {
        uint64 UsedBytes = 0;
        uint64 BudgetBytes = 0;
        uint64 HighWaterBytes = 0;
        uint64 RejectionCount = 0;
    };

    struct FReservation
    {
        FDWCEditorResourceReservationDiagnostic Diagnostic;
    };

    mutable FCriticalSection Mutex;
    FDWCEditorResourceBudgetConfig Config;
    TMap<EDWCEditorResourcePool, FPoolState> Pools;
    TMap<uint64, FReservation> Reservations;
    uint64 NextReservationId = 1;
    uint64 GlobalCPUUsedBytes = 0;
    uint64 GlobalCPUHighWaterBytes = 0;
    uint64 GlobalCPURejectionCount = 0;
    FName MemoryDiagnosticCollectorName;
};

uint64 FDWCEditorResourceBudgetConfig::GetPoolBudgetBytes(const EDWCEditorResourcePool Pool) const
{
    switch (Pool)
    {
    case EDWCEditorResourcePool::WorkerPrivateCPU: return WorkerPrivateCPUBytes;
    case EDWCEditorResourcePool::AssetCommitCPU: return AssetCommitCPUBytes;
    case EDWCEditorResourcePool::PreviewWorkspaceCPU: return PreviewWorkspaceCPUBytes;
    case EDWCEditorResourcePool::SharedCacheCPU: return SharedCacheCPUBytes;
    case EDWCEditorResourcePool::UploadStagingCPU: return UploadStagingCPUBytes;
    case EDWCEditorResourcePool::PreviewGPU: return PreviewGPUBytes;
    default: return 0;
    }
}

FDWCEditorMemoryLease::FDWCEditorMemoryLease(
    TSharedPtr<FDWCEditorResourceGovernorState, ESPMode::ThreadSafe> InState,
    const uint64 InReservationId)
    : State(MoveTemp(InState))
    , ReservationId(InReservationId)
{
}

FDWCEditorMemoryLease::~FDWCEditorMemoryLease()
{
    Reset();
}

FDWCEditorMemoryLease::FDWCEditorMemoryLease(FDWCEditorMemoryLease&& Other) noexcept
    : State(MoveTemp(Other.State))
    , ReservationId(Other.ReservationId)
{
    Other.ReservationId = 0;
}

FDWCEditorMemoryLease& FDWCEditorMemoryLease::operator=(FDWCEditorMemoryLease&& Other) noexcept
{
    if (this != &Other)
    {
        Reset();
        State = MoveTemp(Other.State);
        ReservationId = Other.ReservationId;
        Other.ReservationId = 0;
    }
    return *this;
}

bool FDWCEditorMemoryLease::IsValid() const
{
    return State.IsValid() && ReservationId != 0 && State->GetReservedBytes(ReservationId) != 0;
}

uint64 FDWCEditorMemoryLease::GetReservedBytes() const
{
    return State.IsValid() && ReservationId != 0 ? State->GetReservedBytes(ReservationId) : 0;
}

EDWCEditorResourcePool FDWCEditorMemoryLease::GetPool() const
{
    return State.IsValid() && ReservationId != 0
        ? State->GetPool(ReservationId)
        : EDWCEditorResourcePool::WorkerPrivateCPU;
}

bool FDWCEditorMemoryLease::TryGrow(const uint64 AdditionalBytes, FString* OutError)
{
    if (!State.IsValid() || ReservationId == 0)
    {
        if (OutError != nullptr)
        {
            *OutError = TEXT("The memory lease is not active.");
        }
        return false;
    }
    return State->TryGrow(ReservationId, AdditionalBytes, OutError);
}

bool FDWCEditorMemoryLease::TryResize(const uint64 NewBytes, FString* OutError)
{
    if (NewBytes == 0)
    {
        Reset();
        if (OutError != nullptr)
        {
            OutError->Reset();
        }
        return true;
    }
    if (!State.IsValid() || ReservationId == 0)
    {
        if (OutError != nullptr)
        {
            *OutError = TEXT("The memory lease is not active.");
        }
        return false;
    }
    return State->TryResize(ReservationId, NewBytes, OutError);
}

bool FDWCEditorMemoryLease::ReleaseBytes(const uint64 Bytes, FString* OutError)
{
    const uint64 CurrentBytes = GetReservedBytes();
    if (Bytes > CurrentBytes)
    {
        if (OutError != nullptr)
        {
            *OutError = TEXT("Cannot release more bytes than the lease owns.");
        }
        return false;
    }
    return TryResize(CurrentBytes - Bytes, OutError);
}

void FDWCEditorMemoryLease::Reset()
{
    TSharedPtr<FDWCEditorResourceGovernorState, ESPMode::ThreadSafe> StateToRelease = MoveTemp(State);
    const uint64 ReservationToRelease = ReservationId;
    ReservationId = 0;
    if (StateToRelease.IsValid() && ReservationToRelease != 0)
    {
        StateToRelease->Release(ReservationToRelease);
    }
}

FDWCEditorResourceGovernor::FDWCEditorResourceGovernor(
    const FDWCEditorResourceBudgetConfig& InConfig)
    : State(MakeShared<FDWCEditorResourceGovernorState, ESPMode::ThreadSafe>(InConfig))
{
    State->RegisterMemoryDiagnostics(State);
}

uint64 FDWCEditorMemoryLeaseSet::GetReservedBytes() const
{
    uint64 TotalBytes = 0;
    for (const FDWCEditorMemoryLease& Lease : Leases)
    {
        const uint64 Bytes = Lease.GetReservedBytes();
        if (Bytes > MAX_uint64 - TotalBytes)
        {
            return MAX_uint64;
        }
        TotalBytes += Bytes;
    }

    return TotalBytes;
}

uint64 FDWCEditorMemoryLeaseSet::GetReservedBytes(const EDWCEditorResourcePool Pool) const
{
    uint64 TotalBytes = 0;
    for (const FDWCEditorMemoryLease& Lease : Leases)
    {
        if (Lease.GetPool() == Pool)
        {
            const uint64 Bytes = Lease.GetReservedBytes();
            if (Bytes > MAX_uint64 - TotalBytes)
            {
                return MAX_uint64;
            }
            TotalBytes += Bytes;
        }
    }
    return TotalBytes;
}

FDWCEditorMemoryLease FDWCEditorResourceGovernor::TryAcquire(
    const FDWCEditorResourceReservationRequest& Request,
    FString* OutError)
{
    EDWCEditorResourceAdmissionResult Result = EDWCEditorResourceAdmissionResult::InvalidRequest;
    uint64 ReservationId = State->TryAcquire(Request, Result, true, OutError);
    if (ReservationId == 0 && Result == EDWCEditorResourceAdmissionResult::TemporarilyUnavailable &&
        TryRelievePressure(Request))
    {
        ReservationId = State->TryAcquire(Request, Result, false, OutError);
    }
    if (ReservationId == 0 &&
        Result == EDWCEditorResourceAdmissionResult::TemporarilyUnavailable &&
        State->ShouldReportAdmissionFailures())
    {
        FDWCEditorMemoryDiagnostics::ReportAdmissionFailure(
            Request,
            State->GetDiagnostics(),
            OutError != nullptr ? *OutError : TEXT("The resource request remained unavailable after pressure reclaim."));
    }
    return ReservationId != 0
        ? FDWCEditorMemoryLease(State, ReservationId)
        : FDWCEditorMemoryLease();
}

FDWCEditorMemoryLease FDWCEditorResourceGovernor::TryAcquireForAdmission(
    const FDWCEditorResourceReservationRequest& Request,
    EDWCEditorResourceAdmissionResult& OutResult,
    FString* OutError)
{
    uint64 ReservationId = State->TryAcquire(Request, OutResult, false, OutError);
    if (ReservationId == 0 && OutResult == EDWCEditorResourceAdmissionResult::TemporarilyUnavailable &&
        TryRelievePressure(Request))
    {
        ReservationId = State->TryAcquire(Request, OutResult, false, OutError);
    }
    return ReservationId != 0
        ? FDWCEditorMemoryLease(State, ReservationId)
        : FDWCEditorMemoryLease();
}

FDWCEditorResourceGovernorDiagnostics FDWCEditorResourceGovernor::GetDiagnostics() const
{
    return State->GetDiagnostics();
}

void FDWCEditorResourceGovernor::ResetDiagnosticCounters()
{
    State->ResetDiagnosticCounters();
}

FDWCEditorMemoryLeaseSet FDWCEditorResourceGovernor::TryAcquireBundleForAdmission(
    const TArray<FDWCEditorResourceReservationRequest>& Requests,
    EDWCEditorResourceAdmissionResult& OutResult,
    FString* OutError)
{
    check(IsInGameThread());
    OutResult = EDWCEditorResourceAdmissionResult::InvalidRequest;
    if (OutError != nullptr)
    {
        OutError->Reset();
    }
    if (Requests.IsEmpty())
    {
        if (OutError != nullptr)
        {
            *OutError = TEXT("A resource reservation bundle must contain at least one request.");
        }
        return {};
    }

    TMap<EDWCEditorResourcePool, FDWCEditorResourceReservationRequest> MergedRequests;
    const FDWCEditorAsyncOperationIdentity ExpectedOwner = Requests[0].Owner;
    if (!ExpectedOwner.IsValid())
    {
        if (OutError != nullptr)
        {
            *OutError = TEXT("A resource reservation bundle requires a valid operation owner.");
        }
        return {};
    }
    for (const FDWCEditorResourceReservationRequest& Request : Requests)
    {
        const bool bSameOwner = Request.Owner.IsSameRequestKey(ExpectedOwner) &&
            Request.Owner.OperationId == ExpectedOwner.OperationId &&
            Request.Owner.Generation == ExpectedOwner.Generation &&
            Request.Owner.Domain == ExpectedOwner.Domain &&
            Request.Owner.DomainRevision == ExpectedOwner.DomainRevision;
        if (Request.Bytes == 0 || !bSameOwner)
        {
            if (OutError != nullptr)
            {
                *OutError = TEXT("Every reservation in a bundle must be non-zero and share one operation owner.");
            }
            return {};
        }
        FDWCEditorResourceReservationRequest& Merged = MergedRequests.FindOrAdd(Request.Pool);
        if (Merged.Bytes == 0)
        {
            Merged = Request;
        }
        else
        {
            if (Request.Bytes > MAX_uint64 - Merged.Bytes)
            {
                if (OutError != nullptr)
                {
                    *OutError = TEXT("The resource reservation bundle byte count overflowed.");
                }
                return {};
            }
            Merged.Bytes += Request.Bytes;
            if (!Request.DebugName.IsEmpty())
            {
                Merged.DebugName = Request.DebugName;
            }
        }
    }

    TArray<FDWCEditorResourceReservationRequest> OrderedRequests;
    MergedRequests.GenerateValueArray(OrderedRequests);
    OrderedRequests.Sort([](const FDWCEditorResourceReservationRequest& A,
                            const FDWCEditorResourceReservationRequest& B)
    {
        return static_cast<uint8>(A.Pool) < static_cast<uint8>(B.Pool);
    });

    FDWCEditorMemoryLeaseSet Result;
    Result.Leases.Reserve(OrderedRequests.Num());
    for (const FDWCEditorResourceReservationRequest& Request : OrderedRequests)
    {
        EDWCEditorResourceAdmissionResult RequestResult =
            EDWCEditorResourceAdmissionResult::InvalidRequest;
        FDWCEditorMemoryLease Lease = TryAcquireForAdmission(Request, RequestResult, OutError);
        if (!Lease.IsValid())
        {
            Result.Reset();
            OutResult = RequestResult;
            return {};
        }
        Result.Leases.Add(MoveTemp(Lease));
    }
    OutResult = EDWCEditorResourceAdmissionResult::Admitted;
    return Result;
}

void FDWCEditorResourceGovernor::SetPressureHandler(FPressureHandler InHandler)
{
    FScopeLock Lock(&PressureHandlerMutex);
    PressureHandler = MoveTemp(InHandler);
}

bool FDWCEditorResourceGovernor::TryRelievePressure(
    const FDWCEditorResourceReservationRequest& Request) const
{
    if (!IsInGameThread())
    {
        return false;
    }

    FPressureHandler HandlerCopy;
    {
        FScopeLock Lock(&PressureHandlerMutex);
        HandlerCopy = PressureHandler;
    }
    return HandlerCopy ? HandlerCopy(Request) : false;
}
