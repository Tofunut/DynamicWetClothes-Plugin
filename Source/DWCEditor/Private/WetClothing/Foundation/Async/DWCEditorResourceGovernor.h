//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Async/DWCEditorAsyncOperationTypes.h"

struct FDWCEditorResourceBudgetConfig
{
    static constexpr uint64 MiB = 1024ull * 1024ull;

    uint64 GlobalEditorCPUBytes = 1024ull * MiB;
    uint64 WorkerPrivateCPUBytes = 512ull * MiB;
    uint64 PreviewWorkspaceCPUBytes = 640ull * MiB;
    uint64 SpatialCacheCPUBytes = 64ull * MiB;
    uint64 UploadStagingCPUBytes = 64ull * MiB;
    uint64 PreviewGPUBytes = 384ull * MiB;

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
    void Reset();

private:
    friend class FDWCEditorResourceGovernor;

    FDWCEditorMemoryLease(
        TSharedPtr<FDWCEditorResourceGovernorState, ESPMode::ThreadSafe> InState,
        uint64 InReservationId);

    TSharedPtr<FDWCEditorResourceGovernorState, ESPMode::ThreadSafe> State;
    uint64 ReservationId = 0;
};

class FDWCEditorResourceGovernor
{
public:
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

    FDWCEditorResourceGovernorDiagnostics GetDiagnostics() const;
    void ResetDiagnosticCounters();

private:
    TSharedRef<FDWCEditorResourceGovernorState, ESPMode::ThreadSafe> State;
};
