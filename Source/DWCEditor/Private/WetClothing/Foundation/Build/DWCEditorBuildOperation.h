//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Build/DWCEditorBuildActionTypes.h"
#include "WetClothing/Foundation/Jobs/DWCEditorWorkerJobTypes.h"

enum class EDWCEditorBuildOperationPhase : uint8
{
    Pending,
    Preparing,
    Running,
    CommitPending,
    Committing,
    Saving,
    Revalidating,
    Retiring,
    Completed
};

enum class EDWCEditorBuildTerminalReason : uint8
{
    None,
    Succeeded,
    SucceededWithWarnings,
    Failed,
    Canceled,
    Superseded,
    Stale,
    OwnerShutdown
};

struct FDWCEditorBuildOperationResult
{
    EDWCEditorBuildTerminalReason Reason = EDWCEditorBuildTerminalReason::Failed;
    FString Summary;
    FString AttentionSummary;
    TArray<int32> AffectedMaterialSlotIndices;

    bool IsSuccessful() const
    {
        return Reason == EDWCEditorBuildTerminalReason::Succeeded ||
            Reason == EDWCEditorBuildTerminalReason::SucceededWithWarnings;
    }

    bool HasWarnings() const
    {
        return Reason == EDWCEditorBuildTerminalReason::SucceededWithWarnings;
    }

    bool WasCanceled() const
    {
        return Reason == EDWCEditorBuildTerminalReason::Canceled ||
            Reason == EDWCEditorBuildTerminalReason::Superseded ||
            Reason == EDWCEditorBuildTerminalReason::OwnerShutdown;
    }
};

struct FDWCEditorBuildOperationSnapshot
{
    uint64 OperationId = 0;
    EDWCEditorBuildAction Action = EDWCEditorBuildAction::SaveAsset;
    EDWCEditorBuildOperationPhase Phase = EDWCEditorBuildOperationPhase::Pending;
    EDWCEditorBuildTerminalReason TerminalReason = EDWCEditorBuildTerminalReason::None;
    int32 SubmittedJobCount = 0;
    int32 FinishedJobCount = 0;
    bool bCancellationRequested = false;
    double StartedSeconds = 0.0;
    double FinishedSeconds = 0.0;
};

/**
 * Game-thread owner for one logical WCA build request. Worker jobs only own
 * their local execution; this object owns the aggregate cancellation and
 * exactly-once terminal completion contract.
 */
class FDWCEditorBuildOperation final
    : public TSharedFromThis<FDWCEditorBuildOperation>
{
  public:
    using FCompletion = TFunction<void(const FDWCEditorBuildOperationResult&)>;

    FDWCEditorBuildOperation(
        uint64 InOperationId,
        EDWCEditorBuildAction InAction,
        FGuid InSessionEpoch,
        FCompletion InCompletion);

    uint64 GetOperationId() const { return OperationId; }
    EDWCEditorBuildAction GetAction() const { return Action; }
    const FGuid& GetSessionEpoch() const { return SessionEpoch; }
    EDWCEditorBuildOperationPhase GetPhase() const { return Phase; }
    EDWCEditorBuildTerminalReason GetCancellationReason() const { return CancellationReason; }

    void SetPhase(EDWCEditorBuildOperationPhase NewPhase);
    void RegisterTicket(const FDWCEditorWorkerJobTicket& Ticket);
    void NotifyTicketFinished(const FDWCEditorWorkerJobTicket& Ticket);
    TArray<FDWCEditorWorkerJobTicket> GetOutstandingTickets() const;
    bool HasOutstandingTickets() const;

    void RequestCancellation(EDWCEditorBuildTerminalReason Reason);
    bool IsCancellationRequested() const { return CancellationReason != EDWCEditorBuildTerminalReason::None; }
    bool IsTerminal() const { return Phase == EDWCEditorBuildOperationPhase::Completed; }
    bool Complete(FDWCEditorBuildOperationResult Result);
    void DetachPresentationCallback();
    FDWCEditorBuildOperationSnapshot GetSnapshot() const;

    void SetOwnerCompletion(TFunction<void(uint64)> InOwnerCompletion)
    {
        OwnerCompletion = MoveTemp(InOwnerCompletion);
    }

  private:
    uint64 OperationId = 0;
    EDWCEditorBuildAction Action = EDWCEditorBuildAction::SaveAsset;
    FGuid SessionEpoch;
    EDWCEditorBuildOperationPhase Phase = EDWCEditorBuildOperationPhase::Pending;
    EDWCEditorBuildTerminalReason TerminalReason = EDWCEditorBuildTerminalReason::None;
    EDWCEditorBuildTerminalReason CancellationReason = EDWCEditorBuildTerminalReason::None;
    TMap<uint64, FDWCEditorWorkerJobTicket> OutstandingTickets;
    int32 SubmittedJobCount = 0;
    int32 FinishedJobCount = 0;
    double StartedSeconds = 0.0;
    double FinishedSeconds = 0.0;
    FCompletion PresentationCompletion;
    TFunction<void(uint64)> OwnerCompletion;
};
