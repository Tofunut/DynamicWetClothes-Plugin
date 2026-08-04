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
#include "WetClothing/Foundation/Input/DWCEditorSurfaceAuthoringTool.h"
#include "WetClothing/Foundation/Input/DWCEditorInteractiveToolsHost.h"
#include "WetClothing/Foundation/Jobs/DWCEditorWorkerJobTypes.h"

class AActor;
class FAdvancedPreviewScene;
class FDWCTransparencyAuthoringController;
class FDWCTransparencyLiveStrokeLayer;
class FDWCEditorWorkerJobScheduler;
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
struct FDWCTransparencyPixelComposeContext;
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
    void SetTransparencyPreviewStrength(float InStrength);
    void SetShowSavedWrinkle(bool bInShowSavedWrinkle);
    void SetWrinkleSuppressionStrength(float InStrength);
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
    UTexture2D* GetTransparencyPreviewTexture() const;
    UTexture2D* GetWrinkleSuppressionPreviewTexture() const;
    bool RebuildWrinkleSuppressionBuffer();
    bool UpdateWrinkleSuppressionPreviewTexture();
    bool CanUseDynamicFinalPreviewComposition() const;
    bool UsesFinalAlphaPreview() const;
    bool UsesWrinkleSuppressionPreview() const;
    void RefreshDeferredFinalPreviewBuffers();
    void InvalidateWrinkleSuppressionSourceCache();
    bool RebuildOuterEdgeFeatherBuffer();
    bool EnsureManualOverrideBuffers();
    bool EnsureRevealColorBuffer();
    bool IsAuthoringInteractionActive() const;
    void ReleaseSmoothBrushScratch();
    void RebuildHitTriangles();
    void EnsureBrushCursor();
    void RefreshBrushCursor();
    void ClearBrushCursor();
    bool RasterizeBrushSample(
        const FDWCTransparencyBrushStroke& Stroke,
        const FDWCTransparencyBrushSample& Sample,
        FIntRect* OutDirtyRect = nullptr,
        const FIntRect* ClipRect = nullptr);
    bool RasterizeRevealColorSample(
        const FDWCTransparencyRevealColorStroke& Stroke,
        const FDWCTransparencyBrushSample& Sample,
        FIntRect* OutDirtyRect = nullptr,
        const FIntRect* ClipRect = nullptr);
    bool ShouldDeferBrushRaster(const FDWCTransparencyBrushSample& Sample) const;
    FIntRect ComputeCurrentHoverDirtyRect() const;
    void RefreshHoverPreviewRegion();
    void QueueInteractivePaintWork(
        const FDWCTransparencyBrushStroke& Stroke,
        const FDWCTransparencyBrushSample& Sample);
    void QueueInteractivePaintWork(
        const FDWCTransparencyRevealColorStroke& Stroke,
        const FDWCTransparencyBrushSample& Sample);
    TArray<FIntRect> BuildInteractivePaintRegions(
        const FDWCTransparencyBrushSample& Sample,
        EDWCTransparencyUVAddressMode AddressMode) const;
    uint64 GetPendingInteractivePaintRegionCount() const;
    uint64 GetInteractivePaintWorkAllocatedBytes() const;
    void FinalizeAuthoringPreviewUpdate();
    void QueuePreviewTextureUpdate(
        const FIntRect& DirtyRect,
        bool bWrap,
        bool bAllowFullRebuild = true);
    void UploadPreviewTextureRegion(
        const FIntRect& DirtyRect,
        bool bRebuildPixels,
        bool bIncludeHover = false);
    void InvalidatePreviewContent();
    FWetClothingTransparencyLayerData* GetSelectedLayer();
    float GetStoredEditedAlpha(int32 PixelIndex) const;
    float ApplyHoverToEditedAlpha(int32 PixelIndex, float EditedAlpha) const;
    FColor ApplyHoverToRevealColor(int32 PixelIndex, const FColor& BaseColor) const;
    FColor BuildVisualizationPixel(
        int32 PixelIndex,
        const FDWCTransparencyPixelComposeContext& Context,
        bool bIncludeHover = false) const;
    void InvalidatePreviewViewport();
    USkeletalMeshComponent* FindFocusMeshComponent() const;
    void CollectDiagnosticMemoryStats(TArray<FDWCEditorPreviewMemoryBucket>& OutBuckets) const;
    void CollectDiagnosticOperationStats(TArray<FDWCEditorPreviewOperationCounter>& OutCounters) const;
    void ResetDiagnosticCounters();

  private:
    struct FInteractivePaintWork
    {
        TOptional<FDWCTransparencyBrushStroke> AlphaStroke;
        TOptional<FDWCTransparencyRevealColorStroke> RevealStroke;
        FDWCTransparencyBrushSample Sample;
        TArray<FIntRect> Regions;
        int32 NextRegionIndex = 0;
    };

    TWeakObjectPtr<UWetClothingAsset> WetClothingAsset;
    TWeakPtr<FDWCTransparencyAuthoringController> AuthoringController;
    FDWCEditorWorkerJobSchedulerPtr WorkerJobScheduler;
    TSharedPtr<FDWCEditorSessionStore> SessionStore;
    TSharedPtr<FDWCEditorSpatialQueryService> SpatialQueryService;
    TSharedPtr<FDWCEditorTextureWorkspace> TextureWorkspace;
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
    FDWCEditorTextureLease WrinkleSuppressionPreviewHandle;
    TObjectPtr<UProceduralMeshComponent> BrushCursorComponent = nullptr;
    TSharedPtr<const FDWCTransparencyAutoBakeResult> AutoBakePreviewResult;
    TArray<uint8> WrinkleSuppressionBuffer;
    TObjectPtr<UTexture2D> CachedWrinkleSuppressionMaskTexture = nullptr;
    FGuid CachedWrinkleSuppressionBakeGuid;
    FIntPoint CachedWrinkleSuppressionResolution = FIntPoint::ZeroValue;
    TArray<uint16> CachedWrinkleSuppressionCoverageBuffer;
    TArray<uint8> OuterEdgeFeatherBuffer;
    TArray<uint8> ManualPremultipliedBuffer;
    TArray<uint8> ManualWeightBuffer;
    // Authored reveal-color strokes are composited over the immutable
    // auto-bake color buffer by a worker job.
    TArray<FColor> RevealColorBuffer;
    TArray<uint8> SmoothBrushPremultipliedScratch;
    TArray<uint8> SmoothBrushWeightScratch;
    // Reveal-color smoothing needs a stable source snapshot while it writes
    // into InnerColorBuffer. Keep the scratch allocation across samples.
    TArray<FColor> SmoothRevealColorScratch;
    // Sparse transient stroke state is never serialized into the WCA. It
    // tracks only touched tiles until the controller commits or cancels.
    TUniquePtr<FDWCTransparencyLiveStrokeLayer> LiveStrokeLayer;
    bool bManualOverridesRequireWorkerRebuild = false;
    bool bRevealColorRequiresWorkerRebuild = false;
    // Large brush strokes commit their full preview composition once at
    // interaction end instead of snapshotting a 2K/4K result per sample.
    bool bDeferredBrushPreviewRebuild = false;
    // A saved stroke always receives one authoritative worker replay after
    // mouse-up. Interactive tiles remain feedback only until that replay.
    bool bAuthoringWorkerRebuildRequested = false;
    bool bAuthoringFinishPending = false;
    TArray<FInteractivePaintWork> PendingInteractivePaintWork;
    FDWCEditorSpatialLease SpatialLease;
    FDWCEditorSpatialHandle SpatialHandle;
    FDWCTransparencyPaintSettings PaintSettings;
    FDWCTransparencySurfaceHit CurrentSurfaceHit;
    FIntRect LastHoverDirtyRect;
    EWetClothingTransparencyPreviewMode PreviewMode = EWetClothingTransparencyPreviewMode::TargetMeshOnly;
    EDWCTransparencyVisualizationMode VisualizationMode = EDWCTransparencyVisualizationMode::Final;
    float WetnessPreviewPercent = 100.0f;
    float TransparencyPreviewStrength = 0.4f;
    float WrinkleSuppressionStrength = 0.6f;
    bool bWrinkleSuppressionPreviewDirty = false;
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
    uint64 PreviewTextureRegionUploadCount = 0;
    uint64 PreviewTextureRegionUploadBytes = 0;
    uint64 WrinkleSuppressionRebuildCount = 0;
    uint64 OuterEdgeFeatherRebuildCount = 0;
    uint64 InteractivePaintQueuedRegionCount = 0;
    uint64 InteractivePaintProcessedRegionCount = 0;
    uint64 InteractivePaintCanceledRegionCount = 0;
    uint64 InteractivePaintPeakQueuedRegionCount = 0;
    uint64 InteractivePaintAuthoritativeReplayCount = 0;
    // A visualization job owns an immutable content snapshot. This revision
    // prevents an older asynchronous result from replacing newer brush edits.
    uint64 PreviewContentRevision = 0;
    uint64 PendingPreviewContentRevision = 0;
    FDWCEditorWorkerJobTicket PendingPreviewTicket;
};
