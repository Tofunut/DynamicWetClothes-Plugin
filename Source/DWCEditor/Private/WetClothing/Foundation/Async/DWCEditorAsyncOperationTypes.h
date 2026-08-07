//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Authoring/DWCEditorAuthoringTypes.h"

enum class EDWCEditorAsyncOperationState : uint8
{
    Pending,
    Admitted,
    Preparing,
    Ready,
    Running,
    CommitPending,
    Committing,
    Retiring,
    Completed
};

enum class EDWCEditorAsyncCancellationState : uint8
{
    None,
    CancelRequested,
    CancelAcknowledged
};

enum class EDWCEditorAsyncRequestPolicy : uint8
{
    // Keep at most one waiting request per key. A newer request cancels active
    // obsolete work and replaces the lightweight pending prepare closure.
    LatestWins,
    // Preserve every request and execute requests with the same key in order.
    FIFO,
    // Reject a request while another request with the same key is outstanding.
    Singleton
};

enum class EDWCEditorResourcePool : uint8
{
    WorkerPrivateCPU,
    PreviewWorkspaceCPU,
    SpatialCacheCPU,
    UploadStagingCPU,
    PreviewGPU
};

enum class EDWCEditorResourceAdmissionResult : uint8
{
    Admitted,
    TemporarilyUnavailable,
    InvalidRequest
};

struct FDWCEditorAsyncOperationKey
{
    FName Namespace;
    int32 MaterialSlotIndex = INDEX_NONE;
    FGuid ResourceGuid;

    bool IsValid() const { return !Namespace.IsNone(); }

    bool operator==(const FDWCEditorAsyncOperationKey& Other) const
    {
        return Namespace == Other.Namespace &&
            MaterialSlotIndex == Other.MaterialSlotIndex &&
            ResourceGuid == Other.ResourceGuid;
    }

    friend uint32 GetTypeHash(const FDWCEditorAsyncOperationKey& Key)
    {
        uint32 Hash = GetTypeHash(Key.Namespace);
        Hash = HashCombine(Hash, GetTypeHash(Key.MaterialSlotIndex));
        return HashCombine(Hash, GetTypeHash(Key.ResourceGuid));
    }
};

struct FDWCEditorAsyncOperationIdentity
{
    FDWCEditorAsyncOperationKey Key;
    FGuid SessionEpoch;
    uint64 OperationId = 0;
    uint64 Generation = 0;
    EDWCEditorAuthoringDomain Domain = EDWCEditorAuthoringDomain::None;
    uint64 DomainRevision = 0;

    bool IsValid() const
    {
        return Key.IsValid() && SessionEpoch.IsValid() && OperationId != 0 && Generation != 0;
    }

    bool IsSameRequestKey(const FDWCEditorAsyncOperationIdentity& Other) const
    {
        return Key == Other.Key && SessionEpoch == Other.SessionEpoch;
    }
};

struct FDWCEditorMemoryBreakdown
{
    uint64 SharedResidentBytes = 0;
    uint64 SnapshotBytes = 0;
    uint64 WorkingBytes = 0;
    uint64 OutputBytes = 0;
    uint64 ScratchBytes = 0;
    uint64 UploadStagingBytes = 0;

    bool TryGetOperationPrivateBytes(uint64& OutBytes) const;
    bool TryGetTotalDescribedBytes(uint64& OutBytes) const;
    bool IsEmpty() const;
};

struct FDWCEditorResourceReservationRequest
{
    EDWCEditorResourcePool Pool = EDWCEditorResourcePool::WorkerPrivateCPU;
    uint64 Bytes = 0;
    FDWCEditorAsyncOperationIdentity Owner;
    FString DebugName;
};

struct FDWCEditorResourcePoolDiagnostics
{
    EDWCEditorResourcePool Pool = EDWCEditorResourcePool::WorkerPrivateCPU;
    uint64 UsedBytes = 0;
    uint64 BudgetBytes = 0;
    uint64 HighWaterBytes = 0;
    uint64 RejectionCount = 0;
};

struct FDWCEditorResourceReservationDiagnostic
{
    uint64 ReservationId = 0;
    EDWCEditorResourcePool Pool = EDWCEditorResourcePool::WorkerPrivateCPU;
    uint64 ReservedBytes = 0;
    FDWCEditorAsyncOperationIdentity Owner;
    FString DebugName;
    double AcquiredSeconds = 0.0;
};

struct FDWCEditorAsyncOperationDiagnostic
{
    FDWCEditorAsyncOperationIdentity Identity;
    EDWCEditorAsyncRequestPolicy RequestPolicy = EDWCEditorAsyncRequestPolicy::FIFO;
    EDWCEditorAsyncOperationState State = EDWCEditorAsyncOperationState::Pending;
    EDWCEditorAsyncCancellationState CancellationState = EDWCEditorAsyncCancellationState::None;
    FDWCEditorMemoryBreakdown Memory;
    uint64 ReservedBytes = 0;
    double SubmittedSeconds = 0.0;
    double StateEnteredSeconds = 0.0;
    double CancelRequestedSeconds = 0.0;
};

struct FDWCEditorResourceGovernorDiagnostics
{
    uint64 GlobalCPUUsedBytes = 0;
    uint64 GlobalCPUBudgetBytes = 0;
    uint64 GlobalCPUHighWaterBytes = 0;
    uint64 GlobalCPURejectionCount = 0;
    TArray<FDWCEditorResourcePoolDiagnostics> Pools;
    TArray<FDWCEditorResourceReservationDiagnostic> Reservations;
};
