//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Foundation/Build/DWCEditorBuildOperationManager.h"

#include "WetClothing/Foundation/Jobs/DWCEditorWorkerJobScheduler.h"

FDWCEditorBuildOperationManager::FDWCEditorBuildOperationManager(
    TSharedRef<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe> InScheduler)
    : Scheduler(MoveTemp(InScheduler))
{
}

void FDWCEditorBuildOperationManager::SetActionBarrier(FActionBarrier InBarrier)
{
    check(IsInGameThread());
    ActionBarrier = MoveTemp(InBarrier);
}

TSharedPtr<FDWCEditorBuildOperation> FDWCEditorBuildOperationManager::BeginOperation(
    const EDWCEditorBuildAction Action,
    const EDWCEditorAsyncRequestPolicy RequestPolicy,
    FDWCEditorBuildOperation::FCompletion Completion,
    FString* OutError)
{
    check(IsInGameThread());
    if (OutError != nullptr)
    {
        OutError->Reset();
    }
    if (bShuttingDown || !Scheduler.IsValid())
    {
        if (OutError != nullptr) *OutError = TEXT("The editor build operation manager is shutting down.");
        return nullptr;
    }
    if (ActionBarrier)
    {
        FString BarrierError;
        if (!ActionBarrier(Action, BarrierError))
        {
            if (OutError != nullptr)
            {
                *OutError = BarrierError.IsEmpty()
                    ? TEXT("The build action is blocked by an exclusive Build.")
                    : MoveTemp(BarrierError);
            }
            return nullptr;
        }
    }

    // All operations managed here can mutate the same WCA and generated assets.
    // Different actions must therefore not commit concurrently.
    for (const TPair<EDWCEditorBuildAction, uint64>& Pair : CurrentOperationByAction)
    {
        if (Pair.Key == Action)
        {
            continue;
        }
        const TSharedPtr<FDWCEditorBuildOperation> Existing = Operations.FindRef(Pair.Value);
        if (Existing.IsValid() && !Existing->IsTerminal() && !Existing->IsCancellationRequested())
        {
            if (OutError != nullptr)
            {
                *OutError = TEXT("Another generated-asset build operation is already running for this WCA.");
            }
            return nullptr;
        }
    }

    if (const uint64* ExistingId = CurrentOperationByAction.Find(Action))
    {
        const TSharedPtr<FDWCEditorBuildOperation> Existing = Operations.FindRef(*ExistingId);
        if (Existing.IsValid() && !Existing->IsTerminal())
        {
            // Logical build operations do not own a delayed-start queue. FIFO is
            // consequently treated as serialized admission instead of replacing
            // the current pointer and orphaning the existing operation.
            if (RequestPolicy == EDWCEditorAsyncRequestPolicy::Singleton ||
                RequestPolicy == EDWCEditorAsyncRequestPolicy::FIFO)
            {
                if (OutError != nullptr) *OutError = TEXT("A build operation for this action is already running.");
                return nullptr;
            }
            if (RequestPolicy == EDWCEditorAsyncRequestPolicy::LatestWins)
            {
                CancelOperation(Existing.ToSharedRef(), EDWCEditorBuildTerminalReason::Superseded);
            }
        }
    }

    const uint64 OperationId = NextOperationId++;
    TSharedRef<FDWCEditorBuildOperation> Operation = MakeShared<FDWCEditorBuildOperation>(
        OperationId,
        Action,
        Scheduler->GetSessionEpoch(),
        MoveTemp(Completion));
    TWeakPtr<FDWCEditorBuildOperationManager> WeakThis = AsShared();
    Operation->SetOwnerCompletion(
        [WeakThis](const uint64 CompletedOperationId)
        {
            if (const TSharedPtr<FDWCEditorBuildOperationManager> Self = WeakThis.Pin())
            {
                Self->HandleOperationCompleted(CompletedOperationId);
            }
        });
    Operations.Add(OperationId, Operation);
    CurrentOperationByAction.Add(Action, OperationId);
    return Operation;
}

void FDWCEditorBuildOperationManager::CancelOperation(
    const TSharedRef<FDWCEditorBuildOperation>& Operation,
    const EDWCEditorBuildTerminalReason Reason)
{
    check(IsInGameThread());
    Operation->RequestCancellation(Reason);
    if (Scheduler.IsValid())
    {
        for (const FDWCEditorWorkerJobTicket& Ticket : Operation->GetOutstandingTickets())
        {
            Scheduler->CancelTicket(Ticket);
        }
    }
    if (!Operation->IsTerminal() && !Operation->HasOutstandingTickets())
    {
        FDWCEditorBuildOperationResult Result;
        Result.Reason = Reason;
        Result.Summary = Reason == EDWCEditorBuildTerminalReason::Superseded
            ? TEXT("The build operation was superseded by a newer request.")
            : Reason == EDWCEditorBuildTerminalReason::OwnerShutdown
                ? TEXT("The build operation was retired because the editor closed.")
                : TEXT("The build operation was canceled.");
        Operation->Complete(MoveTemp(Result));
    }
}

