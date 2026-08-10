//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Foundation/Bake/DWCEditorBakeCoordinator.h"

#include "Algo/Unique.h"
#include "Core/DWCEditorUtils.h"
#include "DataAssets/WetClothingAsset.h"
#include "WetClothing/DerivedAssets/Textures/Wrinkle/WetWrinkleBakeService.h"
#include "WetClothing/DerivedAssets/Textures/Wrinkle/WetWrinkleNormalMapBaker.h"
#include "WetClothing/Foundation/Build/DWCEditorBuildOperationManager.h"
#include "WetClothing/DerivedAssets/Textures/Transparency/DWCTransparencyEditedMapBaker.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyFinalWorkingSet.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyAffectedStage4Rebake.h"
#include "WetClothing/Foundation/Jobs/DWCEditorCancellationToken.h"
#include "WetClothing/Foundation/Jobs/DWCEditorWorkerJobScheduler.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSpatialQueryService.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSurfacePatchProjectionCacheService.h"
#include "WetClothing/Foundation/TextureAccess/WetWrinkleTextureRasterUtils.h"
#include "WetClothing/Modes/Transparency/AutoMap/DWCTransparencyAutoMapGenerator.h"
#include "WetClothing/Modes/Transparency/Processing/DWCWrinkleSuppressionCoverageService.h"

namespace
{
    DEFINE_LOG_CATEGORY_STATIC(LogDWCWrinkleBakeCoordinator, Log, All);
    constexpr int32 DefaultWrinkleBakeMaxInFlightJobs = 2;

    TAutoConsoleVariable<int32> CVarDWCWrinkleBakeMaxInFlightJobs(
        TEXT("DWC.WrinkleEditor.Bake.MaxInFlightJobs"),
        DefaultWrinkleBakeMaxInFlightJobs,
        TEXT("Maximum number of wrinkle bake snapshots/jobs owned by one bake request. "
             "The global worker scheduler still enforces its own active-job limit."),
        ECVF_Default);

    int32 ResolveWrinkleBakeMaxInFlightJobs()
    {
        return FMath::Clamp(
            CVarDWCWrinkleBakeMaxInFlightJobs.GetValueOnGameThread(),
            1,
            DefaultWrinkleBakeMaxInFlightJobs);
    }

    struct FWrinkleBakeWorkerResult final : FDWCEditorWorkerJobResult
    {
        TSharedPtr<const FWetWrinkleNormalMapBakeSnapshot, ESPMode::ThreadSafe> Snapshot;
        FWetWrinkleNormalMapComputedResult Computed;
    };

    struct FTransparencyBakeWorkerResult final : FDWCEditorWorkerJobResult
    {
        TSharedPtr<const FDWCTransparencyEditedMapBakeSnapshot, ESPMode::ThreadSafe> Snapshot;
        FDWCTransparencyEditedMapComputedResult Computed;
    };

    struct FTransparencyAutoBakeWorkerResult final : FDWCEditorWorkerJobResult
    {
        FDWCTransparencyAutoMapComputedResult Computed;
    };

    FString DescribeCompletion(const EDWCEditorWorkerJobCompletion Completion)
    {
        switch (Completion)
        {
        case EDWCEditorWorkerJobCompletion::Canceled: return TEXT("canceled");
        case EDWCEditorWorkerJobCompletion::Superseded: return TEXT("superseded by a newer request");
        case EDWCEditorWorkerJobCompletion::Stale: return TEXT("discarded because the authored data changed");
        case EDWCEditorWorkerJobCompletion::Failed: return TEXT("worker calculation failed");
        default: return TEXT("completed");
        }
    }
}

struct FDWCEditorBakeCoordinator::FWrinkleBatch
{
    TSharedPtr<FDWCEditorBuildOperation> Operation;
    int32 SubmittedCount = 0;
    int32 FinishedCount = 0;
    int32 BakedMapCount = 0;
    int32 BakedPatchCount = 0;
    int32 BakedStrokeCount = 0;
    bool bSaveAfterCommit = false;
    bool bCanceled = false;
    bool bFinalized = false;
    TArray<FString> NormalTextureNames;
    TArray<FString> MaskTextureNames;
    TArray<FWetWrinkleInvalidatedTransparencyOutput> InvalidatedTransparencyOutputs;
    TArray<FString> Failures;
    // Worker tickets are owned by Operation so cancellation is request-scoped.
    TArray<int32> PendingMaterialSlotIndices;
    int32 InFlightJobs = 0;
    int32 MaxInFlightJobs = DefaultWrinkleBakeMaxInFlightJobs;
    FWetWrinkleNormalMapBakeSettings Settings;
    TUniquePtr<FWetWrinkleNormalMapBakeSession> SnapshotSession;
    // Presentation completion is owned by Operation.
};

struct FDWCEditorBakeCoordinator::FTransparencyBatch
{
    TSharedPtr<FDWCEditorBuildOperation> Operation;
    int32 SubmittedCount = 0;
    int32 FinishedCount = 0;
    int32 BakedMapCount = 0;
    int32 BakedRevealSurfaceCount = 0;
    int32 AppliedStrokeCount = 0;
    int32 AppliedSampleCount = 0;
    bool bSaveAfterCommit = false;
    bool bCanceled = false;
    bool bFinalized = false;
    bool bAffectedStage4Only = false;
    bool bSubmissionComplete = true;
    TArray<FString> TextureNames;
    TArray<FString> Warnings;
    TArray<FString> Failures;
    // Worker tickets are owned by Operation so cancellation is request-scoped.
    FDWCTransparencyAffectedRebakeSequence AffectedSequence;
    // Presentation completion is owned by Operation.
};

FDWCEditorBakeCoordinator::FDWCEditorBakeCoordinator(
    UWetClothingAsset* InAsset,
    TSharedRef<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe> InScheduler,
    TSharedRef<FDWCEditorBuildOperationManager> InOperationManager,
    TSharedRef<FDWCEditorSpatialQueryService> InSpatialQueryService,
    TSharedRef<FDWCEditorSurfacePatchProjectionCacheService> InSurfacePatchProjectionCache,
    TSharedPtr<FDWCWrinkleSuppressionCoverageService> InCoverageService)
    : Asset(InAsset)
    , Scheduler(InScheduler)
    , OperationManager(MoveTemp(InOperationManager))
    , SpatialQueryService(MoveTemp(InSpatialQueryService))
    , SurfacePatchProjectionCache(MoveTemp(InSurfacePatchProjectionCache))
    , CoverageService(MoveTemp(InCoverageService))
{
}

FDWCEditorBakeCoordinator::~FDWCEditorBakeCoordinator()
{
    Shutdown();
}

bool FDWCEditorBakeCoordinator::RequestWrinkleBake(
    TArray<int32> MaterialSlotIndices,
    const bool bSaveAfterCommit,
    FCompletion Completion,
    FString* OutError)
{
    check(IsInGameThread());
    if (OutError != nullptr)
    {
        OutError->Reset();
    }
    UWetClothingAsset* TargetAsset = Asset.Get();
    if (TargetAsset == nullptr || !Scheduler.IsValid() || !OperationManager.IsValid() ||
        !SpatialQueryService.IsValid() || !SurfacePatchProjectionCache.IsValid())
    {
        if (OutError != nullptr) *OutError = TEXT("The bake asset or worker scheduler is unavailable.");
        return false;
    }

    MaterialSlotIndices.Sort();
    MaterialSlotIndices.SetNum(Algo::Unique(MaterialSlotIndices));
    MaterialSlotIndices.Remove(INDEX_NONE);
    if (MaterialSlotIndices.IsEmpty())
    {
        if (OutError != nullptr) *OutError = TEXT("No material slots were provided for wrinkle baking.");
        return false;
    }

    if (ActiveWrinkleBatch.IsValid() && ActiveWrinkleBatch->Operation.IsValid())
    {
        const TSharedPtr<FWrinkleBatch> PreviousBatch = ActiveWrinkleBatch;
        OperationManager->CancelOperation(
            PreviousBatch->Operation.ToSharedRef(),
            EDWCEditorBuildTerminalReason::Superseded);
    }

    TSharedPtr<FDWCEditorBuildOperation> Operation = OperationManager->BeginOperation(
        EDWCEditorBuildAction::BakeWrinkleTextures,
        EDWCEditorAsyncRequestPolicy::LatestWins,
        [Completion = MoveTemp(Completion)](const FDWCEditorBuildOperationResult& BuildResult) mutable
        {
            if (!Completion)
            {
                return;
            }
            FDWCEditorBakeBatchResult Result;
            Result.bSucceeded = BuildResult.IsSuccessful();
            Result.bHadWarnings = BuildResult.HasWarnings();
            Result.bCanceled = BuildResult.WasCanceled();
            Result.Summary = BuildResult.Summary;
            Result.AttentionSummary = BuildResult.AttentionSummary;
            Result.OutOfDateTransparencyMaterialSlots = BuildResult.AffectedMaterialSlotIndices;
            Completion(Result);
        },
        OutError);
    if (!Operation.IsValid())
    {
        return false;
    }
    TSharedRef<FWrinkleBatch> Batch = MakeShared<FWrinkleBatch>();
    Batch->Operation = Operation;
    Batch->bSaveAfterCommit = bSaveAfterCommit;
    ActiveWrinkleBatch = Batch;
    Operation->SetPhase(EDWCEditorBuildOperationPhase::Preparing);

    Batch->Settings.Resolution = TargetAsset->GetWrinkleMapResolution();
    Batch->Settings.PaddingPixels = TargetAsset->Authored.WrinkleData.BakeSettings.PaddingPixels;
    Batch->Settings.bIncludeDisabledPatches = TargetAsset->Authored.WrinkleData.BakeSettings.bIncludeDisabledPatches;
    Batch->PendingMaterialSlotIndices = MoveTemp(MaterialSlotIndices);
    // A 4K bake keeps the raster, coverage, result pixels, dilation scratch,
    // source snapshots, and generated texture source alive together. Serialize
    // that tier even when the user configured a higher general job count.
    const int32 RequestedMaxInFlightJobs = ResolveWrinkleBakeMaxInFlightJobs();
    const int32 ResolutionBoundMaxInFlightJobs = Batch->Settings.Resolution >= 4096
        ? 1
        : RequestedMaxInFlightJobs;
    Batch->MaxInFlightJobs = ResolutionBoundMaxInFlightJobs;
    Batch->SnapshotSession = MakeUnique<FWetWrinkleNormalMapBakeSession>(
        SpatialQueryService.ToSharedRef(),
        SurfacePatchProjectionCache.ToSharedRef());

    if (!PumpWrinkleJobs(Batch) && Batch->PendingMaterialSlotIndices.IsEmpty())
    {
        FinalizeWrinkleBatch(Batch);
    }
    return true;
}

