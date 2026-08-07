//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Preview/Commit/DWCEditorPreviewCommitTypes.h"

enum class EDWCEditorPreviewInvalidationReason : uint8
{
    None,
    AuthoredDataChanged,
    ContextChanged,
    ResolutionChanged,
    WorkspaceEvicted,
    ResourceGenerationMismatch,
    DataRevisionMismatch,
    SchedulerDeferred,
    InvalidPayload,
    WorkerFailed
};

enum class EDWCEditorPreviewRecoveryState : uint8
{
    Ready,
    IncrementalPending,
    FullRebuildRequired,
    FullRebuildInFlight,
    RetryBackoff,
    Degraded,
    Suspended
};

enum class EDWCEditorPreviewRecoveryAction : uint8
{
    None,
    DropStale,
    RetryFullRebuild,
    Degraded
};

struct FDWCEditorPreviewRecoveryPolicy
{
    int32 WorkspaceRetryLimit = 5;
    int32 WorkerRetryLimit = 3;
    int32 InvalidPayloadRetryLimit = 1;
    int32 SchedulerRetryLimit = 5;
    double InitialBackoffSeconds = 0.1;
    double MaximumBackoffSeconds = 1.0;
};

struct FDWCEditorPreviewRecoveryDiagnostics
{
    uint64 SoftRecoveryCount = 0;
    uint64 FullRebuildRequestCount = 0;
    uint64 FullRebuildAttemptCount = 0;
    uint64 RetryCount = 0;
    uint64 StaleDropCount = 0;
    uint64 DegradedCount = 0;
};

/**
 * Small game-thread state machine shared by preview producers. It owns retry
 * policy only; texture leases and CPU working data remain owned by each mode.
 */
class FDWCEditorPreviewRecoveryController final
{
  public:
    explicit FDWCEditorPreviewRecoveryController(
        const FDWCEditorPreviewRecoveryPolicy& InPolicy = FDWCEditorPreviewRecoveryPolicy());

    void Reset(bool bAdvanceGeneration = true);
    void ResetDiagnostics() { Diagnostics = {}; }
    void Invalidate(EDWCEditorPreviewInvalidationReason Reason);
    void RequestFullRebuild(EDWCEditorPreviewInvalidationReason Reason);
    void MarkIncrementalPending();
    void MarkIncrementalSucceeded();
    bool TryBeginFullRebuild(double CurrentTimeSeconds = 0.0);
    void MarkSucceeded();
    EDWCEditorPreviewRecoveryAction MarkFailure(
        EDWCEditorPreviewInvalidationReason Reason,
        double CurrentTimeSeconds);
    EDWCEditorPreviewRecoveryAction HandleCommitResult(
        EDWCEditorPreviewCommitResult Result,
        double CurrentTimeSeconds);
    void RecordStaleResult();
    void Suspend();
    void Resume(bool bRequireFullRebuild);

    bool IsRetryDue(double CurrentTimeSeconds) const;
    bool IsReady() const { return State == EDWCEditorPreviewRecoveryState::Ready; }
    bool RequiresFullRebuild() const;
    bool IsFullRebuildInFlight() const
    {
        return State == EDWCEditorPreviewRecoveryState::FullRebuildInFlight;
    }
    bool IsDegraded() const { return State == EDWCEditorPreviewRecoveryState::Degraded; }
    bool IsSuspended() const { return State == EDWCEditorPreviewRecoveryState::Suspended; }
    uint64 GetGeneration() const { return Generation; }
    uint64 GetLastSuccessfulGeneration() const { return LastSuccessfulGeneration; }
    int32 GetAttemptCount() const { return AttemptCount; }
    double GetNextRetryTimeSeconds() const { return NextRetryTimeSeconds; }
    EDWCEditorPreviewInvalidationReason GetLastReason() const { return LastReason; }
    EDWCEditorPreviewRecoveryState GetState() const { return State; }
    const FDWCEditorPreviewRecoveryDiagnostics& GetDiagnostics() const { return Diagnostics; }

  private:
    int32 ResolveRetryLimit(EDWCEditorPreviewInvalidationReason Reason) const;
    double ResolveBackoffSeconds() const;

    FDWCEditorPreviewRecoveryPolicy Policy;
    FDWCEditorPreviewRecoveryDiagnostics Diagnostics;
    EDWCEditorPreviewRecoveryState State = EDWCEditorPreviewRecoveryState::Ready;
    EDWCEditorPreviewInvalidationReason LastReason = EDWCEditorPreviewInvalidationReason::None;
    uint64 Generation = 1;
    uint64 LastSuccessfulGeneration = 0;
    int32 AttemptCount = 0;
    double NextRetryTimeSeconds = 0.0;
    bool bRebuildRequiredAfterResume = false;
};
