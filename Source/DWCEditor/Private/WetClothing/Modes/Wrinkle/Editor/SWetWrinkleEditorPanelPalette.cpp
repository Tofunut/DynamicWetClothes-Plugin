// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "SWetWrinkleEditorPanel.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetThumbnail.h"
#include "DataAssets/WetClothingAsset.h"
#include "Editor.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Modules/ModuleManager.h"
#include "WetClothing/Modes/Wrinkle/Editor/WetWrinkleEditorSettings.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "WCAEditorPanel"

namespace
{
    DECLARE_DELEGATE_RetVal_OneParam(FReply, FOnWetWrinkleTextureTileContextMenu, const FPointerEvent&);

    class SWetWrinkleTexturePaletteTile final : public SCompoundWidget
    {
      public:
        SLATE_BEGIN_ARGS(SWetWrinkleTexturePaletteTile) {}
        SLATE_DEFAULT_SLOT(FArguments, Content)
        SLATE_EVENT(FOnWetWrinkleTextureTileContextMenu, OnContextMenu)
        SLATE_END_ARGS()

        void Construct(const FArguments& InArgs)
        {
            OnContextMenu = InArgs._OnContextMenu;
            ChildSlot[InArgs._Content.Widget];
        }

        virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
        {
            if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton && OnContextMenu.IsBound())
            {
                return OnContextMenu.Execute(MouseEvent);
            }
            return SCompoundWidget::OnMouseButtonUp(MyGeometry, MouseEvent);
        }

      private:
        FOnWetWrinkleTextureTileContextMenu OnContextMenu;
    };
}

void SWetWrinkleEditorPanel::RefreshWrinkleTexturePalette(bool bForceAssetScan)
{
    FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    TArray<FString> SearchPaths;
    GetDefault<UWetWrinkleEditorSettings>()->GetNormalTextureSearchPaths(SearchPaths);
    if (bForceAssetScan && !SearchPaths.IsEmpty())
    {
        AssetRegistryModule.Get().ScanPathsSynchronous(SearchPaths, true);
    }

    TMap<FSoftObjectPath, FAssetData> UniqueTextureAssets;
    for (const FString& SearchPath : SearchPaths)
    {
        TArray<FAssetData> PathAssets;
        AssetRegistryModule.Get().GetAssetsByPath(FName(*SearchPath), PathAssets, true);
        for (const FAssetData& AssetData : PathAssets)
        {
            if (AssetData.AssetClassPath == UTexture2D::StaticClass()->GetClassPathName())
            {
                UniqueTextureAssets.FindOrAdd(AssetData.ToSoftObjectPath()) = AssetData;
            }
        }
    }

    TArray<FAssetData> TextureAssets;
    UniqueTextureAssets.GenerateValueArray(TextureAssets);
    TextureAssets.Sort([](const FAssetData& A, const FAssetData& B)
    {
        const int32 NameCompare = A.AssetName.ToString().Compare(B.AssetName.ToString());
        return NameCompare == 0 ? A.PackageName.ToString() < B.PackageName.ToString() : NameCompare < 0;
    });

    TMap<FSoftObjectPath, FWrinkleTexturePaletteItemPtr> PreviousItems =
        MoveTemp(WrinklePalettePanel->GetItemsByPath());
    WrinklePalettePanel->GetAllItems().Reset(TextureAssets.Num());
    WrinklePalettePanel->GetItemsByPath().Reset();

    const UWetWrinkleEditorSettings* UserSettings = GetDefault<UWetWrinkleEditorSettings>();
    for (const FAssetData& TextureAsset : TextureAssets)
    {
        const FSoftObjectPath TexturePath = TextureAsset.ToSoftObjectPath();
        FWrinkleTexturePaletteItemPtr Item = PreviousItems.FindRef(TexturePath);
        if (!Item.IsValid())
        {
            Item = MakeShared<FWetWrinkleTexturePaletteItem>();
        }
        Item->DisplayName = FText::FromName(TextureAsset.AssetName);
        Item->TexturePath = TexturePath;
        Item->bHidden = UserSettings->IsNormalTextureHidden(Item->TexturePath);
        Item->bAssetAvailable = true;
        Item->bRemoved = false;
        WrinklePalettePanel->GetAllItems().Add(Item);
        WrinklePalettePanel->GetItemsByPath().Add(TexturePath, Item);
    }

    RefreshWrinkleTexturePaletteView();
}

