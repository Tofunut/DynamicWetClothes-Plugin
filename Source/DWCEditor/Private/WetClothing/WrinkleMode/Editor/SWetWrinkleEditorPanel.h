#pragma once

#include "CoreMinimal.h"
#include "ScopedTransaction.h"
#include "Widgets/SCompoundWidget.h"
#include "WetClothing/Common/Editor/WetClothingAssetEditorTypes.h"
#include "WetClothing/Common/Analysis/WetClothingAssetMeshAnalyzer.h"
#include "WetClothing/Common/Widgets/SWetClothingAssetUVView.h"
#include "WetClothing/WrinkleMode/Viewport/WetWrinkleHitData.h"

class FAssetThumbnail;
class FAssetThumbnailPool;
class IDetailsView;
class ITableRow;
class SInlineEditableTextBlock;
class SWetWrinkleViewport;
class STableViewBase;
class UWetClothingAsset;
class UTexture;
class UTexture2D;
struct FAssetData;
struct FWetClothingWetPartEntry;
struct FWetWrinklePatchPlacement;
struct FWetWrinklePatchStroke;

struct FWetWrinklePatchStrokeListItem
{
    FGuid StrokeGuid;
};

struct FWetWrinkleBrushPresetOption
{
    FText DisplayName;
    FSoftObjectPath TexturePath;
};

class SWetWrinkleEditorPanel : public SCompoundWidget
{
  public:
    SLATE_BEGIN_ARGS(SWetWrinkleEditorPanel) {}
    SLATE_ARGUMENT(UWetClothingAsset*, WetClothingAsset)
    SLATE_ARGUMENT(TSharedPtr<IDetailsView>, DetailsView)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    void RefreshFromAsset();
    FReply ExecuteBakeWrinkleNormalMap();
    FReply ExecuteBakeWrinkleMask();

  private:
    using FStrokeListItemPtr = TSharedPtr<FWetWrinklePatchStrokeListItem>;
    using FMaterialSlotItemPtr = TSharedPtr<FWetClothingMaterialSlotItem>;
    using FPatchTextureItemPtr = TSharedPtr<FWetWrinkleBrushPresetOption>;
    using FWetPartEntryPtr = TSharedPtr<FWetClothingWetPartEntry>;

    FReply HandleSaveClicked();
    TSharedRef<SWidget> BuildBakeMapsMenu();
    FReply HandleBakeAllMapsClicked();
    FReply HandleBakeWetnessProfileMapsClicked();
    FReply HandleBakeWrinkleNormalMapClicked();
    FReply HandleBakeWrinkleMaskClicked();
    FReply BakeWrinkleMapsForSelectedSlot(bool bBakeNormalMap, bool bBakeMask);
    FReply HandleFocusClicked();
    void HandleSurfaceHitChanged(const FWetWrinkleSurfaceHit& SurfaceHit);
    void HandlePaintStrokeStarted(const FWetWrinkleSurfaceHit& SurfaceHit);
    void HandlePaintStampRequested(const FWetWrinkleSurfaceHit& SurfaceHit);
    void HandlePaintStrokeEnded();
    TSharedRef<SWidget> BuildPatchBrushSection();
    TSharedRef<SWidget> BuildPatchListSection();
    void PushBrushSettingsToViewport();
    void RefreshStrokeList();
    void RefreshStrokeOverlay(bool bRebuildAccumulatedPreview = true);
    void RefreshMaterialSlotOptions();
    void RefreshBrushPresetOptions();
    void RefreshPartMapItems();
    void EnsureWrinkleUVChannelForModeEntry();
    bool HasUsableWrinkleUVChannel() const;
    bool HasGeneratedWrinkleUVForMaterialSlot(int32 MaterialSlotIndex) const;
    bool EnsureWrinkleUVChannelForMaterialSlot(int32 MaterialSlotIndex, bool bShowFailureDialog);
    void InvalidateWrinkleUVViewCache();
    void RefreshUVChannelOptions();
    void RefreshWrinkleUVView();
    TSharedRef<SWidget> BuildWrinkleUVViewSection();