bool FDWCEditorBakeCoordinator::PumpWrinkleJobs(const TSharedRef<FWrinkleBatch>& Batch)
{
    check(IsInGameThread());
    UWetClothingAsset* TargetAsset = Asset.Get();
    if (TargetAsset == nullptr || !Scheduler.IsValid() || !Batch->Operation.IsValid() ||
        Batch->Operation->IsCancellationRequested() ||
        ActiveWrinkleBatch != Batch || !Batch->SnapshotSession.IsValid())
    {
        return false;
    }

    TWeakPtr<FDWCEditorBakeCoordinator> WeakThis = AsShared();
    bool bSubmittedAny = false;
    while (Batch->InFlightJobs < Batch->MaxInFlightJobs &&
           !Batch->PendingMaterialSlotIndices.IsEmpty())
    {
        const int32 MaterialSlotIndex = Batch->PendingMaterialSlotIndices[0];
        Batch->PendingMaterialSlotIndices.RemoveAt(0, 1, EAllowShrinking::No);

        FDWCEditorWorkerJobDescriptor Descriptor;
        Descriptor.Key.Kind = EDWCEditorWorkerJobKind::WrinkleBake;
        Descriptor.Key.MaterialSlotIndex = MaterialSlotIndex;
        Descriptor.Domain = EDWCEditorAuthoringDomain::Wrinkle;
        Descriptor.DomainRevision = Scheduler->GetCurrentDomainRevision(Descriptor.Domain);
        Descriptor.Priority = EDWCEditorWorkerJobPriority::UserInitiated;
        Descriptor.RequestPolicy = EDWCEditorAsyncRequestPolicy::FIFO;
        const FIntPoint FinalTextureSize =
            WetWrinkleTextureRaster::ResolveFinalTextureSize(Batch->Settings.Resolution);
        const FIntPoint WorkingTextureSize =
            WetWrinkleTextureRaster::ResolveWorkingTextureSize(FinalTextureSize);
        const uint64 PixelCount = static_cast<uint64>(WorkingTextureSize.X) *
            static_cast<uint64>(WorkingTextureSize.Y);
        Descriptor.MemoryEstimate.WorkingBytes = PixelCount * sizeof(FVector4f);
        Descriptor.DebugName = FString::Printf(TEXT("Wrinkle bake slot %d"), MaterialSlotIndex);

        FString SubmitError;
        const FDWCEditorWorkerJobTicket Ticket = Scheduler->SubmitPrepared(
            Descriptor,
            [WeakThis, Batch, MaterialSlotIndex](
                const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& Token,
                FDWCEditorWorkerJobScheduler::FPreparedWorkerJob& OutPrepared,
                FString& PrepareError)
            {
                check(IsInGameThread());
                const TSharedPtr<FDWCEditorBakeCoordinator> Self = WeakThis.Pin();
                UWetClothingAsset* CurrentAsset = Self.IsValid() ? Self->Asset.Get() : nullptr;
                if (CurrentAsset == nullptr || !Batch->SnapshotSession.IsValid() || Token->IsCanceled())
                {
                    PrepareError = TEXT("The wrinkle bake request became unavailable before snapshot preparation.");
                    return false;
                }
                TSharedRef<FWetWrinkleNormalMapBakeSnapshot, ESPMode::ThreadSafe> Snapshot =
                    MakeShared<FWetWrinkleNormalMapBakeSnapshot, ESPMode::ThreadSafe>();
                if (!FWetWrinkleNormalMapBaker::BuildMaterialSlotSnapshot(
                        CurrentAsset,
                        MaterialSlotIndex,
                        Batch->Settings,
                        *Batch->SnapshotSession,
                        *Snapshot,
                        PrepareError))
                {
                    return false;
                }
                OutPrepared.ActualMemoryEstimate.SnapshotBytes = Snapshot->GetEstimatedSnapshotBytes();
                OutPrepared.ActualMemoryEstimate.WorkingBytes = Snapshot->GetEstimatedWorkingBytes();
                OutPrepared.ActualMemoryEstimate.OutputBytes = Snapshot->GetEstimatedResultBytes();
                OutPrepared.Work = [Snapshot](
                    const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& WorkToken)
                {
                    TSharedPtr<FWrinkleBakeWorkerResult, ESPMode::ThreadSafe> Result =
                        MakeShared<FWrinkleBakeWorkerResult, ESPMode::ThreadSafe>();
                    Result->Snapshot = Snapshot;
                    Result->Computed = FWetWrinkleNormalMapBaker::ComputeSnapshot(*Snapshot, &WorkToken.Get());
                    Result->bSucceeded = Result->Computed.bSucceeded;
                    Result->Error = Result->Computed.Error;
                    Result->ResultBytes = Result->Computed.ResultBytes;
                    return StaticCastSharedPtr<FDWCEditorWorkerJobResult>(Result);
                };
                return true;
            },
            [WeakThis, Batch, MaterialSlotIndex](
                const FDWCEditorWorkerJobTicket& FinishedTicket,
                TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe> BaseResult)
            {
                const TSharedPtr<FDWCEditorBakeCoordinator> Self = WeakThis.Pin();
                const TSharedPtr<FWrinkleBakeWorkerResult, ESPMode::ThreadSafe> Result =
                    StaticCastSharedPtr<FWrinkleBakeWorkerResult>(BaseResult);
                UWetClothingAsset* CurrentAsset = Self.IsValid() ? Self->Asset.Get() : nullptr;
                if (!Self.IsValid() || CurrentAsset == nullptr || !Result.IsValid() || !Result->Snapshot.IsValid())
                {
                    return;
                }

                FWetWrinkleNormalMapBakeResult CommitResult;
                FString CommitError;
                if (!FWetWrinkleNormalMapBaker::CommitComputedResult(
                        CurrentAsset,
                        *Result->Snapshot,
                        MoveTemp(Result->Computed),
                        CommitResult,
                        CommitError))
                {
                    Batch->Failures.Add(FString::Printf(TEXT("Slot %d: %s"), MaterialSlotIndex, *CommitError));
                    return;
                }
                if (Self->CoverageService.IsValid())
                {
                    Self->CoverageService->InvalidateAssetSlot(CurrentAsset, MaterialSlotIndex);
                }
                Batch->BakedMapCount += CommitResult.BakedMapCount;
                Batch->BakedPatchCount += CommitResult.BakedStampCount;
                Batch->BakedStrokeCount += CommitResult.BakedProceduralStrokeCount;
                for (const UTexture2D* Texture : CommitResult.BakedNormalMaps)
                {
                    Batch->NormalTextureNames.Add(GetPathNameSafe(Texture));
                }
                for (const UTexture2D* Texture : CommitResult.BakedMasks)
                {
                    Batch->MaskTextureNames.Add(GetPathNameSafe(Texture));
                }
                for (FWetWrinkleInvalidatedTransparencyOutput& Invalidated :
                     CommitResult.InvalidatedTransparencyOutputs)
                {
                    if (!Batch->InvalidatedTransparencyOutputs.ContainsByPredicate(
                            [&Invalidated](const FWetWrinkleInvalidatedTransparencyOutput& Existing)
                            {
                                return Existing.MaterialSlotIndex == Invalidated.MaterialSlotIndex;
                            }))
                    {
                        Batch->InvalidatedTransparencyOutputs.Add(MoveTemp(Invalidated));
                    }
                }
            },
            &SubmitError,
            [WeakThis, Batch, MaterialSlotIndex](
                const FDWCEditorWorkerJobTicket& FinishedTicket,
                const EDWCEditorWorkerJobCompletion JobCompletion,
                const FString& WorkerError)
            {
                if (const TSharedPtr<FDWCEditorBakeCoordinator> Self = WeakThis.Pin())
                {
                    Self->HandleWrinkleJobFinished(
                        Batch,
                        FinishedTicket,
                        MaterialSlotIndex,
                        static_cast<uint8>(JobCompletion),
                        WorkerError);
                }
            });

        if (!Ticket.IsValid())
        {
            Batch->Failures.Add(FString::Printf(TEXT("Slot %d: %s"), MaterialSlotIndex, *SubmitError));
            continue;
        }
        Batch->Operation->RegisterTicket(Ticket);
        Batch->Operation->SetPhase(EDWCEditorBuildOperationPhase::Running);
        ++Batch->SubmittedCount;
        ++Batch->InFlightJobs;
        bSubmittedAny = true;
    }

    return bSubmittedAny;
}