void SWetWrinkleEditorPanel::RefreshWrinkleTexturePaletteView()
{
    WrinklePalettePanel->GetVisibleItems().Reset();
    const bool bShowHidden = GetDefault<UWetWrinkleEditorSettings>()->bShowHiddenNormalTextures;
    for (const FWrinkleTexturePaletteItemPtr& Item : WrinklePalettePanel->GetAllItems())
    {
        if (Item.IsValid() && !Item->bRemoved && (!Item->bHidden || bShowHidden))
        {
            WrinklePalettePanel->GetVisibleItems().Add(Item);
        }
    }

    WrinklePalettePanel->RequestRefresh();
}

void SWetWrinkleEditorPanel::SortWrinkleTexturePaletteItems()
{
    WrinklePalettePanel->GetAllItems().Sort(
        [](const FWrinkleTexturePaletteItemPtr& A, const FWrinkleTexturePaletteItemPtr& B)
        {
            if (!A.IsValid() || !B.IsValid())
            {
                return A.IsValid();
            }
            const int32 NameCompare = A->DisplayName.ToString().Compare(B->DisplayName.ToString());
            return NameCompare == 0
                ? A->TexturePath.ToString() < B->TexturePath.ToString()
                : NameCompare < 0;
        });
}

SWetWrinkleEditorPanel::FWrinkleTexturePaletteItemPtr SWetWrinkleEditorPanel::UpsertWrinkleTexturePaletteItem(
    const FAssetData& AssetData)
{
    if (!IsAssetInsideWrinkleTextureSearchPaths(AssetData))
    {
        return nullptr;
    }

    const FSoftObjectPath TexturePath = AssetData.ToSoftObjectPath();
    FWrinkleTexturePaletteItemPtr Item = WrinklePalettePanel->GetItemsByPath().FindRef(TexturePath);
    if (!Item.IsValid())
    {
        Item = MakeShared<FWetWrinkleTexturePaletteItem>();
        WrinklePalettePanel->GetAllItems().Add(Item);
        WrinklePalettePanel->GetItemsByPath().Add(TexturePath, Item);
    }

    Item->DisplayName = FText::FromName(AssetData.AssetName);
    Item->TexturePath = TexturePath;
    Item->bHidden = GetDefault<UWetWrinkleEditorSettings>()->IsNormalTextureHidden(TexturePath);
    Item->bAssetAvailable = true;
    Item->bRemoved = false;
    SortWrinkleTexturePaletteItems();
    return Item;
}

bool SWetWrinkleEditorPanel::RemoveWrinkleTexturePaletteItem(const FSoftObjectPath& TexturePath)
{
    FWrinkleTexturePaletteItemPtr Item;
    if (!WrinklePalettePanel->GetItemsByPath().RemoveAndCopyValue(TexturePath, Item))
    {
        return false;
    }

    WrinklePalettePanel->GetAllItems().RemoveSingle(Item);
    if (Item.IsValid())
    {
        Item->Texture.Reset();
        Item->AssetThumbnail.Reset();
        Item->bAssetAvailable = false;
        Item->bRemoved = true;
    }
    return true;
}

bool SWetWrinkleEditorPanel::IsAssetInsideWrinkleTextureSearchPaths(const FAssetData& AssetData) const
{
    if (AssetData.AssetClassPath != UTexture2D::StaticClass()->GetClassPathName())
    {
        return false;
    }

    TArray<FString> SearchPaths;
    GetDefault<UWetWrinkleEditorSettings>()->GetNormalTextureSearchPaths(SearchPaths);
    const FString PackagePath = AssetData.PackagePath.ToString();
    return SearchPaths.ContainsByPredicate(
        [&PackagePath](const FString& SearchPath)
        {
            return PackagePath == SearchPath || PackagePath.StartsWith(SearchPath + TEXT("/"));
        });
}

void SWetWrinkleEditorPanel::HandleWrinkleTextureAssetAdded(const FAssetData& AssetData)
{
    if (UpsertWrinkleTexturePaletteItem(AssetData).IsValid())
    {
        RefreshWrinkleTexturePaletteView();
    }
}

void SWetWrinkleEditorPanel::HandleWrinkleTextureAssetRemoved(const FAssetData& AssetData)
{
    const FSoftObjectPath RemovedPath = AssetData.ToSoftObjectPath();
    GetMutableDefault<UWetWrinkleEditorSettings>()->SetNormalTextureHidden(RemovedPath, false);
    if (BrushSettings.WrinkleNormalTexture != nullptr &&
        FSoftObjectPath(BrushSettings.WrinkleNormalTexture.Get()) == RemovedPath)
    {
        BrushSettings.WrinkleNormalTexture = nullptr;
        PushBrushPreviewSettingsToViewport();
    }
    RemoveWrinkleTexturePaletteItem(RemovedPath);
    RefreshWrinkleTexturePaletteView();
    RefreshWrinkleNormalThumbnail();
}

