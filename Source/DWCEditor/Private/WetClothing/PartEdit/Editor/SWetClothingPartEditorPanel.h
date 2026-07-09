#pragma once

#include "AssetRegistry/AssetData.h"
#include "CoreMinimal.h"
#include "Styling/SlateTypes.h"
#include "WetClothing/Common/Editor/WetClothingAssetEditorTypes.h"
#include "DataAssets/WetClothingAsset.h"
#include "WetClothing/Common/Analysis/WetClothingAssetMeshAnalyzer.h"
#include "WetClothing/PartEdit/Partition/WetPartAutoPartitionTypes.h"
#include "WetClothing/Common/Widgets/SWetClothingAssetUVView.h"
#include "Widgets/SCompoundWidget.h"

class FAssetThumbnail;
class FAssetThumbnailPool;
class IDetailsView;
class STableViewBase;
class SWetClothingAssetViewport;
class SInlineEditableTextBlock;
class UTexture;
class UTexture2D;
struct FSlateBrush;

class SWetClothingPartEditorPanel : public SCompoundWidget
{
  public:
    SLATE_BEGIN_ARGS(SWetClothingPartEditorPanel) {}
    SLATE_ARGUMENT(UWetClothingAsset*, WetClothingAsset)
    SLATE_ARGUMENT(TSharedPtr<IDetailsView>, DetailsView)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

    void RefreshFromAsset();
    bool HasPendingVisualBakeTasks(FString* OutSummary = nullptr) const;
    bool BakeWetnessProfileMapsAndUpdateMaterials(FString& OutSummary, bool* OutHadWarnings = nullptr);
    bool SaveBakedWetnessAssets() const;

  private:
    using FMaterialSlotItemPtr = TSharedPtr<FWetClothingMaterialSlotItem>;
    using FTextureItemPtr = TSharedPtr<FWetClothingTextureItem>;
    using FUVChannelItemPtr = TSharedPtr<int32>;
    using FUVIslandItemPtr = TSharedPtr<FWetClothingAssetUVIsland>;
    using FUVSelectionToolItemPtr = TSharedPtr<FWetClothingUVSelectionToolItem>;
    using FUVDisplayModeItemPtr = TSharedPtr<EWetClothingAssetUVDisplayMode>;
    using FAutoPartitionColorModeItemPtr = TSharedPtr<EWetPartAutoPartitionColorMode>;
    using FWetPartEntryPtr = TSharedPtr<FWetClothingWetPartEntry>;

    void RefreshMaterialSlotItems();
    void RefreshMaterialTextures();
    void RefreshTextureToggleWidgets();
    void RefreshUVChannels();
    void RefreshUVIslandList();
    void RefreshUVView();
    void RefreshPreviewIslandHighlight();
    void RefreshWetPartList();
    void RefreshPreviewWetPartOverlay();
    void RefreshWetPartWidgets();

    void                                 EnsureDefaultWetPartForSelectedScope();
    int32                                GetSelectedUVChannelIndex() const;
    int32                                FindNextWetPartForSelectedScope() const;
    FWetClothingWetPartEntry*       FindMutableWetPartEntry(int32 WetPartID) const;
    const FWetClothingWetPartEntry* FindWetPartEntry(int32 WetPartID) const;
    const FWetClothingWetPartEntry* FindWetPartEntryForUVIsland(int32 UVIslandID) const;
    const FWetClothingWetPartEntry* FindEffectiveWetPartEntryForUVIsland(int32 UVIslandID) const;
    FWetPartEntryPtr                     FindWetPartItemByID(int32 WetPartID) const;
    FMaterialSlotItemPtr                  FindMaterialSlotItem(int32 MaterialSlotIndex) const;
    TSet<int32>                          GetUVIslandIDsForWetPart(int32 WetPartID) const;
    int32                                GetEffectiveWetPartForUVIsland(int32 UVIslandID) const;
    FLinearColor                         GetDefaultWetPartColor(int32 WetPartID) const;
    FString                              GetDefaultWetPartName(int32 WetPartID) const;
    FString                              GetWetPartDisplayName(const FWetClothingWetPartEntry& Entry) const;
    FString                              GetAssignedProfileLabel(const FWetClothingWetPartEntry& Entry) const;
    TMap<int32, int32>                   BuildUVIslandWetPartIDMap() const;
    TMap<int32, FLinearColor>            BuildUVIslandColorMap() const;
    TSet<int32>                          BuildHiddenUVIslandIDSet() const;

