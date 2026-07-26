#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetClothingTransparencyData.h"
#include "SEditorViewport.h"
#include "UObject/GCObject.h"

class AActor;
class FAdvancedPreviewScene;
class FEditorViewportClient;
class FScopedTransaction;
class UMaterial;
class UMaterialInterface;
class UProceduralMeshComponent;
class UMaterialInstanceDynamic;
class UTexture2D;
class USkeletalMeshComponent;
class UWetClothingAsset;
struct FDWCTransparencyAutoBakeResult;

struct FDWCTransparencyPaintSettings
{
    EDWCTransparencyBrushMode Mode = EDWCTransparencyBrushMode::Apply;
    EDWCTransparencyRevealColorBrushMode RevealColorMode = EDWCTransparencyRevealColorBrushMode::Paint;
    float RadiusUV = 0.0677f;
    float Strength = 0.5f;
    float Falloff = 0.5f;
    float Spacing = 0.25f;
    float TargetAlpha = 1.0f;
    bool bEnabled = true;
    bool bRevealColorPaint = false;
    FLinearColor RevealColor = FLinearColor::White;
};

struct FDWCTransparencySurfaceHit
{
    bool bHit = false;
    int32 MaterialSlotIndex = INDEX_NONE;
    int32 TriangleID = INDEX_NONE;
    int32 UVIslandID = INDEX_NONE;
    FVector WorldPosition = FVector::ZeroVector;
    FVector WorldNormal = FVector::UpVector;
    FVector WorldTangent = FVector::ForwardVector;
    FVector2D UV = FVector2D::ZeroVector;
    double DistanceSq = TNumericLimits<double>::Max();
};

struct FDWCTransparencyCachedHitTriangle
{
    int32 MaterialSlotIndex = INDEX_NONE;
    int32 TriangleID = INDEX_NONE;
    int32 UVIslandID = INDEX_NONE;
    FVector LocalPositions[3];
    FVector WorldPositions[3];
    FVector2D UVs[3];
    FVector WorldNormal = FVector::UpVector;
    FVector WorldTangent = FVector::ForwardVector;
    FBox WorldBounds = FBox(ForceInit);
};

/** World-space acceleration node for the selected target slot hit cache. */
struct FDWCTransparencyHitBVHNode
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

DECLARE_DELEGATE(FOnDWCTransparencyStrokesChanged);

enum class EWetClothingTransparencyPreviewMode : uint8
{
    TargetMeshOnly,
    FullBlueprint
};

enum class EDWCTransparencyVisualizationMode : uint8
{
    Final,
    InnerColor,
    AutoAlpha,
    WrinkleSeparation,
    ValidHit,
    HitDistance,
    RayConfidence,
    SourcePriority
};

class SWetClothingTransparencyPreviewViewport : public SEditorViewport, public FGCObject
{
  public:
    SLATE_BEGIN_ARGS(SWetClothingTransparencyPreviewViewport) {}
    SLATE_ARGUMENT(UWetClothingAsset*, WetClothingAsset)
    SLATE_EVENT(FOnDWCTransparencyStrokesChanged, OnStrokesChanged)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    virtual ~SWetClothingTransparencyPreviewViewport() override;

    virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
    virtual FString GetReferencerName() const override { return TEXT("SWetClothingTransparencyPreviewViewport"); }

