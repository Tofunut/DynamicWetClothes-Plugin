//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetClothingWrinkleData.h"
#include "SEditorViewport.h"
#include "UObject/GCObject.h"
#include "WetClothing/Foundation/Preview/Session/DWCEditorPreviewSession.h"
#include "WetClothing/Foundation/Preview/Orchestration/DWCEditorPreviewOrchestrator.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSpatialQueryTypes.h"
#include "WetClothing/Foundation/TextureWorkspace/DWCEditorTextureWorkspaceTypes.h"
#include "WetClothing/Foundation/Preview/Commit/DWCEditorPreviewCommitTypes.h"
#include "WetClothing/Foundation/Preview/Recovery/DWCEditorPreviewRecovery.h"
#include "WetClothing/Foundation/Input/DWCEditorSurfaceAuthoringTool.h"
#include "WetClothing/Foundation/Input/DWCEditorInteractiveToolsHost.h"
#include "WetClothing/Foundation/Jobs/DWCEditorWorkerJobTypes.h"
#include "WetClothing/Modes/Wrinkle/Viewport/WetWrinkleIncrementalPreviewWorker.h"
#include "WetWrinkleHitData.h"

class FAdvancedPreviewScene;
class FDWCEditorWorkerJobScheduler;
class FDWCEditorPreviewCommitCoordinator;
class FWetWrinkleAuthoringController;
class FDWCEditorSessionStore;
class FDWCEditorSpatialQueryService;
class FDWCEditorRenderUploadQueue;
class FDWCEditorTextureWorkspace;
using FDWCEditorWorkerJobSchedulerPtr = TSharedPtr<FDWCEditorWorkerJobScheduler, ESPMode::ThreadSafe>;
class FPrimitiveDrawInterface;
class FWetWrinkleViewportClient;
class UMaterialInstanceDynamic;
class USkeletalMesh;
class USkeletalMeshComponent;
class UTexture;
class UTexture2D;
class UWetClothingAsset;
struct FWetWrinklePatchPlacement;
struct FWetProceduralRidgeStroke;
struct FWetProceduralRidgeStrokePoint;

using FWetWrinkleProjectedSurface = FDWCEditorProjectedSurface;

struct FWetWrinkleAccumulatedPreviewState
{
    TObjectPtr<UTexture> SourceTexture = nullptr;
    int32 MaterialSlotIndex = INDEX_NONE;
    int32 UVChannelIndex = INDEX_NONE;
    FDWCEditorTextureLease TextureHandle;
    FIntPoint TextureSize = FIntPoint::ZeroValue;
    FIntPoint WorkingTextureSize = FIntPoint::ZeroValue;
    bool bDirty = true;
    bool bRebuildPending = false;
    FDWCEditorWorkerJobTicket PendingTicket;
    // Worker results are only valid for the authored content snapshot they were
    // built from. Keep the latest requested revision separate from the pending
    // worker revision so a fast second edit cannot be hidden by an older result.
    uint64 ContentRevision = 0;
    uint64 PendingContentRevision = 0;
    uint64 LastUsedSerial = 0;
    FDWCEditorPreviewRecoveryController Recovery;
    TArray<FWetWrinkleIncrementalCommand> PendingIncrementalCommands;
    FDWCEditorWorkerJobTicket PendingIncrementalTicket;
    uint64 IncrementalGeneration = 1;
    uint64 NextIncrementalSequence = 0;
};

struct FWetProceduralRidgeTransientPreviewState
{
    TObjectPtr<UTexture> SourceTexture = nullptr;
    int32 MaterialSlotIndex = INDEX_NONE;
    int32 UVChannelIndex = INDEX_NONE;
    FDWCEditorTextureLease TextureHandle;
    FIntPoint TextureSize = FIntPoint::ZeroValue;
    FIntPoint WorkingTextureSize = FIntPoint::ZeroValue;
    TOptional<FWetProceduralRidgeStroke> LastCommittedStroke;
    FDWCEditorWorkerJobTicket PendingIncrementalTicket;
    uint64 RequestSerial = 0;
};

class SWetWrinkleViewport : public SEditorViewport, public FGCObject, public IDWCEditorSurfaceToolTarget
{
    friend class FWetWrinkleViewportClient;

