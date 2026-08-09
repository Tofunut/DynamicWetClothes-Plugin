//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Build/DWCEditorBuildOperation.h"

class FDWCEditorWorkerJobScheduler;

/** Per-editor-session authority for logical build ownership and cancellation. */
class FDWCEditorBuildOperationManager final
    : public TSharedFromThis<FDWCEditorBuildOperationManager>
{
  public:
    explicit FDWCEditorBuildOperationManager(
        TSharedRef<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe> InScheduler);

    TSharedPtr<FDWCEditorBuildOperation> BeginOperation(
        EDWCEditorBuildAction Action,
        EDWCEditorAsyncRequestPolicy RequestPolicy,
        FDWCEditorBuildOperation::FCompletion Completion,
        FString* OutError = nullptr);

    void CancelOperation(
        const TSharedRef<FDWCEditorBuildOperation>& Operation,
        EDWCEditorBuildTerminalReason Reason = EDWCEditorBuildTerminalReason::Canceled);
    void BeginShutdown();
    void CompleteShutdown();

    bool IsActionActive(EDWCEditorBuildAction Action) const;
    bool IsCurrent(const TSharedRef<FDWCEditorBuildOperation>& Operation) const;
    TSet<EDWCEditorBuildAction> GetRunningActions() const;
    TArray<FDWCEditorBuildOperationSnapshot> GetSnapshots() const;

  private:
    void HandleOperationCompleted(uint64 OperationId);

    TSharedPtr<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe> Scheduler;
    TMap<uint64, TSharedPtr<FDWCEditorBuildOperation>> Operations;
    TMap<EDWCEditorBuildAction, uint64> CurrentOperationByAction;
    uint64 NextOperationId = 1;
    bool bShuttingDown = false;
};