void FDWCEditorBuildOperationManager::BeginShutdown()
{
    check(IsInGameThread());
    if (bShuttingDown)
    {
        return;
    }
    bShuttingDown = true;
    const TArray<TSharedPtr<FDWCEditorBuildOperation>> Snapshot = [&]()
    {
        TArray<TSharedPtr<FDWCEditorBuildOperation>> Values;
        Operations.GenerateValueArray(Values);
        return Values;
    }();
    for (const TSharedPtr<FDWCEditorBuildOperation>& Operation : Snapshot)
    {
        if (Operation.IsValid() && !Operation->IsTerminal())
        {
            Operation->DetachPresentationCallback();
            CancelOperation(Operation.ToSharedRef(), EDWCEditorBuildTerminalReason::OwnerShutdown);
        }
    }
}

void FDWCEditorBuildOperationManager::CompleteShutdown()
{
    check(IsInGameThread());
    const TArray<TSharedPtr<FDWCEditorBuildOperation>> Snapshot = [&]()
    {
        TArray<TSharedPtr<FDWCEditorBuildOperation>> Values;
        Operations.GenerateValueArray(Values);
        return Values;
    }();
    for (const TSharedPtr<FDWCEditorBuildOperation>& Operation : Snapshot)
    {
        if (Operation.IsValid() && !Operation->IsTerminal())
        {
            FDWCEditorBuildOperationResult Result;
            Result.Reason = EDWCEditorBuildTerminalReason::OwnerShutdown;
            Result.Summary = TEXT("The build operation was retired because the editor closed.");
            Operation->Complete(MoveTemp(Result));
        }
    }
    CurrentOperationByAction.Reset();
    Operations.Reset();
    Scheduler.Reset();
}

bool FDWCEditorBuildOperationManager::IsActionActive(const EDWCEditorBuildAction Action) const
{
    const uint64* OperationId = CurrentOperationByAction.Find(Action);
    const TSharedPtr<FDWCEditorBuildOperation> Operation =
        OperationId != nullptr ? Operations.FindRef(*OperationId) : nullptr;
    return Operation.IsValid() && !Operation->IsTerminal() && !Operation->IsCancellationRequested();
}

bool FDWCEditorBuildOperationManager::IsCurrent(
    const TSharedRef<FDWCEditorBuildOperation>& Operation) const
{
    const uint64* CurrentId = CurrentOperationByAction.Find(Operation->GetAction());
    return CurrentId != nullptr && *CurrentId == Operation->GetOperationId() &&
        !Operation->IsCancellationRequested();
}

TSet<EDWCEditorBuildAction> FDWCEditorBuildOperationManager::GetRunningActions() const
{
    TSet<EDWCEditorBuildAction> Actions;
    for (const TPair<EDWCEditorBuildAction, uint64>& Pair : CurrentOperationByAction)
    {
        if (IsActionActive(Pair.Key))
        {
            Actions.Add(Pair.Key);
        }
    }
    return Actions;
}

TArray<FDWCEditorBuildOperationSnapshot> FDWCEditorBuildOperationManager::GetSnapshots() const
{
    TArray<FDWCEditorBuildOperationSnapshot> Snapshots;
    Snapshots.Reserve(Operations.Num());
    for (const TPair<uint64, TSharedPtr<FDWCEditorBuildOperation>>& Pair : Operations)
    {
        if (Pair.Value.IsValid())
        {
            Snapshots.Add(Pair.Value->GetSnapshot());
        }
    }
    return Snapshots;
}

void FDWCEditorBuildOperationManager::HandleOperationCompleted(const uint64 OperationId)
{
    check(IsInGameThread());
    const TSharedPtr<FDWCEditorBuildOperation> Operation = Operations.FindRef(OperationId);
    if (!Operation.IsValid())
    {
        return;
    }
    if (const uint64* CurrentId = CurrentOperationByAction.Find(Operation->GetAction());
        CurrentId != nullptr && *CurrentId == OperationId)
    {
        CurrentOperationByAction.Remove(Operation->GetAction());
    }
    Operations.Remove(OperationId);
}
