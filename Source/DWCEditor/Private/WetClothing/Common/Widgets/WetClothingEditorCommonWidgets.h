#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Common/Editor/WetClothingAssetEditorTypes.h"
#include "Widgets/Input/SComboBox.h"

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

struct FWetClothingMaterialSlotRowArgs
{
    const UWetClothingAsset* WetClothingAsset = nullptr;
    USkeletalMesh* TargetMesh = nullptr;
    int32 SelectedMaterialSlotIndex = INDEX_NONE;
    UTexture* OverridePreviewTexture = nullptr;
    TSharedPtr<FAssetThumbnailPool> ThumbnailPool;
    TArray<TSharedPtr<FAssetThumbnail>>* ThumbnailSink = nullptr;
    FOnWettableMaterialSlotClicked OnWettableSlotClicked;
};

struct FWetClothingBakeMapsMenuArgs
{
    FSimpleDelegate OnBakeAllMaps;
    FSimpleDelegate OnBakeWetnessProfileMaps;
    FSimpleDelegate OnBakeTransparencyRevealMaps;
    FSimpleDelegate OnBakeWrinkleNormalMap;
    FSimpleDelegate OnBakeWrinkleMask;
};

class FWetClothingEditorCommonWidgets
{
  public:
    static constexpr float MaterialSlotListHeaderTopPadding = 14.0f;
    static constexpr float MaterialSlotListSeparatorBottomPadding = 10.0f;

    static TSharedRef<SWidget> BuildSectionHeader(const TAttribute<FText>& Title, const TAttribute<FText>& Detail = TAttribute<FText>());
    static TSharedRef<SWidget> BuildPreviewSection(
        const TSharedRef<SWidget>& PreviewContent,
        const FOnWetClothingPreviewFocusClicked& OnFocusClicked,
        TSharedPtr<SWidget> ExtraToolbarContent = TSharedPtr<SWidget>());
    static TSharedRef<SWidget> BuildBakeMapsMenu(const FWetClothingBakeMapsMenuArgs& Args);

    static FText GetUVDisplayModeLabel(EWetClothingAssetUVDisplayMode DisplayMode);
    static TSharedRef<SWidget> GenerateUVDisplayModeComboItem(TSharedPtr<EWetClothingAssetUVDisplayMode> Item);


    static TSharedRef<SWidget> BuildTextureComboContent(
        TSharedPtr<FWetClothingTextureItem> Item,
        float ThumbnailSize,
        bool bCompactLayout,
        TSharedPtr<FAssetThumbnailPool> ThumbnailPool,
        TArray<TSharedPtr<FAssetThumbnail>>* ThumbnailSink);

    static TSharedRef<SWidget> GenerateTextureComboItem(
        TSharedPtr<FWetClothingTextureItem> Item,
        TSharedPtr<FAssetThumbnailPool> ThumbnailPool,
        TArray<TSharedPtr<FAssetThumbnail>>* ThumbnailSink);

    static TSharedRef<SWidget> BuildUVViewTextureSelector(
        TArray<TSharedPtr<FWetClothingTextureItem>>* TextureItems,
        TSharedPtr<FWetClothingTextureItem> SelectedTextureItem,
        TSharedPtr<FAssetThumbnailPool> ThumbnailPool,
        TArray<TSharedPtr<FAssetThumbnail>>* ThumbnailSink,
        TSharedPtr<SComboBox<TSharedPtr<FWetClothingTextureItem>>>* OutComboBox,
        TSharedPtr<SBox>* OutSelectedContentBox,
        TFunction<void(TSharedPtr<FWetClothingTextureItem>, ESelectInfo::Type)> OnSelectionChanged);

    static TSharedRef<SWidget> BuildUVViewTextureAndViewRow(
        const TSharedRef<SWidget>& TextureSelector,
        const TSharedRef<SWidget>& ViewOptionsButton);

    static TSharedRef<SWidget> BuildUVViewOptionsButton(
        TArray<TSharedPtr<EWetClothingAssetUVDisplayMode>>* DisplayModeItems,
        TSharedPtr<EWetClothingAssetUVDisplayMode> SelectedDisplayModeItem,
        TAttribute<FText> SelectedDisplayModeText,
        TFunction<void(TSharedPtr<EWetClothingAssetUVDisplayMode>)> OnDisplayModeChanged,
        TAttribute<float> BackgroundTextureOpacity,
        TFunction<void(float)> OnBackgroundTextureOpacityChanged,
        TAttribute<float> UVIslandLineOpacity,
        TFunction<void(float)> OnUVIslandLineOpacityChanged,
        TAttribute<float> UVIslandLineThicknessScale,
        TFunction<void(float)> OnUVIslandLineThicknessScaleChanged);

    static TSharedRef<ITableRow> GenerateMaterialSlotRow(
        TSharedPtr<FWetClothingMaterialSlotItem> Item,
        const TSharedRef<STableViewBase>& OwnerTable,
        const FWetClothingMaterialSlotRowArgs& Args);

    static TSharedRef<ITableRow> GeneratePartMapRow(
        TSharedPtr<FWetClothingWetPartEntry> Item,
        const TSharedRef<STableViewBase>& OwnerTable);

    static bool IsMaterialSlotWettable(const UWetClothingAsset* WetClothingAsset, int32 MaterialSlotIndex);
    static void SetMaterialSlotWettable(UWetClothingAsset* WetClothingAsset, int32 MaterialSlotIndex, bool bIsWettableSlot);
    static void MarkMaterialSlotWettable(UWetClothingAsset* WetClothingAsset, int32 MaterialSlotIndex);
};
