//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Async/DWCEditorAsyncOperationTypes.h"
#include "WetClothing/Foundation/Authoring/DWCEditorAuthoringTypes.h"

class FDWCEditorCancellationToken;

enum class EDWCEditorWorkerJobKind : uint8
{
    WrinkleAccumulatedPreview,
    WrinkleIncrementalPreview,
    WrinkleTransientPreview,
    WrinkleHoverPreview,
    TransparencyVisualization,
    TransparencyAlphaIncremental,
    TransparencyRevealColorIncremental,
    TransparencyAlphaDirtyReplay,
    TransparencyRevealColorDirtyReplay,
    TransparencyRevealColorCommit,
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
    PendingPhaseAdmission,
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

    FDWCEditorMemoryBreakdown ToMemoryBreakdown() const
    {
        FDWCEditorMemoryBreakdown Result;
        Result.SharedResidentBytes = ResidentSharedBytes;
        Result.SnapshotBytes = SnapshotBytes;
        Result.WorkingBytes = WorkingBytes;
        Result.OutputBytes = OutputBytes;
        Result.ScratchBytes = ScratchBytes;
        return Result;
    }
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

    FDWCEditorAsyncOperationKey ToOperationKey() const
    {
        FDWCEditorAsyncOperationKey Result;
        switch (Kind)
        {
        case EDWCEditorWorkerJobKind::WrinkleAccumulatedPreview:
            Result.Namespace = TEXT("WrinkleAccumulatedPreview");
            break;
        case EDWCEditorWorkerJobKind::WrinkleIncrementalPreview:
            Result.Namespace = TEXT("WrinkleIncrementalPreview");
            break;
        case EDWCEditorWorkerJobKind::WrinkleTransientPreview:
            Result.Namespace = TEXT("WrinkleTransientPreview");
            break;
        case EDWCEditorWorkerJobKind::WrinkleHoverPreview:
            Result.Namespace = TEXT("WrinkleHoverPreview");
            break;
        case EDWCEditorWorkerJobKind::TransparencyVisualization:
            Result.Namespace = TEXT("TransparencyVisualization");
            break;
        case EDWCEditorWorkerJobKind::TransparencyAlphaIncremental:
            Result.Namespace = TEXT("TransparencyAlphaIncremental");
            break;
        case EDWCEditorWorkerJobKind::TransparencyRevealColorIncremental:
            Result.Namespace = TEXT("TransparencyRevealColorIncremental");
            break;
        case EDWCEditorWorkerJobKind::TransparencyAlphaDirtyReplay:
            Result.Namespace = TEXT("TransparencyAlphaDirtyReplay");
            break;
        case EDWCEditorWorkerJobKind::TransparencyRevealColorDirtyReplay:
            Result.Namespace = TEXT("TransparencyRevealColorDirtyReplay");
            break;
        case EDWCEditorWorkerJobKind::TransparencyRevealColorCommit:
            Result.Namespace = TEXT("TransparencyRevealColorCommit");
            break;
        case EDWCEditorWorkerJobKind::WrinkleBake:
            Result.Namespace = TEXT("WrinkleBake");
            break;
        case EDWCEditorWorkerJobKind::TransparencyAutoBake:
            Result.Namespace = TEXT("TransparencyAutoBake");
            break;
        case EDWCEditorWorkerJobKind::TransparencyFinalBake:
            Result.Namespace = TEXT("TransparencyFinalBake");
            break;
        }
        Result.MaterialSlotIndex = MaterialSlotIndex;
        Result.ResourceGuid = LayerGuid;
        return Result;
    }
};

struct FDWCEditorWorkerJobDescriptor
{
    FDWCEditorWorkerJobKey Key;
    EDWCEditorAuthoringDomain Domain = EDWCEditorAuthoringDomain::None;
    uint64 DomainRevision = 0;
    EDWCEditorWorkerJobPriority Priority = EDWCEditorWorkerJobPriority::Background;
    EDWCEditorAsyncRequestPolicy RequestPolicy = EDWCEditorAsyncRequestPolicy::FIFO;
    FDWCEditorWorkerMemoryEstimate MemoryEstimate;
    FString DebugName;

    uint64 GetReservedBytes() const
    {
        return MemoryEstimate.GetTotalBytes();
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

    FDWCEditorAsyncOperationIdentity ToOperationIdentity() const
    {
        FDWCEditorAsyncOperationIdentity Result;
        Result.Key = Key.ToOperationKey();
        Result.SessionEpoch = SessionEpoch;
        Result.OperationId = JobId;
        Result.Generation = Generation;
        Result.Domain = Domain;
        Result.DomainRevision = DomainRevision;
        return Result;
    }
};

struct FDWCEditorWorkerJobResult
{
    virtual ~FDWCEditorWorkerJobResult() = default;

    bool bSucceeded = true;
    bool bIsPhaseContinuation = false;
    FString Error;
    uint64 ResultBytes = 0;
};

using FDWCEditorWorkerPhaseWork = TFunction<TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe>(
    const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>&)>;

/**
 * A non-terminal worker result. The scheduler keeps the same ticket and RAII
 * lease while it admits the exact memory required by the next phase.
 */
struct FDWCEditorWorkerPhaseContinuationResult final : FDWCEditorWorkerJobResult
{
    FDWCEditorWorkerPhaseContinuationResult()
    {
        bIsPhaseContinuation = true;
    }

    FDWCEditorWorkerPhaseWork NextWork;
    FDWCEditorWorkerMemoryEstimate RetainedMemoryEstimate;
    FDWCEditorWorkerMemoryEstimate NextPhaseMemoryEstimate;
    FName NextPhaseName;
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
    FDWCEditorResourceGovernorDiagnostics Resources;
    int32 PendingAdmissionCount = 0;
    int32 PendingPhaseAdmissionCount = 0;
    int32 PreparingCount = 0;
    int32 ReadyCount = 0;
    int32 ActiveCount = 0;
    uint64 ReservedBytes = 0;
    uint64 RetainedPhaseBytes = 0;
    uint64 TotalBudgetBytes = 0;
    uint64 PerJobBudgetBytes = 0;
    uint64 HighWaterReservedBytes = 0;
    uint64 BudgetRejectionCount = 0;
    uint64 QueueRejectionCount = 0;
    uint64 MailboxReplacementCount = 0;
    uint64 AdmissionDeferredCount = 0;
    uint64 PhaseAdmissionDeferredCount = 0;
    uint64 SingletonRejectionCount = 0;
    uint64 CompletedJobCount = 0;
    double TotalQueueSeconds = 0.0;
    double TotalWorkerSeconds = 0.0;
    double MaxQueueSeconds = 0.0;
    double MaxWorkerSeconds = 0.0;
    TArray<FDWCEditorWorkerJobDiagnostic> Jobs;
};
