// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "WetClothing/Foundation/Bake/DWCEditorBakeCoordinator.h"

#include "Algo/Unique.h"
#include "Core/DWCEditorUtils.h"
#include "DataAssets/WetClothingAsset.h"
#include "WetClothing/DerivedAssets/Textures/Wrinkle/WetWrinkleBakeService.h"
#include "WetClothing/DerivedAssets/Textures/Wrinkle/WetWrinkleNormalMapBaker.h"
#include "WetClothing/Foundation/Bake/DWCEditorBakeMemoryBudget.h"
#include "WetClothing/DerivedAssets/Textures/Transparency/DWCTransparencyEditedMapBaker.h"
#include "WetClothing/Foundation/Jobs/DWCEditorCancellationToken.h"
#include "WetClothing/Foundation/Jobs/DWCEditorWorkerJobScheduler.h"
#include "WetClothing/Modes/Transparency/AutoMap/DWCTransparencyAutoMapGenerator.h"

namespace
{
    DEFINE_LOG_CATEGORY_STATIC(LogDWCWrinkleBakeCoordinator, Log, All);
    constexpr int32  DefaultWrinkleBakeMaxInFlightJobs = 2;
    constexpr uint64 DefaultWrinkleBakeMaxInFlightBytes = 512ull * 1024ull * 1024ull;

    TAutoConsoleVariable<int32> CVarDWCWrinkleBakeMaxInFlightJobs(
        TEXT("DWC.WrinkleEditor.Bake.MaxInFlightJobs"),
        DefaultWrinkleBakeMaxInFlightJobs,
        TEXT("Maximum number of wrinkle bake snapshots/jobs owned by one bake request. "
             "The global worker scheduler still enforces its own active-job limit."),
        ECVF_Default);

    TAutoConsoleVariable<int32> CVarDWCWrinkleBakeMaxInFlightMB(
        TEXT("DWC.WrinkleEditor.Bake.MaxInFlightMB"),
        static_cast<int32>(DefaultWrinkleBakeMaxInFlightBytes / (1024ull * 1024ull)),
        TEXT("Maximum memory, in MiB, retained by one wrinkle bake request for submitted snapshots. "
             "This limits queued snapshot memory in addition to the worker scheduler's active-job budget."),
        ECVF_Default);

    int32 ResolveWrinkleBakeMaxInFlightJobs()
    {
        return FMath::Clamp(
            CVarDWCWrinkleBakeMaxInFlightJobs.GetValueOnGameThread(),
            1,
            DefaultWrinkleBakeMaxInFlightJobs);
    }

    uint64 ResolveWrinkleBakeMaxInFlightBytes()
    {
        constexpr uint64 BytesPerMiB = 1024ull * 1024ull;
        return static_cast<uint64>(FMath::Max(
                   CVarDWCWrinkleBakeMaxInFlightMB.GetValueOnGameThread(),
                   1)) *
               BytesPerMiB;
    }

    struct FWrinkleBakeWorkerResult final : FDWCEditorWorkerJobResult
    {
        FWetWrinkleNormalMapComputedResult Computed;
    };

    struct FTransparencyBakeWorkerResult final : FDWCEditorWorkerJobResult
    {
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
        case EDWCEditorWorkerJobCompletion::Canceled:
            return TEXT("canceled");
        case EDWCEditorWorkerJobCompletion::Superseded:
            return TEXT("superseded by a newer request");
        case EDWCEditorWorkerJobCompletion::Stale:
            return TEXT("discarded because the authored data changed");
        case EDWCEditorWorkerJobCompletion::Failed:
            return TEXT("worker calculation failed");
        default:
            return TEXT("completed");
        }
    }
} // namespace

