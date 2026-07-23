#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetClothingWrinkleData.h"
#include "SEditorViewport.h"
#include "UObject/GCObject.h"
#include "WetClothing/Foundation/MeshAnalysis/WetClothingAssetMeshAnalyzer.h"
#include "WetWrinkleHitData.h"

class FAdvancedPreviewScene;
class FPrimitiveDrawInterface;
class FWetWrinkleViewportClient;
class SRichTextBlock;
class UMaterial;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class USkeletalMesh;
class USkeletalMeshComponent;
class UTexture;
class UTexture2D;
class UWetClothingAsset;
struct FWetWrinklePatchPlacement;
struct FWetProceduralRidgeStroke;
struct FWetProceduralRidgeStrokePoint;

enum class EWetWrinklePreviewMaterialStatus : uint8
{
    Uninitialized,
    Ready,
    Unsupported,
    Failed
};


struct FWetWrinkleCachedHitTriangle
{
    int32 MaterialSlotIndex = INDEX_NONE;
    int32 TriangleID = INDEX_NONE;
    int32 UVIslandID = INDEX_NONE;
    FVector LocalPositions[3] = { FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector };
    FVector WorldPositions[3] = { FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector };
    FVector2D UVs[3] = { FVector2D::ZeroVector, FVector2D::ZeroVector, FVector2D::ZeroVector };
    FVector WorldNormal = FVector::UpVector;
    FVector WorldTangent = FVector::ForwardVector;
    FVector WorldBitangent = FVector::RightVector;
    FVector LocalNormal = FVector::UpVector;
    FVector LocalTangent = FVector::ForwardVector;
    FVector LocalBitangent = FVector::RightVector;
    FBox WorldBounds = FBox(ForceInit);
    FBox2D UVBounds = FBox2D(ForceInit);
};

struct FWetWrinkleProjectedSurface
{
    int32 MaterialSlotIndex = INDEX_NONE;
    int32 TriangleID = INDEX_NONE;
    int32 UVIslandID = INDEX_NONE;
    FVector Barycentric = FVector(1.0, 0.0, 0.0);
    FVector WorldPosition = FVector::ZeroVector;
    FVector WorldNormal = FVector::UpVector;
    FVector WorldTangent = FVector::ForwardVector;
    FVector WorldBitangent = FVector::RightVector;
};

struct FWetWrinklePreviewMaterialSlotState
{
    int32 MaterialSlotIndex = INDEX_NONE;
    int32 PreviewUVChannelIndex = INDEX_NONE;
    TObjectPtr<UMaterialInterface> MeshOriginalMaterial = nullptr;
    TObjectPtr<UMaterialInterface> DwcWetMaterial = nullptr;
    TObjectPtr<UMaterialInterface> PreviewSourceMaterial = nullptr;
    TObjectPtr<UMaterial> TransientPreviewMaterial = nullptr;
    TObjectPtr<UMaterialInterface> TransientPreviewParent = nullptr;
    TObjectPtr<UMaterialInstanceDynamic> PreviewMID = nullptr;
    EWetWrinklePreviewMaterialStatus PreviewStatus = EWetWrinklePreviewMaterialStatus::Uninitialized;
    FString PreviewBuildError;
    bool bUsesDwcWetMaterial = false;
};

struct FWetWrinkleAccumulatedPreviewState
{
    TObjectPtr<UTexture> SourceTexture = nullptr;
    int32 MaterialSlotIndex = INDEX_NONE;
    int32 UVChannelIndex = INDEX_NONE;
    TObjectPtr<UTexture2D> AccumulatedNormalTexture = nullptr;
    FIntPoint TextureSize = FIntPoint::ZeroValue;
    FIntPoint WorkingTextureSize = FIntPoint::ZeroValue;
    TArray<FColor> Pixels;
    TArray<FColor> WorkingPixels;
    bool bDirty = true;
};

