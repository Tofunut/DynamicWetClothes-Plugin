#pragma once

#include "CoreMinimal.h"
#include "SEditorViewport.h"
#include "UObject/GCObject.h"
#include "WetClothing/Analysis/WetClothingAssetMeshAnalyzer.h"
#include "WetWrinkleHitData.h"

class FAdvancedPreviewScene;
class FWetWrinkleAssetViewportClient;
class SRichTextBlock;
class UMaterialInterface;
class UProceduralMeshComponent;
class USkeletalMesh;
class USkeletalMeshComponent;
class UWetWrinkleAsset;

class SWetWrinkleAssetViewport : public SEditorViewport, public FGCObject
{
    friend class FWetWrinkleAssetViewportClient;

  public:
    SLATE_BEGIN_ARGS(SWetWrinkleAssetViewport) {}
    SLATE_ARGUMENT(UWetWrinkleAsset*, WetWrinkleAsset)
    SLATE_EVENT(FOnWetWrinkleSurfaceHitChanged, OnSurfaceHitChanged)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    virtual ~SWetWrinkleAssetViewport() override;

    virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
    virtual FString GetReferencerName() const override
    {
        return TEXT("SWetWrinkleAssetViewport");
    }

    void RefreshPreviewMesh();
    void SetBrushSettings(const FWetWrinkleBrushSettings& InBrushSettings);
    bool TraceSurface(const FVector& RayOrigin, const FVector& RayDirection, FWetWrinkleSurfaceHit& OutHit) const;
    void FocusOnPreviewMesh(bool bInstant = false);

  protected:
    virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;
    virtual TSharedPtr<SWidget> BuildViewportToolbar() override;
    virtual void PopulateViewportOverlays(TSharedRef<SOverlay> Overlay) override;
    virtual void OnFocusViewportToSelection() override;

  private:
    USkeletalMesh* ResolveTargetMesh() const;
    void RebuildHitTriangles();
    void HandleSurfaceHitFromClient(const FWetWrinkleSurfaceHit& SurfaceHit);
    void RefreshBrushCursor();
    void ClearBrushCursor();
    UMaterialInterface* ResolveCursorMaterial();
    FText GetViewportHintText() const;
    float CalculateBrushCursorWorldRadius() const;

  private:
    TWeakObjectPtr<UWetWrinkleAsset> WetWrinkleAsset;
    FOnWetWrinkleSurfaceHitChanged OnSurfaceHitChanged;
    TSharedPtr<FAdvancedPreviewScene> PreviewScene;
    TSharedPtr<FWetWrinkleAssetViewportClient> ViewportClient;
    TObjectPtr<USkeletalMeshComponent> PreviewMeshComponent = nullptr;
    TObjectPtr<UProceduralMeshComponent> BrushCursorComponent = nullptr;
    TObjectPtr<UMaterialInterface> CursorMaterial = nullptr;
    TSharedPtr<SRichTextBlock> OverlayText;
    TArray<FWetClothingAssetUVTriangle> HitTriangles;
    FWetWrinkleBrushSettings BrushSettings;
    FWetWrinkleSurfaceHit CurrentSurfaceHit;
};