void FDWCEditorBakeCoordinator::HandleWrinkleJobFinished(
    const TSharedRef<FWrinkleBatch>& Batch,
    const FDWCEditorWorkerJobTicket& Ticket,
    const int32 MaterialSlotIndex,
    const uint8 CompletionCode,
    const FString& WorkerError)
{
    check(IsInGameThread());
    const EDWCEditorWorkerJobCompletion Completion =
        static_cast<EDWCEditorWorkerJobCompletion>(CompletionCode);
    if (Batch->Operation.IsValid())
    {
        Batch->Operation->NotifyTicketFinished(Ticket);
    }
    ++Batch->FinishedCount;
    Batch->InFlightJobs = FMath::Max(0, Batch->InFlightJobs - 1);
    if (Completion != EDWCEditorWorkerJobCompletion::Applied)
    {
        if (Batch->Operation.IsValid() &&
            (Completion == EDWCEditorWorkerJobCompletion::Canceled ||
             Completion == EDWCEditorWorkerJobCompletion::Superseded ||
             Completion == EDWCEditorWorkerJobCompletion::Stale))
        {
            const EDWCEditorBuildTerminalReason Reason =
                Completion == EDWCEditorWorkerJobCompletion::Superseded
                    ? EDWCEditorBuildTerminalReason::Superseded
                    : Completion == EDWCEditorWorkerJobCompletion::Stale
                        ? EDWCEditorBuildTerminalReason::Stale
                        : EDWCEditorBuildTerminalReason::Canceled;
            Batch->Operation->RequestCancellation(Reason);
        }
        Batch->bCanceled = Batch->Operation.IsValid() && Batch->Operation->IsCancellationRequested();
        const FString FailureReason = WorkerError.IsEmpty()
            ? DescribeCompletion(Completion)
            : WorkerError;
        Batch->Failures.Add(FString::Printf(
            TEXT("Slot %d: %s"),
            MaterialSlotIndex,
            *FailureReason));
    }
    if (!Batch->bCanceled && ActiveWrinkleBatch == Batch)
    {
        PumpWrinkleJobs(Batch);
    }
    if ((Batch->bCanceled || Batch->PendingMaterialSlotIndices.IsEmpty()) &&
        Batch->InFlightJobs == 0)
    {
        FinalizeWrinkleBatch(Batch);
    }
}

void FDWCEditorBakeCoordinator::FinalizeWrinkleBatch(const TSharedRef<FWrinkleBatch>& Batch)
{
    check(IsInGameThread());
    if (Batch->bFinalized)
    {
        return;
    }
    Batch->bFinalized = true;

    UE_LOG(
        LogDWCWrinkleBakeCoordinator,
        Display,
        TEXT("Wrinkle bake diagnostics: batch=%llu, submitted=%d, finished=%d, maxConcurrentJobs=%d."),
        Batch->Operation.IsValid() ? Batch->Operation->GetOperationId() : 0,
        Batch->SubmittedCount,
        Batch->FinishedCount,
        Batch->MaxInFlightJobs);

    // Snapshots are only used while jobs are in flight. Releasing the session
    // here drops source readbacks and any remaining build-time cache before
    // potentially saving the generated assets or notifying the editor.
    Batch->PendingMaterialSlotIndices.Reset();
    Batch->SnapshotSession.Reset();

    if (Batch->Operation.IsValid() &&
        Batch->Operation->GetCancellationReason() == EDWCEditorBuildTerminalReason::OwnerShutdown)
    {
        if (ActiveWrinkleBatch == Batch)
        {
            ActiveWrinkleBatch.Reset();
        }
        FDWCEditorBuildOperationResult ShutdownResult;
        ShutdownResult.Reason = EDWCEditorBuildTerminalReason::OwnerShutdown;
        ShutdownResult.Summary = TEXT("The wrinkle bake was retired because the editor closed.");
        Batch->Operation->Complete(MoveTemp(ShutdownResult));
        return;
    }

    // A replacement request revokes the previous batch's right to publish.
    // Its workers may still finish later, but they must not overwrite the
    // status, save, or UI completion produced by the current batch.
    if (ActiveWrinkleBatch != Batch)
    {
        if (Batch->Operation.IsValid())
        {
            FDWCEditorBuildOperationResult SupersededResult;
            SupersededResult.Reason = Batch->Operation->GetCancellationReason();
            if (SupersededResult.Reason == EDWCEditorBuildTerminalReason::None)
            {
                SupersededResult.Reason = EDWCEditorBuildTerminalReason::Superseded;
            }
            SupersededResult.Summary = TEXT("The wrinkle bake was superseded by a newer request.");
            Batch->Operation->Complete(MoveTemp(SupersededResult));
        }
        return;
    }

    UWetClothingAsset* TargetAsset = Asset.Get();
    bool bSaved = true;
    if (TargetAsset != nullptr)
    {
        FWetWrinkleBakeService::RefreshBakeStatusFromCurrentOutputs(
            TargetAsset,
            Batch->Failures.IsEmpty() ? FString() : FString::Join(Batch->Failures, TEXT("\n")));
        if (Batch->BakedMapCount > 0 && Batch->bSaveAfterCommit)
        {
            bSaved = DWCEditorUtils::SaveAsset(TargetAsset);
        }
    }

    FDWCEditorBakeBatchResult Result;
    Result.bSucceeded = Batch->BakedMapCount > 0 && Batch->Failures.IsEmpty() && bSaved;
    Result.bHadWarnings =
        !Batch->Failures.IsEmpty() ||
        !Batch->InvalidatedTransparencyOutputs.IsEmpty() ||
        !bSaved;
    Result.bCanceled = Batch->bCanceled;
    Batch->InvalidatedTransparencyOutputs.Sort(
        [](const FWetWrinkleInvalidatedTransparencyOutput& Left,
           const FWetWrinkleInvalidatedTransparencyOutput& Right)
        {
            return Left.MaterialSlotIndex < Right.MaterialSlotIndex;
        });
    for (const FWetWrinkleInvalidatedTransparencyOutput& Invalidated :
         Batch->InvalidatedTransparencyOutputs)
    {
        Result.OutOfDateTransparencyMaterialSlots.Add(Invalidated.MaterialSlotIndex);
    }
    Result.Summary = FString::Printf(
        TEXT("Baked %d wrinkle map set(s) from %d patch(es) and %d procedural ridge stroke(s)."),
        Batch->BakedMapCount,
        Batch->BakedPatchCount,
        Batch->BakedStrokeCount);
    if (!Batch->NormalTextureNames.IsEmpty())
    {
        Result.Summary += FString::Printf(TEXT("\n\nNormal textures:\n- %s"), *FString::Join(Batch->NormalTextureNames, TEXT("\n- ")));
    }
    if (!Batch->MaskTextureNames.IsEmpty())
    {
        Result.Summary += FString::Printf(TEXT("\n\nSeparation masks:\n- %s"), *FString::Join(Batch->MaskTextureNames, TEXT("\n- ")));
    }
    if (!Batch->InvalidatedTransparencyOutputs.IsEmpty())
    {
        TArray<FString> InvalidatedDescriptions;
        InvalidatedDescriptions.Reserve(Batch->InvalidatedTransparencyOutputs.Num());
        for (const FWetWrinkleInvalidatedTransparencyOutput& Invalidated :
             Batch->InvalidatedTransparencyOutputs)
        {
            const FString SlotLabel = Invalidated.MaterialSlotName.IsEmpty()
                ? FString::Printf(TEXT("Slot %d"), Invalidated.MaterialSlotIndex)
                : FString::Printf(
                    TEXT("%s (Slot %d)"),
                    *Invalidated.MaterialSlotName,
                    Invalidated.MaterialSlotIndex);
            InvalidatedDescriptions.Add(Invalidated.TransparencyTextureName.IsEmpty()
                ? SlotLabel
                : FString::Printf(TEXT("%s\n  %s"), *SlotLabel, *Invalidated.TransparencyTextureName));
        }
        Result.AttentionSummary = FString::Printf(
            TEXT("Transparency Maps are now out of date:\n- %s")
            TEXT("\n\nThe previous maps remain visible in editor preview. ")
            TEXT("Use Build for Runtime > Rebake Affected Transparency Maps before runtime use."),
            *FString::Join(InvalidatedDescriptions, TEXT("\n- ")));
    }
    if (!Batch->Failures.IsEmpty())
    {
        Result.Summary += FString::Printf(TEXT("\n\nSkipped or failed:\n- %s"), *FString::Join(Batch->Failures, TEXT("\n- ")));
    }
    if (!bSaved)
    {
        Result.Summary += TEXT("\n\nThe generated assets could not be saved.");
    }

    if (ActiveWrinkleBatch == Batch)
    {
        ActiveWrinkleBatch.Reset();
    }
    if (Batch->Operation.IsValid())
    {
        FDWCEditorBuildOperationResult BuildResult;
        BuildResult.Reason = Result.bSucceeded
            ? (Result.bHadWarnings
                ? EDWCEditorBuildTerminalReason::SucceededWithWarnings
                : EDWCEditorBuildTerminalReason::Succeeded)
            : Result.bCanceled
                ? EDWCEditorBuildTerminalReason::Canceled
                : EDWCEditorBuildTerminalReason::Failed;
        BuildResult.Summary = MoveTemp(Result.Summary);
        BuildResult.AttentionSummary = MoveTemp(Result.AttentionSummary);
        BuildResult.AffectedMaterialSlotIndices = MoveTemp(Result.OutOfDateTransparencyMaterialSlots);
        Batch->Operation->Complete(MoveTemp(BuildResult));
    }
}

