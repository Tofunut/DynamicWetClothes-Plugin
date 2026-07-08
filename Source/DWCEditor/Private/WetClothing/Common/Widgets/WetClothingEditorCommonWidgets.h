#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Common/Editor/WetClothingAssetEditorTypes.h"

class FAssetThumbnail;
class FAssetThumbnailPool;
class ITableRow;
class SWidget;
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