    void RefreshPreview();
    void FocusOnPreviewMesh(bool bInstant = false);
    void SetPreviewMode(EWetClothingTransparencyPreviewMode NewMode);
    EWetClothingTransparencyPreviewMode GetPreviewMode() const { return PreviewMode; }
    void SetWetnessPreviewPercent(float InPercent);
    float GetWetnessPreviewPercent() const { return WetnessPreviewPercent; }
    void SetTransparencyPreviewStrength(float InStrength);
    float GetTransparencyPreviewStrength() const { return TransparencyPreviewStrength; }
    void SetWrinkleSuppressionStrength(float InStrength);
    void RefreshWrinkleSuppressionPreview();
    void RefreshOuterEdgeFeatherPreview();
    void SetPaintSettings(const FDWCTransparencyPaintSettings& InSettings);
    void SetTransparencyPaintingEnabled(bool bEnabled);
    void SetRevealColorPaintingEnabled(bool bEnabled);
    void RebuildManualOverridesFromStrokes();
    void RefreshManualPreviewFromStrokes();
    bool TraceSurface(const FVector& RayOrigin, const FVector& RayDirection, FDWCTransparencySurfaceHit& OutHit) const;
    bool CanPaint() const;
    void HandleSurfaceHitFromClient(const FDWCTransparencySurfaceHit& SurfaceHit);
    void BeginPaintStrokeFromClient(const FDWCTransparencySurfaceHit& SurfaceHit);
    void RequestPaintStampFromClient(const FDWCTransparencySurfaceHit& SurfaceHit);
    void EndPaintStrokeFromClient();
    void SetVisualizationMode(EDWCTransparencyVisualizationMode InMode);
    EDWCTransparencyVisualizationMode GetVisualizationMode() const { return VisualizationMode; }
    void SetAutoBakePreviewResult(TSharedPtr<FDWCTransparencyAutoBakeResult> InResult);
    void ClearAutoBakePreviewResult();
    void SetTransparencyEditContext(
        const FGuid& InLayerGuid,
        int32 InMaterialSlotIndex,
        int32 InUVChannelIndex,
        EDWCTransparencyUVAddressMode InAddressMode);
    void FlushPendingPreviewTextureUpdates();

  protected:
    virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;
    virtual TSharedPtr<SWidget> BuildViewportToolbar() override;

  private:
    void ClearPreview();
    void BuildTargetMeshPreview();
    void BuildFullBlueprintPreview();
    void ConfigurePreviewMeshComponent(USkeletalMeshComponent* MeshComponent);
    void ApplyPreviewMaterials(USkeletalMeshComponent* MeshComponent);
    void ApplyRevealColorPaintTargetVisibility();
    void RefreshExistingFullBlueprintPreviewMaterials(int32 PreviousMaterialSlotIndex);
    void ApplyWetnessPreview();
    UMaterialInstanceDynamic* GetOrBuildSelectedPreviewMID(UMaterialInterface* SourceMaterial);
    void ApplyTransparencyPreviewParameters();
    void DisableTransparencyPreviewParameters(UMaterialInstanceDynamic* MID) const;
    bool RebuildTransparencyPreviewTexture();
    bool RebuildWrinkleSuppressionBuffer();
    bool UpdateWrinkleSuppressionPreviewTexture();
    bool CanUseDynamicFinalPreviewComposition() const;
    bool UsesFinalAlphaPreview() const;
    bool UsesWrinkleSuppressionPreview() const;
    void RefreshDeferredFinalPreviewBuffers();
    void InvalidateWrinkleSuppressionSourceCache();
    bool RebuildOuterEdgeFeatherBuffer();
    bool EnsureManualOverrideBuffers();
    void ReleaseSmoothBrushScratch();
    void RebuildHitTriangles();
    void RebuildHitTriangleAccelerationStructures();
    void EnsureBrushCursor();
    void RefreshBrushCursor();
    void ClearBrushCursor();
    bool RasterizeBrushSample(const FDWCTransparencyBrushStroke& Stroke, const FDWCTransparencyBrushSample& Sample, FIntRect* OutDirtyRect = nullptr);
    bool RasterizeRevealColorSample(const FDWCTransparencyRevealColorStroke& Stroke, const FDWCTransparencyBrushSample& Sample, FIntRect* OutDirtyRect = nullptr);
    FIntRect ComputeCurrentHoverDirtyRect() const;
    void RefreshHoverPreviewRegion();
    void UpdatePreviewTextureRegion(const FIntRect& DirtyRect);
    void UploadPreviewTextureRegion(const FIntRect& DirtyRect);
    void AppendPaintSample(const FVector2D& PositionUV, int32 UVIslandID);
    FWetClothingTransparencyLayerData* GetSelectedLayer();
    float GetStoredEditedAlpha(int32 PixelIndex) const;
    float ApplyHoverToEditedAlpha(int32 PixelIndex, float EditedAlpha) const;
    bool BuildVisualizationPixels(TArray<FColor>& OutPixels) const;
    FColor BuildVisualizationPixel(int32 PixelIndex) const;
    void InvalidatePreviewViewport();
    USkeletalMeshComponent* FindFocusMeshComponent() const;