struct FDWCEditorBakeCoordinator::FWrinkleBatch
{
    uint64                                      BatchId = 0;
    int32                                       SubmittedCount = 0;
    int32                                       FinishedCount = 0;
    int32                                       BakedMapCount = 0;
    int32                                       BakedPatchCount = 0;
    int32                                       BakedStrokeCount = 0;
    bool                                        bSaveAfterCommit = false;
    bool                                        bCanceled = false;
    bool                                        bFinalized = false;
    TArray<FString>                             NormalTextureNames;
    TArray<FString>                             MaskTextureNames;
    TArray<FString>                             Failures;
    TArray<FDWCEditorWorkerJobTicket>           Tickets;
    TArray<int32>                               PendingMaterialSlotIndices;
    FDWCEditorBakeMemoryBudget                  MemoryBudget;
    FWetWrinkleNormalMapBakeSettings            Settings;
    TUniquePtr<FWetWrinkleNormalMapBakeSession> SnapshotSession;
    FCompletion                                 Completion;
};

struct FDWCEditorBakeCoordinator::FTransparencyBatch
{
    uint64                            BatchId = 0;
    int32                             SubmittedCount = 0;
    int32                             FinishedCount = 0;
    int32                             BakedMapCount = 0;
    int32                             AppliedStrokeCount = 0;
    int32                             AppliedSampleCount = 0;
    bool                              bSaveAfterCommit = false;
    bool                              bCanceled = false;
    bool                              bFinalized = false;
    TArray<FString>                   TextureNames;
    TArray<FString>                   Warnings;
    TArray<FString>                   Failures;
    TArray<FDWCEditorWorkerJobTicket> Tickets;
    FCompletion                       Completion;
};

FDWCEditorBakeCoordinator::FDWCEditorBakeCoordinator(
    UWetClothingAsset*                                            InAsset,
    TSharedRef<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe> InScheduler)
    : Asset(InAsset), Scheduler(InScheduler)
{
}

FDWCEditorBakeCoordinator::~FDWCEditorBakeCoordinator()
{
    bShuttingDown = true;
    CancelAll();
}

