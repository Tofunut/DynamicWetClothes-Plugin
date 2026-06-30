#pragma once

#include "AssetRegistry/AssetData.h"
#include "CoreMinimal.h"
#include "Styling/SlateTypes.h"
#include "WetClothing/Editor/WetClothingAssetEditorTypes.h"
#include "WetClothingAsset.h"
#include "WetClothing/Analysis/WetClothingAssetMeshAnalyzer.h"
#include "WetClothing/Widgets/SWetClothingAssetUVView.h"
#include "Widgets/SCompoundWidget.h"

class FAssetThumbnail;
class FAssetThumbnailPool;
class IDetailsView;
class STableViewBase;
class SWetClothingAssetViewport;
class SInlineEditableTextBlock;
class UTexture;
struct FSlateBrush;

class SWetClothingAssetEditorPanel : public SCompoundWidget
{
  public:
    SLATE_BEGIN_ARGS(SWetClothingAssetEditorPanel) {}
    SLATE_ARGUMENT(UWetClothingAsset*, WetClothingAsset)
    SLATE_ARGUMENT(TSharedPtr<IDetailsView>, DetailsView)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

    void RefreshFromAsset();

  private:
    using FMaterialSlotItemPtr = TSharedPtr<FWetClothingMaterialSlotItem>;
    using FTextureItemPtr = TSharedPtr<FWetClothingTextureItem>;
    using FUVChannelItemPtr = TSharedPtr<int32>;
    using FUVIslandItemPtr = TSharedPtr<FWetClothingAssetUVIsland>;
    using FUVSelectionToolItemPtr = TSharedPtr<FWetClothingUVSelectionToolItem>;
    using FUVDisplayModeItemPtr = TSharedPtr<EWetClothingAssetUVDisplayMode>;
    using FWetPartEntryPtr = TSharedPtr<FWetClothingAssetWetPartEntry>;
    using FWetnessProfileAssetItemPtr = TSharedPtr<FWetnessProfileAssetItem>;

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
    void RefreshAvailableWetnessProfiles();

    void                                   EnsureDefaultWetPartForSelectedScope();
    int32                                  GetSelectedUVChannelIndex() const;
    int32                                  FindNextWetPartForSelectedScope() const;
    FWetClothingAssetWetPartEntry*       FindMutableWetPartEntry(int32 WetPartID) const;
    const FWetClothingAssetWetPartEntry* FindWetPartEntry(int32 WetPartID) const;
    const FWetClothingAssetWetPartEntry* FindWetPartEntryForIsland(int32 IslandID) const;
    const FWetClothingAssetWetPartEntry* FindEffectiveWetPartEntryForIsland(int32 IslandID) const;
    FWetPartEntryPtr                       FindWetPartItemByID(int32 WetPartID) const;
    TSet<int32>                            GetIslandIDsForWetPart(int32 WetPartID) const;
    int32                                  GetEffectiveWetPartForIsland(int32 IslandID) const;
    FLinearColor                           GetDefaultWetPartColor(int32 WetPartID) const;
    FString                                GetDefaultWetPartName(int32 WetPartID) const;
    FString                                GetWetPartDisplayName(const FWetClothingAssetWetPartEntry& Entry) const;
    FString                                GetAssignedProfileLabel(const FWetClothingAssetWetPartEntry& Entry) const;
    TMap<int32, int32>                     BuildIslandWetPartIDMap() const;
    TMap<int32, FLinearColor>              BuildIslandColorMap() const;
    TArray<FString>                        GetProfileSearchPaths() const;

    void ApplyIslandSelection(const TArray<int32>& HitIslandIDs, bool bAppendSelection);
    void SetSelectedIslandIDs(const TSet<int32>& InSelectedIslandIDs, int32 InPrimarySelectedIslandID, bool bSyncListSelection = true);
    void SyncUVIslandListSelectionToState();
    void ResetIslandSelection();

