#include "WetClothing/Foundation/Preview/Recovery/DWCEditorPreviewRecovery.h"

FDWCEditorPreviewRecoveryController::FDWCEditorPreviewRecoveryController(
    const FDWCEditorPreviewRecoveryPolicy& InPolicy)
    : Policy(InPolicy)
{
}

void FDWCEditorPreviewRecoveryController::Reset(const bool bAdvanceGeneration)
{
    if (bAdvanceGeneration)
    {
        ++Generation;
    }
    State = EDWCEditorPreviewRecoveryState::Ready;
    LastReason = EDWCEditorPreviewInvalidationReason::None;
    AttemptCount = 0;
    NextRetryTimeSeconds = 0.0;
    bRebuildRequiredAfterResume = false;
}

void FDWCEditorPreviewRecoveryController::Invalidate(
    const EDWCEditorPreviewInvalidationReason Reason)
{
    ++Generation;
    AttemptCount = 0;
    NextRetryTimeSeconds = 0.0;
    LastReason = Reason;
    if (State == EDWCEditorPreviewRecoveryState::Suspended)
    {
        bRebuildRequiredAfterResume = true;
        return;
    }
    State = EDWCEditorPreviewRecoveryState::FullRebuildRequired;
    ++Diagnostics.FullRebuildRequestCount;
}

void FDWCEditorPreviewRecoveryController::RequestFullRebuild(
    const EDWCEditorPreviewInvalidationReason Reason)
{
    LastReason = Reason;
    if (State == EDWCEditorPreviewRecoveryState::Suspended)
    {
        bRebuildRequiredAfterResume = true;
        return;
    }
    if (State == EDWCEditorPreviewRecoveryState::FullRebuildRequired ||
        State == EDWCEditorPreviewRecoveryState::FullRebuildInFlight ||
        State == EDWCEditorPreviewRecoveryState::RetryBackoff ||
        State == EDWCEditorPreviewRecoveryState::Degraded)
    {
        return;
    }
    State = EDWCEditorPreviewRecoveryState::FullRebuildRequired;
    ++Diagnostics.FullRebuildRequestCount;
}

void FDWCEditorPreviewRecoveryController::MarkIncrementalPending()
{
    if (State == EDWCEditorPreviewRecoveryState::Ready)
    {
        State = EDWCEditorPreviewRecoveryState::IncrementalPending;
    }
}

void FDWCEditorPreviewRecoveryController::MarkIncrementalSucceeded()
{
    if (State == EDWCEditorPreviewRecoveryState::IncrementalPending)
    {
        State = EDWCEditorPreviewRecoveryState::Ready;
        LastSuccessfulGeneration = Generation;
    }
}

bool FDWCEditorPreviewRecoveryController::TryBeginFullRebuild(const double CurrentTimeSeconds)
{
    if (State == EDWCEditorPreviewRecoveryState::RetryBackoff)
    {
        if (!IsRetryDue(CurrentTimeSeconds))
        {
            return false;
        }
        State = EDWCEditorPreviewRecoveryState::FullRebuildRequired;
    }
    if (State != EDWCEditorPreviewRecoveryState::FullRebuildRequired)
    {
        return false;
    }
    State = EDWCEditorPreviewRecoveryState::FullRebuildInFlight;
    NextRetryTimeSeconds = 0.0;
    ++AttemptCount;
    ++Diagnostics.FullRebuildAttemptCount;
    return true;
}

void FDWCEditorPreviewRecoveryController::MarkSucceeded()
{
    State = EDWCEditorPreviewRecoveryState::Ready;
    LastSuccessfulGeneration = Generation;
    LastReason = EDWCEditorPreviewInvalidationReason::None;
    AttemptCount = 0;
    NextRetryTimeSeconds = 0.0;
    bRebuildRequiredAfterResume = false;
}

EDWCEditorPreviewRecoveryAction FDWCEditorPreviewRecoveryController::MarkFailure(
    const EDWCEditorPreviewInvalidationReason Reason,
    const double CurrentTimeSeconds)
{
    LastReason = Reason;
    const int32 RetryLimit = ResolveRetryLimit(Reason);
    if (RetryLimit <= 0 || AttemptCount >= RetryLimit)
    {
        State = EDWCEditorPreviewRecoveryState::Degraded;
        NextRetryTimeSeconds = 0.0;
        ++Diagnostics.DegradedCount;
        return EDWCEditorPreviewRecoveryAction::Degraded;
    }
    State = EDWCEditorPreviewRecoveryState::RetryBackoff;
    NextRetryTimeSeconds = CurrentTimeSeconds + ResolveBackoffSeconds();
    ++Diagnostics.RetryCount;
    return EDWCEditorPreviewRecoveryAction::RetryFullRebuild;
}