bool FDWCEditorBakeCoordinator::RequestTransparencyBake(
    TArray<FGuid> LayerGuids,
    const bool bSaveAfterCommit,
    FCompletion Completion,
    FString* OutError)
{
    check(IsInGameThread());
    if (OutError != nullptr) OutError->Reset();
    UWetClothingAsset* TargetAsset = Asset.Get();
    if (TargetAsset == nullptr || !Scheduler.IsValid())
    {
        if (OutError != nullptr) *OutError = TEXT("The bake asset or worker scheduler is unavailable.");
        return false;
    }
    if (LayerGuids.IsEmpty())
    {
        if (OutError != nullptr) *OutError = TEXT("No transparency layers were provided for baking.");
        return false;
    }

    if (ActiveTransparencyBatch.IsValid() && ActiveTransparencyBatch->Operation.IsValid())
    {
        const TSharedPtr<FTransparencyBatch> PreviousBatch = ActiveTransparencyBatch;
        OperationManager->CancelOperation(
            PreviousBatch->Operation.ToSharedRef(),
            EDWCEditorBuildTerminalReason::Superseded);
    }
    TSharedPtr<FDWCEditorBuildOperation> Operation = OperationManager->BeginOperation(
        EDWCEditorBuildAction::BakeTransparencyTextures,
        EDWCEditorAsyncRequestPolicy::LatestWins,
        [Completion = MoveTemp(Completion)](const FDWCEditorBuildOperationResult& BuildResult) mutable
        {
            if (!Completion)
            {
                return;
            }
            FDWCEditorBakeBatchResult Result;
            Result.bSucceeded = BuildResult.IsSuccessful();
            Result.bHadWarnings = BuildResult.HasWarnings();
            Result.bCanceled = BuildResult.WasCanceled();
            Result.Summary = BuildResult.Summary;
            Result.AttentionSummary = BuildResult.AttentionSummary;
            Result.OutOfDateTransparencyMaterialSlots = BuildResult.AffectedMaterialSlotIndices;
            Completion(Result);
        },
        OutError);
    if (!Operation.IsValid())
    {
        return false;
    }
    TSharedRef<FTransparencyBatch> Batch = MakeShared<FTransparencyBatch>();
    Batch->Operation = Operation;
    Batch->bSaveAfterCommit = bSaveAfterCommit;
    ActiveTransparencyBatch = Batch;
    Operation->SetPhase(EDWCEditorBuildOperationPhase::Preparing);

    for (const FGuid& LayerGuid : LayerGuids)
    {
        const FWetClothingTransparencyLayerData* Layer =
            TargetAsset->Authored.TransparencyData.TransparencyLayers.FindByPredicate(
                [&LayerGuid](const FWetClothingTransparencyLayerData& Candidate)
                {
                    return Candidate.LayerGuid == LayerGuid;
                });
        if (Layer == nullptr)
        {
            Batch->Failures.Add(TEXT("A requested transparency layer is unavailable."));
            continue;
        }

        if (Layer->SourceType == EDWCTransparencySourceType::ManualColorOrTexture)
        {
            TSharedRef<FDWCTransparencySourcePayload> SourcePayload =
                MakeShared<FDWCTransparencySourcePayload>();
            FString GenerateSummary;
            TArray<FString> GenerateWarnings;
            if (!FDWCTransparencyAutoMapGenerator::GenerateBaseRevealColorMap(
                    *TargetAsset,
                    *Layer,
                    *SourcePayload,
                    GenerateSummary,
                    GenerateWarnings))
            {
                Batch->Failures.Add(FString::Printf(
                    TEXT("Slot %d: %s"),
                    Layer->TargetSurface.OuterMaterialSlotIndex,
                    *GenerateSummary));
                continue;
            }
            Batch->Warnings.Append(GenerateWarnings);
            FString SubmitError;
            if (!SubmitTransparencyJob(Batch, LayerGuid, SourcePayload, SubmitError))
            {
                Batch->Failures.Add(FString::Printf(
                    TEXT("Slot %d: %s"),
                    Layer->TargetSurface.OuterMaterialSlotIndex,
                    *SubmitError));
            }
            continue;
        }

        FDWCEditorWorkerJobDescriptor Descriptor;
        Descriptor.Key.Kind = EDWCEditorWorkerJobKind::TransparencyAutoBake;
        Descriptor.Key.MaterialSlotIndex = Layer->TargetSurface.OuterMaterialSlotIndex;
        Descriptor.Key.LayerGuid = LayerGuid;
        Descriptor.Domain = EDWCEditorAuthoringDomain::Transparency;
        Descriptor.DomainRevision = Scheduler->GetCurrentDomainRevision(Descriptor.Domain);
        Descriptor.Priority = EDWCEditorWorkerJobPriority::UserInitiated;
        Descriptor.RequestPolicy = EDWCEditorAsyncRequestPolicy::FIFO;
        const uint64 PixelCount = static_cast<uint64>(
            TargetAsset->Authored.TransparencyData.TransparencyBakeResolution) *
            static_cast<uint64>(TargetAsset->Authored.TransparencyData.TransparencyBakeResolution);
        Descriptor.MemoryEstimate.WorkingBytes = PixelCount * sizeof(FVector4f);
        Descriptor.DebugName = FString::Printf(
            TEXT("Transparency projection slot %d"),
            Layer->TargetSurface.OuterMaterialSlotIndex);

        TSharedRef<bool, ESPMode::ThreadSafe> bFinalSubmitted =
            MakeShared<bool, ESPMode::ThreadSafe>(false);
        TSharedRef<FString, ESPMode::ThreadSafe> FinalSubmitErrorHolder =
            MakeShared<FString, ESPMode::ThreadSafe>();
        TWeakPtr<FDWCEditorBakeCoordinator> WeakThis = AsShared();
        const int32 MaterialSlotIndex = Layer->TargetSurface.OuterMaterialSlotIndex;
        FString SubmitError;
        const FDWCEditorWorkerJobTicket Ticket = Scheduler->SubmitPrepared(
            Descriptor,
            [WeakThis, LayerGuid](
                const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& Token,
                FDWCEditorWorkerJobScheduler::FPreparedWorkerJob& OutPrepared,
                FString& PrepareError)
            {
                check(IsInGameThread());
                const TSharedPtr<FDWCEditorBakeCoordinator> Self = WeakThis.Pin();
                UWetClothingAsset* CurrentAsset = Self.IsValid() ? Self->Asset.Get() : nullptr;
                if (CurrentAsset == nullptr || Token->IsCanceled())
                {
                    PrepareError = TEXT("The transparency projection request became unavailable before snapshot preparation.");
                    return false;
                }
                const FWetClothingTransparencyLayerData* CurrentLayer =
                    CurrentAsset->Authored.TransparencyData.TransparencyLayers.FindByPredicate(
                        [&LayerGuid](const FWetClothingTransparencyLayerData& Candidate)
                        {
                            return Candidate.LayerGuid == LayerGuid;
                        });
                if (CurrentLayer == nullptr)
                {
                    PrepareError = TEXT("The transparency layer was removed before snapshot preparation.");
                    return false;
                }
                TSharedRef<FDWCTransparencyAutoMapSnapshot, ESPMode::ThreadSafe> AutoSnapshot =
                    MakeShared<FDWCTransparencyAutoMapSnapshot, ESPMode::ThreadSafe>();
                if (!FDWCTransparencyAutoMapGenerator::BuildProjectionSnapshot(
                        *CurrentAsset,
                        *CurrentLayer,
                        *AutoSnapshot,
                        PrepareError))
                {
                    return false;
                }
                OutPrepared.ActualMemoryEstimate.SnapshotBytes = AutoSnapshot->GetEstimatedBytes();
                OutPrepared.Work = [AutoSnapshot](
                    const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& WorkToken)
                {
                    TSharedPtr<FTransparencyAutoBakeWorkerResult, ESPMode::ThreadSafe> Result =
                        MakeShared<FTransparencyAutoBakeWorkerResult, ESPMode::ThreadSafe>();
                    Result->Computed = FDWCTransparencyAutoMapGenerator::ComputeSameMeshSnapshot(
                        *AutoSnapshot,
                        &WorkToken.Get());
                    Result->bSucceeded = Result->Computed.bSucceeded;
                    Result->Error = Result->Computed.Error;
                    Result->ResultBytes = Result->Computed.ResultBytes;
                    return StaticCastSharedPtr<FDWCEditorWorkerJobResult>(Result);
                };
                return true;
            },
            [WeakThis, Batch, bFinalSubmitted, FinalSubmitErrorHolder, LayerGuid, MaterialSlotIndex](
                const FDWCEditorWorkerJobTicket& FinishedTicket,
                TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe> BaseResult)
            {
                const TSharedPtr<FDWCEditorBakeCoordinator> Self = WeakThis.Pin();
                const TSharedPtr<FTransparencyAutoBakeWorkerResult, ESPMode::ThreadSafe> Result =
                    StaticCastSharedPtr<FTransparencyAutoBakeWorkerResult>(BaseResult);
                if (!Self.IsValid() || !Result.IsValid())
                {
                    return;
                }
                for (const FString& Warning : Result->Computed.Warnings)
                {
                    Batch->Warnings.Add(FString::Printf(TEXT("Slot %d: %s"), MaterialSlotIndex, *Warning));
                }
                TSharedRef<FDWCTransparencySourcePayload> SourcePayload =
                    MakeShared<FDWCTransparencySourcePayload>(MoveTemp(Result->Computed.SourcePayload));
                FString FinalSubmitError;
                if (Self->SubmitTransparencyJob(
                        Batch,
                        LayerGuid,
                        SourcePayload,
                        FinalSubmitError,
                        false))
                {
                    *bFinalSubmitted = true;
                }
                else
                {
                    *FinalSubmitErrorHolder = MoveTemp(FinalSubmitError);
                }
            },
            &SubmitError,
            [WeakThis, Batch, bFinalSubmitted, FinalSubmitErrorHolder, MaterialSlotIndex](
                const FDWCEditorWorkerJobTicket& FinishedTicket,
                const EDWCEditorWorkerJobCompletion JobCompletion,
                const FString& WorkerError)
            {
                if (Batch->Operation.IsValid())
                {
                    Batch->Operation->NotifyTicketFinished(FinishedTicket);
                }
                if (JobCompletion == EDWCEditorWorkerJobCompletion::Applied && *bFinalSubmitted)
                {
                    return;
                }
                if (const TSharedPtr<FDWCEditorBakeCoordinator> Self = WeakThis.Pin())
                {
                    Self->HandleTransparencyJobFinished(
                        Batch,
                        FinishedTicket,
                        MaterialSlotIndex,
                        static_cast<uint8>(
                            JobCompletion == EDWCEditorWorkerJobCompletion::Applied
                                ? EDWCEditorWorkerJobCompletion::Failed
                                : JobCompletion),
                        WorkerError.IsEmpty() ? *FinalSubmitErrorHolder : WorkerError);
                }
            });
        if (!Ticket.IsValid())
        {
            Batch->Failures.Add(FString::Printf(
                TEXT("Slot %d: %s"),
                MaterialSlotIndex,
                *SubmitError));
            continue;
        }
        if (Batch->Operation.IsValid())
        {
            Batch->Operation->RegisterTicket(Ticket);
            Batch->Operation->SetPhase(EDWCEditorBuildOperationPhase::Running);
        }
        ++Batch->SubmittedCount;
    }
    if (Batch->SubmittedCount == 0)
    {
        FinalizeTransparencyBatch(Batch);
    }
    return true;
}

