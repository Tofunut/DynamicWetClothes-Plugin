// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "UObject/WeakObjectPtr.h"
#include "CoreMinimal.h"
#include "EditorViewportClient.h"
#include "WetClothing/Foundation/Cache/DWCEditorCacheStore.h"

class FAdvancedPreviewScene;
class FSceneView;
class SDWCPartViewport;
class USkeletalMeshComponent;
class HHitProxy;
struct FDWCEditorMemoryOwnerRecord;

class FDWCPartViewportClient : public FEditorViewportClient
{
  public:
    FDWCPartViewportClient(
        FAdvancedPreviewScene*              InPreviewScene,
        const TSharedRef<SDWCPartViewport>& InViewportWidget);

    virtual void Tick(float DeltaSeconds) override;
    virtual void ProcessClick(FSceneView& View, HHitProxy* HitProxy, FKey Key, EInputEvent Event, uint32 HitX, uint32 HitY) override;

    void FocusOnPreviewMesh(const USkeletalMeshComponent* InPreviewMeshComponent, bool bInstant = false);
    void RequestFocusOnPreviewMeshNextTick(const USkeletalMeshComponent* InPreviewMeshComponent);
    void SetPreviewMeshComponent(const USkeletalMeshComponent* InPreviewMeshComponent);
    void SetPickableTopology(FDWCEditorCacheLease&& InTopologyLease);
    void ClearPickableIslandCache();
    void SetPreviewPaused(bool bInPaused);
    void CollectMemoryDiagnostics(TArray<FDWCEditorMemoryOwnerRecord>& OutOwners) const;

  private:
    FAdvancedPreviewScene*                       PreviewScene = nullptr;
    TWeakPtr<SDWCPartViewport>                   ViewportWidget;
    TWeakObjectPtr<const USkeletalMeshComponent> PreviewMeshComponent;
    TWeakObjectPtr<const USkeletalMeshComponent> PendingFocusMeshComponent;
    bool                                         bFocusPreviewMeshOnNextTick = false;
    bool                                         bPreviewPaused = false;
    FDWCEditorCacheLease                         ActivePickTopologyLease;
};
