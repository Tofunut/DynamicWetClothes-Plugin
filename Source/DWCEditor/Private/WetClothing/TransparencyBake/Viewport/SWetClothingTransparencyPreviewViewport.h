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
    float RadiusUV = 0.025f;
    float Strength = 0.5f;
    float Falloff = 0.5f;
    float Spacing = 0.25f;
    float TargetAlpha = 1.0f;
    bool bEnabled = true;
};

struct FDWCTransparencySurfaceHit
{
    bool bHit = false;
    int32 MaterialSlotIndex = INDEX_NONE;
    int32 TriangleID = INDEX_NONE;
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
    FVector LocalPositions[3];
    FVector WorldPositions[3];
    FVector2D UVs[3];
    FVector WorldNormal = FVector::UpVector;
    FVector WorldTangent = FVector::ForwardVector;
    FBox WorldBounds = FBox(ForceInit);
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
    void SetPaintSettings(const FDWCTransparencyPaintSettings& InSettings);
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
    void SetAutoBakePreviewResult(TSharedPtr<const FDWCTransparencyAutoBakeResult> InResult);
    void ClearAutoBakePreviewResult();
    void SetTransparencyEditContext(
        const FGuid& InLayerGuid,
        int32 InMaterialSlotIndex,
        int32 InUVChannelIndex,
        EDWCTransparencyUVAddressMode InAddressMode);

  protected:
    virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;
    virtual TSharedPtr<SWidget> BuildViewportToolbar() override;

  private:
    void ClearPreview();
    void BuildTargetMeshPreview();
    void BuildFullBlueprintPreview();
    void ConfigurePreviewMeshComponent(USkeletalMeshComponent* MeshComponent);
    void ApplyRevealMaterials(USkeletalMeshComponent* MeshComponent);
    void ApplyWetnessPreview(USkeletalMeshComponent* MeshComponent);
    void ApplyTransparencyPreviewParameters();
    bool RebuildTransparencyPreviewTexture();
    bool RebuildWrinkleSuppressionBuffer();
    void RebuildHitTriangles();
    void EnsureBrushCursor();
    void RefreshBrushCursor();
    void ClearBrushCursor();
    bool RasterizeBrushSample(const FDWCTransparencyBrushStroke& Stroke, const FDWCTransparencyBrushSample& Sample, FIntRect* OutDirtyRect = nullptr);
    FIntRect ComputeCurrentHoverDirtyRect() const;
    void RefreshHoverPreviewRegion();
    void UpdatePreviewTextureRegion(const FIntRect& DirtyRect);
    void AppendPaintSample(const FVector2D& PositionUV);
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
    TObjectPtr<UTexture2D> TransparencyPreviewTexture = nullptr;
    TObjectPtr<UProceduralMeshComponent> BrushCursorComponent = nullptr;
    TSharedPtr<const FDWCTransparencyAutoBakeResult> AutoBakePreviewResult;
    TArray<uint8> WrinkleSuppressionBuffer;
    TArray<uint8> ManualPremultipliedBuffer;
    TArray<uint8> ManualWeightBuffer;
    TArray<FDWCTransparencyCachedHitTriangle> CachedHitTriangles;
    FDWCTransparencyPaintSettings PaintSettings;
    FDWCTransparencySurfaceHit CurrentSurfaceHit;
    FOnDWCTransparencyStrokesChanged OnStrokesChanged;
    TUniquePtr<FScopedTransaction> ActivePaintTransaction;
    FGuid ActiveStrokeGuid;
    FVector2D LastPointerUV = FVector2D::ZeroVector;
    float DistanceToNextStamp = 0.0f;
    FIntRect LastHoverDirtyRect;
    EWetClothingTransparencyPreviewMode PreviewMode = EWetClothingTransparencyPreviewMode::TargetMeshOnly;
    EDWCTransparencyVisualizationMode VisualizationMode = EDWCTransparencyVisualizationMode::Final;
    float WetnessPreviewPercent = 100.0f;
    float TransparencyPreviewStrength = 0.4f;
    float WrinkleSuppressionStrength = 0.6f;
    FGuid SelectedLayerGuid;
    int32 SelectedMaterialSlotIndex = INDEX_NONE;
    int32 SelectedUVChannelIndex = 0;
    EDWCTransparencyUVAddressMode SelectedUVAddressMode = EDWCTransparencyUVAddressMode::Clamp;
};
