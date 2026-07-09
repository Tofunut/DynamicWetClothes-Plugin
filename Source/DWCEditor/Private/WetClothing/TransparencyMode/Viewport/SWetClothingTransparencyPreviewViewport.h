#pragma once

#include "CoreMinimal.h"
#include "SEditorViewport.h"
#include "UObject/GCObject.h"

class AActor;
class FAdvancedPreviewScene;
class FEditorViewportClient;
class UMaterialInstanceDynamic;
class USkeletalMeshComponent;
class UWetClothingAsset;

enum class EWetClothingTransparencyPreviewMode : uint8
{
    TargetMeshOnly,
    FullBlueprint
};

class SWetClothingTransparencyPreviewViewport : public SEditorViewport, public FGCObject
{
  public:
    SLATE_BEGIN_ARGS(SWetClothingTransparencyPreviewViewport) {}
    SLATE_ARGUMENT(UWetClothingAsset*, WetClothingAsset)
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
    EWetClothingTransparencyPreviewMode PreviewMode = EWetClothingTransparencyPreviewMode::TargetMeshOnly;
    float WetnessPreviewPercent = 100.0f;
};
