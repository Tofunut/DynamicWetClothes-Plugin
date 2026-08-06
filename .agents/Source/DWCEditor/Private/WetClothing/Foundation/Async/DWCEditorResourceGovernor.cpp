#include "WetClothing/Foundation/Async/DWCEditorResourceGovernor.h"

#include "HAL/PlatformTime.h"
#include "Misc/ScopeLock.h"
#include "WetClothing/Foundation/Async/DWCEditorAsyncOperationContract.h"

namespace
{
    constexpr EDWCEditorResourcePool ResourcePools[] =
    {
        EDWCEditorResourcePool::WorkerPrivateCPU,
        EDWCEditorResourcePool::PreviewWorkspaceCPU,
        EDWCEditorResourcePool::SpatialCacheCPU,
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
        Config.SpatialCacheCPUBytes = FMath::Max<uint64>(Config.SpatialCacheCPUBytes, 1);
        Config.UploadStagingCPUBytes = FMath::Max<uint64>(Config.UploadStagingCPUBytes, 1);
        Config.PreviewGPUBytes = FMath::Max<uint64>(Config.PreviewGPUBytes, 1);

        for (const EDWCEditorResourcePool Pool : ResourcePools)
        {
            FPoolState& PoolState = Pools.Add(Pool);
            PoolState.BudgetBytes = Config.GetPoolBudgetBytes(Pool);
        }
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
        if (!FitsWithinBudget(PoolState->UsedBytes, Request.Bytes, PoolState->BudgetBytes))
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
        if (!FitsWithinBudget(PoolState.UsedBytes, AdditionalBytes, PoolState.BudgetBytes))
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
};

uint64 FDWCEditorResourceBudgetConfig::GetPoolBudgetBytes(const EDWCEditorResourcePool Pool) const
{
    switch (Pool)
    {
    case EDWCEditorResourcePool::WorkerPrivateCPU: return WorkerPrivateCPUBytes;
    case EDWCEditorResourcePool::PreviewWorkspaceCPU: return PreviewWorkspaceCPUBytes;
    case EDWCEditorResourcePool::SpatialCacheCPU: return SpatialCacheCPUBytes;
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
}

FDWCEditorMemoryLease FDWCEditorResourceGovernor::TryAcquire(
    const FDWCEditorResourceReservationRequest& Request,
    FString* OutError)
{
    EDWCEditorResourceAdmissionResult Result = EDWCEditorResourceAdmissionResult::InvalidRequest;
    const uint64 ReservationId = State->TryAcquire(Request, Result, true, OutError);
    return ReservationId != 0
        ? FDWCEditorMemoryLease(State, ReservationId)
        : FDWCEditorMemoryLease();
}

FDWCEditorMemoryLease FDWCEditorResourceGovernor::TryAcquireForAdmission(
    const FDWCEditorResourceReservationRequest& Request,
    EDWCEditorResourceAdmissionResult& OutResult,
    FString* OutError)
{
    const uint64 ReservationId = State->TryAcquire(Request, OutResult, false, OutError);
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
