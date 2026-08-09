//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Foundation/Build/DWCEditorBuildOperation.h"

#include "HAL/PlatformTime.h"

DEFINE_LOG_CATEGORY_STATIC(LogDWCEditorBuildOperation, Log, All);

FDWCEditorBuildOperation::FDWCEditorBuildOperation(
    const uint64 InOperationId,
    const EDWCEditorBuildAction InAction,
    const FGuid InSessionEpoch,
    FCompletion InCompletion)
    : OperationId(InOperationId)
    , Action(InAction)
    , SessionEpoch(InSessionEpoch)
    , StartedSeconds(FPlatformTime::Seconds())
    , PresentationCompletion(MoveTemp(InCompletion))
{
    check(OperationId != 0);
    check(SessionEpoch.IsValid());
}

void FDWCEditorBuildOperation::SetPhase(const EDWCEditorBuildOperationPhase NewPhase)
{
    check(IsInGameThread());
    if (IsTerminal() || NewPhase == Phase)
    {
        return;
    }
    if (static_cast<uint8>(NewPhase) < static_cast<uint8>(Phase))
    {
        UE_LOG(
            LogDWCEditorBuildOperation,
            Warning,
            TEXT("Ignored backward build operation transition for operation %llu (%d -> %d)."),
            OperationId,
            static_cast<int32>(Phase),
            static_cast<int32>(NewPhase));
        return;
    }
    Phase = NewPhase;
}

void FDWCEditorBuildOperation::RegisterTicket(const FDWCEditorWorkerJobTicket& Ticket)
{
    check(IsInGameThread());
    if (!Ticket.IsValid() || IsTerminal())
    {
        return;
    }
    if (!OutstandingTickets.Contains(Ticket.JobId))
    {
        OutstandingTickets.Add(Ticket.JobId, Ticket);
        ++SubmittedJobCount;
    }
}

void FDWCEditorBuildOperation::NotifyTicketFinished(const FDWCEditorWorkerJobTicket& Ticket)
{
    check(IsInGameThread());
    if (OutstandingTickets.Remove(Ticket.JobId) > 0)
    {
        ++FinishedJobCount;
    }
}

TArray<FDWCEditorWorkerJobTicket> FDWCEditorBuildOperation::GetOutstandingTickets() const
{
    TArray<FDWCEditorWorkerJobTicket> Tickets;
    OutstandingTickets.GenerateValueArray(Tickets);
    return Tickets;
}

bool FDWCEditorBuildOperation::HasOutstandingTickets() const
{
    return !OutstandingTickets.IsEmpty();
}

void FDWCEditorBuildOperation::RequestCancellation(const EDWCEditorBuildTerminalReason Reason)
{
    check(IsInGameThread());
    if (IsTerminal() || CancellationReason != EDWCEditorBuildTerminalReason::None)
    {
        return;
    }
    check(
        Reason == EDWCEditorBuildTerminalReason::Canceled ||
        Reason == EDWCEditorBuildTerminalReason::Superseded ||
        Reason == EDWCEditorBuildTerminalReason::Stale ||
        Reason == EDWCEditorBuildTerminalReason::OwnerShutdown);
    CancellationReason = Reason;
    SetPhase(EDWCEditorBuildOperationPhase::Retiring);
}

bool FDWCEditorBuildOperation::Complete(FDWCEditorBuildOperationResult Result)
{
    check(IsInGameThread());
    if (IsTerminal())
    {
        return false;
    }
    if (CancellationReason != EDWCEditorBuildTerminalReason::None)
    {
        Result.Reason = CancellationReason;
    }
    OutstandingTickets.Reset();
    TerminalReason = Result.Reason;
    Phase = EDWCEditorBuildOperationPhase::Completed;
    FinishedSeconds = FPlatformTime::Seconds();

    FCompletion LocalPresentation = MoveTemp(PresentationCompletion);
    TFunction<void(uint64)> LocalOwnerCompletion = MoveTemp(OwnerCompletion);
    if (LocalOwnerCompletion)
    {
        LocalOwnerCompletion(OperationId);
    }
    if (LocalPresentation)
    {
        LocalPresentation(Result);
    }
    return true;
}

void FDWCEditorBuildOperation::DetachPresentationCallback()
{
    check(IsInGameThread());
    PresentationCompletion = nullptr;
}

FDWCEditorBuildOperationSnapshot FDWCEditorBuildOperation::GetSnapshot() const
{
    FDWCEditorBuildOperationSnapshot Snapshot;
    Snapshot.OperationId = OperationId;
    Snapshot.Action = Action;
    Snapshot.Phase = Phase;
    Snapshot.TerminalReason = TerminalReason;
    Snapshot.SubmittedJobCount = SubmittedJobCount;
    Snapshot.FinishedJobCount = FinishedJobCount;
    Snapshot.bCancellationRequested = IsCancellationRequested();
    Snapshot.StartedSeconds = StartedSeconds;
    Snapshot.FinishedSeconds = FinishedSeconds;
    return Snapshot;
}
