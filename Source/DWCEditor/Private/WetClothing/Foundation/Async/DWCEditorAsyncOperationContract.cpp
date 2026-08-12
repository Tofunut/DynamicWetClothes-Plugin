//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Foundation/Async/DWCEditorAsyncOperationContract.h"

bool FDWCEditorAsyncOperationContract::CanTransition(
    const EDWCEditorAsyncOperationState From,
    const EDWCEditorAsyncOperationState To)
{
    switch (From)
    {
    case EDWCEditorAsyncOperationState::Pending:
        return To == EDWCEditorAsyncOperationState::Admitted ||
            To == EDWCEditorAsyncOperationState::Completed;
    case EDWCEditorAsyncOperationState::Admitted:
        return To == EDWCEditorAsyncOperationState::Preparing ||
            To == EDWCEditorAsyncOperationState::Completed;
    case EDWCEditorAsyncOperationState::Preparing:
        return To == EDWCEditorAsyncOperationState::Ready ||
            To == EDWCEditorAsyncOperationState::Completed;
    case EDWCEditorAsyncOperationState::Ready:
        return To == EDWCEditorAsyncOperationState::Running ||
            To == EDWCEditorAsyncOperationState::Completed;
    case EDWCEditorAsyncOperationState::Running:
        return To == EDWCEditorAsyncOperationState::CommitPending ||
            To == EDWCEditorAsyncOperationState::Completed;
    case EDWCEditorAsyncOperationState::CommitPending:
        return To == EDWCEditorAsyncOperationState::Committing ||
            To == EDWCEditorAsyncOperationState::Completed;
    case EDWCEditorAsyncOperationState::Committing:
        return To == EDWCEditorAsyncOperationState::Retiring ||
            To == EDWCEditorAsyncOperationState::Completed;
    case EDWCEditorAsyncOperationState::Retiring:
        return To == EDWCEditorAsyncOperationState::Completed;
    case EDWCEditorAsyncOperationState::Completed:
    default:
        return false;
    }
}

bool FDWCEditorAsyncOperationContract::ValidateTransition(
    const EDWCEditorAsyncOperationState From,
    const EDWCEditorAsyncOperationState To,
    const TCHAR* OperationDebugName)
{
    const bool bValid = CanTransition(From, To);
    ensureMsgf(
        bValid,
        TEXT("Invalid async operation transition for '%s': %s -> %s."),
        OperationDebugName != nullptr ? OperationDebugName : TEXT("Unknown"),
        LexToString(From),
        LexToString(To));
    return bValid;
}

bool FDWCEditorAsyncOperationContract::CanTransitionCancellation(
    const EDWCEditorAsyncCancellationState From,
    const EDWCEditorAsyncCancellationState To)
{
    switch (From)
    {
    case EDWCEditorAsyncCancellationState::None:
        return To == EDWCEditorAsyncCancellationState::CancelRequested;
    case EDWCEditorAsyncCancellationState::CancelRequested:
        return To == EDWCEditorAsyncCancellationState::CancelAcknowledged;
    case EDWCEditorAsyncCancellationState::CancelAcknowledged:
    default:
        return false;
    }
}

bool FDWCEditorAsyncOperationContract::ValidateCancellationTransition(
    const EDWCEditorAsyncCancellationState From,
    const EDWCEditorAsyncCancellationState To,
    const TCHAR* OperationDebugName)
{
    const bool bValid = CanTransitionCancellation(From, To);
    ensureMsgf(
        bValid,
        TEXT("Invalid async cancellation transition for '%s': %u -> %u."),
        OperationDebugName != nullptr ? OperationDebugName : TEXT("Unknown"),
        static_cast<uint8>(From),
        static_cast<uint8>(To));
    return bValid;
}

bool FDWCEditorAsyncOperationContract::CanCommit(
    const FDWCEditorAsyncOperationIdentity& Identity,
    const FGuid& CurrentSessionEpoch,
    const uint64 CurrentGeneration,
    const uint64 CurrentDomainRevision)
{
    if (!Identity.IsValid() || Identity.SessionEpoch != CurrentSessionEpoch ||
        Identity.Generation != CurrentGeneration)
    {
        return false;
    }

    return Identity.Domain == EDWCEditorAuthoringDomain::None ||
        Identity.DomainRevision == CurrentDomainRevision;
}

bool FDWCEditorAsyncOperationContract::IsCPUResourcePool(const EDWCEditorResourcePool Pool)
{
    return Pool != EDWCEditorResourcePool::PreviewGPU;
}

const TCHAR* FDWCEditorAsyncOperationContract::LexToString(
    const EDWCEditorAsyncOperationState State)
{
    switch (State)
    {
    case EDWCEditorAsyncOperationState::Pending: return TEXT("Pending");
    case EDWCEditorAsyncOperationState::Admitted: return TEXT("Admitted");
    case EDWCEditorAsyncOperationState::Preparing: return TEXT("Preparing");
    case EDWCEditorAsyncOperationState::Ready: return TEXT("Ready");
    case EDWCEditorAsyncOperationState::Running: return TEXT("Running");
    case EDWCEditorAsyncOperationState::CommitPending: return TEXT("CommitPending");
    case EDWCEditorAsyncOperationState::Committing: return TEXT("Committing");
    case EDWCEditorAsyncOperationState::Retiring: return TEXT("Retiring");
    case EDWCEditorAsyncOperationState::Completed: return TEXT("Completed");
    default: return TEXT("Unknown");
    }
}

const TCHAR* FDWCEditorAsyncOperationContract::LexToString(const EDWCEditorResourcePool Pool)
{
    switch (Pool)
    {
    case EDWCEditorResourcePool::WorkerPrivateCPU: return TEXT("WorkerPrivateCPU");
    case EDWCEditorResourcePool::AssetCommitCPU: return TEXT("AssetCommitCPU");
    case EDWCEditorResourcePool::PreviewWorkspaceCPU: return TEXT("PreviewWorkspaceCPU");
    case EDWCEditorResourcePool::SharedCacheCPU: return TEXT("SharedCacheCPU");
    case EDWCEditorResourcePool::UploadStagingCPU: return TEXT("UploadStagingCPU");
    case EDWCEditorResourcePool::PreviewGPU: return TEXT("PreviewGPU");
    default: return TEXT("Unknown");
    }
}
