#pragma once

#include "CoreMinimal.h"
#include "SEditorViewport.h"
#include "UObject/GCObject.h"

class FAdvancedPreviewScene;
class FWetnessProfileViewportClient;
class UMaterial;
class UMaterialInstanceDynamic;
class UMaterialInstanceConstant;
class UMaterialInterface;
class UPrimitiveComponent;
class USkeletalMesh;
class USkeletalMeshComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UTexture2D;
class UTexture;
class UWetnessProfile;
class STextBlock;

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
    virtual void Tick(const FGeometry& AllottedGeometry, double InCurrentTime, float InDeltaTime) override;
    void FocusOnPreviewMesh(bool bInstant = false);

    void SetPreviewAbsorbedWater(float InAmount);
    float GetPreviewAbsorbedWater() const { return PreviewAbsorbedWater; }

    void SetPreviewSurfaceWater(float InAmount);
    float GetPreviewSurfaceWater() const { return PreviewSurfaceWater; }

    void SetPreviewDropletDetailSize(float InDropletDetailSize);
    float GetPreviewDropletDetailSize() const { return PreviewDropletDetailSize; }

    void SetPreviewAnimationEnabled(bool bInEnabled);
    void SetPreviewAnimationSpeed(float InSpeed);
    bool IsPreviewAnimationEnabled() const { return bPreviewAnimationEnabled; }
    float GetPreviewAnimationSpeed() const { return PreviewAnimationSpeed; }

    void SetPreviewSkeletalMeshOverride(USkeletalMesh* InPreviewMesh);
    void ClearPreviewSkeletalMeshOverride();
    void UseSpherePreview();
    USkeletalMesh* GetDisplayedPreviewSkeletalMesh() const;
    bool IsUsingPreviewMeshOverride() const { return bHasPreviewMeshOverride; }

  protected:
    virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;
    virtual void PopulateViewportOverlays(TSharedRef<SOverlay> Overlay) override;
    virtual void OnFocusViewportToSelection() override;

  private:
    void InitializePreviewComponents();
    void ApplyResolvedPreviewMesh(bool bFocus);
    USkeletalMesh* ResolveProfilePreviewSkeletalMesh() const;
    UPrimitiveComponent* GetActivePreviewComponent() const;
    void RebuildGeneratedSpherePreviewMaterial();
    void RebuildGeneratedPreviewMaterials(USkeletalMesh* SkeletalMesh);
    void RefreshPreviewMaterialParameters();
    void RefreshGeneratedPreviewMaterialParameters();
    void RefreshGeneratedPreviewAnimationTime();
    void UpdateRealtimeState();
    FText GetOverlayText() const;

    TWeakObjectPtr<UWetnessProfile> WetnessProfile;
    TSharedPtr<FAdvancedPreviewScene> PreviewScene;
    TSharedPtr<FWetnessProfileViewportClient> ViewportClient;
    TObjectPtr<UStaticMeshComponent> PreviewMeshComponent = nullptr;
    TObjectPtr<USkeletalMeshComponent> PreviewSkeletalMeshComponent = nullptr;
    TObjectPtr<UStaticMesh> PreviewSphereMesh = nullptr;
    TObjectPtr<USkeletalMesh> PreviewMeshOverride = nullptr;
    TObjectPtr<UMaterialInterface> PreviewBaseMaterial = nullptr;
    TObjectPtr<UMaterialInstanceDynamic> PreviewMaterialInstance = nullptr;
    TObjectPtr<USkeletalMesh> GeneratedPreviewMesh = nullptr;
    TArray<TObjectPtr<UMaterial>> GeneratedPreviewMaterials;
    TArray<TObjectPtr<UMaterialInstanceConstant>> GeneratedPreviewMaterialInstances;
    TArray<TObjectPtr<UMaterialInstanceDynamic>> GeneratedPreviewDynamicMaterials;
    int32 GeneratedPreviewMaterialSlotCount = 0;
    bool bGeneratedSpherePreviewMaterialValid = false;
    TObjectPtr<UTexture2D> PreviewWetnessMapTexture = nullptr;
    TObjectPtr<UTexture2D> PreviewWetPartDataTexture = nullptr;
    TObjectPtr<UTexture2D> PreviewSurfaceDropletTexture = nullptr;
    TObjectPtr<UTexture> PreviewDefaultNormalTexture = nullptr;
    TObjectPtr<UTexture> PreviewDefaultMaskTexture = nullptr;
    TSharedPtr<STextBlock> OverlayText;

    bool bHasPreviewMeshOverride = false;
    bool bPreviewAnimationEnabled = true;
    float PreviewAbsorbedWater = 0.5f;
    float PreviewSurfaceWater = 0.5f;
    float PreviewDropletDetailSize = 1.0f;
    float PreviewAnimationSpeed = 1.0f;
    float PreviewAnimationTime = 0.0f;
};