    void ApplyIslandSelection(const TArray<int32>& HitUVIslandIDs, bool bAppendSelection);
    void SetSelectedUVIslandIDs(const TSet<int32>& InSelectedUVIslandIDs, int32 InPrimarySelectedUVIslandID, bool bSyncListSelection = true);
    void SyncUVIslandListSelectionToState();
    void ResetIslandSelection();

    TSharedRef<ITableRow>     GenerateMaterialSlotRow(FMaterialSlotItemPtr Item, const TSharedRef<STableViewBase>& OwnerTable);
    void                      HandleMaterialSlotSelectionChanged(FMaterialSlotItemPtr Item, ESelectInfo::Type);
    FReply                    HandleWettableMaterialSlotClicked(int32 MaterialSlotIndex);
    void                      MarkSelectedMaterialSlotWettable();
    TSharedRef<SWidget>       GenerateTextureComboItem(FTextureItemPtr Item);
    void                      HandleTextureSelectionChanged(FTextureItemPtr Item, ESelectInfo::Type SelectInfo);
    TSharedRef<SWidget>       BuildTextureComboContent(FTextureItemPtr Item, float ThumbnailSize, bool bCompactLayout);
    FReply                    HandleApplyMaterialSetupClicked();
    bool                      IsApplyMaterialSetupEnabled() const;
    TSharedRef<SWidget>       GenerateUVChannelComboItem(FUVChannelItemPtr Item);
    void                      HandleUVChannelSelectionChanged(FUVChannelItemPtr Item, ESelectInfo::Type SelectInfo);
    TSharedRef<SWidget>       GenerateUVDisplayModeComboItem(FUVDisplayModeItemPtr Item);
    void                      HandleUVDisplayModeSelectionChanged(FUVDisplayModeItemPtr Item, ESelectInfo::Type SelectInfo);
    TSharedRef<ITableRow>     GenerateUVIslandRow(FUVIslandItemPtr Item, const TSharedRef<STableViewBase>& OwnerTable);
    void                      HandleUVIslandSelectionChanged(FUVIslandItemPtr Item, ESelectInfo::Type SelectInfo);
    void                      HandleUVIslandSelectionChangedFromUVView(const TArray<int32>& UVIslandIDs, EWetClothingAssetUVSelectionOp SelectionOp);
    void                      HandleUVIslandPickedFromPreview(int32 UVIslandID, bool bAppendSelection);
    TSharedRef<SWidget>       GenerateAutoPartitionColorModeComboItem(FAutoPartitionColorModeItemPtr Item) const;
    void                      HandleAutoPartitionColorModeSelectionChanged(FAutoPartitionColorModeItemPtr Item, ESelectInfo::Type SelectInfo);
    TSharedRef<ITableRow>     GenerateWetPartRow(FWetPartEntryPtr Item, const TSharedRef<STableViewBase>& OwnerTable);
    void                      HandleWetPartSelectionChanged(FWetPartEntryPtr Item, ESelectInfo::Type SelectInfo);
    void                      HandleWetPartItemDoubleClicked(FWetPartEntryPtr Item);
    void                      HandleWetPartNameCommitted(const FText& InText, ETextCommit::Type CommitType, FWetPartEntryPtr Item);
    FReply                    HandleWetPartColorClicked(FWetPartEntryPtr Item);
    void                      HandleWetPartColorCommitted(FLinearColor NewColor, FWetPartEntryPtr Item);
    FReply                    HandleToggleWetPartViewClicked(FWetPartEntryPtr Item);
    const FSlateBrush*        GetWetPartVisibilityBrush(FWetPartEntryPtr Item) const;
    void                      HandleWetnessProfilePicked(FWetPartEntryPtr Item, const FAssetData& ProfileAssetData);
    TSharedRef<SWidget>       GenerateAssignWetPartComboItem(FWetPartEntryPtr Item);
    void                      HandleAssignWetPartSelectionChanged(FWetPartEntryPtr Item, ESelectInfo::Type SelectInfo);
    FReply                    HandleAddWetPartClicked();
    FReply                    HandleRemoveWetPartClicked();
    bool                      IsWetPartRemoveEnabled() const;
    bool                      IsAutoPartitionEnabled() const;
    bool                      HasAutoPartitionDataToReplace() const;
    FReply                    HandleAutoPartitionClicked();
    UTexture2D*               ResolveAutoPartitionTexture() const;
    TMap<int32, FLinearColor> BuildAutoPartitionPreviewColorMap(const TArray<FWetPartAutoPartitionCluster>& Clusters) const;
    void                      ApplyAutoPartitionClusters(const TArray<FWetPartAutoPartitionCluster>& Clusters);
    FReply                    HandleAssignSelectedUVIslandToWetPartClicked();
    FReply                    HandleUVSelectionToolButtonClicked(FUVSelectionToolItemPtr Item);
    void                      SetCurrentUVSelectionTool(EWetClothingAssetUVSelectionTool InTool);
    const FSlateBrush*        GetUVSelectionToolBrush(FUVSelectionToolItemPtr Item) const;
    FSlateColor               GetUVSelectionToolIconColor(FUVSelectionToolItemPtr Item) const;
    FSlateColor               GetUVSelectionToolButtonColor(FUVSelectionToolItemPtr Item) const;