bool FDWCEditorBakeCoordinator::RequestTransparencyFinalBake(
    const FGuid LayerGuid,
    TSharedRef<const FDWCTransparencySourcePayload> SourcePayload,
    TSharedPtr<const FDWCTransparencyAlphaWorkingSnapshot> AlphaSnapshot,
    TSharedRef<const FDWCTransparencyFinalSettingsSnapshot> FinalSettings,
    const bool bSaveAfterCommit,
    FCompletion Completion,
    FString* OutError)
{
    check(IsInGameThread());
    if (OutError != nullptr) OutError->Reset();
    if (ActiveTransparencyBatch.IsValid())
    {
        if (OutError != nullptr) *OutError = TEXT("A transparency bake is already in progress.");
        return false;
    }
    if (!OperationManager.IsValid())
    {
        if (OutError != nullptr) *OutError = TEXT("The build operation manager is unavailable.");
        return false;
    }
    TSharedPtr<FDWCEditorBuildOperation> Operation = OperationManager->BeginOperation(
        EDWCEditorBuildAction::BakeTransparencyTextures,
        EDWCEditorAsyncRequestPolicy::Singleton,
        [Completion = MoveTemp(Completion)](const FDWCEditorBuildOperationResult& BuildResult) mutable
        {
            if (!Completion) return;
            FDWCEditorBakeBatchResult Result;
            Result.bSucceeded = BuildResult.IsSuccessful();
            Result.bHadWarnings = BuildResult.HasWarnings();
            Result.bCanceled = BuildResult.WasCanceled();
            Result.Summary = BuildResult.Summary;
            Completion(Result);
        },
        OutError);
    if (!Operation.IsValid())
    {
        return false;
    }
    TSharedRef<FTransparencyBatch> Batch = MakeShared<FTransparencyBatch>();
    Batch->Operation = Operation;
    Batch->bSaveAfterCommit = bSaveAfterCommit;
    ActiveTransparencyBatch = Batch;
    Operation->SetPhase(EDWCEditorBuildOperationPhase::Preparing);
    FString SubmitError;
    if (!SubmitTransparencyJob(
            Batch,
            LayerGuid,
            SourcePayload,
            SubmitError,
            true,
            MoveTemp(AlphaSnapshot),
            FinalSettings))
    {
        Batch->bFinalized = true;
        if (ActiveTransparencyBatch == Batch)
        {
            ActiveTransparencyBatch.Reset();
        }
        Operation->DetachPresentationCallback();
        FDWCEditorBuildOperationResult FailedResult;
        FailedResult.Reason = EDWCEditorBuildTerminalReason::Failed;
        FailedResult.Summary = SubmitError;
        Operation->Complete(MoveTemp(FailedResult));
        if (OutError != nullptr) *OutError = SubmitError;
        return false;
    }
    return true;
}