    TSharedRef<ITableRow> GenerateMaterialSlotRow(FMaterialSlotItemPtr Item, const TSharedRef<STableViewBase>& OwnerTable);
    void                  HandleMaterialSlotSelectionChanged(FMaterialSlotItemPtr Item, ESelectInfo::Type SelectInfo);
    TSharedRef<SWidget>   GenerateTextureComboItem(FTextureItemPtr Item);
    void                  HandleTextureSelectionChanged(FTextureItemPtr Item, ESelectInfo::Type SelectInfo);
    TSharedRef<SWidget>   BuildTextureComboContent(FTextureItemPtr Item, float ThumbnailSize, bool bCompactLayout);
    TSharedRef<SWidget>   BuildProfileMapBakePanel();
    FReply                HandleApplyMaterialSetupClicked();
    bool                  IsApplyMaterialSetupEnabled() const;
    TSharedRef<SWidget>   GenerateUVChannelComboItem(FUVChannelItemPtr Item);
    void                  HandleUVChannelSelectionChanged(FUVChannelItemPtr Item, ESelectInfo::Type SelectInfo);
    TSharedRef<SWidget>   GenerateUVDisplayModeComboItem(FUVDisplayModeItemPtr Item);
    void                  HandleUVDisplayModeSelectionChanged(FUVDisplayModeItemPtr Item, ESelectInfo::Type SelectInfo);
    TSharedRef<ITableRow> GenerateUVIslandRow(FUVIslandItemPtr Item, const TSharedRef<STableViewBase>& OwnerTable);
    void                  HandleUVIslandSelectionChanged(FUVIslandItemPtr Item, ESelectInfo::Type SelectInfo);
    void                  HandleUVIslandSelectionChangedFromUVView(const TArray<int32>& IslandIDs, EWetClothingAssetUVSelectionOp SelectionOp);
    void                  HandleUVIslandPickedFromPreview(int32 IslandID, bool bAppendSelection);
    TSharedRef<ITableRow> GenerateWetPartRow(FWetPartEntryPtr Item, const TSharedRef<STableViewBase>& OwnerTable);
    void                  HandleWetPartSelectionChanged(FWetPartEntryPtr Item, ESelectInfo::Type SelectInfo);
    void                  HandleWetPartItemDoubleClicked(FWetPartEntryPtr Item);
    void                  HandleWetPartNameCommitted(const FText& InText, ETextCommit::Type CommitType, FWetPartEntryPtr Item);
    FReply                HandleWetPartColorClicked(FWetPartEntryPtr Item);
    void                  HandleWetPartColorCommitted(FLinearColor NewColor, FWetPartEntryPtr Item);
    FReply                HandleToggleWetPartViewClicked(FWetPartEntryPtr Item);
    const FSlateBrush*    GetWetPartVisibilityBrush(FWetPartEntryPtr Item) const;
    TSharedRef<SWidget>   BuildWetnessProfilePickerMenu(FWetPartEntryPtr Item);
    void                  HandleWetnessProfilePicked(FWetPartEntryPtr Item, FWetnessProfileAssetItemPtr ProfileItem);
    FReply                HandleAddProfileSearchPathClicked();
    TSharedRef<SWidget>   GenerateAssignWetPartComboItem(FWetPartEntryPtr Item);
    void                  HandleAssignWetPartSelectionChanged(FWetPartEntryPtr Item, ESelectInfo::Type SelectInfo);
    FReply                HandleAddWetPartClicked();
    FReply                HandleRemoveWetPartClicked();
    bool                  IsWetPartRemoveEnabled() const;
    bool                  IsAutoPartitionEnabled() const;
    bool                  HasAutoPartitionDataToReplace() const;
    FReply                HandleAutoPartitionClicked();
    FReply                HandleAssignSelectedIslandToWetPartClicked();
    FReply                HandleUVSelectionToolButtonClicked(FUVSelectionToolItemPtr Item);
    void                  SetCurrentUVSelectionTool(EWetClothingAssetUVSelectionTool InTool);
    const FSlateBrush*    GetUVSelectionToolBrush(FUVSelectionToolItemPtr Item) const;
    FSlateColor           GetUVSelectionToolIconColor(FUVSelectionToolItemPtr Item) const;
    FSlateColor           GetUVSelectionToolButtonColor(FUVSelectionToolItemPtr Item) const;

