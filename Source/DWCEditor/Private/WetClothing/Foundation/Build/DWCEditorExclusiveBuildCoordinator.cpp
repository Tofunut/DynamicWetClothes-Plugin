// Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Foundation/Build/DWCEditorExclusiveBuildCoordinator.h"

#include "Containers/Ticker.h"
#include "HAL/PlatformTime.h"
#include "WetClothing/Foundation/Jobs/DWCEditorWorkerJobScheduler.h"
#include "WetClothing/Foundation/Resources/DWCEditorResourceBroker.h"

namespace
{
    DEFINE_LOG_CATEGORY_STATIC(LogDWCEditorExclusiveBuildCoordinator, Log, All);
}

FDWCEditorExclusiveBuildCoordinator::FDWCEditorExclusiveBuildCoordinator(
    TSharedRef<FDWCEditorResourceBroker> InResourceBroker,
    TSharedRef<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe> InWorkerJobScheduler,
    const FGuid InSessionId,
    FString InAssetPath)
    : ResourceBroker(MoveTemp(InResourceBroker))
    , WorkerJobScheduler(MoveTemp(InWorkerJobScheduler))
    , SessionId(InSessionId)
    , AssetPath(MoveTemp(InAssetPath))
{
}

FDWCEditorExclusiveBuildCoordinator::~FDWCEditorExclusiveBuildCoordinator()
{
    Shutdown();
}

bool FDWCEditorExclusiveBuildCoordinator::Request(
    const FString& DebugName,
    FWork Work,
    FString* OutError)
{
    check(IsInGameThread());
    if (OutError != nullptr)
    {
        OutError->Reset();
    }
    if (bShuttingDown || !ResourceBroker.IsValid() || !WorkerJobScheduler.IsValid())
    {
        if (OutError != nullptr)
        {
            *OutError = TEXT("The WCA editor Build coordinator is unavailable.");
        }
        return false;
    }
    if (!Work)
    {
        if (OutError != nullptr)
        {
            *OutError = TEXT("The exclusive Build has no work callback.");
        }
        return false;
    }
    if (IsActive())
    {
        if (OutError != nullptr)
        {
            *OutError = TEXT("An exclusive WCA Build is already in progress.");
        }
        return false;
    }

    FDWCEditorExclusiveBuildRequest Request;
    Request.SessionId = SessionId;
    Request.AssetPath = AssetPath;
    Request.DebugName = DebugName;
    TUniquePtr<FDWCEditorExclusiveBuildLease> NewLease =
        ResourceBroker->TryBeginExclusiveBuild(Request, OutError);
    if (!NewLease.IsValid())
    {
        return false;
    }

    Lease = MoveTemp(NewLease);
    PendingWork = MoveTemp(Work);
    DrainStartedSeconds = FPlatformTime::Seconds();
    TickHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateSP(AsShared(), &FDWCEditorExclusiveBuildCoordinator::HandleTick),
        0.0f);
    return true;
}

bool FDWCEditorExclusiveBuildCoordinator::IsActive() const
{
    return Lease.IsValid() || static_cast<bool>(PendingWork) || bWorkExecuting;
}

void FDWCEditorExclusiveBuildCoordinator::Shutdown()
{
    check(IsInGameThread());
    if (bShuttingDown)
    {
        return;
    }
    bShuttingDown = true;
    FinishBuild();
    WorkerJobScheduler.Reset();
    ResourceBroker.Reset();
}

bool FDWCEditorExclusiveBuildCoordinator::HandleTick(float)
{
    check(IsInGameThread());
    if (bShuttingDown || !Lease.IsValid() || !PendingWork || !ResourceBroker.IsValid())
    {
        FinishBuild();
        return false;
    }

    if (ResourceBroker->HasOutstandingInteractiveWork())
    {
        const double DrainSeconds = FPlatformTime::Seconds() - DrainStartedSeconds;
        if (DrainSeconds >= 2.0)
        {
            UE_LOG(
                LogDWCEditorExclusiveBuildCoordinator,
                Display,
                TEXT("Exclusive Build is waiting for preview work to retire: queued=%d active=%d reserved=%.2f MiB."),
                WorkerJobScheduler.IsValid() ? WorkerJobScheduler->GetQueuedJobCount() : 0,
                WorkerJobScheduler.IsValid() ? WorkerJobScheduler->GetActiveJobCount() : 0,
                WorkerJobScheduler.IsValid()
                    ? static_cast<double>(WorkerJobScheduler->GetReservedBytes()) / (1024.0 * 1024.0)
                    : 0.0);
            DrainStartedSeconds = FPlatformTime::Seconds();
        }
        return true;
    }

    ResourceBroker->SetExclusiveBuildState(
        Lease->GetScopeId(),
        EDWCEditorExclusiveBuildState::Active);
    FWork Work = MoveTemp(PendingWork);
    bWorkExecuting = true;
    Work();
    bWorkExecuting = false;
    FinishBuild();
    return false;
}

void FDWCEditorExclusiveBuildCoordinator::FinishBuild()
{
    PendingWork = nullptr;
    if (TickHandle.IsValid())
    {
        FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
        TickHandle.Reset();
    }
    if (Lease.IsValid() && ResourceBroker.IsValid())
    {
        ResourceBroker->SetExclusiveBuildState(
            Lease->GetScopeId(),
            EDWCEditorExclusiveBuildState::Retiring);
    }
    Lease.Reset();
    bWorkExecuting = false;
}