bool FDWCEditorBakeCoordinator::RequestAffectedTransparencyStage4Rebake(
    TArray<int32> MaterialSlotIndices,
    const bool bSaveAfterCommit,
    FCompletion Completion,
    FString* OutError)
{
    check(IsInGameThread());
    if (OutError != nullptr) OutError->Reset();
    UWetClothingAsset* TargetAsset = Asset.Get();
    if (TargetAsset == nullptr || !Scheduler.IsValid() || !OperationManager.IsValid())
    {
        if (OutError != nullptr) *OutError = TEXT("The bake asset, scheduler, or spatial query service is unavailable.");
        return false;
    }
    if (ActiveTransparencyBatch.IsValid())
    {
        if (OutError != nullptr) *OutError = TEXT("A transparency bake is already in progress.");
        return false;
    }

    MaterialSlotIndices.Sort();
    MaterialSlotIndices.SetNum(Algo::Unique(MaterialSlotIndices));
    TArray<FDWCTransparencyAffectedRebakeCandidate> Candidates;
    FDWCTransparencyAffectedStage4Rebake::CollectCandidates(
        *TargetAsset,
        MaterialSlotIndices,
        Candidates);

    TSharedRef<FTransparencyBatch> Batch = MakeShared<FTransparencyBatch>();
    if (!OperationManager.IsValid())
    {
        if (OutError != nullptr) *OutError = TEXT("The build operation manager is unavailable.");
        return false;
    }
    TSharedPtr<FDWCEditorBuildOperation> Operation = OperationManager->BeginOperation(
        EDWCEditorBuildAction::RebakeAffectedTransparencyMaps,
        EDWCEditorAsyncRequestPolicy::Singleton,
        [Completion = MoveTemp(Completion)](const FDWCEditorBuildOperationResult& BuildResult) mutable
        {
            if (!Completion) return;
            FDWCEditorBakeBatchResult Result;
            Result.bSucceeded = BuildResult.IsSuccessful();
            Result.bHadWarnings = BuildResult.HasWarnings();
            Result.bCanceled = BuildResult.WasCanceled();
            Result.Summary = BuildResult.Summary;
            Completion(Result);
        },
        OutError);
    if (!Operation.IsValid())
    {
        return false;
    }
    Batch->Operation = Operation;
    Batch->bSaveAfterCommit = bSaveAfterCommit;
    Batch->bAffectedStage4Only = true;
    Batch->bSubmissionComplete = false;
    TArray<FGuid> AffectedLayerGuids;
    for (const FDWCTransparencyAffectedRebakeCandidate& Candidate : Candidates)
    {
        if (Candidate.IsEligible())
        {
            AffectedLayerGuids.Add(Candidate.LayerGuid);
        }
        else if (Candidate.Status != EDWCTransparencyAffectedRebakeStatus::AlreadyCurrent)
        {
            Batch->Warnings.Add(FString::Printf(
                TEXT("Slot %d: %s"),
                Candidate.MaterialSlotIndex,
                *Candidate.Detail));
        }
    }
    if (AffectedLayerGuids.IsEmpty())
    {
        if (OutError != nullptr)
        {
            *OutError = Candidates.IsEmpty()
                ? TEXT("No transparency layers match the requested material slots.")
                : TEXT("No Transparency Stage 4 outputs require an affected wrinkle-only rebake.");
        }
        Operation->DetachPresentationCallback();
        FDWCEditorBuildOperationResult FailedResult;
        FailedResult.Reason = EDWCEditorBuildTerminalReason::Failed;
        FailedResult.Summary = OutError != nullptr ? *OutError : TEXT("No affected transparency outputs require a rebake.");
        Operation->Complete(MoveTemp(FailedResult));
        return false;
    }
    Batch->AffectedSequence.Initialize(MoveTemp(AffectedLayerGuids));

    ActiveTransparencyBatch = Batch;
    Operation->SetPhase(EDWCEditorBuildOperationPhase::Preparing);
    PumpAffectedTransparencyStage4Jobs(Batch);
    return true;
}

bool FDWCEditorBakeCoordinator::PumpAffectedTransparencyStage4Jobs(
    const TSharedRef<FTransparencyBatch>& Batch)
{
    check(IsInGameThread());
    if (Batch->bFinalized || !Batch->Operation.IsValid() ||
        Batch->Operation->IsCancellationRequested() || ActiveTransparencyBatch != Batch)
    {
        return false;
    }

    UWetClothingAsset* TargetAsset = Asset.Get();
    if (TargetAsset == nullptr)
    {
        Batch->Failures.Add(TEXT("The transparency bake target became unavailable."));
        Batch->AffectedSequence.DiscardRemaining();
    }
    FGuid LayerGuid;
    while (TargetAsset != nullptr && Batch->AffectedSequence.TryBeginNext(LayerGuid))
    {
        TSharedRef<FDWCTransparencySourcePayload> SourcePayload =
            MakeShared<FDWCTransparencySourcePayload>();
        FString RestoreError;
        if (!FDWCTransparencyAffectedStage4Rebake::RestoreCanonicalSource(
                *TargetAsset, LayerGuid, *SourcePayload, RestoreError))
        {
            Batch->Failures.Add(RestoreError);
            Batch->AffectedSequence.CompleteActive();
            continue;
        }
        Batch->AffectedSequence.SetActivePayloadBytes(SourcePayload->GetAllocatedBytes());

        FString SubmitError;
        if (SubmitTransparencyJob(Batch, LayerGuid, SourcePayload, SubmitError, true))
        {
            // Only one restored full-resolution source is retained at a time.
            return true;
        }
        Batch->Failures.Add(FString::Printf(
            TEXT("Slot %d: %s"), SourcePayload->MaterialSlotIndex, *SubmitError));
        Batch->AffectedSequence.CompleteActive();
    }

    Batch->bSubmissionComplete = Batch->AffectedSequence.IsComplete();
    if (Batch->FinishedCount >= Batch->SubmittedCount)
    {
        FinalizeTransparencyBatch(Batch);
    }
    return false;
}