struct FWetWrinkleHitBVHNode
{
    FBox Bounds = FBox(ForceInit);
    int32 LeftChildIndex = INDEX_NONE;
    int32 RightChildIndex = INDEX_NONE;
    int32 FirstTriangleIndex = 0;
    int32 TriangleCount = 0;

    bool IsLeaf() const
    {
        return LeftChildIndex == INDEX_NONE && RightChildIndex == INDEX_NONE;
    }
};

struct FWetProceduralRidgeTransientPreviewState
{
    TObjectPtr<UTexture> SourceTexture = nullptr;
    int32 MaterialSlotIndex = INDEX_NONE;
    int32 UVChannelIndex = INDEX_NONE;
    TObjectPtr<UTexture2D> NormalTexture = nullptr;
    FIntPoint TextureSize = FIntPoint::ZeroValue;
    FIntPoint WorkingTextureSize = FIntPoint::ZeroValue;
    TArray<FColor> Pixels;
    TArray<FColor> WorkingPixels;
    TArray<FVector2D> PreviousPointUVs;
    uint8 PreviousShape = 0;
    bool bPreviousFlipFoldSide = false;
    float PreviousWidthUV = 0.0f;
    float PreviousStrength = 0.0f;
    float PreviousFalloff = 0.0f;
    float PreviousStartTaper = 0.0f;
    float PreviousEndTaper = 0.0f;
    uint8 PreviousStartEndpointMode = 0;
    uint8 PreviousEndEndpointMode = 0;
    FWetProceduralRidgeFlareSettings PreviousFlareSettings;
    FWetProceduralRidgeVariationSettings PreviousNaturalVariation;
};

class SWetWrinkleViewport : public SEditorViewport, public FGCObject
{
    friend class FWetWrinkleViewportClient;

  public:
    SLATE_BEGIN_ARGS(SWetWrinkleViewport) {}
    SLATE_ARGUMENT(UWetClothingAsset*, WetClothingAsset)
    SLATE_ARGUMENT(bool, UseDefaultPreviewMaterial)
    SLATE_ARGUMENT(bool, UseOriginalMeshMaterialForPreview)
    SLATE_EVENT(FOnWetWrinkleSurfaceHitChanged, OnSurfaceHitChanged)
    SLATE_EVENT(FOnWetWrinklePaintStrokeStarted, OnPaintStrokeStarted)
    SLATE_EVENT(FOnWetWrinklePaintStampRequested, OnPaintStampRequested)
    SLATE_EVENT(FOnWetWrinklePaintStrokeEnded, OnPaintStrokeEnded)
    SLATE_EVENT(FOnWetWrinklePaintStrokeCanceled, OnPaintStrokeCanceled)
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
    void SetBrushSettings(const FWetWrinkleBrushSettings& InBrushSettings);
    void RefreshStoredStampOverlay(bool bRebuildAccumulatedPreview = true);
    void AppendAccumulatedPreviewStamp(const FWetWrinklePatchPlacement& Stamp);
    void AppendAccumulatedPreviewProceduralStroke(const FWetProceduralRidgeStroke& Stroke);
    void SetGeneratedNormalPreviewTexture(int32 MaterialSlotIndex, int32 UVChannelIndex, UTexture2D* GeneratedNormalTexture);
    void ClearGeneratedNormalPreviewTexture();
    void SetSelectedStrokeGuid(const FGuid& InStrokeGuid);
    void SetSelectedProceduralStrokeGuid(const FGuid& InStrokeGuid);
    void SetSelectedProceduralStrokePointIndex(int32 InPointIndex);
    void SetTransientProceduralStroke(
        const TArray<FWetWrinkleSurfaceHit>& SurfaceHits,
        bool bStartJunction = false,
        bool bEndJunction = false);
    void PreviewEditedProceduralStroke(const FWetProceduralRidgeStroke& Stroke);
    void SetEditingProceduralStrokeGuid(const FGuid& InStrokeGuid);
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
    void ClearTransientProceduralStroke();
    void PreviewBrushAtUV(int32 MaterialSlotIndex, int32 UVChannelIndex, const FVector2D& UV);
    void ClearExternalBrushPreview();
    bool TryBuildSurfaceHitAtUV(int32 MaterialSlotIndex, int32 UVChannelIndex, const FVector2D& UV, FWetWrinkleSurfaceHit& OutHit) const;
    bool TryBuildSurfaceHitAtUVNearWorldPosition(int32 MaterialSlotIndex, int32 UVChannelIndex, const FVector2D& UV, const FVector& ReferenceWorldPosition, FWetWrinkleSurfaceHit& OutHit) const;
    bool TraceSurface(const FVector& RayOrigin, const FVector& RayDirection, FWetWrinkleSurfaceHit& OutHit) const;
    void FocusOnPreviewMesh(bool bInstant = false);

