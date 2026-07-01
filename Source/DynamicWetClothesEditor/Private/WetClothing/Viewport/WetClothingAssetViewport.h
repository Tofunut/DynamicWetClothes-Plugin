#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Analysis/WetClothingAssetMeshAnalyzer.h"
#include "SEditorViewport.h"
#include "UObject/GCObject.h"

class FAdvancedPreviewScene;
class FWetClothingAssetViewportClient;
class SRichTextBlock;
class UWetClothingAsset;
class UMaterialInterface;
class UProceduralMeshComponent;
class USkeletalMeshComponent;

DECLARE_DELEGATE_TwoParams(FOnWetClothingPreviewIslandPicked, int32 /*IslandID*/, bool /*bAppendSelection*/);

class SWetClothingAssetViewport : public SEditorViewport, public FGCObject
{
    friend class FWetClothingAssetViewportClient;

  public:
    SLATE_BEGIN_ARGS(SWetClothingAssetViewport) {}
    SLATE_ARGUMENT(UWetClothingAsset*, WetClothingAsset)
    SLATE_EVENT(FOnWetClothingPreviewIslandPicked, OnIslandPicked)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    virtual ~SWetClothingAssetViewport() override;

    virtual void    AddReferencedObjects(FReferenceCollector& Collector) override;
    virtual FString GetReferencerName() const override
    {
        return TEXT("SWetClothingAssetViewport");
    }

    void  RefreshPreviewMesh();
    void  SetHighlightedMaterialSlot(int32 SlotIndex);
    void  ClearMaterialSlotHighlight();
    void  SetSelectableIslands(const TArray<TSharedPtr<FWetClothingAssetUVIsland>>& InIslands);
    void  SetHighlightedIslandIDs(const TSet<int32>& InIslandIDs);
    void  ClearHighlightedIsland();
    void  SetWetPartIslandAssignments(const TMap<int32, int32>& InIslandToWetPartID, const TMap<int32, FLinearColor>& InIslandColors);
    void  ClearWetPartIslandColors();
    void  FocusOnPreviewMesh(bool bInstant = false);
    void  SetSelectionOverlayThicknessScale(float InThicknessScale);
    float GetSelectionOverlayThicknessScale() const { return SelectionOverlayThicknessScale; }

  protected:
    virtual TSharedRef<FEditorViewportClient> MakeEditorViewportClient() override;
    virtual TSharedPtr<SWidget>               BuildViewportToolbar() override;
    virtual void                              PopulateViewportOverlays(TSharedRef<SOverlay> Overlay) override;
    virtual void                              OnFocusViewportToSelection() override;

  private:
    void                HandleIslandPickedFromClient(int32 IslandID, bool bAppendSelection);
    void                RefreshWetPartOverlayMesh();
    void                RefreshSelectionOverlayMesh();
    void                CacheOriginalMaterials();
    void                RestoreOriginalMaterials();
    UMaterialInterface* ResolveWetPartOverlayMaterial();
    FText               GetViewportHintText() const;

  private:
    TWeakObjectPtr<UWetClothingAsset>           WetClothingAsset;
    FOnWetClothingPreviewIslandPicked             OnIslandPicked;
    TSharedPtr<FAdvancedPreviewScene>             PreviewScene;
    TSharedPtr<FWetClothingAssetViewportClient> ViewportClient;
    TObjectPtr<USkeletalMeshComponent>            PreviewMeshComponent = nullptr;
    TObjectPtr<UProceduralMeshComponent>          WetPartOverlayComponent = nullptr;
    TObjectPtr<UProceduralMeshComponent>          SelectionOverlayComponent = nullptr;
    TObjectPtr<UMaterialInterface>                WetPartOverlayMaterial = nullptr;
    TArray<TObjectPtr<UMaterialInterface>>        OriginalPreviewMaterials;
    TArray<FWetClothingAssetUVIsland>           CurrentSelectableIslands;
    TSet<int32>                                   CurrentHighlightedIslandIDs;
    TMap<int32, int32>                            CurrentWetPartIslandAssignments;
    TMap<int32, FLinearColor>                     CurrentWetPartIslandColors;
    int32                                         CurrentHighlightedMaterialSlot = INDEX_NONE;
    float                                         SelectionOverlayThicknessScale = 1.0f;
    TSharedPtr<SRichTextBlock>                    OverlayText;
};