  public:
    SLATE_BEGIN_ARGS(SWetWrinkleViewport) {}
    SLATE_ARGUMENT(UWetClothingAsset*, WetClothingAsset)
    SLATE_ARGUMENT(FDWCEditorWorkerJobSchedulerPtr, WorkerJobScheduler)
    SLATE_ARGUMENT(TSharedPtr<FDWCEditorSessionStore>, SessionStore)
    SLATE_ARGUMENT(TSharedPtr<FDWCEditorSpatialQueryService>, SpatialQueryService)
    SLATE_ARGUMENT(TSharedPtr<FDWCEditorTextureWorkspace>, TextureWorkspace)
    SLATE_ARGUMENT(TSharedPtr<FDWCEditorPreviewCommitCoordinator>, PreviewCommitCoordinator)
    SLATE_ARGUMENT(TSharedPtr<FDWCEditorRenderUploadQueue>, RenderUploadQueue)
    SLATE_EVENT(FOnWetWrinkleSurfaceHitChanged, OnSurfaceHitChanged)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    virtual ~SWetWrinkleViewport() override;
    virtual void Tick(const FGeometry& AllottedGeometry, double InCurrentTime, float InDeltaTime) override;

    virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
    virtual FString GetReferencerName() const override
    {
        return TEXT("SWetWrinkleViewport");
    }

    void RefreshPreviewMesh(bool bForceMaterialRebuild = false);
    void SuspendPreview(EDWCEditorPreviewSuspendReason Reason);
    void ResumePreviewIfNeeded();
    bool IsPreviewSuspended() const { return bPreviewSuspended; }
    void SynchronizeBrushSettings(const FWetWrinkleBrushSettings& InBrushSettings);
    void SetBrushTopology(int32 MaterialSlotIndex, int32 UVChannelIndex);
    void UpdateBrushPreviewSettings(const FWetWrinkleBrushSettings& InBrushSettings);
    void SetPreviewWetness(float PreviewWetness);
    void SetShowBakedTransparency(bool bInShowBakedTransparency);
    void RefreshStoredStampOverlay(bool bRebuildAccumulatedPreview = true);
    void InvalidateAccumulatedPreviewTextures();
    void AppendAccumulatedPreviewStamp(const FWetWrinklePatchPlacement& Stamp);
    void AppendAccumulatedPreviewProceduralStroke(const FWetProceduralRidgeStroke& Stroke);
    void SetGeneratedNormalPreviewTexture(
        int32 MaterialSlotIndex,
        int32 UVChannelIndex,
        UTexture2D* GeneratedNormalTexture,
        bool bRefreshPreview = true);
    void ClearGeneratedNormalPreviewTexture(bool bRefreshPreview = true);
    void SetSelectedProceduralStrokeGuid(const FGuid& InStrokeGuid);
    void SetSelectedProceduralStrokePointIndex(int32 InPointIndex);
    void SetTransientProceduralStroke(
        const TArray<FWetWrinkleSurfaceHit>& SurfaceHits,
        bool bStartJunction = false,
        bool bEndJunction = false);
    void PreviewEditedProceduralStroke(const FWetProceduralRidgeStroke& Stroke);
    bool SetEditingProceduralStrokeGuid(const FGuid& InStrokeGuid, bool bRefreshPreview = true);
    int32 FindNearestProceduralStrokePoint(
        const FWetProceduralRidgeStroke& Stroke,
        const FVector& WorldPosition,
        float MaxDistance) const;
    bool ResolveProceduralStrokePointWorld(
        const FWetProceduralRidgeStrokePoint& Point,
        int32 MaterialSlotIndex,
        FVector& OutWorldPosition,
        FVector& OutWorldNormal) const;
    bool TryBuildSurfaceHitFromProceduralStrokePoint(
        const FWetProceduralRidgeStrokePoint& Point,
        int32 MaterialSlotIndex,
        int32 UVChannelIndex,
        FWetWrinkleSurfaceHit& OutHit) const;
    bool ClearTransientProceduralStroke(bool bRefreshPreview = true);
    bool TryBuildSurfaceHitAtUVNearWorldPosition(int32 MaterialSlotIndex, int32 UVChannelIndex, const FVector2D& UV, const FVector& ReferenceWorldPosition, FWetWrinkleSurfaceHit& OutHit) const;
    bool TraceSurface(const FVector& RayOrigin, const FVector& RayDirection, FWetWrinkleSurfaceHit& OutHit) const;
    void FocusOnPreviewMesh(bool bInstant = false);
    void SetAuthoringController(const TSharedPtr<FWetWrinkleAuthoringController>& InController);

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
    virtual void PopulateViewportOverlays(TSharedRef<SOverlay> Overlay) override;
    virtual void OnFocusViewportToSelection() override;

