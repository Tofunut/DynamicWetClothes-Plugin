//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetClothingTransparencyData.h"
#include "SEditorViewport.h"
#include "UObject/GCObject.h"
#include "WetClothing/Foundation/Preview/Session/DWCEditorPreviewSession.h"
#include "WetClothing/Foundation/Preview/Orchestration/DWCEditorPreviewOrchestrator.h"
#include "WetClothing/Foundation/Authoring/State/DWCEditorSessionState.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSpatialQueryTypes.h"
#include "WetClothing/Foundation/TextureWorkspace/DWCEditorTextureWorkspaceTypes.h"
#include "WetClothing/Foundation/Preview/Commit/DWCEditorPreviewCommitTypes.h"
#include "WetClothing/Foundation/Preview/Recovery/DWCEditorPreviewRecovery.h"
#include "WetClothing/Foundation/Input/DWCEditorSurfaceAuthoringTool.h"
#include "WetClothing/Foundation/Input/DWCEditorInteractiveToolsHost.h"
#include "WetClothing/Foundation/Jobs/DWCEditorWorkerJobTypes.h"
#include "WetClothing/Modes/Transparency/Brush/DWCTransparencyAlphaTileStore.h"
#include "WetClothing/Modes/Transparency/Brush/DWCTransparencyLiveStrokeLayer.h"
#include "WetClothing/Modes/Transparency/Brush/DWCTransparencyRevealColorTileStore.h"

class AActor;
class FAdvancedPreviewScene;
class FDWCTransparencyAuthoringController;
class FDWCEditorWorkerJobScheduler;
class FDWCEditorPreviewCommitCoordinator;
class FDWCEditorSessionStore;
class FDWCEditorSpatialQueryService;
class FDWCEditorRenderUploadQueue;
class FDWCEditorTextureWorkspace;
using FDWCEditorWorkerJobSchedulerPtr = TSharedPtr<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe>;
class FEditorViewportClient;
class UMaterial;
class UMaterialInterface;
class UProceduralMeshComponent;
class UMaterialInstanceDynamic;
class UTexture2D;
class USkeletalMeshComponent;
class UWetClothingAsset;
struct FDWCTransparencyAutoBakeResult;
struct FDWCTransparencyAlphaComposeTileSnapshot;
struct FDWCTransparencyRevealColorComposeTileSnapshot;
struct FDWCTransparencyPixelComposeContext;
enum class EDWCTransparencyDirtyReplayTarget : uint8;
using FDWCTransparencySurfaceHit = FDWCEditorSurfaceHit;