  protected:
    virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;
    virtual TSharedPtr<SWidget> BuildViewportToolbar() override;
    virtual void PopulateViewportOverlays(TSharedRef<SOverlay> Overlay) override;
    virtual void OnFocusViewportToSelection() override;

  private:
    USkeletalMesh* ResolveTargetMesh() const;
    void ApplyMaterialSlotVisibility();
    void RebuildHitTriangles();
    void RebuildHitTriangleAccelerationStructures();
    void FlushTransientProceduralPreviewUpload();
    void HandleSurfaceHitFromClient(const FWetWrinkleSurfaceHit& SurfaceHit);
    void BeginPaintStrokeFromClient(const FWetWrinkleSurfaceHit& SurfaceHit);
    void RequestPaintStampFromClient(const FWetWrinkleSurfaceHit& SurfaceHit);
    void EndPaintStrokeFromClient();
    void CancelPaintStrokeFromClient();
    void RefreshBrushCursor();
    void ClearBrushCursor();
    void DrawBrushCursor(FPrimitiveDrawInterface* PDI) const;
    void RefreshWrinklePreviewHoverParameters();
    float CalculateBrushCursorWorldRadius() const;
    FText GetViewportHintText() const;
    const UWetClothingAsset* ResolveSourceWetClothingAsset() const;
    UTexture* ResolveSourceTextureForMaterialSlot(int32 MaterialSlotIndex, int32 UVChannelIndex) const;
    bool ArePreviewMaterialSlotsCurrent() const;
    void RebuildPreviewMaterialSlots();
    void ReleasePreviewMaterialSlots();
    void ApplyPreviewMaterialsToMesh();
    void MarkPreviewMaterialsNeedReapply();
    UMaterialInterface* ResolveDwcWetMaterialForSlot(int32 MaterialSlotIndex) const;
    UMaterialInterface* GetPreviewSourceMaterial(int32 MaterialSlotIndex) const;
    UTexture2D* ResolveWetnessProfileMapForSlot(int32 MaterialSlotIndex) const;
    void RefreshWrinklePreviewMaterials();
    bool EnsurePreviewMaterialForSlot(int32 MaterialSlotIndex);
    void ResetPreviewMaterialParameters(int32 MaterialSlotIndex);
    void ReleaseAccumulatedPreviewStates();
    void MarkAccumulatedPreviewStatesDirty();
    FWetWrinkleAccumulatedPreviewState* FindOrAddAccumulatedPreviewState(UTexture* SourceTexture, int32 MaterialSlotIndex, int32 UVChannelIndex);
    UTexture2D* ResolveAccumulatedPreviewTexture(UTexture* SourceTexture, int32 MaterialSlotIndex, int32 UVChannelIndex);
    bool RebuildAccumulatedPreviewTexture(FWetWrinkleAccumulatedPreviewState& PreviewState);
    void ReleaseTransientProceduralPreviewState();
    bool EnsureTransientProceduralPreviewState(int32 MaterialSlotIndex, int32 UVChannelIndex);
    bool UpdateTransientProceduralPreview(const FWetProceduralRidgeStroke& Stroke);
    int32 ResolveActivePreviewMaterialSlot() const;
    void FindProjectedSurfacesAtUV(int32 MaterialSlotIndex, int32 UVChannelIndex, const FVector2D& UV, TArray<FWetWrinkleProjectedSurface>& OutSurfaces) const;
    bool TryProjectUVToWorld(int32 MaterialSlotIndex, int32 UVChannelIndex, const FVector2D& UV, FVector& OutWorldPosition, FVector& OutWorldNormal, FVector& OutWorldTangent, FVector& OutWorldBitangent) const;
    void DrawProceduralStrokeGuides(FPrimitiveDrawInterface* PDI) const;