    int32 GetWrinkleUVViewChannelIndex() const;
    int32 GetProtectedBaseUVChannelCount() const;
    bool IsUVChannelDeleteAllowed(int32 UVChannelIndex) const;
    FText GetWrinkleUVChannelText() const;
    FText GetSelectedMeshUVChannelText() const;
    FText GetMeshUVChannelDisplayText(int32 UVChannelIndex) const;
    TSharedRef<SWidget> GenerateMeshUVChannelComboRow(TSharedPtr<int32> Item) const;
    void HandleMeshUVChannelComboChanged(TSharedPtr<int32> Item, ESelectInfo::Type SelectInfo);
    FReply HandleDeleteMeshUVChannelClicked();
    bool IsDeleteMeshUVChannelEnabled() const;
    FReply HandleGenerateWrinkleUVChannelClicked();
    FReply HandleAutoGenerateClicked();
    FText GetHitInfoText() const;
    FText GetPatchListSummaryText() const;
    FText GetMaterialSlotCountText() const;
    FText GetPartMapSectionText() const;
    TSharedRef<SWidget> GenerateMaterialSlotComboRow(TSharedPtr<int32> Item) const;
    FText GetSelectedMaterialSlotText() const;
    void HandleMaterialSlotComboChanged(TSharedPtr<int32> Item, ESelectInfo::Type SelectInfo);
    TSharedRef<ITableRow> GenerateMaterialSlotRow(FMaterialSlotItemPtr Item, const TSharedRef<STableViewBase>& OwnerTable);
    void HandleMaterialSlotSelectionChanged(FMaterialSlotItemPtr Item, ESelectInfo::Type SelectInfo);
    FReply HandleWettableMaterialSlotClicked(int32 MaterialSlotIndex);
    TSharedRef<ITableRow> GeneratePartMapRow(FWetPartEntryPtr Item, const TSharedRef<STableViewBase>& OwnerTable);
    TSharedRef<SWidget> GenerateBrushPresetComboRow(TSharedPtr<FWetWrinkleBrushPresetOption> Item) const;
    TSharedRef<ITableRow> GeneratePatchTextureRow(FPatchTextureItemPtr Item, const TSharedRef<STableViewBase>& OwnerTable);
    FText GetSelectedBrushPresetText() const;
    void HandleBrushPresetChanged(TSharedPtr<FWetWrinkleBrushPresetOption> Item, ESelectInfo::Type SelectInfo);
    void HandlePatchTextureSelectionChanged(FPatchTextureItemPtr Item, ESelectInfo::Type SelectInfo);
    FString GetBrushHeightTextureObjectPath() const;
    void HandleBrushHeightTextureChanged(const FAssetData& AssetData);
    TSharedRef<ITableRow> GenerateStrokeRow(FStrokeListItemPtr Item, const TSharedRef<STableViewBase>& OwnerTable);
    void HandleStrokeSelectionChanged(FStrokeListItemPtr Item, ESelectInfo::Type SelectInfo);
    FReply HandleClearStrokesClicked();
    bool IsClearStrokesEnabled() const;
    void HandleStrokeEnabledChanged(ECheckBoxState NewState, FStrokeListItemPtr Item);
    void HandleStrokeNameCommitted(const FText& InText, ETextCommit::Type CommitType, FStrokeListItemPtr Item);
    FReply HandleDeleteStrokeClicked(FStrokeListItemPtr Item);