  private:
    USkeletalMesh* ResolveTargetMesh() const;
    void ApplyMaterialSlotVisibility();
    void RebuildHitTriangles();
    void HandleSurfaceHitFromClient(const FWetWrinkleSurfaceHit& SurfaceHit);
    void RefreshBrushCursor();
    void ClearBrushCursor();
    void DrawBrushCursor(FPrimitiveDrawInterface* PDI) const;
    void RefreshWrinklePreviewHoverParameters();
    void RefreshWrinklePreviewAccumulatedParameters();
    void RefreshWrinklePreviewTransientParameters();
    FDWCEditorPreviewLayer BuildAccumulatedPreviewLayer(int32 MaterialSlotIndex);
    FDWCEditorPreviewLayer BuildTransientPreviewLayer(int32 MaterialSlotIndex) const;
    FDWCEditorPreviewLayer BuildHoverPreviewLayer(int32 MaterialSlotIndex) const;
    float CalculateBrushCursorWorldRadius() const;
    const UWetClothingAsset* ResolveSourceWetClothingAsset() const;
    UTexture* ResolveSourceTextureForMaterialSlot(int32 MaterialSlotIndex) const;
    void InitializePreviewSession();
    void HandlePreviewSessionSlotsChanged();
    void HandlePreviewSessionMaterialReady(int32 MaterialSlotIndex, UMaterialInstanceDynamic* PreviewMID);
    void ApplyPreviewMaterialsToMesh();
    void MarkPreviewMaterialsNeedReapply();
    void RefreshWrinklePreviewMaterials();
    UMaterialInstanceDynamic* GetActiveWrinklePreviewMID(bool bCreateIfMissing = true);
    void ReleaseAccumulatedPreviewStates();
    void ReleaseAccumulatedPreviewStateResources(
        FWetWrinkleAccumulatedPreviewState& PreviewState,
        bool bClearMaterialBinding);
    void PrepareAccumulatedPreviewStatesForSlot(int32 MaterialSlotIndex, int32 UVChannelIndex);
    void PruneAccumulatedPreviewStates(int32 MaterialSlotIndex, int32 UVChannelIndex);
    void MarkAccumulatedPreviewStatesDirty();
    FWetWrinkleAccumulatedPreviewState* FindOrAddAccumulatedPreviewState(UTexture* SourceTexture, int32 MaterialSlotIndex, int32 UVChannelIndex);
    UTexture2D* ResolveAccumulatedPreviewTexture(UTexture* SourceTexture, int32 MaterialSlotIndex, int32 UVChannelIndex);
    bool RebuildAccumulatedPreviewTexture(FWetWrinkleAccumulatedPreviewState& PreviewState);
    void QueueAccumulatedIncrementalCommand(
        FWetWrinkleAccumulatedPreviewState& PreviewState,
        FWetWrinkleIncrementalCommand&& Command);
    bool ScheduleAccumulatedIncrementalPreview(FWetWrinkleAccumulatedPreviewState& PreviewState);
    void InvalidateAccumulatedIncrementalState(FWetWrinkleAccumulatedPreviewState& PreviewState);
    void ResetTransientProceduralPreviewResources();
    void ReleaseTransientProceduralPreviewState();
    bool EnsureTransientProceduralPreviewState(int32 MaterialSlotIndex, int32 UVChannelIndex);
    bool UpdateTransientProceduralPreview(const FWetProceduralRidgeStroke& Stroke);
    bool ScheduleTransientProceduralPreview(const FWetProceduralRidgeStroke& Stroke);
    int32 ResolveActivePreviewMaterialSlot() const;
    void FindProjectedSurfacesAtUV(int32 MaterialSlotIndex, int32 UVChannelIndex, const FVector2D& UV, TArray<FWetWrinkleProjectedSurface>& OutSurfaces) const;
    void DrawProceduralStrokeGuides(FPrimitiveDrawInterface* PDI) const;
    void CollectDiagnosticMemoryStats(TArray<FDWCEditorPreviewMemoryBucket>& OutBuckets) const;
    void CollectDiagnosticOperationStats(TArray<FDWCEditorPreviewOperationCounter>& OutCounters) const;
    void ResetDiagnosticCounters();

