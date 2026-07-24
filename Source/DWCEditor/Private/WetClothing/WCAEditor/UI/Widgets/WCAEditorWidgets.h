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
    TFunction<FText(int32)> GetMaterialSlotStatusText;
    TFunction<TSharedRef<SWidget>(int32)> BuildTrailingWidget;
};

struct FWCABakeMapsMenuArgs
{
    EWCAEditorMode EditorMode = EWCAEditorMode::PartEdit;
    FSimpleDelegate OnBakeAllMaps;
    FSimpleDelegate OnBakeRenderProfileData;
    FSimpleDelegate OnBakeGPUWetnessMapData;
    FSimpleDelegate OnBakeWrinkleNormalMap;
    FSimpleDelegate OnBakeTransparencyMaps;
    FCanExecuteAction CanBakeAnyMaps;
    FCanExecuteAction CanBakeRenderProfileData;
    FCanExecuteAction CanBakeGPUWetnessMapData;
    FCanExecuteAction CanBakeWrinkleNormalMap;
    FCanExecuteAction CanBakeTransparencyMaps;
};

struct FWCAGenerateMaterialsMenuArgs
{
    UWetClothingAsset* WetClothingAsset = nullptr;
    FSimpleDelegate OnGenerateMaterials;
};

class FWCAEditorWidgets
{
  public:
    static constexpr float MaterialSlotListHeaderTopPadding = 14.0f;
    static constexpr float MaterialSlotListSeparatorBottomPadding = 10.0f;

    static TSharedRef<SWidget> BuildSectionHeader(const TAttribute<FText>& Title, const TAttribute<FText>& Detail = TAttribute<FText>());
    static TSharedRef<SWidget> BuildPreviewSection(
        const TSharedRef<SWidget>& PreviewContent,
        const FOnWetClothingPreviewFocusClicked& OnFocusClicked,
        TSharedPtr<SWidget> ExtraToolbarContent = TSharedPtr<SWidget>());
    static TSharedRef<SWidget> BuildBakeMapsMenu(const FWCABakeMapsMenuArgs& Args);
    static TSharedRef<SWidget> BuildGenerateMaterialsMenu(const FWCAGenerateMaterialsMenuArgs& Args);

    static FText GetUVDisplayModeLabel(EWCAUVDisplayMode DisplayMode);
    static TSharedRef<SWidget> GenerateUVDisplayModeComboItem(TSharedPtr<EWCAUVDisplayMode> Item);


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
        TArray<TSharedPtr<EWCAUVDisplayMode>>* DisplayModeItems,
        TSharedPtr<EWCAUVDisplayMode> SelectedDisplayModeItem,
        TAttribute<FText> SelectedDisplayModeText,
        TFunction<void(TSharedPtr<EWCAUVDisplayMode>)> OnDisplayModeChanged,
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
