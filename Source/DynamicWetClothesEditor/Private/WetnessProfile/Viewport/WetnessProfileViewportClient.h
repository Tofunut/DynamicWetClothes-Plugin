#pragma once

#include "CoreMinimal.h"
#include "EditorViewportClient.h"

class FAdvancedPreviewScene;
class SWetnessProfileViewport;
class UStaticMeshComponent;

class FWetnessProfileViewportClient : public FEditorViewportClient
{
  public:
    FWetnessProfileViewportClient(FAdvancedPreviewScene* InPreviewScene, const TSharedRef<SWetnessProfileViewport>& InViewportWidget);

    virtual void Tick(float DeltaSeconds) override;

    void FocusOnPreviewMesh(const UStaticMeshComponent* InPreviewMeshComponent, bool bInstant = false);
    void SetPreviewMeshComponent(const UStaticMeshComponent* InPreviewMeshComponent);

  private:
    FAdvancedPreviewScene*                     PreviewScene = nullptr;
    TWeakObjectPtr<const UStaticMeshComponent> PreviewMeshComponent;
};