bool FDWCEditorBakeCoordinator::SubmitTransparencyJob(
    const TSharedRef<FTransparencyBatch>& Batch,
    const FGuid LayerGuid,
    TSharedRef<const FDWCTransparencySourcePayload> SourcePayload,
    FString& OutError,
    const bool bCountAsBatchJob,
    TSharedPtr<const FDWCTransparencyAlphaWorkingSnapshot> AlphaSnapshot,
    TSharedPtr<const FDWCTransparencyFinalSettingsSnapshot> FinalSettingsOverride)
{
    UWetClothingAsset* TargetAsset = Asset.Get();
    if (TargetAsset == nullptr || !Scheduler.IsValid())
    {
        OutError = TEXT("The transparency bake target is unavailable.");
        return false;
    }
    const FWetClothingTransparencyLayerData* Layer =
        TargetAsset->Authored.TransparencyData.TransparencyLayers.FindByPredicate(
            [&LayerGuid](const FWetClothingTransparencyLayerData& Candidate)
            {
                return Candidate.LayerGuid == LayerGuid;
            });
    if (Layer == nullptr)
    {
        OutError = TEXT("The transparency layer is unavailable.");
        return false;
    }

    FDWCEditorWorkerJobDescriptor Descriptor;
    Descriptor.Key.Kind = EDWCEditorWorkerJobKind::TransparencyFinalBake;
    Descriptor.Key.MaterialSlotIndex = Layer->TargetSurface.OuterMaterialSlotIndex;
    Descriptor.Key.LayerGuid = LayerGuid;
    Descriptor.Domain = EDWCEditorAuthoringDomain::Transparency;
    Descriptor.DomainRevision = Scheduler->GetCurrentDomainRevision(Descriptor.Domain);
    Descriptor.Priority = EDWCEditorWorkerJobPriority::UserInitiated;
    Descriptor.RequestPolicy = EDWCEditorAsyncRequestPolicy::FIFO;
    Descriptor.MemoryEstimate.ResidentSharedBytes = SourcePayload->GetAllocatedBytes() +
        (AlphaSnapshot.IsValid() ? AlphaSnapshot->GetAllocatedBytes() : 0);
    Descriptor.DebugName = FString::Printf(
        TEXT("Transparency bake slot %d"),
        Descriptor.Key.MaterialSlotIndex);

    TWeakPtr<FDWCEditorBakeCoordinator> WeakThis = AsShared();
    const int32 MaterialSlotIndex = Descriptor.Key.MaterialSlotIndex;
    const FDWCEditorWorkerJobTicket Ticket = Scheduler->SubmitPrepared(
        Descriptor,
        [WeakThis, LayerGuid, SourcePayload, AlphaSnapshot, FinalSettingsOverride](
            const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& Token,
            FDWCEditorWorkerJobScheduler::FPreparedWorkerJob& OutPrepared,
            FString& PrepareError)
        {
            check(IsInGameThread());
            const TSharedPtr<FDWCEditorBakeCoordinator> Self = WeakThis.Pin();
            UWetClothingAsset* CurrentAsset = Self.IsValid() ? Self->Asset.Get() : nullptr;
            if (CurrentAsset == nullptr || Token->IsCanceled())
            {
                PrepareError = TEXT("The transparency bake request became unavailable before snapshot preparation.");
                return false;
            }
            const FWetClothingTransparencyLayerData* CurrentLayer =
                CurrentAsset->Authored.TransparencyData.TransparencyLayers.FindByPredicate(
                    [&LayerGuid](const FWetClothingTransparencyLayerData& Candidate)
                    {
                        return Candidate.LayerGuid == LayerGuid;
                    });
            if (CurrentLayer == nullptr)
            {
                PrepareError = TEXT("The transparency layer was removed before snapshot preparation.");
                return false;
            }
            TSharedRef<FDWCTransparencyEditedMapBakeSnapshot, ESPMode::ThreadSafe> Snapshot =
                MakeShared<FDWCTransparencyEditedMapBakeSnapshot, ESPMode::ThreadSafe>();
            if (FinalSettingsOverride.IsValid() && !AlphaSnapshot.IsValid())
            {
                PrepareError = TEXT("The final transparency settings snapshot requires an alpha working snapshot.");
                return false;
            }
            const bool bSnapshotBuilt = FinalSettingsOverride.IsValid()
                ? FDWCTransparencyEditedMapBaker::BuildSnapshot(
                    *CurrentAsset,
                    *CurrentLayer,
                    SourcePayload,
                    *AlphaSnapshot,
                    Self->CoverageService,
                    *FinalSettingsOverride,
                    *Snapshot,
                    PrepareError)
                : AlphaSnapshot.IsValid()
                ? FDWCTransparencyEditedMapBaker::BuildSnapshot(
                    *CurrentAsset,
                    *CurrentLayer,
                    SourcePayload,
                    *AlphaSnapshot,
                    Self->CoverageService,
                    *Snapshot,
                    PrepareError)
                : FDWCTransparencyEditedMapBaker::BuildSnapshot(
                    *CurrentAsset,
                    *CurrentLayer,
                    SourcePayload,
                    Self->CoverageService,
                    *Snapshot,
                    PrepareError);
            if (!bSnapshotBuilt)
            {
                return false;
            }
            OutPrepared.ActualMemoryEstimate.ResidentSharedBytes = SourcePayload->GetAllocatedBytes();
            OutPrepared.ActualMemoryEstimate.SnapshotBytes = Snapshot->GetEstimatedPrivateBytes();
            OutPrepared.ActualMemoryEstimate.OutputBytes = Snapshot->GetEstimatedOutputBytes();
            OutPrepared.ActualMemoryEstimate.ScratchBytes = Snapshot->GetEstimatedScratchBytes();
            OutPrepared.Work = [Snapshot](
                const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& WorkToken)
            {
                TSharedPtr<FTransparencyBakeWorkerResult, ESPMode::ThreadSafe> Result =
                    MakeShared<FTransparencyBakeWorkerResult, ESPMode::ThreadSafe>();
                Result->Snapshot = Snapshot;
                Result->Computed = FDWCTransparencyEditedMapBaker::ComputeSnapshot(*Snapshot, &WorkToken.Get());
                Result->bSucceeded = Result->Computed.bSucceeded;
                Result->Error = Result->Computed.Error;
                Result->ResultBytes = Result->Computed.ResultBytes;
                return StaticCastSharedPtr<FDWCEditorWorkerJobResult>(Result);
            };
            return true;
        },
        [WeakThis, Batch, SourcePayload, LayerGuid, MaterialSlotIndex](
            const FDWCEditorWorkerJobTicket& FinishedTicket,
            TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe> BaseResult)
        {
            const TSharedPtr<FDWCEditorBakeCoordinator> Self = WeakThis.Pin();
            const TSharedPtr<FTransparencyBakeWorkerResult, ESPMode::ThreadSafe> Result =
                StaticCastSharedPtr<FTransparencyBakeWorkerResult>(BaseResult);
            UWetClothingAsset* CurrentAsset = Self.IsValid() ? Self->Asset.Get() : nullptr;
            if (!Self.IsValid() || CurrentAsset == nullptr || !Result.IsValid() || !Result->Snapshot.IsValid())
            {
                return;
            }
            FWetClothingTransparencyLayerData* CurrentLayer =
                CurrentAsset->Authored.TransparencyData.TransparencyLayers.FindByPredicate(
                    [&LayerGuid](const FWetClothingTransparencyLayerData& Candidate)
                    {
                        return Candidate.LayerGuid == LayerGuid;
                    });
            if (CurrentLayer == nullptr)
            {
                Batch->Failures.Add(FString::Printf(TEXT("Slot %d: the target layer was removed."), MaterialSlotIndex));
                return;
            }
            FDWCTransparencyEditedMapBakeResult CommitResult;
            FString CommitError;
            if (!FDWCTransparencyEditedMapBaker::CommitComputedResult(
                    *CurrentAsset,
                    *Result->Snapshot,
                    MoveTemp(Result->Computed),
                    CommitResult,
                    CommitError))
            {
                Batch->Failures.Add(FString::Printf(TEXT("Slot %d: %s"), MaterialSlotIndex, *CommitError));
                return;
            }

            CurrentAsset->Modify();
            CurrentLayer->AutoBakeMetadata.AutoBakeGuid = FGuid::NewGuid();
            CurrentLayer->AutoBakeMetadata.BuildSignature = SourcePayload->BuildSignature;
            CurrentLayer->AutoBakeMetadata.Resolution = SourcePayload->Resolution.X;
            CurrentLayer->AutoBakeMetadata.PaddingPixels =
                CurrentAsset->Authored.TransparencyData.TransparencyPaddingPixels;
            CurrentLayer->AutoBakeMetadata.ValidHitCount = SourcePayload->ValidHitCount;
            CurrentLayer->AutoBakeMetadata.NoHitCount = SourcePayload->NoHitCount;
            FString CurrentnessReason;
            if (!FDWCTransparencyEditedMapBaker::IsLayerBakeCurrent(
                    *CurrentAsset,
                    *CurrentLayer,
                    &CurrentnessReason))
            {
                Batch->Failures.Add(FString::Printf(
                    TEXT("Slot %d: the committed Transparency Map is still out of date. %s"),
                    MaterialSlotIndex,
                    *CurrentnessReason));
            }
            ++Batch->BakedMapCount;
            Batch->AppliedStrokeCount += CommitResult.AppliedStrokeCount;
            Batch->AppliedSampleCount += CommitResult.AppliedSampleCount;
            Batch->TextureNames.Add(GetPathNameSafe(CommitResult.TransparencyMap));
            if (CommitResult.RevealSurfaceMap != nullptr)
            {
                Batch->TextureNames.Add(GetPathNameSafe(CommitResult.RevealSurfaceMap));
                ++Batch->BakedRevealSurfaceCount;
            }
            if (CommitResult.IgnoredNoHitOverridePixelCount > 0)
            {
                Batch->Warnings.Add(FString::Printf(
                    TEXT("Slot %d: %d edited pixel(s) had no valid inner-surface color."),
                    MaterialSlotIndex,
                    CommitResult.IgnoredNoHitOverridePixelCount));
            }
            if (!CommitResult.WarningMessage.IsEmpty())
            {
                Batch->Warnings.Add(FString::Printf(TEXT("Slot %d: %s"), MaterialSlotIndex, *CommitResult.WarningMessage));
            }
        },
        &OutError,
        [WeakThis, Batch, MaterialSlotIndex](
            const FDWCEditorWorkerJobTicket& FinishedTicket,
            const EDWCEditorWorkerJobCompletion JobCompletion,
            const FString& WorkerError)
        {
            if (const TSharedPtr<FDWCEditorBakeCoordinator> Self = WeakThis.Pin())
            {
                Self->HandleTransparencyJobFinished(
                    Batch,
                    FinishedTicket,
                    MaterialSlotIndex,
                    static_cast<uint8>(JobCompletion),
                    WorkerError);
            }
        });
    if (!Ticket.IsValid())
    {
        return false;
    }
    if (Batch->Operation.IsValid())
    {
        Batch->Operation->RegisterTicket(Ticket);
        Batch->Operation->SetPhase(EDWCEditorBuildOperationPhase::Running);
    }
    if (bCountAsBatchJob)
    {
        ++Batch->SubmittedCount;
    }
    return true;
}

void FDWCEditorBakeCoordinator::HandleTransparencyJobFinished(
    const TSharedRef<FTransparencyBatch>& Batch,
    const FDWCEditorWorkerJobTicket& Ticket,
    const int32 MaterialSlotIndex,
    const uint8 CompletionCode,
    const FString& WorkerError)
{
    check(IsInGameThread());
    const EDWCEditorWorkerJobCompletion Completion =
        static_cast<EDWCEditorWorkerJobCompletion>(CompletionCode);
    if (Batch->Operation.IsValid())
    {
        Batch->Operation->NotifyTicketFinished(Ticket);
    }
    ++Batch->FinishedCount;
    if (Completion != EDWCEditorWorkerJobCompletion::Applied)
    {
        if (Batch->Operation.IsValid() &&
            (Completion == EDWCEditorWorkerJobCompletion::Canceled ||
             Completion == EDWCEditorWorkerJobCompletion::Superseded ||
             Completion == EDWCEditorWorkerJobCompletion::Stale))
        {
            const EDWCEditorBuildTerminalReason Reason =
                Completion == EDWCEditorWorkerJobCompletion::Superseded
                    ? EDWCEditorBuildTerminalReason::Superseded
                    : Completion == EDWCEditorWorkerJobCompletion::Stale
                        ? EDWCEditorBuildTerminalReason::Stale
                        : EDWCEditorBuildTerminalReason::Canceled;
            Batch->Operation->RequestCancellation(Reason);
        }
        Batch->bCanceled = Batch->Operation.IsValid() && Batch->Operation->IsCancellationRequested();
        const FString FailureReason = WorkerError.IsEmpty()
            ? DescribeCompletion(Completion)
            : WorkerError;
        Batch->Failures.Add(FString::Printf(
            TEXT("Slot %d: %s"),
            MaterialSlotIndex,
            *FailureReason));
    }
    if (Batch->bAffectedStage4Only)
    {
        Batch->AffectedSequence.CompleteActive();
        if (!Batch->bCanceled && Batch->AffectedSequence.HasPending())
        {
            PumpAffectedTransparencyStage4Jobs(Batch);
        }
        else
        {
            if (Batch->bCanceled)
            {
                Batch->AffectedSequence.DiscardRemaining();
            }
            Batch->bSubmissionComplete = Batch->AffectedSequence.IsComplete();
        }
    }
    if (Batch->bSubmissionComplete && Batch->FinishedCount >= Batch->SubmittedCount)
    {
        FinalizeTransparencyBatch(Batch);
    }
}

