#pragma once

#include "CoreMinimal.h"
#include "EditorViewportClient.h"
#include "WetWrinkleHitData.h"

class FAdvancedPreviewScene;
class FDWCEditorInteractiveToolsHost;
class FPrimitiveDrawInterface;
class FSceneView;
class HHitProxy;
class SWetWrinkleViewport;
class USkeletalMeshComponent;

class FWetWrinkleViewportClient : public FEditorViewportClient
{
  public:
    FWetWrinkleViewportClient(
        FAdvancedPreviewScene* InPreviewScene,
        const TSharedRef<SWetWrinkleViewport>& InViewportWidget,
        FDWCEditorInteractiveToolsHost* InInputToolsHost);

    virtual void Tick(float DeltaSeconds) override;
    virtual bool InputKey(const FInputKeyEventArgs& EventArgs) override;
    virtual void Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI) override;
    void FocusOnPreviewMesh(const USkeletalMeshComponent* InPreviewMeshComponent, bool bInstant = false);
    void RequestFocusOnPreviewMeshNextTick(const USkeletalMeshComponent* InPreviewMeshComponent);
    void SetPreviewMeshComponent(const USkeletalMeshComponent* InPreviewMeshComponent);

  private:
    FAdvancedPreviewScene* PreviewScene = nullptr;
    FDWCEditorInteractiveToolsHost* InputToolsHost = nullptr;
    TWeakPtr<SWetWrinkleViewport> ViewportWidget;
    TWeakObjectPtr<const USkeletalMeshComponent> PreviewMeshComponent;
    TWeakObjectPtr<const USkeletalMeshComponent> PendingFocusMeshComponent;
    bool bFocusPreviewMeshOnNextTick = false;
};