    FText                                          GetMaterialSlotCountText() const;
    FText                                          GetSelectedMaterialSlotText() const;
    FText                                          GetMaterialSlotStatusText(int32 MaterialSlotIndex) const;
    FText                                          GetSelectedTextureText() const;
    FText                                          GetWetnessProfileMapBakeSourceText() const;
    FText                                          GetWetnessProfileMapBakeSlotsText() const;
    FText                                          GetWetnessProfileMapBakeStatusText() const;
    FText                                          GetWetnessProfileMapBakeSettingsText() const;
    FText                                          GetSelectedUVChannelText() const;
    FText                                          GetSelectedUVDisplayModeText() const;
    float                                          GetUVViewBackgroundTextureOpacity() const;
    float                                          GetUVViewIslandLineOpacity() const;
    float                                          GetUVViewIslandLineThicknessScale() const;
    void                                           HandleUVViewBackgroundTextureOpacityChanged(float NewValue);
    void                                           HandleUVViewIslandLineOpacityChanged(float NewValue);
    void                                           HandleUVViewIslandLineThicknessScaleChanged(float NewValue);
    FText                                          GetUVIslandCountText() const;
    FText                                          GetSelectedUVIslandText() const;
    FText                                          GetUVStatusText() const;
    FText                                          GetWetPartSectionText() const;
    FText                                          GetAssignUVIslandToWetPartText() const;
    FText                                          GetSelectedAssignWetPartText() const;
    FSlateColor                                    GetSelectedAssignWetPartColor() const;
    FText                                          GetSelectedWetPartText() const;
    FText                                          GetWetnessProfileLibraryStatusText() const;
    FText                                          GetBlendModeText(FWetPartEntryPtr Item) const;
    FText                                          GetWetnessProfileButtonText(FWetPartEntryPtr Item) const;
    FText                                          GetAutoPartitionColorModeText() const;
    float                                          GetAutoPartitionTolerance() const;
    void                                           HandleAutoPartitionToleranceChanged(float InValue);
    float                                          GetSelectionLineThicknessScale() const;
    void                                           HandleSelectionLineThicknessChanged(float InValue);
    FReply                                         HandleFocusPreviewClicked();
    FReply                                         HandleSaveAssetClicked();
    FReply                                         HandleBakeAllMapsClicked();
    FReply                                         HandleBakeWrinkleNormalMapClicked();
    FReply                                         HandleBakeWrinkleMaskClicked();
    bool                                           IsWetnessProfileMapBakeSourceValid() const;
    bool                                           CanBakeAnyWetnessProfileMap() const;
    FReply                                         HandleBakeSelectedWetnessProfileMapClicked();
    FReply                                         HandleBakeAllWetnessProfileMapsClicked();
    UTexture*                                      ResolveSelectedMaterialTexture() const;
    UTexture*                                      ResolveTextureAddressTexture() const;
    void                                           SaveSelectedTexture();
    UTexture*                                      FindSavedTextureSelection(int32 MaterialSlotIndex, int32 UVChannelIndex) const;
    UTexture*                                      ResolveOrSaveTextureSelection(int32 MaterialSlotIndex, int32 UVChannelIndex);
    bool                                           HasSavedTextureSelection(int32 MaterialSlotIndex, int32 UVChannelIndex) const;
    void                                           SaveTextureSelection(int32 MaterialSlotIndex, int32 UVChannelIndex, UTexture* Texture);
    const FWetClothingBakedWetnessProfileMap* FindBakedWetnessProfileMap(UTexture* SourceTexture, int32 UVChannelIndex) const;
    void                                           CollectMaterialSlotsForWetnessProfileMap(UTexture* SourceTexture, int32 UVChannelIndex, TArray<int32>& OutMaterialSlotIndices) const;
    void                                           CollectWetnessProfileMapSourceTextures(int32 UVChannelIndex, TArray<UTexture*>& OutSourceTextures) const;