void FDWCEditorBakeCoordinator::FinalizeTransparencyBatch(
    const TSharedRef<FTransparencyBatch>& Batch)
{
    check(IsInGameThread());
    if (Batch->bFinalized)
    {
        return;
    }
    Batch->bFinalized = true;
    if (Batch->Operation.IsValid() &&
        Batch->Operation->GetCancellationReason() == EDWCEditorBuildTerminalReason::OwnerShutdown)
    {
        if (ActiveTransparencyBatch == Batch)
        {
            ActiveTransparencyBatch.Reset();
        }
        FDWCEditorBuildOperationResult ShutdownResult;
        ShutdownResult.Reason = EDWCEditorBuildTerminalReason::OwnerShutdown;
        ShutdownResult.Summary = TEXT("The transparency bake was retired because the editor closed.");
        Batch->Operation->Complete(MoveTemp(ShutdownResult));
        return;
    }
    if (ActiveTransparencyBatch != Batch)
    {
        if (Batch->Operation.IsValid())
        {
            FDWCEditorBuildOperationResult SupersededResult;
            SupersededResult.Reason = Batch->Operation->GetCancellationReason();
            if (SupersededResult.Reason == EDWCEditorBuildTerminalReason::None)
            {
                SupersededResult.Reason = EDWCEditorBuildTerminalReason::Superseded;
            }
            SupersededResult.Summary = TEXT("The transparency bake was superseded by a newer request.");
            Batch->Operation->Complete(MoveTemp(SupersededResult));
        }
        return;
    }
    UWetClothingAsset* TargetAsset = Asset.Get();
    bool bSaved = true;
    if (TargetAsset != nullptr)
    {
        if (Batch->BakedMapCount > 0)
        {
            TargetAsset->SetTransparencyBakeStatus(
                Batch->Failures.IsEmpty() ? EDWCBakeStatus::Valid : EDWCBakeStatus::OutOfDate);
            TargetAsset->MarkPackageDirty();
        }
        else if (!Batch->Failures.IsEmpty())
        {
            TargetAsset->SetTransparencyBakeStatus(
                EDWCBakeStatus::Failed,
                FString::Join(Batch->Failures, TEXT("\n")));
        }
        if (Batch->BakedMapCount > 0 && Batch->bSaveAfterCommit)
        {
            bSaved = DWCEditorUtils::SaveAsset(TargetAsset);
        }
    }

    FDWCEditorBakeBatchResult Result;
    Result.bSucceeded = Batch->BakedMapCount > 0 && Batch->Failures.IsEmpty() && bSaved;
    Result.bHadWarnings = !Batch->Warnings.IsEmpty() || !Batch->Failures.IsEmpty() || !bSaved;
    Result.bCanceled = Batch->bCanceled;
    Result.Summary = FString::Printf(
        TEXT("Baked %d transparency map(s), including %d Reveal Surface map(s). Applied strokes: %d, samples: %d."),
        Batch->BakedMapCount,
        Batch->BakedRevealSurfaceCount,
        Batch->AppliedStrokeCount,
        Batch->AppliedSampleCount);
    if (!Batch->TextureNames.IsEmpty())
    {
        Result.Summary += FString::Printf(TEXT("\n\nTextures:\n- %s"), *FString::Join(Batch->TextureNames, TEXT("\n- ")));
    }
    if (!Batch->Warnings.IsEmpty())
    {
        Result.Summary += FString::Printf(TEXT("\n\nWarnings:\n- %s"), *FString::Join(Batch->Warnings, TEXT("\n- ")));
    }
    if (!Batch->Failures.IsEmpty())
    {
        Result.Summary += FString::Printf(TEXT("\n\nSkipped or failed:\n- %s"), *FString::Join(Batch->Failures, TEXT("\n- ")));
    }
    if (!bSaved)
    {
        Result.Summary += TEXT("\n\nThe generated assets could not be saved.");
    }
    if (ActiveTransparencyBatch == Batch)
    {
        ActiveTransparencyBatch.Reset();
    }
    if (Batch->Operation.IsValid())
    {
        FDWCEditorBuildOperationResult BuildResult;
        BuildResult.Reason = Result.bSucceeded
            ? (Result.bHadWarnings
                ? EDWCEditorBuildTerminalReason::SucceededWithWarnings
                : EDWCEditorBuildTerminalReason::Succeeded)
            : Result.bCanceled
                ? EDWCEditorBuildTerminalReason::Canceled
                : EDWCEditorBuildTerminalReason::Failed;
        BuildResult.Summary = MoveTemp(Result.Summary);
        Batch->Operation->Complete(MoveTemp(BuildResult));
    }
}

void FDWCEditorBakeCoordinator::CancelAll()
{
    check(IsInGameThread());
    const TSharedPtr<FWrinkleBatch> WrinkleBatch = ActiveWrinkleBatch;
    const TSharedPtr<FTransparencyBatch> TransparencyBatch = ActiveTransparencyBatch;
    const EDWCEditorBuildTerminalReason Reason = bShuttingDown
        ? EDWCEditorBuildTerminalReason::OwnerShutdown
        : EDWCEditorBuildTerminalReason::Canceled;
    if (WrinkleBatch.IsValid() && WrinkleBatch->Operation.IsValid())
    {
        WrinkleBatch->bCanceled = true;
        WrinkleBatch->PendingMaterialSlotIndices.Reset();
        if (bShuttingDown)
        {
            WrinkleBatch->Operation->DetachPresentationCallback();
        }
        if (OperationManager.IsValid())
        {
            OperationManager->CancelOperation(WrinkleBatch->Operation.ToSharedRef(), Reason);
        }
        if (WrinkleBatch->InFlightJobs == 0)
        {
            FinalizeWrinkleBatch(WrinkleBatch.ToSharedRef());
        }
    }
    if (TransparencyBatch.IsValid() && TransparencyBatch->Operation.IsValid())
    {
        TransparencyBatch->bCanceled = true;
        TransparencyBatch->AffectedSequence.DiscardRemaining();
        TransparencyBatch->bSubmissionComplete = true;
        if (bShuttingDown)
        {
            TransparencyBatch->Operation->DetachPresentationCallback();
        }
        if (OperationManager.IsValid())
        {
            OperationManager->CancelOperation(TransparencyBatch->Operation.ToSharedRef(), Reason);
        }
        if (TransparencyBatch->FinishedCount >= TransparencyBatch->SubmittedCount)
        {
            FinalizeTransparencyBatch(TransparencyBatch.ToSharedRef());
        }
    }
}

void FDWCEditorBakeCoordinator::Shutdown()
{
    check(IsInGameThread());
    if (bShuttingDown)
    {
        return;
    }
    bShuttingDown = true;
    CancelAll();
}

bool FDWCEditorBakeCoordinator::IsWrinkleBakeActive() const
{
    return OperationManager.IsValid() &&
        OperationManager->IsActionActive(EDWCEditorBuildAction::BakeWrinkleTextures);
}

bool FDWCEditorBakeCoordinator::IsTransparencyBakeActive() const
{
    return OperationManager.IsValid() &&
        (OperationManager->IsActionActive(EDWCEditorBuildAction::BakeTransparencyTextures) ||
         OperationManager->IsActionActive(EDWCEditorBuildAction::RebakeAffectedTransparencyMaps));
}

EDWCEditorTransparencyBakeKind FDWCEditorBakeCoordinator::GetActiveTransparencyBakeKind() const
{
    if (!IsTransparencyBakeActive())
    {
        return EDWCEditorTransparencyBakeKind::None;
    }
    return ActiveTransparencyBatch->bAffectedStage4Only
        ? EDWCEditorTransparencyBakeKind::AffectedStage4
        : EDWCEditorTransparencyBakeKind::Full;
}