  private:
    TWeakObjectPtr<UWetClothingAsset> WetClothingAsset;
    TSharedPtr<FAdvancedPreviewScene> PreviewScene;
    TSharedPtr<FEditorViewportClient> ViewportClient;
    TObjectPtr<USkeletalMeshComponent> TargetMeshPreviewComponent = nullptr;
    TObjectPtr<AActor> PreviewActor = nullptr;
    TArray<TObjectPtr<USkeletalMeshComponent>> PreviewMeshComponents;
    TArray<TObjectPtr<UMaterialInstanceDynamic>> PreviewMIDs;
    TArray<TObjectPtr<UMaterial>> TransparencyPreviewBaseMaterials;
    TArray<TObjectPtr<UMaterialInterface>> TransparencyPreviewMaterialParents;
    TObjectPtr<UMaterialInterface> CachedPreviewSourceMaterial = nullptr;
    TObjectPtr<UMaterial> CachedPreviewBaseMaterial = nullptr;
    TObjectPtr<UMaterialInterface> CachedPreviewMaterialParent = nullptr;
    TObjectPtr<UMaterialInstanceDynamic> CachedPreviewMID = nullptr;
    TObjectPtr<UMaterialInstanceDynamic> ActiveTransparencyPreviewMID = nullptr;
    int32 CachedPreviewMaterialSlotIndex = INDEX_NONE;
    int32 CachedPreviewUVChannelIndex = INDEX_NONE;
    int32 CachedPreviewMaterialGraphVersion = INDEX_NONE;
    bool bActiveTransparencyPreviewEnabled = false;
    TObjectPtr<UTexture2D> TransparencyPreviewTexture = nullptr;
    TObjectPtr<UTexture2D> WrinkleSuppressionPreviewTexture = nullptr;
    TObjectPtr<UProceduralMeshComponent> BrushCursorComponent = nullptr;
    TSharedPtr<FDWCTransparencyAutoBakeResult> AutoBakePreviewResult;
    TArray<uint8> WrinkleSuppressionBuffer;
    TObjectPtr<UTexture2D> CachedWrinkleSuppressionMaskTexture = nullptr;
    FGuid CachedWrinkleSuppressionBakeGuid;
    FIntPoint CachedWrinkleSuppressionResolution = FIntPoint::ZeroValue;
    TArray<uint16> CachedWrinkleSuppressionCoverageBuffer;
    TArray<uint8> OuterEdgeFeatherBuffer;
    TArray<uint8> ManualPremultipliedBuffer;
    TArray<uint8> ManualWeightBuffer;
    TArray<uint8> SmoothBrushPremultipliedScratch;
    TArray<uint8> SmoothBrushWeightScratch;
    TSharedPtr<TArray<FColor>, ESPMode::ThreadSafe> PreviewVisualizationPixels;
    TArray<FDWCTransparencyCachedHitTriangle> CachedHitTriangles;
    TArray<int32> HitBVHTriangleIndices;
    TArray<FDWCTransparencyHitBVHNode> HitBVHNodes;
    FDWCTransparencyPaintSettings PaintSettings;
    FDWCTransparencySurfaceHit CurrentSurfaceHit;
    FOnDWCTransparencyStrokesChanged OnStrokesChanged;
    TUniquePtr<FScopedTransaction> ActivePaintTransaction;
    FGuid ActiveStrokeGuid;
    FVector2D LastPointerUV = FVector2D::ZeroVector;
    int32 LastPointerUVIslandID = INDEX_NONE;
    float DistanceToNextStamp = 0.0f;
    FIntRect LastHoverDirtyRect;
    TArray<FIntRect> PendingPreviewDirtyRects;
    EWetClothingTransparencyPreviewMode PreviewMode = EWetClothingTransparencyPreviewMode::TargetMeshOnly;
    EDWCTransparencyVisualizationMode VisualizationMode = EDWCTransparencyVisualizationMode::Final;
    float WetnessPreviewPercent = 100.0f;
    float TransparencyPreviewStrength = 0.4f;
    float WrinkleSuppressionStrength = 0.6f;
    bool bWrinkleSuppressionPreviewDirty = false;
    bool bOuterEdgeFeatherPreviewDirty = false;
    bool bTransparencyPaintingEnabled = false;
    bool bRevealColorPaintingEnabled = false;
    bool bActiveRevealColorPaint = false;
    FGuid SelectedLayerGuid;
    int32 SelectedMaterialSlotIndex = INDEX_NONE;
    int32 SelectedUVChannelIndex = 0;
    EDWCTransparencyUVAddressMode SelectedUVAddressMode = EDWCTransparencyUVAddressMode::Clamp;
};