EDWCEditorPreviewRecoveryAction FDWCEditorPreviewRecoveryController::HandleCommitResult(
    const EDWCEditorPreviewCommitResult Result,
    const double CurrentTimeSeconds)
{
    switch (Result)
    {
    case EDWCEditorPreviewCommitResult::Applied:
        MarkSucceeded();
        return EDWCEditorPreviewRecoveryAction::None;
    case EDWCEditorPreviewCommitResult::StaleRequest:
    case EDWCEditorPreviewCommitResult::ConsumerExpired:
    case EDWCEditorPreviewCommitResult::ConsumerSuspended:
    case EDWCEditorPreviewCommitResult::CoordinatorShutdown:
        RecordStaleResult();
        return EDWCEditorPreviewRecoveryAction::DropStale;
    case EDWCEditorPreviewCommitResult::WorkspaceEntryMissing:
    case EDWCEditorPreviewCommitResult::WorkspaceRejected:
        ++Diagnostics.SoftRecoveryCount;
        return MarkFailure(EDWCEditorPreviewInvalidationReason::WorkspaceEvicted, CurrentTimeSeconds);
    case EDWCEditorPreviewCommitResult::ResourceGenerationMismatch:
        ++Diagnostics.SoftRecoveryCount;
        return MarkFailure(
            EDWCEditorPreviewInvalidationReason::ResourceGenerationMismatch,
            CurrentTimeSeconds);
    case EDWCEditorPreviewCommitResult::DataRevisionMismatch:
        return MarkFailure(
            EDWCEditorPreviewInvalidationReason::DataRevisionMismatch,
            CurrentTimeSeconds);
    case EDWCEditorPreviewCommitResult::DescriptorMismatch:
    case EDWCEditorPreviewCommitResult::InvalidPayload:
        return MarkFailure(EDWCEditorPreviewInvalidationReason::InvalidPayload, CurrentTimeSeconds);
    default:
        return MarkFailure(EDWCEditorPreviewInvalidationReason::WorkerFailed, CurrentTimeSeconds);
    }
}

void FDWCEditorPreviewRecoveryController::RecordStaleResult()
{
    ++Diagnostics.StaleDropCount;
    if (State == EDWCEditorPreviewRecoveryState::FullRebuildInFlight)
    {
        State = EDWCEditorPreviewRecoveryState::Ready;
    }
}

void FDWCEditorPreviewRecoveryController::Suspend()
{
    bRebuildRequiredAfterResume = RequiresFullRebuild();
    State = EDWCEditorPreviewRecoveryState::Suspended;
    NextRetryTimeSeconds = 0.0;
}

void FDWCEditorPreviewRecoveryController::Resume(const bool bRequireFullRebuild)
{
    if (State != EDWCEditorPreviewRecoveryState::Suspended)
    {
        return;
    }
    const bool bRebuild = bRequireFullRebuild || bRebuildRequiredAfterResume;
    State = bRebuild
        ? EDWCEditorPreviewRecoveryState::FullRebuildRequired
        : EDWCEditorPreviewRecoveryState::Ready;
    if (bRebuild)
    {
        ++Diagnostics.FullRebuildRequestCount;
    }
    bRebuildRequiredAfterResume = false;
}

bool FDWCEditorPreviewRecoveryController::IsRetryDue(const double CurrentTimeSeconds) const
{
    return State == EDWCEditorPreviewRecoveryState::RetryBackoff &&
        CurrentTimeSeconds >= NextRetryTimeSeconds;
}

bool FDWCEditorPreviewRecoveryController::RequiresFullRebuild() const
{
    return State == EDWCEditorPreviewRecoveryState::FullRebuildRequired ||
        State == EDWCEditorPreviewRecoveryState::FullRebuildInFlight ||
        State == EDWCEditorPreviewRecoveryState::RetryBackoff;
}

int32 FDWCEditorPreviewRecoveryController::ResolveRetryLimit(
    const EDWCEditorPreviewInvalidationReason Reason) const
{
    switch (Reason)
    {
    case EDWCEditorPreviewInvalidationReason::WorkspaceEvicted:
    case EDWCEditorPreviewInvalidationReason::ResourceGenerationMismatch:
    case EDWCEditorPreviewInvalidationReason::DataRevisionMismatch:
        return Policy.WorkspaceRetryLimit;
    case EDWCEditorPreviewInvalidationReason::SchedulerDeferred:
        return Policy.SchedulerRetryLimit;
    case EDWCEditorPreviewInvalidationReason::InvalidPayload:
        return Policy.InvalidPayloadRetryLimit;
    case EDWCEditorPreviewInvalidationReason::WorkerFailed:
        return Policy.WorkerRetryLimit;
    default:
        return Policy.WorkerRetryLimit;
    }
}

double FDWCEditorPreviewRecoveryController::ResolveBackoffSeconds() const
{
    return FMath::Min(
        Policy.MaximumBackoffSeconds,
        Policy.InitialBackoffSeconds * FMath::Pow(2.0, FMath::Max(AttemptCount - 1, 0)));
}

