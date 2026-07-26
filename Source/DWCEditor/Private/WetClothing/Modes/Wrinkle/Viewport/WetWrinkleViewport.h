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
    FVector3f LocalPositions[3] = { FVector3f::ZeroVector, FVector3f::ZeroVector, FVector3f::ZeroVector };
    FVector2f UVs[3] = { FVector2f::ZeroVector, FVector2f::ZeroVector, FVector2f::ZeroVector };
    FVector3f LocalNormal = FVector3f(0.0f, 0.0f, 1.0f);
    FVector3f LocalTangent = FVector3f(1.0f, 0.0f, 0.0f);
    FVector3f LocalBitangent = FVector3f(0.0f, 1.0f, 0.0f);
    FBox3f LocalBounds = FBox3f(ForceInit);
    FBox2f UVBounds = FBox2f(ForceInit);
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
    uint64 LastUsedSerial = 0;
};

struct FWetWrinkleHitBVHNode
{
    FBox3f Bounds = FBox3f(ForceInit);
    int32 LeftChildIndex = INDEX_NONE;
    int32 RightChildIndex = INDEX_NONE;
    int32 FirstTriangleIndex = 0;
    int32 TriangleCount = 0;

    bool IsLeaf() const
    {
        return LeftChildIndex == INDEX_NONE && RightChildIndex == INDEX_NONE;
    }
};

struct FWetWrinkleHitCacheKey
{
    const USkeletalMesh* Mesh = nullptr;
    const void* LODRenderDataIdentity = nullptr;
    int32 LODIndex = 0;
    int32 UVChannelIndex = INDEX_NONE;
    int32 MaterialSlotIndex = INDEX_NONE;
    FString TopologySignature;

    bool operator==(const FWetWrinkleHitCacheKey& Other) const
    {
        return Mesh == Other.Mesh &&
               LODRenderDataIdentity == Other.LODRenderDataIdentity &&
               LODIndex == Other.LODIndex &&
               UVChannelIndex == Other.UVChannelIndex &&
               MaterialSlotIndex == Other.MaterialSlotIndex &&
               TopologySignature == Other.TopologySignature;
    }

    friend uint32 GetTypeHash(const FWetWrinkleHitCacheKey& Key)
    {
        uint32 Hash = GetTypeHash(Key.Mesh);
        Hash = HashCombine(Hash, GetTypeHash(Key.LODRenderDataIdentity));
        Hash = HashCombine(Hash, GetTypeHash(Key.LODIndex));
        Hash = HashCombine(Hash, GetTypeHash(Key.UVChannelIndex));
        Hash = HashCombine(Hash, GetTypeHash(Key.MaterialSlotIndex));
        return HashCombine(Hash, GetTypeHash(Key.TopologySignature));
    }
};

struct FWetWrinkleHitCacheEntry
{
    TArray<FWetWrinkleCachedHitTriangle> Triangles;
    TMap<uint64, int32> TriangleLookup;
    TArray<int32> BVHTriangleIndices;
    TArray<FWetWrinkleHitBVHNode> BVHNodes;
    TArray<TArray<int32>> UVTriangleGrid;
    int32 UVChannelIndex = INDEX_NONE;
    uint64 LastUsedSerial = 0;
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
    void SynchronizeBrushSettings(const FWetWrinkleBrushSettings& InBrushSettings);
    void SetBrushTopology(int32 MaterialSlotIndex, int32 UVChannelIndex);
    void UpdateBrushPreviewSettings(const FWetWrinkleBrushSettings& InBrushSettings);
    void SetPreviewWetness(float PreviewWetness);
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
    TOptional<FWetWrinkleHitCacheKey> MakeHitCacheKey(
        const USkeletalMesh* Mesh,
        int32 LODIndex,
        int32 UVChannelIndex,
        int32 MaterialSlotIndex) const;
    bool RestoreHitCache(const FWetWrinkleHitCacheKey& Key);
    void StoreActiveHitCache();
    void ClearActiveHitCache();
    void ClearAllHitCaches();
    void PruneInactiveHitCaches();
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
    void RefreshWrinklePreviewWetnessParameter();
    void RefreshWrinklePreviewAccumulatedParameters();
    void RefreshWrinklePreviewTransientParameters();
    float CalculateBrushCursorWorldRadius() const;
    FText GetViewportHintText() const;
    const UWetClothingAsset* ResolveSourceWetClothingAsset() const;
    UTexture* ResolveSourceTextureForMaterialSlot(int32 MaterialSlotIndex) const;
    bool ArePreviewMaterialSlotsCurrent() const;
    void RebuildPreviewMaterialSlots();
    void ReleasePreviewMaterialSlots();
    void ApplyPreviewMaterialsToMesh();
    void MarkPreviewMaterialsNeedReapply();
    UMaterialInterface* ResolveDwcWetMaterialForSlot(int32 MaterialSlotIndex) const;
    UMaterialInterface* GetPreviewSourceMaterial(int32 MaterialSlotIndex) const;
    void RefreshWrinklePreviewMaterials();
    bool EnsurePreviewMaterialForSlot(int32 MaterialSlotIndex);
    void ResetPreviewMaterialParameters(int32 MaterialSlotIndex);
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
    uint64 AccumulatedPreviewUseSerial = 0;
    FWetProceduralRidgeTransientPreviewState TransientProceduralPreviewState;
    TSharedPtr<SRichTextBlock> OverlayText;
    TArray<FWetWrinkleCachedHitTriangle> CachedHitTriangles;
    TMap<uint64, int32> CachedHitTriangleLookup;
    TArray<int32> HitBVHTriangleIndices;
    TArray<FWetWrinkleHitBVHNode> HitBVHNodes;
    TArray<TArray<int32>> UVTriangleGrid;
    int32 HitTriangleUVChannelIndex = INDEX_NONE;
    TOptional<FWetWrinkleHitCacheKey> ActiveHitCacheKey;
    TMap<FWetWrinkleHitCacheKey, FWetWrinkleHitCacheEntry> InactiveHitCaches;
    uint64 HitCacheUseSerial = 0;
    int32 LastAppliedActivePreviewMaterialSlot = INDEX_NONE;
    int32 LastHoverPreviewMaterialSlotIndex = INDEX_NONE;
    bool bPreviewMaterialsNeedReapply = true;
    bool bUseDefaultPreviewMaterial = false;
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
    FIntRect PendingTransientProceduralUploadRect;
    bool bHasPendingTransientProceduralUpload = false;
};