  private:
    TWeakObjectPtr<UWetClothingAsset> WetClothingAsset;
    FOnWetWrinkleSurfaceHitChanged OnSurfaceHitChanged;
    FOnWetWrinklePaintStrokeStarted OnPaintStrokeStarted;
    FOnWetWrinklePaintStampRequested OnPaintStampRequested;
    FOnWetWrinklePaintStrokeEnded OnPaintStrokeEnded;
    FOnWetWrinklePaintStrokeCanceled OnPaintStrokeCanceled;
    TSharedPtr<FAdvancedPreviewScene> PreviewScene;
    TSharedPtr<FWetWrinkleViewportClient> ViewportClient;
    TObjectPtr<USkeletalMeshComponent> PreviewMeshComponent = nullptr;
    TObjectPtr<UTexture2D> GeneratedNormalPreviewTexture = nullptr;
    int32 GeneratedNormalPreviewMaterialSlotIndex = INDEX_NONE;
    int32 GeneratedNormalPreviewUVChannelIndex = INDEX_NONE;
    bool bGeneratedNormalPreviewOverrideActive = false;
    TArray<FWetWrinklePreviewMaterialSlotState> PreviewMaterialSlots;
    TArray<FWetWrinkleAccumulatedPreviewState> AccumulatedPreviewStates;
    FWetProceduralRidgeTransientPreviewState TransientProceduralPreviewState;
    TSharedPtr<SRichTextBlock> OverlayText;
    TArray<FWetWrinkleCachedHitTriangle> CachedHitTriangles;
    TMap<uint64, int32> CachedHitTriangleLookup;
    TArray<int32> HitBVHTriangleIndices;
    TArray<FWetWrinkleHitBVHNode> HitBVHNodes;
    TArray<TArray<int32>> UVTriangleGrid;
    int32 HitTriangleUVChannelIndex = INDEX_NONE;
    int32 LastAppliedActivePreviewMaterialSlot = INDEX_NONE;
    int32 LastHoverPreviewMaterialSlotIndex = INDEX_NONE;
    bool bPreviewMaterialsNeedReapply = true;
    bool bUseDefaultPreviewMaterial = false;
    bool bUseOriginalMeshMaterialForPreview = false;
    FWetWrinkleBrushSettings BrushSettings;
    FWetWrinkleSurfaceHit CurrentSurfaceHit;
    FGuid SelectedStrokeGuid;
    FGuid SelectedProceduralStrokeGuid;
    int32 SelectedProceduralStrokePointIndex = INDEX_NONE;
    FGuid EditingProceduralStrokeGuid;
    TArray<FWetWrinkleSurfaceHit> TransientProceduralStrokeHits;
    bool bTransientProceduralStartJunction = false;
    bool bTransientProceduralEndJunction = false;
    bool bTransientProceduralPreviewBound = false;
    TOptional<FWetProceduralRidgeStroke> PendingTransientProceduralStroke;
    FIntRect PendingTransientProceduralUploadRect;
    bool bHasPendingTransientProceduralUpload = false;
};
