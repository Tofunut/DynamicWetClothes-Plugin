#include "WetClothing/Foundation/Preview/Diagnostics/DWCEditorPreviewDiagnostics.h"

#include "Engine/Texture2D.h"
#include "WetClothing/Foundation/Jobs/DWCEditorWorkerJobScheduler.h"
#include "WetClothing/Foundation/Preview/Session/DWCEditorPreviewSession.h"
#include "WetClothing/Foundation/Preview/Commit/DWCEditorPreviewCommitCoordinator.h"

DEFINE_LOG_CATEGORY(LogDWCEditorPreview);

namespace
{
    TArray<FDWCEditorPreviewSession*>& GetActivePreviewSessions()
    {
        static TArray<FDWCEditorPreviewSession*> Sessions;
        return Sessions;
    }

    TArray<FDWCEditorWorkerJobScheduler*>& GetActiveWorkerSchedulers()
    {
        static TArray<FDWCEditorWorkerJobScheduler*> Schedulers;
        return Schedulers;
    }

    TArray<FDWCEditorPreviewCommitCoordinator*>& GetActiveCommitCoordinators()
    {
        static TArray<FDWCEditorPreviewCommitCoordinator*> Coordinators;
        return Coordinators;
    }

    const TCHAR* PreviewWorkerLifecycleToString(const EDWCEditorWorkerJobLifecycleState State)
    {
        switch (State)
        {
        case EDWCEditorWorkerJobLifecycleState::PendingAdmission: return TEXT("PendingAdmission");
        case EDWCEditorWorkerJobLifecycleState::Preparing: return TEXT("Preparing");
        case EDWCEditorWorkerJobLifecycleState::Ready: return TEXT("Ready");
        case EDWCEditorWorkerJobLifecycleState::Running: return TEXT("Running");
        case EDWCEditorWorkerJobLifecycleState::CancelRequested: return TEXT("CancelRequested");
        case EDWCEditorWorkerJobLifecycleState::Finalizing: return TEXT("Finalizing");
        case EDWCEditorWorkerJobLifecycleState::Completed: return TEXT("Completed");
        default: return TEXT("Unknown");
        }
    }
}

void FDWCEditorPreviewDiagnostics::RegisterSession(FDWCEditorPreviewSession* Session)
{
    if (Session != nullptr)
    {
        GetActivePreviewSessions().AddUnique(Session);
    }
}

void FDWCEditorPreviewDiagnostics::UnregisterSession(FDWCEditorPreviewSession* Session)
{
    GetActivePreviewSessions().RemoveSingleSwap(Session, EAllowShrinking::No);
}

void FDWCEditorPreviewDiagnostics::RegisterWorkerScheduler(FDWCEditorWorkerJobScheduler* Scheduler)
{
    if (Scheduler != nullptr)
    {
        GetActiveWorkerSchedulers().AddUnique(Scheduler);
    }
}

void FDWCEditorPreviewDiagnostics::UnregisterWorkerScheduler(FDWCEditorWorkerJobScheduler* Scheduler)
{
    GetActiveWorkerSchedulers().RemoveSingleSwap(Scheduler, EAllowShrinking::No);
}

void FDWCEditorPreviewDiagnostics::RegisterCommitCoordinator(
    FDWCEditorPreviewCommitCoordinator* Coordinator)
{
    if (Coordinator != nullptr)
    {
        GetActiveCommitCoordinators().AddUnique(Coordinator);
    }
}

void FDWCEditorPreviewDiagnostics::UnregisterCommitCoordinator(
    FDWCEditorPreviewCommitCoordinator* Coordinator)
{
    GetActiveCommitCoordinators().RemoveSingleSwap(Coordinator, EAllowShrinking::No);
}

