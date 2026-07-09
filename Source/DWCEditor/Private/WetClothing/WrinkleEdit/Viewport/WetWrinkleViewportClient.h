#pragma once

#include "CoreMinimal.h"
#include "EditorViewportClient.h"
#include "WetWrinkleHitData.h"

class FAdvancedPreviewScene;
class FSceneView;
class HHitProxy;
class SWetWrinkleViewport;
class USkeletalMeshComponent;

class FWetWrinkleViewportClient : public FEditorViewportClient
{
  public:
    FWetWrinkleViewportClient(
        FAdvancedPreviewScene* InPreviewScene,
        const TSharedRef<SWetWrinkleViewport>& InViewportWidget);

    virtual void Tick(float DeltaSeconds) override;
    virtual bool InputKey(const FInputKeyEventArgs& EventArgs) override;
    virtual void MouseMove(FViewport* InViewport, int32 X, int32 Y) override;
    virtual void CapturedMouseMove(FViewport* InViewport, int32 X, int32 Y) override;
    virtual void ProcessClick(FSceneView& View, HHitProxy* HitProxy, FKey Key, EInputEvent Event, uint32 HitX, uint32 HitY) override;
    void FocusOnPreviewMesh(const USkeletalMeshComponent* InPreviewMeshComponent, bool bInstant = false);
    void RequestFocusOnPreviewMeshNextTick(const USkeletalMeshComponent* InPreviewMeshComponent);
    void SetPreviewMeshComponent(const USkeletalMeshComponent* InPreviewMeshComponent);

  private:
    void UpdateSurfaceHitUnderCursor();
    bool TraceSurfaceUnderCursor(FWetWrinkleSurfaceHit& OutSurfaceHit);
    void ClearSurfaceHit();

  private:
    FAdvancedPreviewScene* PreviewScene = nullptr;
    TWeakPtr<SWetWrinkleViewport> ViewportWidget;
    TWeakObjectPtr<const USkeletalMeshComponent> PreviewMeshComponent;
    TWeakObjectPtr<const USkeletalMeshComponent> PendingFocusMeshComponent;
    bool bFocusPreviewMeshOnNextTick = false;
    bool bHasCurrentSurfaceHit = false;
    bool bIsPainting = false;
};
