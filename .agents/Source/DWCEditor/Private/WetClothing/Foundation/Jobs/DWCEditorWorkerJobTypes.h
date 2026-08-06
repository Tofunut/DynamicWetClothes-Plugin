#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Async/DWCEditorAsyncOperationTypes.h"
#include "WetClothing/Foundation/Authoring/DWCEditorAuthoringTypes.h"

enum class EDWCEditorWorkerJobKind : uint8
{
    WrinkleAccumulatedPreview,
    WrinkleIncrementalPreview,
    WrinkleTransientPreview,
    TransparencyVisualization,
    TransparencyAlphaIncremental,
    TransparencyRevealColorIncremental,
    TransparencyAlphaDirtyReplay,
    TransparencyRevealColorDirtyReplay,
    WrinkleBake,
    TransparencyAutoBake,
    TransparencyFinalBake
};

enum class EDWCEditorWorkerJobPriority : uint8
{
    Background,
    UserInitiated,
    Interactive
};

enum class EDWCEditorWorkerJobCompletion : uint8
{
    Applied,
    Canceled,
    Superseded,
    Stale,
    Failed
};

enum class EDWCEditorWorkerJobLifecycleState : uint8
{
    PendingAdmission,
    Preparing,
    Ready,
    Running,
    CancelRequested,
    Finalizing,
    Completed
};

struct FDWCEditorWorkerMemoryEstimate
{
    uint64 ResidentSharedBytes = 0;
    uint64 SnapshotBytes = 0;
    uint64 WorkingBytes = 0;
    uint64 OutputBytes = 0;
    uint64 ScratchBytes = 0;

    uint64 GetTotalBytes() const
    {
        uint64 TotalBytes = 0;
        const uint64 Buckets[] =
        {
            ResidentSharedBytes,
            SnapshotBytes,
            WorkingBytes,
            OutputBytes,
            ScratchBytes
        };
        for (const uint64 BucketBytes : Buckets)
        {
            if (BucketBytes > MAX_uint64 - TotalBytes)
            {
                return MAX_uint64;
            }
            TotalBytes += BucketBytes;
        }
        return TotalBytes;
    }

    bool IsEmpty() const { return GetTotalBytes() == 0; }
};

struct FDWCEditorWorkerJobKey
{
    EDWCEditorWorkerJobKind Kind = EDWCEditorWorkerJobKind::WrinkleAccumulatedPreview;
    int32 MaterialSlotIndex = INDEX_NONE;
    FGuid LayerGuid;

    bool operator==(const FDWCEditorWorkerJobKey& Other) const
    {
        return Kind == Other.Kind &&
            MaterialSlotIndex == Other.MaterialSlotIndex &&
            LayerGuid == Other.LayerGuid;
    }

    friend uint32 GetTypeHash(const FDWCEditorWorkerJobKey& Key)
    {
        uint32 Hash = GetTypeHash(static_cast<uint8>(Key.Kind));
        Hash = HashCombine(Hash, GetTypeHash(Key.MaterialSlotIndex));
        return HashCombine(Hash, GetTypeHash(Key.LayerGuid));
    }
};

struct FDWCEditorWorkerJobDescriptor
{
    FDWCEditorWorkerJobKey Key;
    EDWCEditorAuthoringDomain Domain = EDWCEditorAuthoringDomain::None;
    uint64 DomainRevision = 0;
    EDWCEditorWorkerJobPriority Priority = EDWCEditorWorkerJobPriority::Background;
    EDWCEditorAsyncRequestPolicy RequestPolicy = EDWCEditorAsyncRequestPolicy::FIFO;
    uint64 EstimatedBytes = 0;
    FDWCEditorWorkerMemoryEstimate MemoryEstimate;
    FString DebugName;

    uint64 GetReservedBytes() const
    {
        const uint64 CategorizedBytes = MemoryEstimate.GetTotalBytes();
        return EstimatedBytes > 0 ? EstimatedBytes : CategorizedBytes;
    }

    EDWCEditorAsyncRequestPolicy GetRequestPolicy() const
    {
        return RequestPolicy;
    }
};

struct FDWCEditorWorkerJobTicket
{
    FDWCEditorWorkerJobKey Key;
    FGuid SessionEpoch;
    uint64 JobId = 0;
    uint64 Generation = 0;
    EDWCEditorAuthoringDomain Domain = EDWCEditorAuthoringDomain::None;
    uint64 DomainRevision = 0;

    bool IsValid() const { return JobId != 0 && Generation != 0; }
};

struct FDWCEditorWorkerJobResult
{
    virtual ~FDWCEditorWorkerJobResult() = default;

    bool bSucceeded = true;
    FString Error;
    uint64 ResultBytes = 0;
};

struct FDWCEditorWorkerJobDiagnostic
{
    FDWCEditorWorkerJobTicket Ticket;
    FString DebugName;
    EDWCEditorWorkerJobPriority Priority = EDWCEditorWorkerJobPriority::Background;
    EDWCEditorWorkerJobLifecycleState LifecycleState = EDWCEditorWorkerJobLifecycleState::PendingAdmission;
    EDWCEditorAsyncOperationState OperationState = EDWCEditorAsyncOperationState::Pending;
    EDWCEditorAsyncCancellationState CancellationState = EDWCEditorAsyncCancellationState::None;
    EDWCEditorAsyncRequestPolicy RequestPolicy = EDWCEditorAsyncRequestPolicy::FIFO;
    FDWCEditorWorkerMemoryEstimate MemoryEstimate;
    uint64 ReservedBytes = 0;
    uint64 ResultBytes = 0;
    double QueueSeconds = 0.0;
    double PrepareSeconds = 0.0;
    double WorkerSeconds = 0.0;
    double CommitSeconds = 0.0;
    double CancellationSeconds = 0.0;
};

struct FDWCEditorWorkerSchedulerDiagnostics
{
    int32 PendingAdmissionCount = 0;
    int32 PreparingCount = 0;
    int32 ReadyCount = 0;
    int32 ActiveCount = 0;
    uint64 ReservedBytes = 0;
    uint64 TotalBudgetBytes = 0;
    uint64 PerJobBudgetBytes = 0;
    uint64 HighWaterReservedBytes = 0;
    uint64 BudgetRejectionCount = 0;
    uint64 QueueRejectionCount = 0;
    uint64 MailboxReplacementCount = 0;
    uint64 AdmissionDeferredCount = 0;
    uint64 SingletonRejectionCount = 0;
    uint64 CompletedJobCount = 0;
    double TotalQueueSeconds = 0.0;
    double TotalWorkerSeconds = 0.0;
    double MaxQueueSeconds = 0.0;
    double MaxWorkerSeconds = 0.0;
    TArray<FDWCEditorWorkerJobDiagnostic> Jobs;
};
