#pragma once

#include "CoreMinimal.h"
#include "WetClothingProfile/Analysis/WetClothingProfileMeshAnalyzer.h"
#include "EditorViewportClient.h"

class FAdvancedPreviewScene;
class FSceneView;
class SWetClothingProfileViewport;
class USkeletalMeshComponent;
class HHitProxy;

class FWetClothingProfileViewportClient : public FEditorViewportClient
{
public:
	FWetClothingProfileViewportClient(
		FAdvancedPreviewScene* InPreviewScene,
		const TSharedRef<SWetClothingProfileViewport>& InViewportWidget);

	virtual void Tick(float DeltaSeconds) override;
	virtual void ProcessClick(FSceneView& View, HHitProxy* HitProxy, FKey Key, EInputEvent Event, uint32 HitX, uint32 HitY) override;

	void FocusOnPreviewMesh(const USkeletalMeshComponent* InPreviewMeshComponent, bool bInstant = false);
	void RequestFocusOnPreviewMeshNextTick(const USkeletalMeshComponent* InPreviewMeshComponent);
	void SetPreviewMeshComponent(const USkeletalMeshComponent* InPreviewMeshComponent);
	void SetPickableIslands(const TArray<FWetClothingProfileUVIsland>& InIslands);

private:
	FAdvancedPreviewScene* PreviewScene = nullptr;
	TWeakPtr<SWetClothingProfileViewport> ViewportWidget;
	TWeakObjectPtr<const USkeletalMeshComponent> PreviewMeshComponent;
	TWeakObjectPtr<const USkeletalMeshComponent> PendingFocusMeshComponent;
	bool bFocusPreviewMeshOnNextTick = false;
	TArray<FWetClothingProfileUVIsland> PickableIslands;
};
