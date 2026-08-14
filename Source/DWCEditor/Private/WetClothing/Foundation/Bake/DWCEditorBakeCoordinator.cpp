//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Foundation/Bake/DWCEditorBakeCoordinator.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyResolutionResolver.h"

#include "Algo/Unique.h"
#include "Core/DWCEditorUtils.h"
#include "DataAssets/WetClothingAsset.h"
#include "WetClothing/DerivedAssets/Textures/Wrinkle/WetWrinkleBakeService.h"
#include "WetClothing/DerivedAssets/Textures/Wrinkle/WetWrinkleNormalMapBaker.h"
#include "WetClothing/Foundation/Build/DWCEditorBuildOperationManager.h"
#include "WetClothing/Foundation/Build/DWCTransparencyBuildTargetResolver.h"
#include "WetClothing/Foundation/Cache/DWCEditorCacheStore.h"
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
        TSharedPtr<FWetWrinkleNormalMapBakeSnapshot, ESPMode::ThreadSafe> Snapshot;
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

    bool ValidateTransparencyBuildTarget(
        const UWetClothingAsset& Asset,
        const FWetClothingTransparencyLayerData& Layer,
        FString& OutError)
    {
        OutError.Reset();
        const int32 Slot = Layer.TargetSurface.OuterMaterialSlotIndex;
        if (!Layer.IsRuntimeEnabled())
        {
            OutError = TEXT("The Transparency Target Part is no longer enabled.");
            return false;
        }
        if (Slot == INDEX_NONE || !Asset.IsMaterialSlotWettable(Slot))
        {
            OutError = TEXT("The Transparency Target Part no longer targets a Wettable material slot.");
            return false;
        }
        TArray<FString> Errors;
        if (!FWetClothingTransparencyDataHelpers::ValidateTransparencyLayer(
                Asset.GetDWCSkeletalMesh(),
                Layer,
                Errors,
                Asset.GetDWCDataUVChannelIndex()))
        {
            OutError = FString::Join(Errors, TEXT("\n"));
            return false;
        }
        return true;
    }

    uint64 EstimateAuthoredAlphaSnapshotBytes(
        const FWetClothingTransparencyLayerData& Layer)
    {
        const TArray<FDWCTransparencyBrushStroke>& Strokes = Layer.GetEditableStrokes();
        uint64 Bytes = static_cast<uint64>(Strokes.Num()) *
                       sizeof(FDWCTransparencyBrushStroke);
        for (const FDWCTransparencyBrushStroke& Stroke : Strokes)
        {
            Bytes += static_cast<uint64>(Stroke.DisplayName.Len() + 1) * sizeof(TCHAR);
            Bytes += Stroke.GetSampleAllocatedSize();
        }
        return Bytes;
    }

    uint64 EstimateRevealColorAuthoringBytes(
        const FWetClothingTransparencyLayerData& Layer)
    {
        const TArray<FDWCTransparencyRevealColorStroke>& Strokes = Layer.GetRevealColorPaintStrokes();
        uint64 Bytes = static_cast<uint64>(Strokes.Num()) *
                       sizeof(FDWCTransparencyRevealColorStroke);
        for (const FDWCTransparencyRevealColorStroke& Stroke : Strokes)
        {
            Bytes += Stroke.GetSampleAllocatedSize();
        }
        return Bytes;
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
    int32 BakedRevealNormalCount = 0;
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
    TSharedPtr<FDWCWrinkleSuppressionCoverageService> InCoverageService,
    TSharedPtr<FDWCEditorCacheStore> InCacheStore)
    : Asset(InAsset)
    , Scheduler(InScheduler)
    , OperationManager(MoveTemp(InOperationManager))
    , SpatialQueryService(MoveTemp(InSpatialQueryService))
    , SurfacePatchProjectionCache(MoveTemp(InSurfacePatchProjectionCache))
    , CoverageService(MoveTemp(InCoverageService))
    , CacheStore(MoveTemp(InCacheStore))
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
        SurfacePatchProjectionCache.ToSharedRef(),
        CacheStore);

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
        Descriptor.WorkClass = EDWCEditorWorkClass::UserBuild;
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
                const FWetWrinkleNormalMapBakeMemoryPlan MemoryPlan = Snapshot->GetMemoryPlan();
                OutPrepared.ActualMemoryEstimate.WorkingBytes =
                    MemoryPlan.RasterBytes + MemoryPlan.PostProcessBytes;
                OutPrepared.ActualMemoryEstimate.OutputBytes = MemoryPlan.OutputBytes;
                OutPrepared.Work = [Snapshot](
                    const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& WorkToken)
                {
                    TSharedPtr<FWrinkleBakeWorkerResult, ESPMode::ThreadSafe> Result =
                        MakeShared<FWrinkleBakeWorkerResult, ESPMode::ThreadSafe>();
                    Result->Snapshot = Snapshot;
                    Result->Computed = FWetWrinkleNormalMapBaker::ComputeSnapshot(*Snapshot, &WorkToken.Get());
                    Snapshot->ReleaseWorkerResources();
                    Result->bSucceeded = Result->Computed.bSucceeded;
                    Result->Error = Result->Computed.Error;
                    Result->ResultBytes = Result->Computed.ResultBytes;
                    Result->CommitMemoryEstimate.SnapshotBytes =
                        Snapshot->GetEstimatedCommitBytes();
                    Result->CommitMemoryEstimate.OutputBytes = Result->Computed.ResultBytes;
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
        FString TargetError;
        if (!ValidateTransparencyBuildTarget(*TargetAsset, *Layer, TargetError))
        {
            Batch->Failures.Add(FString::Printf(
                TEXT("Slot %d: %s"),
                Layer->TargetSurface.OuterMaterialSlotIndex,
                *TargetError));
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
                    GenerateWarnings,
                    CacheStore))
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
        Descriptor.WorkClass = EDWCEditorWorkClass::UserBuild;
        const int32 ResolvedResolution = FDWCTransparencyResolutionResolver::Resolve(
            *TargetAsset,
            *Layer).Size;
        const uint64 PixelCount = static_cast<uint64>(ResolvedResolution) *
            static_cast<uint64>(ResolvedResolution);
        // The streamed Stage 2 payload retains color, reveal surface, alpha,
        // coverage, island, hit, distance, and priority buffers. Material
        // surfaces are separately leased and never accumulate in this job.
        Descriptor.MemoryEstimate.WorkingBytes = PixelCount * 24ull;
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
                if (!ValidateTransparencyBuildTarget(
                        *CurrentAsset, *CurrentLayer, PrepareError))
                {
                    return false;
                }
                TSharedPtr<FTransparencyAutoBakeWorkerResult, ESPMode::ThreadSafe> PreparedResult =
                    MakeShared<FTransparencyAutoBakeWorkerResult, ESPMode::ThreadSafe>();
                FString GenerateSummary;
                FDWCTransparencyStage2ExecutionOptions GenerationOptions;
                GenerationOptions.CancellationToken = &Token.Get();
                GenerationOptions.CacheStore = Self->CacheStore;
                // The scheduler admission lease already covers this prepare
                // job's snapshot, working set, and output payload.
                GenerationOptions.bResourcesOwnedByCaller = true;
                if (!FDWCTransparencyAutoMapGenerator::GenerateSameMesh(
                        *CurrentAsset,
                        *CurrentLayer,
                        PreparedResult->Computed.SourcePayload,
                        GenerateSummary,
                        PreparedResult->Computed.Warnings,
                        GenerationOptions))
                {
                    PrepareError = MoveTemp(GenerateSummary);
                    return false;
                }
                PreparedResult->Computed.bSucceeded = true;
                PreparedResult->Computed.Summary = MoveTemp(GenerateSummary);
                PreparedResult->Computed.ResultBytes =
                    PreparedResult->Computed.SourcePayload.GetAllocatedBytes();
                PreparedResult->bSucceeded = true;
                PreparedResult->ResultBytes = PreparedResult->Computed.ResultBytes;
                OutPrepared.ActualMemoryEstimate.OutputBytes =
                    PreparedResult->Computed.ResultBytes;
                OutPrepared.Work = [PreparedResult](
                    const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& WorkToken)
                {
                    if (WorkToken->IsCanceled())
                    {
                        PreparedResult->bSucceeded = false;
                        PreparedResult->Error = TEXT("Transparency projection was canceled before commit.");
                    }
                    return StaticCastSharedPtr<FDWCEditorWorkerJobResult>(PreparedResult);
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
        FString SubmitError;
        if (SubmitTransparencyJob(
                Batch,
                LayerGuid,
                nullptr,
                SubmitError,
                true,
                nullptr,
                nullptr,
                true))
        {
            // The full-resolution source is restored only after scheduler
            // admission and only one affected layer is submitted at a time.
            return true;
        }
        Batch->Failures.Add(SubmitError);
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
    TSharedPtr<const FDWCTransparencySourcePayload> SourcePayload,
    FString& OutError,
    const bool bCountAsBatchJob,
    TSharedPtr<const FDWCTransparencyAlphaWorkingSnapshot> AlphaSnapshot,
    TSharedPtr<const FDWCTransparencyFinalSettingsSnapshot> FinalSettingsOverride,
    const bool bRestoreCanonicalSourceDuringPrepare)
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
    if (!ValidateTransparencyBuildTarget(*TargetAsset, *Layer, OutError))
    {
        return false;
    }

    TSharedPtr<const FDWCTransparencySourcePayload> PlannedSourceIdentity = SourcePayload;
    const bool bAlphaRequiresOuterIslandID = !AlphaSnapshot.IsValid() ||
        AlphaSnapshot->Mode == EDWCTransparencyAlphaSnapshotMode::StrokeReplay;
    uint64 PlannedSourceBytes = 0;
    if (bRestoreCanonicalSourceDuringPrepare)
    {
        TSharedRef<FDWCTransparencySourcePayload> Identity =
            MakeShared<FDWCTransparencySourcePayload>();
        if (!FDWCTransparencyAutoMapGenerator::BuildSignatureOnlyResult(
                *TargetAsset,
                *Layer,
                *Identity,
                OutError))
        {
            return false;
        }
        PlannedSourceIdentity = Identity;
        PlannedSourceBytes =
            FDWCTransparencyEditedMapBaker::EstimateStage4SourcePayloadBytes(
                Identity->Resolution,
                Layer->RequiresRevealSurface(),
                true);
    }
    else if (!SourcePayload.IsValid())
    {
        OutError = TEXT("The transparency bake source payload is unavailable.");
        return false;
    }
    else
    {
        PlannedSourceBytes =
            FDWCTransparencyEditedMapBaker::EstimateStage4SourcePayloadBytes(
                SourcePayload->Resolution,
                Layer->RequiresRevealSurface(),
                true);
    }

    const uint64 AlphaInputBytes = AlphaSnapshot.IsValid()
        ? AlphaSnapshot->GetAllocatedBytes()
        : EstimateAuthoredAlphaSnapshotBytes(*Layer);
    const uint64 AuthoringInputBytes = AlphaInputBytes +
        EstimateRevealColorAuthoringBytes(*Layer);
    FDWCTransparencyStage4MemoryPlan MemoryPlan;
    if (!FDWCTransparencyEditedMapBaker::BuildMemoryPlan(
            PlannedSourceIdentity->Resolution,
            PlannedSourceBytes,
        AuthoringInputBytes,
        bRestoreCanonicalSourceDuringPrepare,
        Layer->RequiresRuntimeRevealNormal(),
        bAlphaRequiresOuterIslandID,
        MemoryPlan,
            OutError))
    {
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
    Descriptor.WorkClass = EDWCEditorWorkClass::UserBuild;
    Descriptor.MemoryEstimate.WorkingBytes = FMath::Max(
        MemoryPlan.GetPreparePeakBytes(),
        MemoryPlan.GetWorkerPeakBytes());
    Descriptor.DebugName = FString::Printf(
        TEXT("Transparency bake slot %d"),
        Descriptor.Key.MaterialSlotIndex);

    TWeakPtr<FDWCEditorBakeCoordinator> WeakThis = AsShared();
    const int32 MaterialSlotIndex = Descriptor.Key.MaterialSlotIndex;
    const FDWCEditorWorkerJobTicket Ticket = Scheduler->SubmitPrepared(
        Descriptor,
        [WeakThis,
         Batch,
         LayerGuid,
         SourcePayload,
         PlannedSourceIdentity,
         AlphaSnapshot,
         FinalSettingsOverride,
         bRestoreCanonicalSourceDuringPrepare](
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
            if (!ValidateTransparencyBuildTarget(
                    *CurrentAsset, *CurrentLayer, PrepareError))
            {
                return false;
            }
            TSharedPtr<const FDWCTransparencySourcePayload> PreparedSourcePayload = SourcePayload;
            if (bRestoreCanonicalSourceDuringPrepare)
            {
                FDWCTransparencySourcePayload CurrentIdentity;
                if (!FDWCTransparencyAutoMapGenerator::BuildSignatureOnlyResult(
                        *CurrentAsset,
                        *CurrentLayer,
                        CurrentIdentity,
                        PrepareError))
                {
                    return false;
                }
                if (!PlannedSourceIdentity.IsValid() ||
                    CurrentIdentity.BuildSignature != PlannedSourceIdentity->BuildSignature ||
                    CurrentIdentity.Resolution != PlannedSourceIdentity->Resolution ||
                    CurrentIdentity.OutputResolutionIdentity !=
                        PlannedSourceIdentity->OutputResolutionIdentity)
                {
                    PrepareError =
                        TEXT("The canonical Stage 2 identity changed while the affected Stage 4 rebake was waiting for admission.");
                    return false;
                }

                TSharedRef<FDWCTransparencySourcePayload> RestoredSource =
                    MakeShared<FDWCTransparencySourcePayload>();
                if (!FDWCTransparencyAffectedStage4Rebake::RestoreCanonicalArtifacts(
                        *CurrentLayer,
                        CurrentIdentity,
                        *RestoredSource,
                        PrepareError,
                        true))
                {
                    return false;
                }
                PreparedSourcePayload = RestoredSource;
                Batch->AffectedSequence.SetActivePayloadBytes(
                    RestoredSource->GetAllocatedBytes());
            }
            if (!PreparedSourcePayload.IsValid())
            {
                PrepareError = TEXT("The transparency source payload was unavailable during snapshot preparation.");
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
                     PreparedSourcePayload.ToSharedRef(),
                    *AlphaSnapshot,
                    Self->CoverageService,
                    *FinalSettingsOverride,
                    *Snapshot,
                    PrepareError)
                : AlphaSnapshot.IsValid()
                ? FDWCTransparencyEditedMapBaker::BuildSnapshot(
                     *CurrentAsset,
                     *CurrentLayer,
                     PreparedSourcePayload.ToSharedRef(),
                    *AlphaSnapshot,
                    Self->CoverageService,
                    *Snapshot,
                    PrepareError)
                : FDWCTransparencyEditedMapBaker::BuildSnapshot(
                     *CurrentAsset,
                     *CurrentLayer,
                     PreparedSourcePayload.ToSharedRef(),
                    Self->CoverageService,
                    *Snapshot,
                    PrepareError);
            if (!bSnapshotBuilt)
            {
                return false;
            }
            FDWCTransparencyStage4MemoryPlan ActualPlan;
            // BuildSnapshot retains a compact Stage 4 source and accounts it
            // as snapshot-private memory; the producer's full source is not
            // owned by the worker job.
            ActualPlan.ResidentSharedBytes = 0;
            ActualPlan.SnapshotBytes = Snapshot->GetEstimatedPrivateBytes();
            ActualPlan.OutputBytes = Snapshot->GetEstimatedOutputBytes();
            ActualPlan.ScratchBytes = Snapshot->GetEstimatedScratchBytes();
            ActualPlan.TransferableSnapshotBytes = Snapshot->GetEstimatedTransferableBytes();
            OutPrepared.ActualMemoryEstimate.WorkingBytes = ActualPlan.GetWorkerPeakBytes();
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
                Result->CommitMemoryEstimate.SnapshotBytes =
                    Snapshot->GetEstimatedPrivateBytes();
                Result->CommitMemoryEstimate.OutputBytes = Result->Computed.ResultBytes;
                if (Result->Computed.bRebuiltCorrectedRevealCheckpoint)
                {
                    const FIntPoint Resolution = Snapshot->GetSourceResolution();
                    Result->CommitMemoryEstimate.ScratchBytes =
                        static_cast<uint64>(Resolution.X) *
                        static_cast<uint64>(Resolution.Y) * sizeof(FColor);
                }
                return StaticCastSharedPtr<FDWCEditorWorkerJobResult>(Result);
            };
            return true;
        },
        [WeakThis, Batch, LayerGuid, MaterialSlotIndex](
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
            FString TargetError;
            if (!ValidateTransparencyBuildTarget(
                    *CurrentAsset, *CurrentLayer, TargetError))
            {
                Batch->Failures.Add(FString::Printf(
                    TEXT("Slot %d: %s"), MaterialSlotIndex, *TargetError));
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
            CurrentLayer->AutoBakeMetadata.BuildSignature =
                Result->Snapshot->GetSourceBuildSignature();
            CurrentLayer->AutoBakeMetadata.Resolution =
                Result->Snapshot->GetSourceResolution().X;
            CurrentLayer->AutoBakeMetadata.PaddingPixels =
                CurrentAsset->Authored.TransparencyData.TransparencyPaddingPixels;
            CurrentLayer->AutoBakeMetadata.ValidHitCount =
                Result->Snapshot->GetSourceValidHitCount();
            CurrentLayer->AutoBakeMetadata.NoHitCount =
                Result->Snapshot->GetSourceNoHitCount();
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
            if (CommitResult.RevealNormalMap != nullptr)
            {
                Batch->TextureNames.Add(GetPathNameSafe(CommitResult.RevealNormalMap));
                ++Batch->BakedRevealNormalCount;
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
            const FDWCTransparencyBuildTargetSnapshot RemainingTargets =
                FDWCTransparencyBuildTargetResolver::Resolve(
                    *TargetAsset, EDWCEditorValidationAccess::ExactPayload);
            const bool bHasRemainingRequiredOutput =
                RemainingTargets.FullBakeState == EDWCEditorBuildActionState::Required ||
                RemainingTargets.FullBakeState == EDWCEditorBuildActionState::Blocked ||
                RemainingTargets.AffectedStage4State == EDWCEditorBuildActionState::Required;
            TargetAsset->SetTransparencyBakeStatus(
                Batch->Failures.IsEmpty() && !bHasRemainingRequiredOutput
                    ? EDWCBakeStatus::Valid
                    : EDWCBakeStatus::OutOfDate);
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
        TEXT("Baked %d transparency map(s), including %d Reveal Normal map(s). Applied strokes: %d, samples: %d."),
        Batch->BakedMapCount,
        Batch->BakedRevealNormalCount,
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
