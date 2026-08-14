// Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"

class FDWCEditorResourceBroker;
class FDWCEditorWorkerJobScheduler;
class FDWCEditorExclusiveBuildLease;

/**
 * Session-level owner for foreground WCA builds that must drain interactive
 * preview work before mutating shared generated assets. It deliberately uses
 * the editor ticker rather than a Slate widget timer so modal UI cannot stall
 * a queued build.
 */
class FDWCEditorExclusiveBuildCoordinator final
    : public TSharedFromThis<FDWCEditorExclusiveBuildCoordinator>
{
  public:
    using FWork = TFunction<void()>;

    FDWCEditorExclusiveBuildCoordinator(
        TSharedRef<FDWCEditorResourceBroker> InResourceBroker,
        TSharedRef<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe> InWorkerJobScheduler,
        FGuid InSessionId,
        FString InAssetPath);
    ~FDWCEditorExclusiveBuildCoordinator();

    bool Request(const FString& DebugName, FWork Work, FString* OutError = nullptr);
    bool IsActive() const;
    void Shutdown();

  private:
    bool HandleTick(float DeltaSeconds);
    void FinishBuild();

    TSharedPtr<FDWCEditorResourceBroker> ResourceBroker;
    TSharedPtr<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe> WorkerJobScheduler;
    FGuid SessionId;
    FString AssetPath;
    TUniquePtr<FDWCEditorExclusiveBuildLease> Lease;
    FWork PendingWork;
    FTSTicker::FDelegateHandle TickHandle;
    double DrainStartedSeconds = 0.0;
    bool bWorkExecuting = false;
    bool bShuttingDown = false;
};