void SWetWrinkleEditorPanel::HandleWrinkleTextureAssetUpdated(const FAssetData& AssetData)
{
    if (FWrinkleTexturePaletteItemPtr Item = UpsertWrinkleTexturePaletteItem(AssetData))
    {
        Item->AssetThumbnail.Reset();
        RefreshWrinkleTexturePaletteView();
        RefreshWrinkleNormalThumbnail();
        if (BrushSettings.WrinkleNormalTexture != nullptr &&
            FSoftObjectPath(BrushSettings.WrinkleNormalTexture.Get()) == AssetData.ToSoftObjectPath())
        {
            PushBrushPreviewSettingsToViewport();
        }
    }
}

TSharedRef<SWidget> SWetWrinkleEditorPanel::BuildWrinkleTexturePalette()
{
    check(WrinklePalettePanel.IsValid());
    return WrinklePalettePanel.ToSharedRef();
}

TSharedRef<ITableRow> SWetWrinkleEditorPanel::GenerateWrinkleTexturePaletteTileRow(
    FWrinkleTexturePaletteItemPtr Item,
    const TSharedRef<STableViewBase>& OwnerTable)
{
    return SNew(STableRow<FWrinkleTexturePaletteItemPtr>, OwnerTable)
        .Padding(2.0f)
            [GenerateWrinkleTexturePaletteTile(Item)];
}

TSharedRef<SWidget> SWetWrinkleEditorPanel::GenerateWrinkleTexturePaletteTile(FWrinkleTexturePaletteItemPtr Item)
{
    if (Item.IsValid() && Item->bAssetAvailable && !Item->AssetThumbnail.IsValid())
    {
        const FAssetData AssetData = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"))
                                         .Get()
                                         .GetAssetByObjectPath(Item->TexturePath);
        if (AssetData.IsValid())
        {
            Item->AssetThumbnail = MakeShared<FAssetThumbnail>(AssetData, 144, 144, MaterialThumbnailPool);
        }
    }

    FAssetThumbnailConfig ThumbnailConfig;
    ThumbnailConfig.bAllowHintText = false;
    ThumbnailConfig.AllowAssetSpecificThumbnailOverlay = false;
    ThumbnailConfig.AllowAssetStatusThumbnailOverlay = false;
    ThumbnailConfig.ShowAssetColor = false;
    ThumbnailConfig.ShowAssetBorder = false;
    ThumbnailConfig.BorderPadding = FMargin(0.0f);

    return SNew(SWetWrinkleTexturePaletteTile)
        .OnContextMenu(FOnWetWrinkleTextureTileContextMenu::CreateSP(
            this,
            &SWetWrinkleEditorPanel::HandleWrinkleTexturePaletteContextMenu,
            Item))
            [SNew(SButton)
             .ButtonStyle(&WrinklePalettePanel->GetButtonStyle())
             .ContentPadding(6.0f)
             .ButtonColorAndOpacity(this, &SWetWrinkleEditorPanel::GetWrinkleTexturePaletteTileColor, Item)
             .ToolTipText(this, &SWetWrinkleEditorPanel::GetWrinkleTexturePaletteTooltipText, Item)
             .OnClicked(this, &SWetWrinkleEditorPanel::HandleWrinkleTexturePaletteClicked, Item)
                 [SNew(SBox)
                  .WidthOverride(144.0f)
                  .HeightOverride(144.0f)
                      [SNew(SScaleBox)
                       .Stretch(EStretch::ScaleToFit)
                       .StretchDirection(EStretchDirection::Both)
                            [Item.IsValid() && Item->AssetThumbnail.IsValid()
                                 ? Item->AssetThumbnail->MakeThumbnailWidget(ThumbnailConfig)
                                 : SNullWidget::NullWidget]]]];
}

FReply SWetWrinkleEditorPanel::HandleWrinkleTexturePaletteClicked(TSharedPtr<FWetWrinkleTexturePaletteItem> Item)
{
    if (!Item.IsValid())
    {
        return FReply::Handled();
    }

    UTexture2D* Texture = Item->Texture.Get();
    if (Texture == nullptr && Item->TexturePath.IsValid())
    {
        Texture = Cast<UTexture2D>(Item->TexturePath.TryLoad());
        Item->Texture = Texture;
    }

    BrushSettings.WrinkleNormalTexture = Texture;
    RefreshWrinkleNormalThumbnail();
    PushBrushPreviewSettingsToViewport();
    WrinklePalettePanel->RequestRefresh();
    return FReply::Handled();
}

