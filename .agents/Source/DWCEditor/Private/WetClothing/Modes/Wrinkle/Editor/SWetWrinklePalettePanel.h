#pragma once

#include "CoreMinimal.h"
#include "Styling/SlateTypes.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/STileView.h"

class FAssetThumbnail;
class ITableRow;
class UTexture2D;

struct FWetWrinkleTexturePaletteItem
{
    FText DisplayName;
    FSoftObjectPath TexturePath;
    TWeakObjectPtr<UTexture2D> Texture;
    TSharedPtr<FAssetThumbnail> AssetThumbnail;
    bool bAssetAvailable = true;
    bool bRemoved = false;
    bool bHidden = false;
};

using FWetWrinkleTexturePaletteItemPtr = TSharedPtr<FWetWrinkleTexturePaletteItem>;

DECLARE_DELEGATE_RetVal_TwoParams(
    TSharedRef<ITableRow>,
    FOnGenerateWetWrinklePaletteTile,
    FWetWrinkleTexturePaletteItemPtr,
    const TSharedRef<STableViewBase>&);

class SWetWrinklePalettePanel : public SCompoundWidget
{
  public:
    SLATE_BEGIN_ARGS(SWetWrinklePalettePanel) {}
    SLATE_EVENT(FOnGenerateWetWrinklePaletteTile, OnGenerateTile)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

    TArray<FWetWrinkleTexturePaletteItemPtr>& GetAllItems() { return AllItems; }
    const TArray<FWetWrinkleTexturePaletteItemPtr>& GetAllItems() const { return AllItems; }
    TArray<FWetWrinkleTexturePaletteItemPtr>& GetVisibleItems() { return VisibleItems; }
    const TArray<FWetWrinkleTexturePaletteItemPtr>& GetVisibleItems() const { return VisibleItems; }
    TMap<FSoftObjectPath, FWetWrinkleTexturePaletteItemPtr>& GetItemsByPath() { return ItemsByPath; }
    const TMap<FSoftObjectPath, FWetWrinkleTexturePaletteItemPtr>& GetItemsByPath() const { return ItemsByPath; }
    const FButtonStyle& GetButtonStyle() const { return ButtonStyle; }

    void RequestRefresh();

  private:
    TSharedRef<ITableRow> GenerateTile(
        FWetWrinkleTexturePaletteItemPtr Item,
        const TSharedRef<STableViewBase>& OwnerTable) const;

    TArray<FWetWrinkleTexturePaletteItemPtr> AllItems;
    TArray<FWetWrinkleTexturePaletteItemPtr> VisibleItems;
    TMap<FSoftObjectPath, FWetWrinkleTexturePaletteItemPtr> ItemsByPath;
    TSharedPtr<STileView<FWetWrinkleTexturePaletteItemPtr>> TileView;
    FButtonStyle ButtonStyle;
    FOnGenerateWetWrinklePaletteTile OnGenerateTile;
};