bool FDWCEditorBakeCoordinator::RequestWrinkleBake(
    TArray<int32> MaterialSlotIndices,
    const bool    bSaveAfterCommit,
    FCompletion   Completion,
    FString*      OutError)
{
    check(IsInGameThread());
    if (OutError != nullptr)
    {
        OutError->Reset();
    }
    UWetClothingAsset* TargetAsset = Asset.Get();
    if (TargetAsset == nullptr || !Scheduler.IsValid())
    {
        if (OutError != nullptr)
            *OutError = TEXT("The bake asset or worker scheduler is unavailable.");
        return false;
    }

    MaterialSlotIndices.Sort();
    MaterialSlotIndices.SetNum(Algo::Unique(MaterialSlotIndices));
    MaterialSlotIndices.Remove(INDEX_NONE);
    if (MaterialSlotIndices.IsEmpty())
    {
        if (OutError != nullptr)
            *OutError = TEXT("No material slots were provided for wrinkle baking.");
        return false;
    }

    if (ActiveWrinkleBatch.IsValid())
    {
        const TSharedPtr<FWrinkleBatch> PreviousBatch = ActiveWrinkleBatch;
        PreviousBatch->bCanceled = true;
        ActiveWrinkleBatch.Reset();
        for (const FDWCEditorWorkerJobTicket& Ticket : PreviousBatch->Tickets)
        {
            Scheduler->Cancel(Ticket.Key);
        }
    }

    TSharedRef<FWrinkleBatch> Batch = MakeShared<FWrinkleBatch>();
    Batch->BatchId = NextBatchId++;
    Batch->bSaveAfterCommit = bSaveAfterCommit;
    Batch->Completion = MoveTemp(Completion);
    ActiveWrinkleBatch = Batch;

    Batch->Settings.Resolution = TargetAsset->Authored.WrinkleData.BakeSettings.DefaultResolution;
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
    Batch->MemoryBudget.Configure(
        ResolutionBoundMaxInFlightJobs,
        ResolveWrinkleBakeMaxInFlightBytes());
    Batch->SnapshotSession = MakeUnique<FWetWrinkleNormalMapBakeSession>();

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
    if (TargetAsset == nullptr || !Scheduler.IsValid() || Batch->bCanceled ||
        ActiveWrinkleBatch != Batch || !Batch->SnapshotSession.IsValid())
    {
        return false;
    }

    TWeakPtr<FDWCEditorBakeCoordinator> WeakThis = AsShared();
    bool                                bSubmittedAny = false;
    while (Batch->MemoryBudget.HasJobCapacity() &&
           !Batch->PendingMaterialSlotIndices.IsEmpty())
    {
        const int32 MaterialSlotIndex = Batch->PendingMaterialSlotIndices[0];

        TSharedRef<FWetWrinkleNormalMapBakeSnapshot, ESPMode::ThreadSafe> Snapshot =
            MakeShared<FWetWrinkleNormalMapBakeSnapshot, ESPMode::ThreadSafe>();
        FString SnapshotError;
        if (!FWetWrinkleNormalMapBaker::BuildMaterialSlotSnapshot(
                TargetAsset,
                MaterialSlotIndex,
                Batch->Settings,
                *Batch->SnapshotSession,
                *Snapshot,
                SnapshotError))
        {
            Batch->PendingMaterialSlotIndices.RemoveAt(0, 1, EAllowShrinking::No);
            Batch->Failures.Add(FString::Printf(TEXT("Slot %d: %s"), MaterialSlotIndex, *SnapshotError));
            continue;
        }

        const uint64 SnapshotBytes = Snapshot->GetEstimatedBytes();
        if (!Batch->MemoryBudget.IsSingleSnapshotAllowed(SnapshotBytes))
        {
            Batch->PendingMaterialSlotIndices.RemoveAt(0, 1, EAllowShrinking::No);
            Batch->Failures.Add(FString::Printf(
                TEXT("Slot %d: the %.1f MiB wrinkle bake snapshot exceeds the %.1f MiB batch memory budget."),
                MaterialSlotIndex,
                static_cast<double>(SnapshotBytes) / (1024.0 * 1024.0),
                static_cast<double>(Batch->MemoryBudget.GetMaxInFlightBytes()) / (1024.0 * 1024.0)));
            continue;
        }

        if (!Batch->MemoryBudget.CanReserve(SnapshotBytes))
        {
            // Keep the slot pending. The completed job callback will pump it
            // again after releasing its snapshot reservation.
            break;
        }

        if (!Batch->MemoryBudget.TryReserve(SnapshotBytes))
        {
            break;
        }
        Batch->PendingMaterialSlotIndices.RemoveAt(0, 1, EAllowShrinking::No);

        FDWCEditorWorkerJobDescriptor Descriptor;
        Descriptor.Key.Kind = EDWCEditorWorkerJobKind::WrinkleBake;
        Descriptor.Key.MaterialSlotIndex = MaterialSlotIndex;
        Descriptor.Domain = EDWCEditorAuthoringDomain::Wrinkle;
        Descriptor.DomainRevision = Scheduler->GetCurrentDomainRevision(Descriptor.Domain);
        Descriptor.Priority = EDWCEditorWorkerJobPriority::UserInitiated;
        Descriptor.RequestPolicy = EDWCEditorAsyncRequestPolicy::FIFO;
        Descriptor.EstimatedBytes = Snapshot->GetEstimatedBytes();
        Descriptor.MemoryEstimate.SnapshotBytes = Descriptor.EstimatedBytes;
        Descriptor.DebugName = FString::Printf(TEXT("Wrinkle bake slot %d"), MaterialSlotIndex);

        FString                         SubmitError;
        const FDWCEditorWorkerJobTicket Ticket = Scheduler->Submit(
            Descriptor,
            [Snapshot](const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& Token)
            {
                TSharedPtr<FWrinkleBakeWorkerResult, ESPMode::ThreadSafe> Result =
                    MakeShared<FWrinkleBakeWorkerResult, ESPMode::ThreadSafe>();
                Result->Computed = FWetWrinkleNormalMapBaker::ComputeSnapshot(*Snapshot, &Token.Get());
                Result->bSucceeded = Result->Computed.bSucceeded;
                Result->Error = Result->Computed.Error;
                Result->ResultBytes = Result->Computed.ResultBytes;
                return StaticCastSharedPtr<FDWCEditorWorkerJobResult>(Result);
            },
            [WeakThis, Batch, Snapshot, MaterialSlotIndex](
                const FDWCEditorWorkerJobTicket&,
                TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe> BaseResult)
            {
                const TSharedPtr<FDWCEditorBakeCoordinator>                     Self = WeakThis.Pin();
                const TSharedPtr<FWrinkleBakeWorkerResult, ESPMode::ThreadSafe> Result =
                    StaticCastSharedPtr<FWrinkleBakeWorkerResult>(BaseResult);
                UWetClothingAsset* CurrentAsset = Self.IsValid() ? Self->Asset.Get() : nullptr;
                if (!Self.IsValid() || CurrentAsset == nullptr || !Result.IsValid())
                {
                    return;
                }

                FWetWrinkleNormalMapBakeResult CommitResult;
                FString                        CommitError;
                if (!FWetWrinkleNormalMapBaker::CommitComputedResult(
                        CurrentAsset,
                        *Snapshot,
                        MoveTemp(Result->Computed),
                        CommitResult,
                        CommitError))
                {
                    Batch->Failures.Add(FString::Printf(TEXT("Slot %d: %s"), MaterialSlotIndex, *CommitError));
                    return;
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
            },
            &SubmitError,
            [WeakThis, Batch, MaterialSlotIndex, SnapshotBytes](
                const FDWCEditorWorkerJobTicket&,
                const EDWCEditorWorkerJobCompletion JobCompletion,
                const FString&                      WorkerError)
            {
                if (const TSharedPtr<FDWCEditorBakeCoordinator> Self = WeakThis.Pin())
                {
                    Self->HandleWrinkleJobFinished(
                        Batch,
                        MaterialSlotIndex,
                        SnapshotBytes,
                        static_cast<uint8>(JobCompletion),
                        WorkerError);
                }
            });

        if (!Ticket.IsValid())
        {
            Batch->MemoryBudget.Release(SnapshotBytes);
            Batch->Failures.Add(FString::Printf(TEXT("Slot %d: %s"), MaterialSlotIndex, *SubmitError));
            continue;
        }
        Batch->Tickets.Add(Ticket);
        ++Batch->SubmittedCount;
        bSubmittedAny = true;
    }

    return bSubmittedAny;
}

void FDWCEditorBakeCoordinator::HandleWrinkleJobFinished(
    const TSharedRef<FWrinkleBatch>& Batch,
    const int32                      MaterialSlotIndex,
    const uint64                     SnapshotBytes,
    const uint8                      CompletionCode,
    const FString&                   WorkerError)
{
    check(IsInGameThread());
    const EDWCEditorWorkerJobCompletion Completion =
        static_cast<EDWCEditorWorkerJobCompletion>(CompletionCode);
    ++Batch->FinishedCount;
    Batch->MemoryBudget.Release(SnapshotBytes);
    if (Completion != EDWCEditorWorkerJobCompletion::Applied)
    {
        Batch->bCanceled |= Completion == EDWCEditorWorkerJobCompletion::Canceled ||
                            Completion == EDWCEditorWorkerJobCompletion::Superseded ||
                            Completion == EDWCEditorWorkerJobCompletion::Stale;
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
        Batch->MemoryBudget.GetInFlightJobs() == 0)
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
        TEXT("Wrinkle bake memory diagnostics: batch=%llu, submitted=%d, finished=%d, peakJobs=%d/%d, peakSnapshots=%.2f/%.2f MiB, largestSnapshot=%.2f MiB."),
        Batch->BatchId,
        Batch->SubmittedCount,
        Batch->FinishedCount,
        Batch->MemoryBudget.GetPeakInFlightJobs(),
        Batch->MemoryBudget.GetMaxInFlightJobs(),
        static_cast<double>(Batch->MemoryBudget.GetPeakInFlightBytes()) / (1024.0 * 1024.0),
        static_cast<double>(Batch->MemoryBudget.GetMaxInFlightBytes()) / (1024.0 * 1024.0),
        static_cast<double>(Batch->MemoryBudget.GetLargestReservedSnapshotBytes()) / (1024.0 * 1024.0));

    // Snapshots are only used while jobs are in flight. Releasing the session
    // here drops source readbacks and any remaining build-time cache before
    // potentially saving the generated assets or notifying the editor.
    Batch->PendingMaterialSlotIndices.Reset();
    Batch->SnapshotSession.Reset();

    // A replacement request revokes the previous batch's right to publish.
    // Its workers may still finish later, but they must not overwrite the
    // status, save, or UI completion produced by the current batch.
    if (ActiveWrinkleBatch != Batch)
    {
        return;
    }

    UWetClothingAsset* TargetAsset = Asset.Get();
    bool               bSaved = true;
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
    Result.bHadWarnings = !Batch->Failures.IsEmpty() || !bSaved;
    Result.bCanceled = Batch->bCanceled;
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
    if (!bShuttingDown && Batch->Completion)
    {
        Batch->Completion(Result);
    }
}