    FText       GetMaterialSlotCountText() const;
    FText       GetSelectedMaterialSlotText() const;
    FText       GetSelectedTextureText() const;
    FText       GetProfileMapBakeSourceText() const;
    FText       GetProfileMapBakeSlotsText() const;
    FText       GetProfileMapBakeStatusText() const;
    FText       GetProfileMapBakeSettingsText() const;
    FText       GetSelectedUVChannelText() const;
    FText       GetSelectedUVDisplayModeText() const;
    FText       GetUVIslandCountText() const;
    FText       GetSelectedUVIslandText() const;
    FText       GetUVStatusText() const;
    FText       GetWetPartSectionText() const;
    FText       GetAssignIslandToWetPartText() const;
    FText       GetSelectedAssignWetPartText() const;
    FSlateColor GetSelectedAssignWetPartColor() const;
    FText       GetSelectedWetPartText() const;
    FText       GetWetnessProfileLibraryStatusText() const;
    FText       GetBlendModeText(FWetPartEntryPtr Item) const;
    FText       GetWetnessProfileButtonText(FWetPartEntryPtr Item) const;
    float       GetAutoPartitionTolerance() const;
    void        HandleAutoPartitionToleranceChanged(float InValue);
    float       GetSelectionLineThicknessScale() const;
    void        HandleSelectionLineThicknessChanged(float InValue);
    FReply      HandleFocusPreviewClicked();
    FReply      HandleSaveAssetClicked();
    bool        IsProfileMapBakeSourceValid() const;
    bool        CanBakeAnyProfileMap() const;
    FReply      HandleBakeSelectedProfileMapClicked();
    FReply      HandleBakeAllProfileMapsClicked();
    UTexture*   ResolveSelectedMaterialTexture() const;
    void        SaveSelectedTexture();
    const FWetClothingAssetBakedProfileMap* FindBakedProfileMap(UTexture* SourceTexture, int32 UVChannelIndex) const;
    void                                      CollectMaterialSlotsForProfileMap(UTexture* SourceTexture, int32 UVChannelIndex, TArray<int32>& OutMaterialSlotIndices) const;
    void                                      CollectProfileMapSourceTextures(int32 UVChannelIndex, TArray<UTexture*>& OutSourceTextures) const;

  private:
    TWeakObjectPtr<UWetClothingAsset>                WetClothingAsset;
    TSharedPtr<IDetailsView>                           DetailsView;
    TSharedPtr<SWetClothingAssetViewport>            PreviewViewport;
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
    TSharedPtr<SWetClothingAssetUVView>              UVView;
    EWetClothingAssetUVSelectionTool                 CurrentUVSelectionTool = EWetClothingAssetUVSelectionTool::Select;
    EWetClothingAssetUVDisplayMode                   CurrentUVDisplayMode = EWetClothingAssetUVDisplayMode::Normal;
    TArray<FUVSelectionToolItemPtr>                    UVSelectionToolItems;
    FUVSelectionToolItemPtr                            SelectedUVSelectionToolItem;
    FString                                            UVStatusMessage;
    TArray<FWetPartEntryPtr>                           CurrentWetPartItems;
    TArray<FWetnessProfileAssetItemPtr>                AvailableWetnessProfileItems;
    TSharedPtr<class SListView<FWetPartEntryPtr>>      WetPartListView;
    TSharedPtr<class SComboBox<FWetPartEntryPtr>>      AssignWetPartComboBox;
    TMap<int32, TWeakPtr<SInlineEditableTextBlock>>    WetPartInlineRenameWidgets;
    int32                                              SelectedWetPartID = INDEX_NONE;
    int32                                              SelectedAssignWetPartID = INDEX_NONE;
    float                                              AutoPartitionTolerancePercent = 17.2f;
};