  private:
    TWeakObjectPtr<UWetClothingAsset> WetClothingAsset;
    FDWCEditorWorkerJobSchedulerPtr WorkerJobScheduler;
    TSharedPtr<FDWCEditorSessionStore> SessionStore;
    TSharedPtr<FDWCEditorSpatialQueryService> SpatialQueryService;
    TSharedPtr<FDWCEditorTextureWorkspace> TextureWorkspace;
    TSharedPtr<FDWCEditorPreviewCommitCoordinator> PreviewCommitCoordinator;
    FDWCEditorPreviewConsumerLifetime PreviewCommitLifetime;
    TSharedPtr<FDWCEditorRenderUploadQueue> RenderUploadQueue;
    FOnWetWrinkleSurfaceHitChanged OnSurfaceHitChanged;
    TWeakPtr<FWetWrinkleAuthoringController> AuthoringController;
    TSharedPtr<FAdvancedPreviewScene> PreviewScene;
    TUniquePtr<FDWCEditorInteractiveToolsHost> InputToolsHost;
    TSharedPtr<FWetWrinkleViewportClient> ViewportClient;
    TObjectPtr<USkeletalMeshComponent> PreviewMeshComponent = nullptr;
    TObjectPtr<UTexture2D> GeneratedNormalPreviewTexture = nullptr;
    int32 GeneratedNormalPreviewMaterialSlotIndex = INDEX_NONE;
    int32 GeneratedNormalPreviewUVChannelIndex = INDEX_NONE;
    bool bGeneratedNormalPreviewOverrideActive = false;
    TUniquePtr<FDWCEditorPreviewSession> PreviewSession;
    TUniquePtr<FDWCEditorPreviewOrchestrator> PreviewOrchestrator;
    TArray<FWetWrinkleAccumulatedPreviewState> AccumulatedPreviewStates;
    uint64 AccumulatedPreviewUseSerial = 0;
    FWetProceduralRidgeTransientPreviewState TransientProceduralPreviewState;
    FDWCEditorSpatialLease SpatialLease;
    FDWCEditorSpatialHandle SpatialHandle;
    uint64 PreviewMeshRefreshCount = 0;
    uint64 AccumulatedPreviewRebuildCount = 0;
    uint64 HitTriangleBuildCount = 0;
    int32 LastAppliedActivePreviewMaterialSlot = INDEX_NONE;
    bool bPreviewMaterialsNeedReapply = true;
    bool bPreviewSuspended = false;
    bool bShowBakedTransparency = true;
    FWetWrinkleBrushSettings BrushSettings;
    FWetWrinkleSurfaceHit CurrentSurfaceHit;
    FGuid SelectedProceduralStrokeGuid;
    int32 SelectedProceduralStrokePointIndex = INDEX_NONE;
    FGuid EditingProceduralStrokeGuid;
    TArray<FWetWrinkleSurfaceHit> TransientProceduralStrokeHits;
    bool bTransientProceduralStartJunction = false;
    bool bTransientProceduralEndJunction = false;
    bool bTransientProceduralPreviewBound = false;
    TOptional<FWetProceduralRidgeStroke> EditedProceduralStrokePreview;
    TOptional<FWetProceduralRidgeStroke> PendingTransientProceduralStroke;
    uint64 AccumulatedIncrementalCommitCount = 0;
    uint64 AccumulatedIncrementalFallbackCount = 0;
    uint64 TransientIncrementalCommitCount = 0;
};
