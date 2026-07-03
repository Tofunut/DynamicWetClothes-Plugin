#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Analysis/WetClothingAssetMeshAnalyzer.h"
#include "EditorViewportClient.h"

class FAdvancedPreviewScene;
class FSceneView;
class SWetClothingAssetViewport;
class USkeletalMeshComponent;
class HHitProxy;

class FWetClothingAssetViewportClient : public FEditorViewportClient
{
  public:
    FWetClothingAssetViewportClient(
        FAdvancedPreviewScene*                       InPreviewScene,
        const TSharedRef<SWetClothingAssetViewport>& InViewportWidget);

    virtual void Tick(float DeltaSeconds) override;
    virtual void ProcessClick(FSceneView& View, HHitProxy* HitProxy, FKey Key, EInputEvent Event, uint32 HitX, uint32 HitY) override;

    void FocusOnPreviewMesh(const USkeletalMeshComponent* InPreviewMeshComponent, bool bInstant = false);
    void RequestFocusOnPreviewMeshNextTick(const USkeletalMeshComponent* InPreviewMeshComponent);
    void SetPreviewMeshComponent(const USkeletalMeshComponent* InPreviewMeshComponent);
    void SetPickableIslands(const TArray<FWetClothingAssetUVIsland>& InIslands);

  private:
    FAdvancedPreviewScene*                       PreviewScene = nullptr;
    TWeakPtr<SWetClothingAssetViewport>          ViewportWidget;
    TWeakObjectPtr<const USkeletalMeshComponent> PreviewMeshComponent;
    TWeakObjectPtr<const USkeletalMeshComponent> PendingFocusMeshComponent;
    bool                                         bFocusPreviewMeshOnNextTick = false;
    TArray<FWetClothingAssetUVIsland>            PickableIslands;
};