void FDWCEditorPreviewDiagnostics::DumpAllSessions()
{
    const TArray<FDWCEditorPreviewSession*>& Sessions = GetActivePreviewSessions();
    UE_LOG(
        LogDWCEditorPreview,
        Display,
        TEXT("DWC editor preview diagnostics: %d active session(s)."),
        Sessions.Num());

    for (int32 SessionIndex = 0; SessionIndex < Sessions.Num(); ++SessionIndex)
    {
        if (const FDWCEditorPreviewSession* Session = Sessions[SessionIndex])
        {
            Session->DumpDiagnostics(SessionIndex);
        }
    }


    const TArray<FDWCEditorWorkerJobScheduler*>& Schedulers = GetActiveWorkerSchedulers();
    UE_LOG(
        LogDWCEditorPreview,
        Display,
        TEXT("DWC editor worker diagnostics: %d scheduler(s)."),
        Schedulers.Num());
    for (int32 SchedulerIndex = 0; SchedulerIndex < Schedulers.Num(); ++SchedulerIndex)
    {
        const FDWCEditorWorkerJobScheduler* Scheduler = Schedulers[SchedulerIndex];
        if (Scheduler == nullptr)
        {
            continue;
        }

        const FDWCEditorWorkerSchedulerDiagnostics Diagnostics = Scheduler->GetDiagnostics();
        UE_LOG(
            LogDWCEditorPreview,
            Display,
            TEXT("  WorkerScheduler[%d]: reserved=%s/%s, highWater=%s, pending=%d, preparing=%d, ready=%d, active=%d, completed=%llu, mailboxReplacements=%llu, admissionDeferrals=%llu, singletonRejects=%llu, budgetRejects=%llu, queueRejects=%llu, maxQueue=%.3fs, maxWorker=%.3fs."),
            SchedulerIndex,
            *FormatBytes(Diagnostics.ReservedBytes),
            *FormatBytes(Diagnostics.TotalBudgetBytes),
            *FormatBytes(Diagnostics.HighWaterReservedBytes),
            Diagnostics.PendingAdmissionCount,
            Diagnostics.PreparingCount,
            Diagnostics.ReadyCount,
            Diagnostics.ActiveCount,
            Diagnostics.CompletedJobCount,
            Diagnostics.MailboxReplacementCount,
            Diagnostics.AdmissionDeferredCount,
            Diagnostics.SingletonRejectionCount,
            Diagnostics.BudgetRejectionCount,
            Diagnostics.QueueRejectionCount,
            Diagnostics.MaxQueueSeconds,
            Diagnostics.MaxWorkerSeconds);
        for (const FDWCEditorWorkerJobDiagnostic& Job : Diagnostics.Jobs)
        {
            const FDWCEditorWorkerMemoryEstimate& Memory = Job.MemoryEstimate;
            UE_LOG(
                LogDWCEditorPreview,
                Display,
                TEXT("    job=%llu '%s': state=%s slot=%d reserved=%s [shared=%s snapshot=%s working=%s output=%s scratch=%s] queue=%.3fs worker=%.3fs result=%s."),
                Job.Ticket.JobId,
                *Job.DebugName,
                PreviewWorkerLifecycleToString(Job.LifecycleState),
                Job.Ticket.Key.MaterialSlotIndex,
                *FormatBytes(Job.ReservedBytes),
                *FormatBytes(Memory.ResidentSharedBytes),
                *FormatBytes(Memory.SnapshotBytes),
                *FormatBytes(Memory.WorkingBytes),
                *FormatBytes(Memory.OutputBytes),
                *FormatBytes(Memory.ScratchBytes),
                Job.QueueSeconds,
                Job.WorkerSeconds,
                *FormatBytes(Job.ResultBytes));
        }
    }

    const TArray<FDWCEditorPreviewCommitCoordinator*>& CommitCoordinators =
        GetActiveCommitCoordinators();
    for (int32 CoordinatorIndex = 0; CoordinatorIndex < CommitCoordinators.Num(); ++CoordinatorIndex)
    {
        const FDWCEditorPreviewCommitCoordinator* Coordinator = CommitCoordinators[CoordinatorIndex];
        if (Coordinator == nullptr)
        {
            continue;
        }
        const FDWCEditorPreviewCommitDiagnostics Diagnostics = Coordinator->GetDiagnostics();
        UE_LOG(
            LogDWCEditorPreview,
            Display,
            TEXT("  PreviewCommit[%d]: applied=%llu, stale=%llu, consumerRejected=%llu, workspaceRejected=%llu, shutdownRejected=%llu, regionApplied=%llu, regionRejected=%llu, regionRejects={entry:%llu,data:%llu,resource:%llu,descriptor:%llu,payload:%llu}."),
            CoordinatorIndex,
            Diagnostics.AppliedCount,
            Diagnostics.StaleRequestCount,
            Diagnostics.ConsumerRejectedCount,
            Diagnostics.WorkspaceRejectedCount,
            Diagnostics.ShutdownRejectedCount,
            Diagnostics.RegionAppliedCount,
            Diagnostics.RegionRejectedCount,
            Diagnostics.WorkspaceEntryMissingCount,
            Diagnostics.DataRevisionMismatchCount,
            Diagnostics.ResourceGenerationMismatchCount,
            Diagnostics.DescriptorMismatchCount,
            Diagnostics.InvalidPayloadCount);
    }
}

void FDWCEditorPreviewDiagnostics::ResetAllCounters()
{
    for (FDWCEditorPreviewSession* Session : GetActivePreviewSessions())
    {
        if (Session != nullptr)
        {
            Session->ResetDiagnosticCounters();
        }
    }

    for (FDWCEditorWorkerJobScheduler* Scheduler : GetActiveWorkerSchedulers())
    {
        if (Scheduler != nullptr)
        {
            Scheduler->ResetDiagnosticCounters();
        }
    }

    for (FDWCEditorPreviewCommitCoordinator* Coordinator : GetActiveCommitCoordinators())
    {
        if (Coordinator != nullptr)
        {
            Coordinator->ResetDiagnosticCounters();
        }
    }

    UE_LOG(LogDWCEditorPreview, Display, TEXT("Reset diagnostics for all active DWC editor preview sessions."));
}

uint64 FDWCEditorPreviewDiagnostics::EstimateTextureBytes(const UTexture2D* Texture)
{
    if (Texture == nullptr)
    {
        return 0;
    }

    uint64 BulkDataBytes = 0;
    if (const FTexturePlatformData* PlatformData = Texture->GetPlatformData())
    {
        for (const FTexture2DMipMap& Mip : PlatformData->Mips)
        {
            BulkDataBytes += static_cast<uint64>(FMath::Max<int64>(Mip.BulkData.GetBulkDataSize(), 0));
        }
    }

    const uint64 MinimumTextureBytes =
        static_cast<uint64>(FMath::Max(Texture->GetSizeX(), 0)) *
        static_cast<uint64>(FMath::Max(Texture->GetSizeY(), 0)) * sizeof(FColor);
    const uint64 ResidentResourceBytes =
        static_cast<uint64>(Texture->CalcTextureMemorySizeEnum(TMC_ResidentMips));
    return BulkDataBytes + FMath::Max(ResidentResourceBytes, MinimumTextureBytes);
}

FString FDWCEditorPreviewDiagnostics::FormatBytes(const uint64 Bytes)
{
    constexpr double BytesPerMiB = 1024.0 * 1024.0;
    return FString::Printf(TEXT("%.2f MiB"), static_cast<double>(Bytes) / BytesPerMiB);
}
