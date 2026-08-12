//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

class FDWCEditorWorkerJobScheduler;
class FDWCEditorBuildOperationManager;
class FDWCEditorCacheStore;
class FDWCEditorSpatialQueryService;
class FDWCEditorSurfacePatchProjectionCacheService;
class FDWCWrinkleSuppressionCoverageService;
class UWetClothingAsset;
struct FDWCTransparencySourcePayload;
struct FDWCTransparencyAlphaWorkingSnapshot;
struct FDWCTransparencyFinalSettingsSnapshot;

enum class EDWCEditorTransparencyBakeKind : uint8
{
    None,
    Full,
    AffectedStage4
};

struct FDWCEditorBakeBatchResult
{
    bool bSucceeded = false;
    bool bHadWarnings = false;
    bool bCanceled = false;
    FString Summary;
    FString AttentionSummary;
    TArray<int32> OutOfDateTransparencyMaterialSlots;
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
        UWetClothingAsset* InAsset,
        TSharedRef<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe> InScheduler,
        TSharedRef<FDWCEditorBuildOperationManager> InOperationManager,
        TSharedRef<FDWCEditorSpatialQueryService> InSpatialQueryService,
        TSharedRef<FDWCEditorSurfacePatchProjectionCacheService> InSurfacePatchProjectionCache,
        TSharedPtr<FDWCWrinkleSuppressionCoverageService> InCoverageService,
        TSharedPtr<FDWCEditorCacheStore> InCacheStore = nullptr);
    ~FDWCEditorBakeCoordinator();

    bool RequestWrinkleBake(
        TArray<int32> MaterialSlotIndices,
        bool bSaveAfterCommit,
        FCompletion Completion,
        FString* OutError = nullptr);

    bool RequestTransparencyBake(
        TArray<FGuid> LayerGuids,
        bool bSaveAfterCommit,
        FCompletion Completion,
        FString* OutError = nullptr);
    bool RequestTransparencyFinalBake(
        FGuid LayerGuid,
        TSharedRef<const FDWCTransparencySourcePayload> SourcePayload,
        TSharedPtr<const FDWCTransparencyAlphaWorkingSnapshot> AlphaSnapshot,
        TSharedRef<const FDWCTransparencyFinalSettingsSnapshot> FinalSettings,
        bool bSaveAfterCommit,
        FCompletion Completion,
        FString* OutError = nullptr);
    /** Rebuilds Stage 4 only when the persisted Stage 2/3 inputs are current and only wrinkle coverage changed. */
    bool RequestAffectedTransparencyStage4Rebake(
        TArray<int32> MaterialSlotIndices,
        bool bSaveAfterCommit,
        FCompletion Completion,
        FString* OutError = nullptr);

    void CancelAll();
    void Shutdown();
    bool IsWrinkleBakeActive() const;
    bool IsTransparencyBakeActive() const;
    EDWCEditorTransparencyBakeKind GetActiveTransparencyBakeKind() const;

  private:
    struct FWrinkleBatch;
    struct FTransparencyBatch;

    void HandleWrinkleJobFinished(
        const TSharedRef<FWrinkleBatch>& Batch,
        const struct FDWCEditorWorkerJobTicket& Ticket,
        int32 MaterialSlotIndex,
        uint8 CompletionCode,
        const FString& WorkerError);
    bool PumpWrinkleJobs(const TSharedRef<FWrinkleBatch>& Batch);
    void FinalizeWrinkleBatch(const TSharedRef<FWrinkleBatch>& Batch);
    bool SubmitTransparencyJob(
        const TSharedRef<FTransparencyBatch>& Batch,
        FGuid LayerGuid,
        TSharedPtr<const FDWCTransparencySourcePayload> SourcePayload,
        FString& OutError,
        bool bCountAsBatchJob = true,
        TSharedPtr<const FDWCTransparencyAlphaWorkingSnapshot> AlphaSnapshot = nullptr,
        TSharedPtr<const FDWCTransparencyFinalSettingsSnapshot> FinalSettingsOverride = nullptr,
        bool bRestoreCanonicalSourceDuringPrepare = false);
    void HandleTransparencyJobFinished(
        const TSharedRef<FTransparencyBatch>& Batch,
        const struct FDWCEditorWorkerJobTicket& Ticket,
        int32 MaterialSlotIndex,
        uint8 CompletionCode,
        const FString& WorkerError);
    bool PumpAffectedTransparencyStage4Jobs(const TSharedRef<FTransparencyBatch>& Batch);
    void FinalizeTransparencyBatch(const TSharedRef<FTransparencyBatch>& Batch);

    TWeakObjectPtr<UWetClothingAsset> Asset;
    TSharedPtr<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe> Scheduler;
    TSharedPtr<FDWCEditorBuildOperationManager> OperationManager;
    TSharedPtr<FDWCEditorSpatialQueryService> SpatialQueryService;
    TSharedPtr<FDWCEditorSurfacePatchProjectionCacheService> SurfacePatchProjectionCache;
    TSharedPtr<FDWCWrinkleSuppressionCoverageService> CoverageService;
    TSharedPtr<FDWCEditorCacheStore> CacheStore;
    TSharedPtr<FWrinkleBatch> ActiveWrinkleBatch;
    TSharedPtr<FTransparencyBatch> ActiveTransparencyBatch;
    bool bShuttingDown = false;
};
