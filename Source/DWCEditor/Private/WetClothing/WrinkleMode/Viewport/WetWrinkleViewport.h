#pragma once

#include "CoreMinimal.h"
#include "SEditorViewport.h"
#include "UObject/GCObject.h"
#include "WetClothing/Common/Analysis/WetClothingAssetMeshAnalyzer.h"
#include "WetWrinkleHitData.h"

class FAdvancedPreviewScene;
class FWetWrinkleViewportClient;
class SRichTextBlock;
class UMaterial;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UProceduralMeshComponent;
class USkeletalMesh;
class USkeletalMeshComponent;
class UTexture;
class UTexture2D;
class UWetClothingAsset;
struct FWetWrinklePatchPlacement;

enum class EWetWrinklePreviewMaterialStatus : uint8
{
    Uninitialized,
    Ready,
    Unsupported,
    Failed
};

struct FWetWrinkleProjectedSurface
{
    int32 MaterialSlotIndex = INDEX_NONE;
    int32 TriangleID = INDEX_NONE;
    FVector WorldPosition = FVector::ZeroVector;
    FVector WorldNormal = FVector::UpVector;
    FVector WorldTangent = FVector::ForwardVector;
    FVector WorldBitangent = FVector::RightVector;
};

struct FWetWrinklePreviewMaterialSlotState
{
    int32 MaterialSlotIndex = INDEX_NONE;
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
    bool bDirty = true;
};

class SWetWrinkleViewport : public SEditorViewport, public FGCObject
{
    friend class FWetWrinkleViewportClient;

  public:
    SLATE_BEGIN_ARGS(SWetWrinkleViewport) {}
    SLATE_ARGUMENT(UWetClothingAsset*, WetClothingAsset)
    SLATE_EVENT(FOnWetWrinkleSurfaceHitChanged, OnSurfaceHitChanged)
    SLATE_EVENT(FOnWetWrinklePaintStrokeStarted, OnPaintStrokeStarted)
    SLATE_EVENT(FOnWetWrinklePaintStampRequested, OnPaintStampRequested)
    SLATE_EVENT(FOnWetWrinklePaintStrokeEnded, OnPaintStrokeEnded)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    virtual ~SWetWrinkleViewport() override;

    virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
    virtual FString GetReferencerName() const override
    {
        return TEXT("SWetWrinkleViewport");
    }

    void RefreshPreviewMesh();
    void SetBrushSettings(const FWetWrinkleBrushSettings& InBrushSettings);
    void RefreshStoredStampOverlay(bool bRebuildAccumulatedPreview = true);
    void AppendAccumulatedPreviewStamp(const FWetWrinklePatchPlacement& Stamp);
    void SetSelectedStrokeGuid(const FGuid& InStrokeGuid);
    void PreviewBrushAtUV(int32 MaterialSlotIndex, int32 UVChannelIndex, const FVector2D& UV);
    void ClearExternalBrushPreview();
    bool TryBuildSurfaceHitAtUV(int32 MaterialSlotIndex, int32 UVChannelIndex, const FVector2D& UV, FWetWrinkleSurfaceHit& OutHit) const;
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
    void HandleSurfaceHitFromClient(const FWetWrinkleSurfaceHit& SurfaceHit);
    void BeginPaintStrokeFromClient(const FWetWrinkleSurfaceHit& SurfaceHit);
    void RequestPaintStampFromClient(const FWetWrinkleSurfaceHit& SurfaceHit);
    void EndPaintStrokeFromClient();
    void RefreshBrushCursor();
    void ClearBrushCursor();
    UMaterialInterface* ResolveCursorMaterial();
    float CalculateBrushCursorWorldRadius() const;
    FText GetViewportHintText() const;
    const UWetClothingAsset* ResolveSourceWetClothingAsset() const;
    UTexture* ResolveSourceTextureForMaterialSlot(int32 MaterialSlotIndex, int32 UVChannelIndex) const;
    void RebuildPreviewMaterialSlots();
    void ReleasePreviewMaterialSlots();
    void ApplyPreviewMaterialsToMesh();
    void MarkPreviewMaterialsNeedReapply();
    UMaterialInterface* ResolveDwcWetMaterialForSlot(int32 MaterialSlotIndex) const;
    UMaterialInterface* GetPreviewSourceMaterial(int32 MaterialSlotIndex) const;
    void ApplyPreviewWetVertexColors();
    void RefreshWrinklePreviewMaterials();
    bool EnsurePreviewMaterialForSlot(int32 MaterialSlotIndex);
    void ResetPreviewMaterialParameters(int32 MaterialSlotIndex);
    void ReleaseAccumulatedPreviewStates();
    void MarkAccumulatedPreviewStatesDirty();
    FWetWrinkleAccumulatedPreviewState* FindOrAddAccumulatedPreviewState(UTexture* SourceTexture, int32 MaterialSlotIndex, int32 UVChannelIndex);
    UTexture2D* ResolveAccumulatedPreviewTexture(UTexture* SourceTexture, int32 MaterialSlotIndex, int32 UVChannelIndex);
    bool RebuildAccumulatedPreviewTexture(FWetWrinkleAccumulatedPreviewState& PreviewState);
    int32 ResolveActivePreviewMaterialSlot() const;
    void FindProjectedSurfacesAtUV(int32 MaterialSlotIndex, int32 UVChannelIndex, const FVector2D& UV, TArray<FWetWrinkleProjectedSurface>& OutSurfaces) const;
    bool TryProjectUVToWorld(int32 MaterialSlotIndex, int32 UVChannelIndex, const FVector2D& UV, FVector& OutWorldPosition, FVector& OutWorldNormal, FVector& OutWorldTangent, FVector& OutWorldBitangent) const;

  private:
    TWeakObjectPtr<UWetClothingAsset> WetClothingAsset;
    FOnWetWrinkleSurfaceHitChanged OnSurfaceHitChanged;
    FOnWetWrinklePaintStrokeStarted OnPaintStrokeStarted;
    FOnWetWrinklePaintStampRequested OnPaintStampRequested;
    FOnWetWrinklePaintStrokeEnded OnPaintStrokeEnded;
    TSharedPtr<FAdvancedPreviewScene> PreviewScene;
    TSharedPtr<FWetWrinkleViewportClient> ViewportClient;
    TObjectPtr<USkeletalMeshComponent> PreviewMeshComponent = nullptr;
    TObjectPtr<UProceduralMeshComponent> BrushCursorComponent = nullptr;
    TObjectPtr<UMaterialInterface> CursorMaterial = nullptr;
    TArray<FWetWrinklePreviewMaterialSlotState> PreviewMaterialSlots;
    TArray<FWetWrinkleAccumulatedPreviewState> AccumulatedPreviewStates;
    TSharedPtr<SRichTextBlock> OverlayText;
    TArray<FWetClothingAssetUVTriangle> HitTriangles;
    int32 HitTriangleUVChannelIndex = INDEX_NONE;
    int32 LastAppliedActivePreviewMaterialSlot = INDEX_NONE;
    bool bPreviewMaterialsNeedReapply = true;
    FWetWrinkleBrushSettings BrushSettings;
    FWetWrinkleSurfaceHit CurrentSurfaceHit;
    FGuid SelectedStrokeGuid;
};