    void HandleUVChannelChanged(int32 NewValue);
    void HandleMaterialSlotChanged(int32 NewValue);
    float GetBrushSizeCm() const;
    FText GetBrushSizeDisplayText() const;
    TSharedRef<SWidget> BuildBrushSizeMenu();
    void HandleBrushRadiusChanged(float NewValue);
    FReply HandleBrushSizePresetClicked(float NewValue);
    void HandleStrengthChanged(float NewValue);
    void HandleFalloffChanged(float NewValue);
    void HandleRotationChanged(float NewValue);
    void HandlePreviewWetnessChanged(float NewValue);
    void HandlePreviewToggleChanged(ECheckBoxState NewState);
    ECheckBoxState GetPreviewToggleState() const;
    FWetWrinklePatchStroke* FindMutableStroke(const FGuid& StrokeGuid) const;
    const FWetWrinklePatchStroke* FindStroke(const FGuid& StrokeGuid) const;
    FWetWrinklePatchPlacement MakeStampFromHit(const FWetWrinkleSurfaceHit& SurfaceHit) const;
    UTexture* ResolveSourceTextureForStamp(int32 MaterialSlotIndex, int32 UVChannelIndex) const;
    UTexture2D* ResolveDefaultBrushHeightTexture() const;
    FText GetMaterialSlotDisplayText(int32 MaterialSlotIndex) const;
    TSharedPtr<int32> FindMaterialSlotOption(int32 MaterialSlotIndex) const;
    FMaterialSlotItemPtr FindMaterialSlotItem(int32 MaterialSlotIndex) const;
    TSharedPtr<FWetWrinkleBrushPresetOption> FindBrushPresetOption(UTexture2D* Texture) const;
    void HandleTextureUVHovered(const FVector2D& UV);
    void HandleTextureUVHoverEnded();
    void HandleTexturePaintStrokeStarted(const FVector2D& UV);
    void HandleTexturePaintStampRequested(const FVector2D& UV);
    void HandleTexturePaintStrokeEnded();
    bool TryBuildTextureSurfaceHit(const FVector2D& UV, FWetWrinkleSurfaceHit& OutSurfaceHit) const;
    bool ShouldAddStampForHit(const FWetWrinkleSurfaceHit& SurfaceHit) const;
    FString MakeDefaultStrokeName() const;
    void MarkAssetEdited();

  private:
    TWeakObjectPtr<UWetClothingAsset> WetClothingAsset;
    TSharedPtr<IDetailsView> DetailsView;
    TSharedPtr<SWetWrinkleViewport> PreviewViewport;
    TSharedPtr<SWetClothingAssetUVView> WrinkleUVView;
    TArray<TSharedPtr<FWetClothingAssetUVIsland>> WrinkleUVIslandItems;
    int32 CachedWrinkleUVViewChannelIndex = INDEX_NONE;
    int32 CachedWrinkleUVViewMaterialSlotIndex = INDEX_NONE;
    TSharedPtr<class SComboBox<TSharedPtr<int32>>> MaterialSlotComboBox;
    TSharedPtr<class SComboBox<TSharedPtr<int32>>> MeshUVChannelComboBox;
    TSharedPtr<class SListView<FMaterialSlotItemPtr>> MaterialSlotListView;
    TSharedPtr<class SComboBox<TSharedPtr<FWetWrinkleBrushPresetOption>>> BrushPresetComboBox;
    TSharedPtr<class SComboButton> BrushSizeComboButton;
    TSharedPtr<class SListView<FPatchTextureItemPtr>> PatchTextureListView;
    TSharedPtr<class SListView<FStrokeListItemPtr>> StrokeListView;
    TArray<FStrokeListItemPtr> StrokeListItems;
    TArray<TSharedPtr<int32>> MaterialSlotOptions;
    TArray<TSharedPtr<int32>> MeshUVChannelOptions;
    TArray<FMaterialSlotItemPtr> MaterialSlotItems;
    TArray<FWetPartEntryPtr> PartMapItems;
    TArray<TSharedPtr<FWetWrinkleBrushPresetOption>> BrushPresetOptions;
    TSharedPtr<FAssetThumbnailPool> MaterialThumbnailPool;
    TSharedPtr<FAssetThumbnailPool> PatchTextureThumbnailPool;
    TArray<TSharedPtr<FAssetThumbnail>> MaterialSlotThumbnails;
    TArray<TSharedPtr<FAssetThumbnail>> PatchTextureThumbnails;
    TSharedPtr<class SListView<FWetPartEntryPtr>> PartMapListView;
    FWetWrinkleBrushSettings BrushSettings;
    float SizeCm = 8.0f;
    float SizeUV = 0.0677f;
    FWetWrinkleSurfaceHit CurrentHit;
    FGuid ActiveStrokeGuid;
    FGuid SelectedStrokeGuid;
    FVector2D LastStampUV = FVector2D::ZeroVector;
    int32 LastStampMaterialSlotIndex = INDEX_NONE;
    int32 LastStampUVChannelIndex = INDEX_NONE;
    int32 SelectedMeshUVChannelIndex = INDEX_NONE;
    bool bHasLastStamp = false;
    bool bAllowImmediateNextStrokeStamp = false;
    TUniquePtr<FScopedTransaction> ActivePaintTransaction;
};
