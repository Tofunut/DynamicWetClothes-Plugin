//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "EditorViewportClient.h"

class FAdvancedPreviewScene;
class SWetnessProfileViewport;
class UPrimitiveComponent;

class FWetnessProfileViewportClient : public FEditorViewportClient
{
  public:
    FWetnessProfileViewportClient(FAdvancedPreviewScene* InPreviewScene, const TSharedRef<SWetnessProfileViewport>& InViewportWidget);

    virtual void Tick(float DeltaSeconds) override;

    void FocusOnPreviewMesh(const UPrimitiveComponent* InPreviewMeshComponent, bool bInstant = false);
    void SetPreviewMeshComponent(const UPrimitiveComponent* InPreviewMeshComponent);

  private:
    FAdvancedPreviewScene*                     PreviewScene = nullptr;
    TWeakObjectPtr<const UPrimitiveComponent> PreviewMeshComponent;
};
