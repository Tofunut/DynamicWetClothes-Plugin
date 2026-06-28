#pragma once

#include "CoreMinimal.h"
#include "WetClothingProfile/Analysis/WetClothingProfileMeshAnalyzer.h"
#include "SEditorViewport.h"
#include "UObject/GCObject.h"

class FAdvancedPreviewScene;
class FWetClothingProfileViewportClient;
class SRichTextBlock;
class UWetClothingProfile;
class UMaterialInterface;
class UProceduralMeshComponent;
class USkeletalMeshComponent;

DECLARE_DELEGATE_TwoParams(FOnWetClothingPreviewIslandPicked, int32 /*IslandID*/, bool /*bAppendSelection*/);

class SWetClothingProfileViewport : public SEditorViewport, public FGCObject
{
	friend class FWetClothingProfileViewportClient;

public:
	SLATE_BEGIN_ARGS(SWetClothingProfileViewport) {}
		SLATE_ARGUMENT(UWetClothingProfile*, WetClothingProfile)
		SLATE_EVENT(FOnWetClothingPreviewIslandPicked, OnIslandPicked)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SWetClothingProfileViewport() override;

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override
	{
		return TEXT("SWetClothingProfileViewport");
	}

	void RefreshPreviewMesh();
	void SetHighlightedMaterialSlot(int32 SlotIndex);
	void ClearMaterialSlotHighlight();
	void SetSelectableIslands(const TArray<TSharedPtr<FWetClothingProfileUVIsland>>& InIslands);
	void SetHighlightedIslandIDs(const TSet<int32>& InIslandIDs);
	void ClearHighlightedIsland();
	void SetWetPartIslandAssignments(const TMap<int32, int32>& InIslandToWetPartID, const TMap<int32, FLinearColor>& InIslandColors);
	void ClearWetPartIslandColors();
	void FocusOnPreviewMesh(bool bInstant = false);
	void SetSelectionOverlayThicknessScale(float InThicknessScale);
	float GetSelectionOverlayThicknessScale() const { return SelectionOverlayThicknessScale; }

protected:
	virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;
	virtual void PopulateViewportOverlays(TSharedRef<SOverlay> Overlay) override;
	virtual void OnFocusViewportToSelection() override;

private:
	void HandleIslandPickedFromClient(int32 IslandID, bool bAppendSelection);
	void RefreshWetPartOverlayMesh();
	void RefreshSelectionOverlayMesh();
	void CacheOriginalMaterials();
	void RestoreOriginalMaterials();
	UMaterialInterface* ResolveWetPartOverlayMaterial();
	FText GetViewportHintText() const;

private:
	TWeakObjectPtr<UWetClothingProfile> WetClothingProfile;
	FOnWetClothingPreviewIslandPicked OnIslandPicked;
	TSharedPtr<FAdvancedPreviewScene> PreviewScene;
	TSharedPtr<FWetClothingProfileViewportClient> ViewportClient;
	TObjectPtr<USkeletalMeshComponent> PreviewMeshComponent = nullptr;
	TObjectPtr<UProceduralMeshComponent> WetPartOverlayComponent = nullptr;
	TObjectPtr<UProceduralMeshComponent> SelectionOverlayComponent = nullptr;
	TObjectPtr<UMaterialInterface> WetPartOverlayMaterial = nullptr;
	TArray<TObjectPtr<UMaterialInterface>> OriginalPreviewMaterials;
	TArray<FWetClothingProfileUVIsland> CurrentSelectableIslands;
	TSet<int32> CurrentHighlightedIslandIDs;
	TMap<int32, int32> CurrentWetPartIslandAssignments;
	TMap<int32, FLinearColor> CurrentWetPartIslandColors;
	int32 CurrentHighlightedMaterialSlot = INDEX_NONE;
	float SelectionOverlayThicknessScale = 1.0f;
	TSharedPtr<SRichTextBlock> OverlayText;
};
