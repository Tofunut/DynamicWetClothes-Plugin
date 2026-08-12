//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Async/DWCEditorResourceGovernor.h"

/**
 * Move-only ownership contract that keeps a live CPU/GPU allocation and its
 * resource-governor reservation in sync.
 *
 * The caller builds or resizes its payload while an operation-phase lease is
 * active, then adopts the resulting allocation before that phase lease is
 * released. A failed adoption leaves the previous accounting unchanged.
 */
class FDWCEditorAccountedMemory final
{
public:
    FDWCEditorAccountedMemory() = default;
    ~FDWCEditorAccountedMemory() = default;

    FDWCEditorAccountedMemory(const FDWCEditorAccountedMemory&) = delete;
    FDWCEditorAccountedMemory& operator=(const FDWCEditorAccountedMemory&) = delete;
    FDWCEditorAccountedMemory(FDWCEditorAccountedMemory&&) noexcept = default;
    FDWCEditorAccountedMemory& operator=(FDWCEditorAccountedMemory&&) noexcept = default;

    void Configure(
        TSharedPtr<FDWCEditorResourceGovernor> InGovernor,
        EDWCEditorResourcePool InPool,
        const FDWCEditorAsyncOperationIdentity& InOwner,
        FString InDebugName);

    /** Adopts an already-built allocation without disturbing the old lease on failure. */
    bool TryAdoptActualBytes(uint64 NewActualBytes, FString* OutError = nullptr);

    /** Releases both the tracked allocation and its reservation. */
    void Reset();

    uint64 GetActualBytes() const { return ActualBytes; }
    uint64 GetReservedBytes() const { return MemoryLease.GetReservedBytes(); }
    uint64 GetAccountingDriftBytes() const
    {
        const uint64 ReservedBytes = GetReservedBytes();
        return ReservedBytes >= ActualBytes ? ReservedBytes - ActualBytes : 0;
    }
    bool IsAccounted() const
    {
        return ActualBytes == 0 || !ResourceGovernor.IsValid() ||
            (MemoryLease.IsValid() && MemoryLease.GetReservedBytes() >= ActualBytes);
    }

private:
    TSharedPtr<FDWCEditorResourceGovernor> ResourceGovernor;
    FDWCEditorAsyncOperationIdentity Owner;
    EDWCEditorResourcePool Pool = EDWCEditorResourcePool::PreviewWorkspaceCPU;
    FString DebugName;
    FDWCEditorMemoryLease MemoryLease;
    uint64 ActualBytes = 0;
};
