#pragma once

#include "CoreMinimal.h"
#include "Framework/Commands/UIAction.h"
#include "WetClothing/WCAEditor/WCAEditorMode.h"
#include "WetClothing/WCAEditor/WCAEditorTypes.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"

class FAssetThumbnail;
class FAssetThumbnailPool;
class ITableRow;
class SWidget;
class SBox;
class STableViewBase;
class USkeletalMesh;
class UTexture;
class UWetClothingAsset;
struct FWetClothingWetPartEntry;

DECLARE_DELEGATE_RetVal_OneParam(FReply, FOnWettableMaterialSlotClicked, int32 /*MaterialSlotIndex*/);
DECLARE_DELEGATE_RetVal(FReply, FOnWetClothingPreviewFocusClicked);

struct FWCAMaterialSlotRowArgs
{
    const UWetClothingAsset* WetClothingAsset = nullptr;
    USkeletalMesh* GeneratedDataUV = nullptr;
    TSharedPtr<FAssetThumbnailPool> ThumbnailPool;
    TArray<TSharedPtr<FAssetThumbnail>>* ThumbnailSink = nullptr;
    FText AllSlotsTitle;
    bool bShowWettableToggle = true;
    FOnWettableMaterialSlotClicked OnWettableSlotClicked;
    TFunction<bool(int32)> IsWettableToggleEnabled;
    TFunction<FText(int32)> GetMaterialSlotStatusText;
    TFunction<FSlateColor(int32)> GetMaterialSlotStatusColor;
    TFunction<FText(int32)> GetMaterialSlotStatusTooltip;
    TFunction<bool(int32)> ShouldShowMaterialSlotStatusInfo;
    TFunction<FReply(int32)> OnMaterialSlotStatusInfoClicked;
    TFunction<FText(int32)> GetMaterialSlotWarningText;
    TFunction<FSlateColor(int32)> GetMaterialSlotRowBackgroundColor;
    TFunction<FSlateColor(int32)> GetMaterialSlotRowAccentColor;
    TFunction<TSharedRef<SWidget>(int32)> BuildThumbnailWidget;
    TFunction<TSharedRef<SWidget>(int32)> BuildLeadingWidget;
    TFunction<TSharedRef<SWidget>(int32)> BuildTrailingWidget;
};

struct FWCARuntimeBuildMenuArgs
{
    FSimpleDelegate OnBuildAllRequired;
    FSimpleDelegate OnBuildCPURuntimeData;
    FSimpleDelegate OnBuildGPURuntimeData;
    FSimpleDelegate OnGenerateMaterials;
    FSimpleDelegate OnBuildRenderProfileData;
    FSimpleDelegate OnBakeWrinkleTextures;
    FSimpleDelegate OnBakeTransparencyTextures;
    FCanExecuteAction CanBuildAllRequired;
    FCanExecuteAction CanBuildCPURuntimeData;
    FCanExecuteAction CanBuildGPURuntimeData;
    FCanExecuteAction CanGenerateMaterials;
    FCanExecuteAction CanBuildRenderProfileData;
    FCanExecuteAction CanBakeWrinkleTextures;
    FCanExecuteAction CanBakeTransparencyTextures;
};

class FWCAEditorWidgets
{
  public:
    static constexpr float MaterialSlotListHeaderTopPadding = 14.0f;
    static constexpr float MaterialSlotListSeparatorBottomPadding = 10.0f;
    static constexpr float MaterialSlotSlotColumnWidth = 42.0f;
    static constexpr float MaterialSlotThumbnailColumnWidth = 112.0f;
    static constexpr float MaterialSlotDataUVColumnWidth = 76.0f;
    static constexpr float MaterialSlotWettableColumnWidth = 76.0f;

    static TSharedRef<SWidget> BuildSectionHeader(const TAttribute<FText>& Title, const TAttribute<FText>& Detail = TAttribute<FText>());
    static TSharedRef<SWidget> BuildPreviewSection(
        const TSharedRef<SWidget>& PreviewContent,
        const FOnWetClothingPreviewFocusClicked& OnFocusClicked,
        TSharedPtr<SWidget> ExtraToolbarContent = TSharedPtr<SWidget>());
    static TSharedRef<SWidget> BuildRuntimeBuildMenu(const FWCARuntimeBuildMenuArgs& Args);

    static TSharedRef<SWidget> BuildTextureComboContent(
        TSharedPtr<FWCATextureItem> Item,
        float ThumbnailSize,
        bool bCompactLayout,
        TSharedPtr<FAssetThumbnailPool> ThumbnailPool,
        TArray<TSharedPtr<FAssetThumbnail>>* ThumbnailSink);

    static TSharedRef<SWidget> GenerateTextureComboItem(
        TSharedPtr<FWCATextureItem> Item,
        TSharedPtr<FAssetThumbnailPool> ThumbnailPool,
        TArray<TSharedPtr<FAssetThumbnail>>* ThumbnailSink);

    static TSharedRef<SWidget> BuildUVViewTextureSelector(
        TArray<TSharedPtr<FWCATextureItem>>* TextureItems,
        TSharedPtr<FWCATextureItem> SelectedTextureItem,
        TSharedPtr<FAssetThumbnailPool> ThumbnailPool,
        TArray<TSharedPtr<FAssetThumbnail>>* ThumbnailSink,
        TSharedPtr<SComboBox<TSharedPtr<FWCATextureItem>>>* OutComboBox,
        TSharedPtr<SBox>* OutSelectedContentBox,
        TFunction<void(TSharedPtr<FWCATextureItem>, ESelectInfo::Type)> OnSelectionChanged);

    static TSharedRef<SWidget> BuildUVViewTextureAndViewRow(
        const TSharedRef<SWidget>& TextureSelector,
        const TSharedRef<SWidget>& ViewOptionsButton);

    static TSharedRef<SWidget> BuildUVViewOptionsButton(
        TAttribute<float> BackgroundTextureOpacity,
        TFunction<void(float)> OnBackgroundTextureOpacityChanged,
        TAttribute<float> UVIslandLineOpacity,
        TFunction<void(float)> OnUVIslandLineOpacityChanged,
        TAttribute<float> UVIslandLineThicknessScale,
        TFunction<void(float)> OnUVIslandLineThicknessScaleChanged,
        bool bShowBackgroundTextureControls = true);

    static TSharedRef<ITableRow> GenerateMaterialSlotRow(
        TSharedPtr<FWCAMaterialSlotItem> Item,
        const TSharedRef<STableViewBase>& OwnerTable,
        const FWCAMaterialSlotRowArgs& Args);

    static TSharedRef<ITableRow> GeneratePartMapRow(
        TSharedPtr<FWetClothingWetPartEntry> Item,
        const TSharedRef<STableViewBase>& OwnerTable);

    static bool IsMaterialSlotWettable(const UWetClothingAsset* WetClothingAsset, int32 MaterialSlotIndex);
    static void SetMaterialSlotWettable(UWetClothingAsset* WetClothingAsset, int32 MaterialSlotIndex, bool bIsWettableSlot);
    static void MarkMaterialSlotWettable(UWetClothingAsset* WetClothingAsset, int32 MaterialSlotIndex);
};