bool FDWCEditorBakeCoordinator::RequestTransparencyBake(
    TArray<FGuid> LayerGuids,
    const bool    bSaveAfterCommit,
    FCompletion   Completion,
    FString*      OutError)
{
    check(IsInGameThread());
    if (OutError != nullptr)
        OutError->Reset();
    UWetClothingAsset* TargetAsset = Asset.Get();
    if (TargetAsset == nullptr || !Scheduler.IsValid())
    {
        if (OutError != nullptr)
            *OutError = TEXT("The bake asset or worker scheduler is unavailable.");
        return false;
    }
    if (LayerGuids.IsEmpty())
    {
        if (OutError != nullptr)
            *OutError = TEXT("No transparency layers were provided for baking.");
        return false;
    }

    if (ActiveTransparencyBatch.IsValid())
    {
        const TSharedPtr<FTransparencyBatch> PreviousBatch = ActiveTransparencyBatch;
        PreviousBatch->bCanceled = true;
        ActiveTransparencyBatch.Reset();
        for (const FDWCEditorWorkerJobTicket& Ticket : PreviousBatch->Tickets)
        {
            Scheduler->Cancel(Ticket.Key);
        }
    }
    TSharedRef<FTransparencyBatch> Batch = MakeShared<FTransparencyBatch>();
    Batch->BatchId = NextBatchId++;
    Batch->bSaveAfterCommit = bSaveAfterCommit;
    Batch->Completion = MoveTemp(Completion);
    ActiveTransparencyBatch = Batch;

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
            TSharedRef<FDWCTransparencyAutoBakeResult> AutoResult =
                MakeShared<FDWCTransparencyAutoBakeResult>();
            FString         GenerateSummary;
            TArray<FString> GenerateWarnings;
            if (!FDWCTransparencyAutoMapGenerator::GenerateBaseRevealColorMap(
                    *TargetAsset,
                    *Layer,
                    *AutoResult,
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
            if (!SubmitTransparencyJob(Batch, LayerGuid, AutoResult, SubmitError))
            {
                Batch->Failures.Add(FString::Printf(
                    TEXT("Slot %d: %s"),
                    Layer->TargetSurface.OuterMaterialSlotIndex,
                    *SubmitError));
            }
            continue;
        }

        TSharedRef<FDWCTransparencyAutoMapSnapshot, ESPMode::ThreadSafe> AutoSnapshot =
            MakeShared<FDWCTransparencyAutoMapSnapshot, ESPMode::ThreadSafe>();
        FString SnapshotError;
        if (!FDWCTransparencyAutoMapGenerator::BuildSameMeshSnapshot(
                *TargetAsset,
                *Layer,
                *AutoSnapshot,
                SnapshotError))
        {
            Batch->Failures.Add(FString::Printf(
                TEXT("Slot %d: %s"),
                Layer->TargetSurface.OuterMaterialSlotIndex,
                *SnapshotError));
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
        Descriptor.EstimatedBytes = AutoSnapshot->GetEstimatedBytes();
        Descriptor.MemoryEstimate.SnapshotBytes = Descriptor.EstimatedBytes;
        Descriptor.DebugName = FString::Printf(
            TEXT("Transparency projection slot %d"),
            Layer->TargetSurface.OuterMaterialSlotIndex);

        TSharedRef<bool, ESPMode::ThreadSafe> bFinalSubmitted =
            MakeShared<bool, ESPMode::ThreadSafe>(false);
        TSharedRef<FString, ESPMode::ThreadSafe> FinalSubmitErrorHolder =
            MakeShared<FString, ESPMode::ThreadSafe>();
        TWeakPtr<FDWCEditorBakeCoordinator> WeakThis = AsShared();
        const int32                         MaterialSlotIndex = Layer->TargetSurface.OuterMaterialSlotIndex;
        FString                             SubmitError;
        const FDWCEditorWorkerJobTicket     Ticket = Scheduler->Submit(
            Descriptor,
            [AutoSnapshot](const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& Token)
            {
                TSharedPtr<FTransparencyAutoBakeWorkerResult, ESPMode::ThreadSafe> Result =
                    MakeShared<FTransparencyAutoBakeWorkerResult, ESPMode::ThreadSafe>();
                Result->Computed = FDWCTransparencyAutoMapGenerator::ComputeSameMeshSnapshot(
                    *AutoSnapshot,
                    &Token.Get());
                Result->bSucceeded = Result->Computed.bSucceeded;
                Result->Error = Result->Computed.Error;
                Result->ResultBytes = Result->Computed.ResultBytes;
                return StaticCastSharedPtr<FDWCEditorWorkerJobResult>(Result);
            },
            [WeakThis, Batch, bFinalSubmitted, FinalSubmitErrorHolder, LayerGuid, MaterialSlotIndex](
                const FDWCEditorWorkerJobTicket&,
                TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe> BaseResult)
            {
                const TSharedPtr<FDWCEditorBakeCoordinator>                              Self = WeakThis.Pin();
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
                TSharedRef<FDWCTransparencyAutoBakeResult> AutoResult =
                    MakeShared<FDWCTransparencyAutoBakeResult>(MoveTemp(Result->Computed.AutoResult));
                FString FinalSubmitError;
                if (Self->SubmitTransparencyJob(
                        Batch,
                        LayerGuid,
                        AutoResult,
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
                const FDWCEditorWorkerJobTicket&,
                const EDWCEditorWorkerJobCompletion JobCompletion,
                const FString&                      WorkerError)
            {
                if (JobCompletion == EDWCEditorWorkerJobCompletion::Applied && *bFinalSubmitted)
                {
                    return;
                }
                if (const TSharedPtr<FDWCEditorBakeCoordinator> Self = WeakThis.Pin())
                {
                    Self->HandleTransparencyJobFinished(
                        Batch,
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
        Batch->Tickets.Add(Ticket);
        ++Batch->SubmittedCount;
    }
    if (Batch->SubmittedCount == 0)
    {
        FinalizeTransparencyBatch(Batch);
    }
    return true;
}

bool FDWCEditorBakeCoordinator::RequestTransparencyFinalBake(
    const FGuid                                      LayerGuid,
    TSharedRef<const FDWCTransparencyAutoBakeResult> AutoResult,
    const bool                                       bSaveAfterCommit,
    FCompletion                                      Completion,
    FString*                                         OutError)
{
    check(IsInGameThread());
    if (OutError != nullptr)
        OutError->Reset();
    if (ActiveTransparencyBatch.IsValid())
    {
        if (OutError != nullptr)
            *OutError = TEXT("A transparency bake is already in progress.");
        return false;
    }
    TSharedRef<FTransparencyBatch> Batch = MakeShared<FTransparencyBatch>();
    Batch->BatchId = NextBatchId++;
    Batch->bSaveAfterCommit = bSaveAfterCommit;
    Batch->Completion = MoveTemp(Completion);
    ActiveTransparencyBatch = Batch;
    FString SubmitError;
    if (!SubmitTransparencyJob(Batch, LayerGuid, AutoResult, SubmitError))
    {
        Batch->bFinalized = true;
        if (ActiveTransparencyBatch == Batch)
        {
            ActiveTransparencyBatch.Reset();
        }
        if (OutError != nullptr)
            *OutError = SubmitError;
        return false;
    }
    return true;
}

bool FDWCEditorBakeCoordinator::SubmitTransparencyJob(
    const TSharedRef<FTransparencyBatch>&            Batch,
    const FGuid                                      LayerGuid,
    TSharedRef<const FDWCTransparencyAutoBakeResult> AutoResult,
    FString&                                         OutError,
    const bool                                       bCountAsBatchJob)
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

    TSharedRef<FDWCTransparencyEditedMapBakeSnapshot, ESPMode::ThreadSafe> Snapshot =
        MakeShared<FDWCTransparencyEditedMapBakeSnapshot, ESPMode::ThreadSafe>();
    if (!FDWCTransparencyEditedMapBaker::BuildSnapshot(
            *TargetAsset,
            *Layer,
            AutoResult,
            *Snapshot,
            OutError))
    {
        return false;
    }

    FDWCEditorWorkerJobDescriptor Descriptor;
    Descriptor.Key.Kind = EDWCEditorWorkerJobKind::TransparencyFinalBake;
    Descriptor.Key.MaterialSlotIndex = Snapshot->GetMaterialSlotIndex();
    Descriptor.Key.LayerGuid = LayerGuid;
    Descriptor.Domain = EDWCEditorAuthoringDomain::Transparency;
    Descriptor.DomainRevision = Scheduler->GetCurrentDomainRevision(Descriptor.Domain);
    Descriptor.Priority = EDWCEditorWorkerJobPriority::UserInitiated;
    Descriptor.RequestPolicy = EDWCEditorAsyncRequestPolicy::FIFO;
    Descriptor.EstimatedBytes = Snapshot->GetEstimatedBytes();
    Descriptor.MemoryEstimate.SnapshotBytes = Descriptor.EstimatedBytes;
    Descriptor.DebugName = FString::Printf(
        TEXT("Transparency bake slot %d"),
        Snapshot->GetMaterialSlotIndex());

    TWeakPtr<FDWCEditorBakeCoordinator> WeakThis = AsShared();
    const int32                         MaterialSlotIndex = Snapshot->GetMaterialSlotIndex();
    const FDWCEditorWorkerJobTicket     Ticket = Scheduler->Submit(
        Descriptor,
        [Snapshot](const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& Token)
        {
            TSharedPtr<FTransparencyBakeWorkerResult, ESPMode::ThreadSafe> Result =
                MakeShared<FTransparencyBakeWorkerResult, ESPMode::ThreadSafe>();
            Result->Computed = FDWCTransparencyEditedMapBaker::ComputeSnapshot(*Snapshot, &Token.Get());
            Result->bSucceeded = Result->Computed.bSucceeded;
            Result->Error = Result->Computed.Error;
            Result->ResultBytes = Result->Computed.ResultBytes;
            return StaticCastSharedPtr<FDWCEditorWorkerJobResult>(Result);
        },
        [WeakThis, Batch, Snapshot, AutoResult, LayerGuid, MaterialSlotIndex](
            const FDWCEditorWorkerJobTicket&,
            TSharedPtr<FDWCEditorWorkerJobResult, ESPMode::ThreadSafe> BaseResult)
        {
            const TSharedPtr<FDWCEditorBakeCoordinator>                          Self = WeakThis.Pin();
            const TSharedPtr<FTransparencyBakeWorkerResult, ESPMode::ThreadSafe> Result =
                StaticCastSharedPtr<FTransparencyBakeWorkerResult>(BaseResult);
            UWetClothingAsset* CurrentAsset = Self.IsValid() ? Self->Asset.Get() : nullptr;
            if (!Self.IsValid() || CurrentAsset == nullptr || !Result.IsValid())
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
            FString                             CommitError;
            if (!FDWCTransparencyEditedMapBaker::CommitComputedResult(
                    *CurrentAsset,
                    *Snapshot,
                    MoveTemp(Result->Computed),
                    CommitResult,
                    CommitError))
            {
                Batch->Failures.Add(FString::Printf(TEXT("Slot %d: %s"), MaterialSlotIndex, *CommitError));
                return;
            }

            CurrentAsset->Modify();
            CurrentLayer->AutoBakeMetadata.AutoBakeGuid = FGuid::NewGuid();
            CurrentLayer->AutoBakeMetadata.BuildSignature = AutoResult->BuildSignature;
            CurrentLayer->AutoBakeMetadata.Resolution = AutoResult->Resolution.X;
            CurrentLayer->AutoBakeMetadata.PaddingPixels =
                CurrentAsset->Authored.TransparencyData.TransparencyPaddingPixels;
            CurrentLayer->AutoBakeMetadata.ValidHitCount = AutoResult->ValidHitCount;
            CurrentLayer->AutoBakeMetadata.NoHitCount = AutoResult->NoHitCount;
            ++Batch->BakedMapCount;
            Batch->AppliedStrokeCount += CommitResult.AppliedStrokeCount;
            Batch->AppliedSampleCount += CommitResult.AppliedSampleCount;
            Batch->TextureNames.Add(GetPathNameSafe(CommitResult.TransparencyMap));
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
            const FDWCEditorWorkerJobTicket&,
            const EDWCEditorWorkerJobCompletion JobCompletion,
            const FString&                      WorkerError)
        {
            if (const TSharedPtr<FDWCEditorBakeCoordinator> Self = WeakThis.Pin())
            {
                Self->HandleTransparencyJobFinished(
                    Batch,
                    MaterialSlotIndex,
                    static_cast<uint8>(JobCompletion),
                    WorkerError);
            }
        });
    if (!Ticket.IsValid())
    {
        return false;
    }
    Batch->Tickets.Add(Ticket);
    if (bCountAsBatchJob)
    {
        ++Batch->SubmittedCount;
    }
    return true;
}

void FDWCEditorBakeCoordinator::HandleTransparencyJobFinished(
    const TSharedRef<FTransparencyBatch>& Batch,
    const int32                           MaterialSlotIndex,
    const uint8                           CompletionCode,
    const FString&                        WorkerError)
{
    check(IsInGameThread());
    const EDWCEditorWorkerJobCompletion Completion =
        static_cast<EDWCEditorWorkerJobCompletion>(CompletionCode);
    ++Batch->FinishedCount;
    if (Completion != EDWCEditorWorkerJobCompletion::Applied)
    {
        Batch->bCanceled |= Completion == EDWCEditorWorkerJobCompletion::Canceled ||
                            Completion == EDWCEditorWorkerJobCompletion::Superseded ||
                            Completion == EDWCEditorWorkerJobCompletion::Stale;
        const FString FailureReason = WorkerError.IsEmpty()
                                          ? DescribeCompletion(Completion)
                                          : WorkerError;
        Batch->Failures.Add(FString::Printf(
            TEXT("Slot %d: %s"),
            MaterialSlotIndex,
            *FailureReason));
    }
    if (Batch->FinishedCount >= Batch->SubmittedCount)
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
    if (ActiveTransparencyBatch != Batch)
    {
        return;
    }
    UWetClothingAsset* TargetAsset = Asset.Get();
    bool               bSaved = true;
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
        TEXT("Baked %d transparency map(s). Applied strokes: %d, samples: %d."),
        Batch->BakedMapCount,
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
    if (!bShuttingDown && Batch->Completion)
    {
        Batch->Completion(Result);
    }
}

void FDWCEditorBakeCoordinator::CancelAll()
{
    check(IsInGameThread());
    const TSharedPtr<FWrinkleBatch>      WrinkleBatch = ActiveWrinkleBatch;
    const TSharedPtr<FTransparencyBatch> TransparencyBatch = ActiveTransparencyBatch;
    if (WrinkleBatch.IsValid())
    {
        WrinkleBatch->bCanceled = true;
    }
    if (TransparencyBatch.IsValid())
    {
        TransparencyBatch->bCanceled = true;
    }
    ActiveWrinkleBatch.Reset();
    ActiveTransparencyBatch.Reset();
    if (Scheduler.IsValid())
    {
        if (WrinkleBatch.IsValid())
        {
            for (const FDWCEditorWorkerJobTicket& Ticket : WrinkleBatch->Tickets)
            {
                Scheduler->Cancel(Ticket.Key);
            }
        }
        if (TransparencyBatch.IsValid())
        {
            for (const FDWCEditorWorkerJobTicket& Ticket : TransparencyBatch->Tickets)
            {
                Scheduler->Cancel(Ticket.Key);
            }
        }
    }
}

bool FDWCEditorBakeCoordinator::IsWrinkleBakeActive() const
{
    return ActiveWrinkleBatch.IsValid() && !ActiveWrinkleBatch->bFinalized;
}

bool FDWCEditorBakeCoordinator::IsTransparencyBakeActive() const
{
    return ActiveTransparencyBatch.IsValid() && !ActiveTransparencyBatch->bFinalized;
}