FReply SWetWrinkleEditorPanel::HandleWrinkleTexturePaletteContextMenu(
    const FPointerEvent& MouseEvent,
    TSharedPtr<FWetWrinkleTexturePaletteItem> Item)
{
    if (!Item.IsValid())
    {
        return FReply::Handled();
    }

    FMenuBuilder MenuBuilder(true, nullptr);
    MenuBuilder.AddMenuEntry(
        Item->bHidden ? LOCTEXT("UnhideWrinkleTexture", "Unhide") : LOCTEXT("HideWrinkleTexture", "Hide"),
        LOCTEXT("HideWrinkleTextureTooltip", "Change whether this texture is shown in the Wrinkle Editor palette."),
        FSlateIcon(),
        FUIAction(FExecuteAction::CreateSP(
            this,
            &SWetWrinkleEditorPanel::HandleSetWrinkleTextureHidden,
            Item,
            !Item->bHidden)));
    MenuBuilder.AddMenuEntry(
        LOCTEXT("CorrectWrinkleTexture", "Correct Normal"),
        LOCTEXT("CorrectWrinkleTextureTooltip", "Open the normal correction preview and create a corrected Texture2D."),
        FSlateIcon(),
        FUIAction(FExecuteAction::CreateSP(this, &SWetWrinkleEditorPanel::HandleCorrectWrinkleTexture, Item)));
    MenuBuilder.AddMenuEntry(
        LOCTEXT("BrowseWrinkleTexture", "Browse to Asset"),
        LOCTEXT("BrowseWrinkleTextureTooltip", "Select this texture in the Content Browser."),
        FSlateIcon(),
        FUIAction(FExecuteAction::CreateLambda([Item]()
        {
            if (GEditor != nullptr && Item.IsValid())
            {
                UObject* Asset = Item->Texture.Get();
                if (Asset == nullptr && Item->TexturePath.IsValid())
                {
                    Asset = Item->TexturePath.TryLoad();
                }
                if (Asset != nullptr)
                {
                    TArray<UObject*> Objects{Asset};
                    GEditor->SyncBrowserToObjects(Objects);
                }
            }
        })));

    FSlateApplication::Get().PushMenu(
        AsShared(),
        FWidgetPath(),
        MenuBuilder.MakeWidget(),
        MouseEvent.GetScreenSpacePosition(),
        FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));
    return FReply::Handled();
}

FReply SWetWrinkleEditorPanel::HandleRefreshWrinkleTexturePaletteClicked()
{
    RefreshWrinkleTexturePalette(true);
    RefreshWrinkleNormalThumbnail();
    PushBrushPreviewSettingsToViewport();
    return FReply::Handled();
}

FText SWetWrinkleEditorPanel::GetWrinkleTexturePaletteTooltipText(TSharedPtr<FWetWrinkleTexturePaletteItem> Item) const
{
    if (!Item.IsValid())
    {
        return FText::GetEmpty();
    }

    return FText::Format(
        LOCTEXT("WrinkleTexturePaletteTooltip", "{0}\n{1}\nTexture Status : {2}"),
        Item->DisplayName,
        FText::FromString(Item->TexturePath.ToString()),
        Item->bHidden ? LOCTEXT("TextureHidden", "Hidden") : LOCTEXT("TextureVisible", "Visible"));
}

FSlateColor SWetWrinkleEditorPanel::GetWrinkleTexturePaletteTileColor(TSharedPtr<FWetWrinkleTexturePaletteItem> Item) const
{
    if (!Item.IsValid() || !Item->bAssetAvailable)
    {
        return FSlateColor(FLinearColor(0.15f, 0.08f, 0.08f, 1.0f));
    }
    const UTexture2D* SelectedTexture = BrushSettings.WrinkleNormalTexture.Get();
    if (SelectedTexture != nullptr && Item->TexturePath == FSoftObjectPath(SelectedTexture))
    {
        return FSlateColor(FLinearColor(0.18f, 0.42f, 0.80f, 1.0f));
    }
    if (Item->bHidden)
    {
        return FSlateColor(FLinearColor(0.16f, 0.12f, 0.06f, 1.0f));
    }
    return FSlateColor(FLinearColor(0.10f, 0.10f, 0.10f, 1.0f));
}



#undef LOCTEXT_NAMESPACE
