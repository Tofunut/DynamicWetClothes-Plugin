//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Async/DWCEditorAsyncOperationTypes.h"

struct FDWCEditorResourceBudgetConfig
{
    static constexpr uint64 MiB = 1024ull * 1024ull;

    uint64 GlobalEditorCPUBytes = 1536ull * MiB;
    uint64 WorkerPrivateCPUBytes = 512ull * MiB;
    uint64 AssetCommitCPUBytes = 384ull * MiB;
    uint64 PreviewWorkspaceCPUBytes = 768ull * MiB;
    uint64 SharedCacheCPUBytes = 256ull * MiB;
    uint64 UploadStagingCPUBytes = 64ull * MiB;
    uint64 PreviewGPUBytes = 384ull * MiB;

    /** CPU pool budgets are retention targets when a process-wide broker is present. */
    bool bAllowCPUPoolBorrowing = false;
    /** Emits throttled owner snapshots when the process-wide editor governor rejects a final request. */
    bool bEnableAdmissionFailureDiagnostics = false;

    uint64 GetPoolBudgetBytes(EDWCEditorResourcePool Pool) const;
};

class FDWCEditorResourceGovernorState;

class FDWCEditorMemoryLease
{
public:
    FDWCEditorMemoryLease() = default;
    ~FDWCEditorMemoryLease();

    FDWCEditorMemoryLease(const FDWCEditorMemoryLease&) = delete;
    FDWCEditorMemoryLease& operator=(const FDWCEditorMemoryLease&) = delete;

    FDWCEditorMemoryLease(FDWCEditorMemoryLease&& Other) noexcept;
    FDWCEditorMemoryLease& operator=(FDWCEditorMemoryLease&& Other) noexcept;

    bool IsValid() const;
    uint64 GetReservationId() const { return ReservationId; }
    uint64 GetReservedBytes() const;
    EDWCEditorResourcePool GetPool() const;

    bool TryGrow(uint64 AdditionalBytes, FString* OutError = nullptr);
    bool TryResize(uint64 NewBytes, FString* OutError = nullptr);
    bool ReleaseBytes(uint64 Bytes, FString* OutError = nullptr);
    void Reset();

private:
    friend class FDWCEditorResourceGovernor;

    FDWCEditorMemoryLease(
        TSharedPtr<FDWCEditorResourceGovernorState, ESPMode::ThreadSafe> InState,
        uint64 InReservationId);

    TSharedPtr<FDWCEditorResourceGovernorState, ESPMode::ThreadSafe> State;
    uint64 ReservationId = 0;
};

/** Move-only all-or-nothing ownership for a set of resource-pool reservations. */
class FDWCEditorMemoryLeaseSet
{
public:
    FDWCEditorMemoryLeaseSet() = default;
    ~FDWCEditorMemoryLeaseSet() = default;

    FDWCEditorMemoryLeaseSet(const FDWCEditorMemoryLeaseSet&) = delete;
    FDWCEditorMemoryLeaseSet& operator=(const FDWCEditorMemoryLeaseSet&) = delete;
    FDWCEditorMemoryLeaseSet(FDWCEditorMemoryLeaseSet&&) noexcept = default;
    FDWCEditorMemoryLeaseSet& operator=(FDWCEditorMemoryLeaseSet&&) noexcept = default;

    bool IsValid() const { return !Leases.IsEmpty(); }
    int32 Num() const { return Leases.Num(); }
    uint64 GetReservedBytes() const;
    uint64 GetReservedBytes(EDWCEditorResourcePool Pool) const;
    /** Removes and returns the reservation for one pool without releasing it. */
    FDWCEditorMemoryLease TakeLease(EDWCEditorResourcePool Pool);
    void Reset() { Leases.Reset(); }

private:
    friend class FDWCEditorResourceGovernor;
    TArray<FDWCEditorMemoryLease> Leases;
};

class FDWCEditorResourceGovernor
{
public:
    using FPressureHandler = TFunction<bool(const FDWCEditorResourceReservationRequest&)>;

    explicit FDWCEditorResourceGovernor(
        const FDWCEditorResourceBudgetConfig& InConfig = FDWCEditorResourceBudgetConfig());

    FDWCEditorResourceGovernor(const FDWCEditorResourceGovernor&) = delete;
    FDWCEditorResourceGovernor& operator=(const FDWCEditorResourceGovernor&) = delete;

    FDWCEditorMemoryLease TryAcquire(
        const FDWCEditorResourceReservationRequest& Request,
        FString* OutError = nullptr);
    FDWCEditorMemoryLease TryAcquireForAdmission(
        const FDWCEditorResourceReservationRequest& Request,
        EDWCEditorResourceAdmissionResult& OutResult,
        FString* OutError = nullptr);
    /** Acquires one merged reservation per pool and rolls every lease back on failure. */
    FDWCEditorMemoryLeaseSet TryAcquireBundleForAdmission(
        const TArray<FDWCEditorResourceReservationRequest>& Requests,
        EDWCEditorResourceAdmissionResult& OutResult,
        FString* OutError = nullptr);

    FDWCEditorResourceGovernorDiagnostics GetDiagnostics() const;
    void ResetDiagnosticCounters();
    void SetPressureHandler(FPressureHandler InHandler);

private:
    bool TryRelievePressure(const FDWCEditorResourceReservationRequest& Request) const;

    TSharedRef<FDWCEditorResourceGovernorState, ESPMode::ThreadSafe> State;
    mutable FCriticalSection PressureHandlerMutex;
    FPressureHandler PressureHandler;
};
