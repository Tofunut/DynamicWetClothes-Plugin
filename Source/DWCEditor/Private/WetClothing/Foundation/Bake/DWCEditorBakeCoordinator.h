// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "UObject/WeakObjectPtr.h"
#include "CoreMinimal.h"

class FDWCEditorWorkerJobScheduler;
class UWetClothingAsset;
struct FDWCTransparencyAutoBakeResult;

struct FDWCEditorBakeBatchResult
{
    bool    bSucceeded = false;
    bool    bHadWarnings = false;
    bool    bCanceled = false;
    FString Summary;
};

/**
 * Owns editor bake requests from immutable snapshot capture through game-thread
 * asset commit. Pixel buffers never enter session state or Slate widgets.
 */
class FDWCEditorBakeCoordinator final
    : public TSharedFromThis<FDWCEditorBakeCoordinator>
{
  public:
    using FCompletion = TFunction<void(const FDWCEditorBakeBatchResult&)>;

    FDWCEditorBakeCoordinator(
        UWetClothingAsset*                                            InAsset,
        TSharedRef<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe> InScheduler);
    ~FDWCEditorBakeCoordinator();

    bool RequestWrinkleBake(
        TArray<int32> MaterialSlotIndices,
        bool          bSaveAfterCommit,
        FCompletion   Completion,
        FString*      OutError = nullptr);

    bool RequestTransparencyBake(
        TArray<FGuid> LayerGuids,
        bool          bSaveAfterCommit,
        FCompletion   Completion,
        FString*      OutError = nullptr);
    bool RequestTransparencyFinalBake(
        FGuid                                            LayerGuid,
        TSharedRef<const FDWCTransparencyAutoBakeResult> AutoResult,
        bool                                             bSaveAfterCommit,
        FCompletion                                      Completion,
        FString*                                         OutError = nullptr);

    void CancelAll();
    bool IsWrinkleBakeActive() const;
    bool IsTransparencyBakeActive() const;

  private:
    struct FWrinkleBatch;
    struct FTransparencyBatch;

    void HandleWrinkleJobFinished(
        const TSharedRef<FWrinkleBatch>& Batch,
        int32                            MaterialSlotIndex,
        uint64                           SnapshotBytes,
        uint8                            CompletionCode,
        const FString&                   WorkerError);
    bool PumpWrinkleJobs(const TSharedRef<FWrinkleBatch>& Batch);
    void FinalizeWrinkleBatch(const TSharedRef<FWrinkleBatch>& Batch);
    bool SubmitTransparencyJob(
        const TSharedRef<FTransparencyBatch>&            Batch,
        FGuid                                            LayerGuid,
        TSharedRef<const FDWCTransparencyAutoBakeResult> AutoResult,
        FString&                                         OutError,
        bool                                             bCountAsBatchJob = true);
    void HandleTransparencyJobFinished(
        const TSharedRef<FTransparencyBatch>& Batch,
        int32                                 MaterialSlotIndex,
        uint8                                 CompletionCode,
        const FString&                        WorkerError);
    void FinalizeTransparencyBatch(const TSharedRef<FTransparencyBatch>& Batch);

    TWeakObjectPtr<UWetClothingAsset>                             Asset;
    TSharedPtr<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe> Scheduler;
    TSharedPtr<FWrinkleBatch>                                     ActiveWrinkleBatch;
    TSharedPtr<FTransparencyBatch>                                ActiveTransparencyBatch;
    uint64                                                        NextBatchId = 1;
    bool                                                          bShuttingDown = false;
};