  private:
    TWeakObjectPtr<UWetClothingAsset>                  WetClothingAsset;
    TSharedPtr<IDetailsView>                           DetailsView;
    TSharedPtr<SWetClothingAssetViewport>              PreviewViewport;
    TArray<FMaterialSlotItemPtr>                       MaterialSlotItems;
    TSharedPtr<FAssetThumbnailPool>                    MaterialThumbnailPool;
    TArray<TSharedPtr<FAssetThumbnail>>                MaterialSlotThumbnails;
    TSharedPtr<class SListView<FMaterialSlotItemPtr>>  MaterialSlotListView;
    int32                                              SelectedMaterialSlotIndex = INDEX_NONE;
    TArray<FTextureItemPtr>                            TextureItems;
    TArray<TSharedPtr<FAssetThumbnail>>                TextureThumbnails;
    FTextureItemPtr                                    SelectedTextureItem;
    TSharedPtr<class SComboBox<FTextureItemPtr>>       TextureComboBox;
    TSharedPtr<class SBox>                             SelectedTextureComboContentBox;
    TSharedPtr<class SBox>                             TextureSelectionContainer;
    bool                                               bShowMaterialTextureInUVView = true;
    TArray<FUVChannelItemPtr>                          UVChannelItems;
    FUVChannelItemPtr                                  SelectedUVChannelItem;
    TSharedPtr<class SComboBox<FUVChannelItemPtr>>     UVChannelComboBox;
    TArray<FUVDisplayModeItemPtr>                      UVDisplayModeItems;
    FUVDisplayModeItemPtr                              SelectedUVDisplayModeItem;
    TSharedPtr<class SComboBox<FUVDisplayModeItemPtr>> UVDisplayModeComboBox;
    TArray<FUVIslandItemPtr>                           UVIslandItems;
    TSharedPtr<class SListView<FUVIslandItemPtr>>      UVIslandListView;
    int32                                              SelectedUVIslandID = INDEX_NONE;
    TSet<int32>                                        SelectedUVIslandIDs;
    bool                                               bSyncingUVIslandListSelection = false;
    TSharedPtr<SWetClothingAssetUVView>                UVView;
    EWetClothingAssetUVSelectionTool                   CurrentUVSelectionTool = EWetClothingAssetUVSelectionTool::Select;
    EWetClothingAssetUVDisplayMode                     CurrentUVDisplayMode = EWetClothingAssetUVDisplayMode::Normal;
    float                                              UVViewBackgroundTextureOpacity = 0.70f;
    float                                              UVViewIslandLineOpacity = 1.0f;
    float                                              UVViewIslandLineThicknessScale = 1.0f;
    TArray<FUVSelectionToolItemPtr>                    UVSelectionToolItems;
    FUVSelectionToolItemPtr                            SelectedUVSelectionToolItem;
    TArray<FAutoPartitionColorModeItemPtr>             AutoPartitionColorModeItems;
    FAutoPartitionColorModeItemPtr                     SelectedAutoPartitionColorModeItem;
    EWetPartAutoPartitionColorMode                     AutoPartitionColorMode = EWetPartAutoPartitionColorMode::DominantColor;
    FString                                            UVStatusMessage;
    TArray<FWetPartEntryPtr>                           CurrentWetPartItems;
    TSharedPtr<class SListView<FWetPartEntryPtr>>      WetPartListView;
    TSharedPtr<class SComboBox<FWetPartEntryPtr>>      AssignWetPartComboBox;
    TMap<int32, TWeakPtr<SInlineEditableTextBlock>>    WetPartInlineRenameWidgets;
    int32                                              SelectedWetPartID = INDEX_NONE;
    int32                                              SelectedAssignWetPartID = INDEX_NONE;
    float                                              AutoPartitionTolerancePercent = 20.0f;
};
