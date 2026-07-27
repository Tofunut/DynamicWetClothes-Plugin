#pragma once

#include "CoreMinimal.h"
#include "SEditorViewport.h"
#include "UObject/GCObject.h"

class FAdvancedPreviewScene;
class FWetnessProfileViewportClient;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UStaticMeshComponent;
class UTexture;
class UWetnessProfile;
class SRichTextBlock;

class SWetnessProfileViewport : public SEditorViewport, public FGCObject
{
  public:
    SLATE_BEGIN_ARGS(SWetnessProfileViewport) {}
    SLATE_ARGUMENT(UWetnessProfile*, WetnessProfile)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    virtual ~SWetnessProfileViewport() override;

    virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
    virtual FString GetReferencerName() const override
    {
        return TEXT("SWetnessProfileViewport");
    }

    /** Updates only MID parameters and invalidates the viewport. */
    void RefreshFromProfile();
    void FocusOnPreviewMesh(bool bInstant = false);

    void SetPreviewAbsorbedWater(float InAmount);
    float GetPreviewAbsorbedWater() const { return PreviewAbsorbedWater; }

    void SetPreviewSurfaceWater(float InAmount);
    float GetPreviewSurfaceWater() const { return PreviewSurfaceWater; }

  protected:
    virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;
    virtual void PopulateViewportOverlays(TSharedRef<SOverlay> Overlay) override;
    virtual void OnFocusViewportToSelection() override;

  private:
    void InitializePreviewComponents();
    void RefreshPreviewMaterialParameters();
    FText GetOverlayText() const;

    TWeakObjectPtr<UWetnessProfile> WetnessProfile;
    TSharedPtr<FAdvancedPreviewScene> PreviewScene;
    TSharedPtr<FWetnessProfileViewportClient> ViewportClient;
    TObjectPtr<UStaticMeshComponent> PreviewMeshComponent = nullptr;
    TObjectPtr<UMaterialInterface> PreviewBaseMaterial = nullptr;
    TObjectPtr<UMaterialInstanceDynamic> PreviewMaterialInstance = nullptr;
    TObjectPtr<UTexture> PreviewDefaultNormalTexture = nullptr;
    TObjectPtr<UTexture> PreviewDefaultMaskTexture = nullptr;
    TSharedPtr<SRichTextBlock> OverlayText;

    float PreviewAbsorbedWater = 0.5f;
    float PreviewSurfaceWater = 0.5f;
};