class SWetClothingTransparencyPreviewViewport
    : public SEditorViewport
    , public FGCObject
    , public IDWCEditorSurfaceToolTarget
{
  public:
    SLATE_BEGIN_ARGS(SWetClothingTransparencyPreviewViewport) {}
    SLATE_ARGUMENT(UWetClothingAsset*, WetClothingAsset)
    SLATE_ARGUMENT(FDWCEditorWorkerJobSchedulerPtr, WorkerJobScheduler)
    SLATE_ARGUMENT(TSharedPtr<FDWCEditorSessionStore>, SessionStore)
    SLATE_ARGUMENT(TSharedPtr<FDWCEditorSpatialQueryService>, SpatialQueryService)
    SLATE_ARGUMENT(TSharedPtr<FDWCEditorTextureWorkspace>, TextureWorkspace)
    SLATE_ARGUMENT(TSharedPtr<FDWCEditorPreviewCommitCoordinator>, PreviewCommitCoordinator)
    SLATE_ARGUMENT(TSharedPtr<FDWCEditorRenderUploadQueue>, RenderUploadQueue)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    virtual ~SWetClothingTransparencyPreviewViewport() override;

    virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
    virtual FString GetReferencerName() const override { return TEXT("SWetClothingTransparencyPreviewViewport"); }

    void RefreshPreview();
    void SuspendPreview(EDWCEditorPreviewSuspendReason Reason);
    void ResumePreviewIfNeeded();
    bool IsPreviewSuspended() const { return bPreviewSuspended; }
    void FocusOnPreviewMesh(bool bInstant = false);
    void SetPreviewMode(EWetClothingTransparencyPreviewMode NewMode);
    EWetClothingTransparencyPreviewMode GetPreviewMode() const { return PreviewMode; }
    void SetWetnessPreviewPercent(float InPercent);
    void ApplyTransparencyPreviewSettings(const FDWCTransparencyPreviewSettings& InSettings);
    void SetShowSavedWrinkle(bool bInShowSavedWrinkle);
    void RefreshWrinkleSuppressionPreview();
    void RefreshOuterEdgeFeatherPreview();
    void SetPaintSettings(const FDWCTransparencyPaintSettings& InSettings);
    void SetAuthoringController(const TSharedPtr<FDWCTransparencyAuthoringController>& InController);
    void ApplyAuthoringBrushSample(
        const FDWCTransparencyBrushStroke& Stroke,
        const FDWCTransparencyBrushSample& Sample);
    void ApplyAuthoringRevealColorSample(
        const FDWCTransparencyRevealColorStroke& Stroke,
        const FDWCTransparencyBrushSample& Sample);
    void CommitAuthoringPreviewUpdate(EDWCTransparencyPaintTarget PaintTarget);
    void FinishAuthoringPreviewUpdate();
    void CancelAuthoringLiveStroke();
    void RebuildManualOverridesFromStrokes();
    void RefreshManualPreviewFromStrokes();
    void ReplayAlphaStrokeHistory(const TArray<FDWCTransparencyBrushStroke>& InvalidatedStrokes);
    void ReplayRevealColorStrokeHistory(
        const TArray<FDWCTransparencyRevealColorStroke>& InvalidatedStrokes);
    bool TraceSurface(const FVector& RayOrigin, const FVector& RayDirection, FDWCTransparencySurfaceHit& OutHit) const;
    bool CanPaint() const;
    bool CanShowBrushCursor() const;
    void HandleSurfaceHitFromClient(const FDWCTransparencySurfaceHit& SurfaceHit);
    void SetVisualizationMode(EDWCTransparencyVisualizationMode InMode);
    void SetAutoBakePreviewResult(TSharedPtr<const FDWCTransparencyAutoBakeResult> InResult);
    void ClearAutoBakePreviewResult();
    void SetTransparencyEditContext(
        const FGuid& InLayerGuid,
        int32 InMaterialSlotIndex,
        int32 InUVChannelIndex,
        EDWCTransparencyUVAddressMode InAddressMode,
        EDWCTransparencyPaintTarget InPaintTarget);
    void ProcessInteractivePaintWork();
    void FlushPendingPreviewTextureUpdates();
    void TickPreviewMaterialCompilations();

    virtual bool HitTestSurface(const FRay& WorldRay, double& OutHitDepth) const override;
    virtual bool CanBeginSurfaceInteraction(const FRay& WorldRay, double& OutHitDepth) override;
    virtual void BeginSurfaceInteraction(const FRay& WorldRay) override;
    virtual void UpdateSurfaceInteraction(const FRay& WorldRay) override;
    virtual void EndSurfaceInteraction() override;
    virtual void CancelSurfaceInteraction() override;
    virtual bool UpdateSurfaceHover(const FRay& WorldRay) override;
    virtual void ClearSurfaceHover() override;

  protected:
    virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;
    virtual TSharedPtr<SWidget> BuildViewportToolbar() override;

  private:
    void ClearPreview();
    void BuildTargetMeshPreview();
    void BuildFullBlueprintPreview();
    void ConfigurePreviewMeshComponent(USkeletalMeshComponent* MeshComponent);
    void InitializePreviewSession();
    void HandlePreviewSessionSlotsChanged();
    void HandlePreviewSessionMaterialReady(int32 MaterialSlotIndex, UMaterialInstanceDynamic* PreviewMID);
    void ApplyPreviewMaterials(USkeletalMeshComponent* MeshComponent);
    void ApplyRevealColorPaintTargetVisibility();
    void RefreshExistingFullBlueprintPreviewMaterials();
    void ApplyWetnessPreview();
    void ApplyTransparencyPreviewParameters();
    void RefreshSavedWrinklePreviewParameters();
    FDWCEditorPreviewLayer BuildTransparencyPreviewLayer();
    bool RebuildTransparencyPreviewTexture();
    void RetryPreviewTextureRebuildIfNeeded();
    UTexture2D* GetTransparencyPreviewTexture() const;
    UTexture2D* GetWrinkleCoverageTexture() const;
    bool CanUseMaterialDrivenPreviewPresentation() const;
    bool UsesFinalAlphaPreview() const;
    void RefreshDeferredFinalPreviewBuffers();
    void SchedulePreviewSettingsApply();
    EActiveTimerReturnType HandlePreviewSettingsApply(double CurrentTime, float DeltaTime);
    bool RebuildOuterEdgeFeatherBuffer();
    bool IsAuthoringInteractionActive() const;
    void RebuildHitTriangles();
    void EnsureBrushCursor();
    void RefreshBrushCursor();
    void ClearBrushCursor();
    void UpdateMaterialHoverLayer();
    void ClearMaterialHoverLayer();
    bool EnsureHoverBaselineTexture();
    bool EnsureHoverIslandMaskTexture(int32 UVIslandID);
    void ReleaseHoverAuxiliaryResources();
    UTexture2D* GetHoverBaselineTexture() const;
    UTexture2D* GetHoverIslandMaskTexture() const;
    void QueueRevealColorIncrementalSample(
        const FDWCTransparencyRevealColorStroke& Stroke,
        const FDWCTransparencyBrushSample& Sample);
    void ScheduleRevealColorIncrementalJob();
    void CancelRevealColorIncrementalWork(bool bRequireFullRebuild);
    bool BuildRevealColorComposeTileSnapshots(
        const TArray<FIntPoint>& TileCoordinates,
        TArray<FDWCTransparencyRevealColorComposeTileSnapshot>& OutTiles) const;
    void QueueAlphaIncrementalSample(
        const FDWCTransparencyBrushStroke& Stroke,
        const FDWCTransparencyBrushSample& Sample);
    void ScheduleAlphaIncrementalJob();
    void CancelAlphaIncrementalWork(bool bRequireFullRebuild);
    bool BuildAlphaComposeTileSnapshots(
        const TArray<FIntPoint>& TileCoordinates,
        TArray<FDWCTransparencyAlphaComposeTileSnapshot>& OutTiles) const;
    void ScheduleDirtyTileReplay(EDWCTransparencyDirtyReplayTarget Target);
    void CancelDirtyTileReplay(EDWCTransparencyDirtyReplayTarget Target, bool bRequireFullRebuild);
    void FinalizeAuthoringPreviewUpdate();
    void InvalidatePreviewContent(bool bRequireFullRebuild = false);
    FWetClothingTransparencyLayerData* GetSelectedLayer();
    void InvalidatePreviewViewport();
    USkeletalMeshComponent* FindFocusMeshComponent() const;
    void CollectDiagnosticMemoryStats(TArray<FDWCEditorPreviewMemoryBucket>& OutBuckets) const;
    void CollectDiagnosticOperationStats(TArray<FDWCEditorPreviewOperationCounter>& OutCounters) const;
    void ResetDiagnosticCounters();

  private:
    struct FPendingAlphaCommand
    {
        FDWCTransparencyBrushStroke Stroke;
        FDWCTransparencyBrushSample Sample;
        uint64 Sequence = 0;
    };

    struct FPendingRevealColorCommand
    {
        FDWCTransparencyRevealColorStroke Stroke;
        FDWCTransparencyBrushSample Sample;
        uint64 Sequence = 0;
    };

    TWeakObjectPtr<UWetClothingAsset> WetClothingAsset;
    TWeakPtr<FDWCTransparencyAuthoringController> AuthoringController;
    FDWCEditorWorkerJobSchedulerPtr WorkerJobScheduler;
    TSharedPtr<FDWCEditorSessionStore> SessionStore;
    TSharedPtr<FDWCEditorSpatialQueryService> SpatialQueryService;
    TSharedPtr<FDWCEditorTextureWorkspace> TextureWorkspace;
    TSharedPtr<FDWCEditorPreviewCommitCoordinator> PreviewCommitCoordinator;
    FDWCEditorPreviewConsumerLifetime PreviewCommitLifetime;
    TSharedPtr<FDWCEditorRenderUploadQueue> RenderUploadQueue;
    TSharedPtr<FAdvancedPreviewScene> PreviewScene;
    TUniquePtr<FDWCEditorInteractiveToolsHost> InputToolsHost;
    TSharedPtr<FEditorViewportClient> ViewportClient;
    TObjectPtr<USkeletalMeshComponent> TargetMeshPreviewComponent = nullptr;
    TObjectPtr<AActor> PreviewActor = nullptr;
    TArray<TObjectPtr<USkeletalMeshComponent>> PreviewMeshComponents;
    TUniquePtr<FDWCEditorPreviewSession> PreviewSession;
    TUniquePtr<FDWCEditorPreviewOrchestrator> PreviewOrchestrator;
    FDWCEditorTextureLease TransparencyPreviewHandle;
    FDWCEditorTextureLease HoverBaselinePreviewHandle;
    FDWCEditorTextureLease HoverIslandMaskPreviewHandle;
    TObjectPtr<UProceduralMeshComponent> BrushCursorComponent = nullptr;
    TSharedPtr<const FDWCTransparencyAutoBakeResult> AutoBakePreviewResult;
    TArray<uint8> OuterEdgeFeatherBuffer;
    FDWCTransparencyAlphaTileStore ManualAlphaTileStore;
    FDWCTransparencyRevealColorTileStore RevealColorTileStore;
    // Sparse transient stroke state is never serialized into the WCA. It
    // tracks only touched tiles until the controller commits or cancels.
    TUniquePtr<FDWCTransparencyLiveStrokeLayer> LiveStrokeLayer;
    bool bManualOverridesRequireWorkerRebuild = false;
    bool bRevealColorRequiresWorkerRebuild = false;
    bool bAuthoringWorkerRebuildRequested = false;
    bool bAuthoringFinishPending = false;
    TArray<FPendingAlphaCommand> PendingAlphaCommands;
    FDWCEditorWorkerJobTicket PendingAlphaIncrementalTicket;
    uint64 NextAlphaCommandSequence = 1;
    uint64 AlphaIncrementalEpoch = 1;
    FDWCEditorPreviewRecoveryController AlphaPreviewRecovery;
    TArray<FPendingRevealColorCommand> PendingRevealColorCommands;
    FDWCEditorWorkerJobTicket PendingRevealColorIncrementalTicket;
    uint64 NextRevealColorCommandSequence = 1;
    uint64 RevealColorIncrementalEpoch = 1;
    FDWCEditorPreviewRecoveryController RevealColorPreviewRecovery;
    TArray<FIntRect> PendingAlphaReplayRegions;
    TArray<FIntRect> PendingRevealColorReplayRegions;
    FDWCEditorWorkerJobTicket PendingAlphaReplayTicket;
    FDWCEditorWorkerJobTicket PendingRevealColorReplayTicket;
    uint64 AlphaReplayEpoch = 1;
    uint64 RevealColorReplayEpoch = 1;
    FDWCEditorSpatialLease SpatialLease;
    FDWCEditorSpatialHandle SpatialHandle;
    FDWCTransparencyPaintSettings PaintSettings;
    FDWCTransparencySurfaceHit CurrentSurfaceHit;
    int32 HoverLayerMaterialSlotIndex = INDEX_NONE;
    int32 HoverIslandMaskID = INDEX_NONE;
    EWetClothingTransparencyPreviewMode PreviewMode = EWetClothingTransparencyPreviewMode::TargetMeshOnly;
    EDWCTransparencyVisualizationMode VisualizationMode = EDWCTransparencyVisualizationMode::Final;
    float WetnessPreviewPercent = 100.0f;
    float TransparencyPreviewStrength = 0.4f;
    float WrinkleSuppressionStrength = 0.6f;
    float WrinkleMaskThreshold = 0.15f;
    float WrinkleMaskSoftness = 0.05f;
    bool bPreviewSettingsApplyScheduled = false;
    bool bOuterEdgeFeatherPreviewDirty = false;
    bool bTransparencyPaintingEnabled = false;
    bool bRevealColorPaintingEnabled = false;
    bool bShowSavedWrinkle = true;
    bool bPreviewSuspended = false;
    FGuid SelectedLayerGuid;
    int32 SelectedMaterialSlotIndex = INDEX_NONE;
    int32 SelectedUVChannelIndex = 0;
    EDWCTransparencyUVAddressMode SelectedUVAddressMode = EDWCTransparencyUVAddressMode::Clamp;
    uint64 PreviewRefreshCount = 0;
    uint64 PreviewClearCount = 0;
    uint64 HitTrianglePrepareCount = 0;
    uint64 PreviewTextureRebuildCount = 0;
    uint64 HoverParameterUpdateCount = 0;
    uint64 HoverBaselineBuildCount = 0;
    uint64 HoverIslandMaskBuildCount = 0;
    uint64 OuterEdgeFeatherRebuildCount = 0;
    uint64 InteractivePaintAuthoritativeReplayCount = 0;
    uint64 AlphaIncrementalCommitCount = 0;
    uint64 AlphaIncrementalCommittedTileCount = 0;
    uint64 AlphaIncrementalCommittedBytes = 0;
    uint64 AlphaIncrementalFallbackCount = 0;
    uint64 RevealColorIncrementalCommitCount = 0;
    uint64 RevealColorIncrementalCommittedTileCount = 0;
    uint64 RevealColorIncrementalCommittedBytes = 0;
    uint64 RevealColorIncrementalFallbackCount = 0;
    // A visualization job owns an immutable content snapshot. This revision
    // prevents an older asynchronous result from replacing newer brush edits.
    uint64 PreviewContentRevision = 0;
    uint64 PendingPreviewContentRevision = 0;
    FDWCEditorWorkerJobTicket PendingPreviewTicket;
    FDWCEditorPreviewRecoveryController PreviewTextureRecovery;
};
