#include "WetClothing/Common/Widgets/WetClothingEditorCommonWidgets.h"

#include "AssetThumbnail.h"
#include "DataAssets/WetClothingAsset.h"
#include "Engine/SkeletalMesh.h"
#include "Core/DWCEditorStyle.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "WetClothing/Common/Analysis/WetClothingAssetMeshAnalyzer.h"
#include "WetClothing/Common/Texture/WetClothingMaterialTextureResolver.h"
#include "WetClothing/Common/Widgets/SWetClothingMaterialSlotPreview.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"

namespace
{
    TArray<FWetClothingAssetUVTriangle> BuildMaterialSlotPreviewTriangles(const USkeletalMesh* SkeletalMesh, int32 MaterialSlotIndex)
    {
        TArray<FWetClothingAssetUVTriangle> PreviewTriangles;
        if (SkeletalMesh == nullptr)
        {
            return PreviewTriangles;
        }

        auto AppendMaterialSlotTriangles = [SkeletalMesh, &PreviewTriangles](int32 SlotIndex)
        {
            TArray<FWetClothingAssetUVIsland> BuiltIslands;
            if (!FWetClothingAssetMeshAnalyzer::BuildMaterialSlotUVIslands(SkeletalMesh, 0, 0, SlotIndex, BuiltIslands, nullptr))
            {
                return;
            }

            for (const FWetClothingAssetUVIsland& Island : BuiltIslands)
            {
                PreviewTriangles.Append(Island.UVTriangles);
            }
        };

        if (MaterialSlotIndex == INDEX_NONE)
        {
            const int32 MaterialCount = SkeletalMesh->GetMaterials().Num();
            for (int32 SlotIndex = 0; SlotIndex < MaterialCount; ++SlotIndex)
            {
                AppendMaterialSlotTriangles(SlotIndex);
            }
        }
        else
        {
            AppendMaterialSlotTriangles(MaterialSlotIndex);
        }

        return PreviewTriangles;
    }

    FText GetWetPartDisplayName(const FWetClothingWetPartEntry& Entry)
    {
        const FString TrimmedName = Entry.DisplayName.TrimStartAndEnd();
        if (!TrimmedName.IsEmpty())
        {
            return FText::FromString(TrimmedName);
        }

        return Entry.WetPartID == 0
                   ? NSLOCTEXT("WetClothingEditorCommonWidgets", "DefaultWetPartName", "Part Default")
                   : FText::Format(NSLOCTEXT("WetClothingEditorCommonWidgets", "NumberedWetPartName", "Part {0}"), FText::AsNumber(Entry.WetPartID));
    }
}

TSharedRef<SWidget> FWetClothingEditorCommonWidgets::BuildSectionHeader(const TAttribute<FText>& Title, const TAttribute<FText>& Detail)
{
    const FSlateFontInfo SectionHeadingFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 13);

    TSharedRef<SHorizontalBox> Header = SNew(SHorizontalBox)
        + SHorizontalBox::Slot()
              .FillWidth(1.0f)
              .VAlign(VAlign_Center)
                  [SNew(STextBlock)
                       .Text(Title)
                       .Font(SectionHeadingFont)];

    if (Detail.IsSet())
    {
        Header->AddSlot()
            .AutoWidth()
            .VAlign(VAlign_Center)
                [SNew(STextBlock)
                     .Text(Detail)];
    }

    return Header;
}

FText FWetClothingEditorCommonWidgets::GetUVDisplayModeLabel(EWetClothingAssetUVDisplayMode DisplayMode)
{
    return DisplayMode == EWetClothingAssetUVDisplayMode::OutlineOnly
               ? NSLOCTEXT("WetClothingEditorCommonWidgets", "UVDisplayModeOutline", "Outline")
               : NSLOCTEXT("WetClothingEditorCommonWidgets", "UVDisplayModeNormal", "Normal");
}

TSharedRef<SWidget> FWetClothingEditorCommonWidgets::GenerateUVDisplayModeComboItem(TSharedPtr<EWetClothingAssetUVDisplayMode> Item)
{
    return SNew(STextBlock)
        .Text(GetUVDisplayModeLabel(Item.IsValid() ? *Item : EWetClothingAssetUVDisplayMode::Normal));
}


TSharedRef<SWidget> FWetClothingEditorCommonWidgets::BuildTextureComboContent(
    TSharedPtr<FWetClothingTextureItem> Item,
    float ThumbnailSize,
    bool bCompactLayout,
    TSharedPtr<FAssetThumbnailPool> ThumbnailPool,
    TArray<TSharedPtr<FAssetThumbnail>>* ThumbnailSink)
{
    TSharedRef<SWidget> ThumbnailWidget =
        SNew(SBorder)
        .Padding(0.0f)
        .BorderImage(FAppStyle::Get().GetBrush(TEXT("WhiteBrush")))
        .BorderBackgroundColor(FLinearColor(0.06f, 0.06f, 0.06f, 1.0f));

    if (Item.IsValid() && Item->Texture.IsValid() && ThumbnailPool.IsValid())
    {
        const uint32 ThumbnailDimension = static_cast<uint32>(FMath::RoundToInt(ThumbnailSize));
        TSharedPtr<FAssetThumbnail> Thumbnail = MakeShared<FAssetThumbnail>(Item->Texture.Get(), ThumbnailDimension, ThumbnailDimension, ThumbnailPool);
        if (ThumbnailSink != nullptr)
        {
            ThumbnailSink->Add(Thumbnail);
        }

        FAssetThumbnailConfig ThumbnailConfig;
        ThumbnailConfig.bAllowFadeIn = false;
        ThumbnailWidget = Thumbnail->MakeThumbnailWidget(ThumbnailConfig);
    }

    return SNew(SHorizontalBox)
        + SHorizontalBox::Slot()
              .AutoWidth()
              .VAlign(VAlign_Center)
                  [SNew(SBox)
                       .WidthOverride(ThumbnailSize)
                       .HeightOverride(ThumbnailSize)
                           [ThumbnailWidget]]
        + SHorizontalBox::Slot()
              .FillWidth(1.0f)
              .VAlign(VAlign_Center)
              .Padding(bCompactLayout ? FMargin(8.0f, 0.0f, 18.0f, 0.0f) : FMargin(8.0f, 0.0f, 6.0f, 0.0f))
                  [SNew(STextBlock)
                       .Text(Item.IsValid() ? FText::FromString(Item->Label) : NSLOCTEXT("WetClothingEditorCommonWidgets", "SelectTextureComboItem", "Select Texture"))
                       .OverflowPolicy(ETextOverflowPolicy::Ellipsis)];
}

TSharedRef<SWidget> FWetClothingEditorCommonWidgets::GenerateTextureComboItem(
    TSharedPtr<FWetClothingTextureItem> Item,
    TSharedPtr<FAssetThumbnailPool> ThumbnailPool,
    TArray<TSharedPtr<FAssetThumbnail>>* ThumbnailSink)
{
    return BuildTextureComboContent(Item, 36.0f, false, ThumbnailPool, ThumbnailSink);
}

TSharedRef<SWidget> FWetClothingEditorCommonWidgets::BuildUVViewTextureSelector(
    TArray<TSharedPtr<FWetClothingTextureItem>>* TextureItems,
    TSharedPtr<FWetClothingTextureItem> SelectedTextureItem,
    TSharedPtr<FAssetThumbnailPool> ThumbnailPool,
    TArray<TSharedPtr<FAssetThumbnail>>* ThumbnailSink,
    TSharedPtr<SComboBox<TSharedPtr<FWetClothingTextureItem>>>* OutComboBox,
    TSharedPtr<SBox>* OutSelectedContentBox,
    TFunction<void(TSharedPtr<FWetClothingTextureItem>, ESelectInfo::Type)> OnSelectionChanged)
{
    TSharedPtr<SComboBox<TSharedPtr<FWetClothingTextureItem>>> LocalComboBox;
    TSharedPtr<SBox> LocalSelectedContentBox;

    TSharedRef<SWidget> ComboWidget =
        SAssignNew(LocalComboBox, SComboBox<TSharedPtr<FWetClothingTextureItem>>)
        .OptionsSource(TextureItems)
        .InitiallySelectedItem(SelectedTextureItem)
        .OnGenerateWidget_Lambda([ThumbnailPool, ThumbnailSink](TSharedPtr<FWetClothingTextureItem> Item)
        {
            return FWetClothingEditorCommonWidgets::GenerateTextureComboItem(Item, ThumbnailPool, ThumbnailSink);
        })
        .OnSelectionChanged_Lambda([OnSelectionChanged](TSharedPtr<FWetClothingTextureItem> Item, ESelectInfo::Type SelectInfo)
        {
            if (OnSelectionChanged)
            {
                OnSelectionChanged(Item, SelectInfo);
            }
        })
        .MaxListHeight(360.0f)
        .ContentPadding(FMargin(6.0f, 4.0f))
            [SAssignNew(LocalSelectedContentBox, SBox)
                 [BuildTextureComboContent(SelectedTextureItem, 24.0f, true, ThumbnailPool, ThumbnailSink)]];

    if (OutComboBox != nullptr)
    {
        *OutComboBox = LocalComboBox;
    }
    if (OutSelectedContentBox != nullptr)
    {
        *OutSelectedContentBox = LocalSelectedContentBox;
    }

    return ComboWidget;
}

TSharedRef<SWidget> FWetClothingEditorCommonWidgets::BuildUVViewTextureAndViewRow(
    const TSharedRef<SWidget>& TextureSelector,
    const TSharedRef<SWidget>& ViewOptionsButton)
{
    return SNew(SHorizontalBox)
        + SHorizontalBox::Slot()
              .FillWidth(1.0f)
                  [SNew(SBorder)
                       .Padding(6.0f)
                       .BorderImage(FAppStyle::Get().GetBrush(TEXT("ToolPanel.GroupBorder")))
                           [TextureSelector]]
        + SHorizontalBox::Slot()
              .AutoWidth()
              .VAlign(VAlign_Center)
              .Padding(10.0f, 0.0f, 0.0f, 0.0f)
                  [ViewOptionsButton];
}

TSharedRef<SWidget> FWetClothingEditorCommonWidgets::BuildUVViewOptionsButton(
    TArray<TSharedPtr<EWetClothingAssetUVDisplayMode>>* DisplayModeItems,
    TSharedPtr<EWetClothingAssetUVDisplayMode> SelectedDisplayModeItem,
    TAttribute<FText> SelectedDisplayModeText,
    TFunction<void(TSharedPtr<EWetClothingAssetUVDisplayMode>)> OnDisplayModeChanged,
    TAttribute<float> BackgroundTextureOpacity,
    TFunction<void(float)> OnBackgroundTextureOpacityChanged,
    TAttribute<float> UVIslandLineOpacity,
    TFunction<void(float)> OnUVIslandLineOpacityChanged,
    TAttribute<float> UVIslandLineThicknessScale,
    TFunction<void(float)> OnUVIslandLineThicknessScaleChanged)
{
    const FSlateFontInfo LabelFont = FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 9);

    auto BuildPercentText = [](const TAttribute<float>& ValueAttribute)
    {
        return TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateLambda([ValueAttribute]()
        {
            const float Value = ValueAttribute.IsSet() ? ValueAttribute.Get() : 1.0f;
            return FText::Format(
                NSLOCTEXT("WetClothingEditorCommonWidgets", "UVViewOpacityPercent", "{0}%"),
                FText::AsNumber(FMath::RoundToInt(FMath::Clamp(Value, 0.0f, 1.0f) * 100.0f)));
        }));
    };

    auto BuildLineWeightText = [](const TAttribute<float>& ValueAttribute)
    {
        return TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateLambda([ValueAttribute]()
        {
            const float Value = ValueAttribute.IsSet() ? ValueAttribute.Get() : 1.0f;
            FNumberFormattingOptions Options;
            Options.MinimumFractionalDigits = 1;
            Options.MaximumFractionalDigits = 1;
            return FText::Format(
                NSLOCTEXT("WetClothingEditorCommonWidgets", "UVViewLineWeightValue", "{0}x"),
                FText::AsNumber(FMath::Clamp(Value, 0.25f, 6.0f), &Options));
        }));
    };

    return SNew(SComboButton)
        .ContentPadding(FMargin(8.0f, 3.0f))
        .ButtonContent()
            [SNew(STextBlock)
                 .Text(NSLOCTEXT("WetClothingEditorCommonWidgets", "UVViewOptionsButton", "View"))]
        .MenuContent()
            [SNew(SBox)
                 .WidthOverride(270.0f)
                 .Padding(10.0f)
                     [SNew(SVerticalBox)

                      + SVerticalBox::Slot()
                            .AutoHeight()
                            .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                                [SNew(STextBlock)
                                     .Text(NSLOCTEXT("WetClothingEditorCommonWidgets", "UVViewModeLabel", "Mode"))
                                     .Font(LabelFont)]

                      + SVerticalBox::Slot()
                            .AutoHeight()
                            .Padding(0.0f, 0.0f, 0.0f, 12.0f)
                                [SNew(SComboBox<TSharedPtr<EWetClothingAssetUVDisplayMode>>)
                                     .OptionsSource(DisplayModeItems)
                                     .InitiallySelectedItem(SelectedDisplayModeItem)
                                     .OnGenerateWidget_Lambda([](TSharedPtr<EWetClothingAssetUVDisplayMode> Item)
                                     {
                                         return FWetClothingEditorCommonWidgets::GenerateUVDisplayModeComboItem(Item);
                                     })
                                     .OnSelectionChanged_Lambda([OnDisplayModeChanged](TSharedPtr<EWetClothingAssetUVDisplayMode> Item, ESelectInfo::Type)
                                     {
                                         if (Item.IsValid() && OnDisplayModeChanged)
                                         {
                                             OnDisplayModeChanged(Item);
                                         }
                                     })
                                         [SNew(STextBlock)
                                              .Text(SelectedDisplayModeText)]]

                      + SVerticalBox::Slot()
                            .AutoHeight()
                            .Padding(0.0f, 0.0f, 0.0f, 4.0f)
                                [SNew(SHorizontalBox)

                                 + SHorizontalBox::Slot()
                                       .FillWidth(1.0f)
                                       .VAlign(VAlign_Center)
                                           [SNew(STextBlock)
                                                .Text(NSLOCTEXT("WetClothingEditorCommonWidgets", "UVViewBackgroundOpacityLabel", "Background Texture Opacity"))
                                                .Font(LabelFont)]

                                 + SHorizontalBox::Slot()
                                       .AutoWidth()
                                       .VAlign(VAlign_Center)
                                           [SNew(STextBlock)
                                                .Text(BuildPercentText(BackgroundTextureOpacity))
                                                .Font(LabelFont)]]

                      + SVerticalBox::Slot()
                            .AutoHeight()
                            .Padding(0.0f, 0.0f, 0.0f, 12.0f)
                                [SNew(SSlider)
                                     .MinValue(0.0f)
                                     .MaxValue(1.0f)
                                     .Value(BackgroundTextureOpacity)
                                     .OnValueChanged_Lambda([OnBackgroundTextureOpacityChanged](float NewValue)
                                     {
                                         if (OnBackgroundTextureOpacityChanged)
                                         {
                                             OnBackgroundTextureOpacityChanged(NewValue);
                                         }
                                     })]

                      + SVerticalBox::Slot()
                            .AutoHeight()
                            .Padding(0.0f, 0.0f, 0.0f, 4.0f)
                                [SNew(SHorizontalBox)

                                 + SHorizontalBox::Slot()
                                       .FillWidth(1.0f)
                                       .VAlign(VAlign_Center)
                                           [SNew(STextBlock)
                                                .Text(NSLOCTEXT("WetClothingEditorCommonWidgets", "UVViewIslandLineOpacityLabel", "UV Island Line Opacity"))
                                                .Font(LabelFont)]

                                 + SHorizontalBox::Slot()
                                       .AutoWidth()
                                       .VAlign(VAlign_Center)
                                           [SNew(STextBlock)
                                                .Text(BuildPercentText(UVIslandLineOpacity))
                                                .Font(LabelFont)]]

                      + SVerticalBox::Slot()
                            .AutoHeight()
                            .Padding(0.0f, 0.0f, 0.0f, 12.0f)
                                [SNew(SSlider)
                                     .MinValue(0.0f)
                                     .MaxValue(1.0f)
                                     .Value(UVIslandLineOpacity)
                                     .OnValueChanged_Lambda([OnUVIslandLineOpacityChanged](float NewValue)
                                     {
                                         if (OnUVIslandLineOpacityChanged)
                                         {
                                             OnUVIslandLineOpacityChanged(NewValue);
                                         }
                                     })]

                      + SVerticalBox::Slot()
                            .AutoHeight()
                            .Padding(0.0f, 0.0f, 0.0f, 4.0f)
                                [SNew(SHorizontalBox)

                                 + SHorizontalBox::Slot()
                                       .FillWidth(1.0f)
                                       .VAlign(VAlign_Center)
                                           [SNew(STextBlock)
                                                .Text(NSLOCTEXT("WetClothingEditorCommonWidgets", "UVViewLineWeightLabel", "Line Weight"))
                                                .Font(LabelFont)]

                                 + SHorizontalBox::Slot()
                                       .AutoWidth()
                                       .VAlign(VAlign_Center)
                                           [SNew(STextBlock)
                                                .Text(BuildLineWeightText(UVIslandLineThicknessScale))
                                                .Font(LabelFont)]]

                      + SVerticalBox::Slot()
                            .AutoHeight()
                                [SNew(SSlider)
                                     .MinValue(0.25f)
                                     .MaxValue(6.0f)
                                     .Value(UVIslandLineThicknessScale)
                                     .OnValueChanged_Lambda([OnUVIslandLineThicknessScaleChanged](float NewValue)
                                     {
                                         if (OnUVIslandLineThicknessScaleChanged)
                                         {
                                             OnUVIslandLineThicknessScaleChanged(NewValue);
                                         }
                                     })]]];
}

TSharedRef<SWidget> FWetClothingEditorCommonWidgets::BuildPreviewSection(
    const TSharedRef<SWidget>& PreviewContent,
    const FOnWetClothingPreviewFocusClicked& OnFocusClicked,
    TSharedPtr<SWidget> ExtraToolbarContent)
{
    const FSlateFontInfo SectionHeadingFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 13);

    TSharedRef<SHorizontalBox> Header = SNew(SHorizontalBox)
        + SHorizontalBox::Slot()
              .FillWidth(1.0f)
              .VAlign(VAlign_Center)
                  [SNew(STextBlock)
                       .Text(NSLOCTEXT("WetClothingEditorCommonWidgets", "PreviewSectionTitle", "Preview"))
                       .Font(SectionHeadingFont)];

    if (ExtraToolbarContent.IsValid())
    {
        Header->AddSlot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            .Padding(0.0f, 0.0f, 10.0f, 0.0f)
                [ExtraToolbarContent.ToSharedRef()];
    }

    Header->AddSlot()
        .AutoWidth()
        .VAlign(VAlign_Center)
            [SNew(SButton)
                 .Text(NSLOCTEXT("WetClothingEditorCommonWidgets", "FocusMeshButton", "Focus Mesh"))
                 .OnClicked_Lambda([OnFocusClicked]()
                 {
                     return OnFocusClicked.IsBound() ? OnFocusClicked.Execute() : FReply::Handled();
                 })];

    return SNew(SBorder)
        .Padding(8.0f)
            [SNew(SVerticalBox)

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 10.0f, 0.0f, 4.0f)
                       [Header]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                       [SNew(SSeparator)
                            .Orientation(Orient_Horizontal)]

             + SVerticalBox::Slot()
                   .FillHeight(1.0f)
                       [PreviewContent]];
}

TSharedRef<SWidget> FWetClothingEditorCommonWidgets::BuildBakeMapsMenu(const FWetClothingBakeMapsMenuArgs& Args)
{
    FMenuBuilder MenuBuilder(true, nullptr);

    MenuBuilder.BeginSection(TEXT("BakeMapTypes"), NSLOCTEXT("WetClothingEditorCommonWidgets", "BakeMapTypesMenuSection", "BAKE"));
    switch (Args.CurrentMode)
    {
    case EWetClothingEditorMode::PartEdit:
        MenuBuilder.AddMenuEntry(
            NSLOCTEXT("WetClothingEditorCommonWidgets", "BakeAllWetPartMapsMenuItem", "Bake All WetPart Maps"),
            NSLOCTEXT("WetClothingEditorCommonWidgets", "BakeAllWetPartMapsMenuItemTooltip", "Bake wetness profile maps for all wettable material slots."),
            FSlateIcon(),
            FUIAction(FExecuteAction::CreateLambda([OnBakeAllWetnessProfileMaps = Args.OnBakeAllWetnessProfileMaps]()
            {
                if (OnBakeAllWetnessProfileMaps.IsBound())
                {
                    OnBakeAllWetnessProfileMaps.Execute();
                }
            })));
        MenuBuilder.AddMenuEntry(
            NSLOCTEXT("WetClothingEditorCommonWidgets", "BakeSelectedWetPartSlotMenuItem", "Bake Selected Material Slot"),
            NSLOCTEXT("WetClothingEditorCommonWidgets", "BakeSelectedWetPartSlotMenuItemTooltip", "Bake the wetness profile map for the selected material slot's texture and UV channel."),
            FSlateIcon(),
            FUIAction(FExecuteAction::CreateLambda([OnBakeSelectedWetnessProfileMap = Args.OnBakeSelectedWetnessProfileMap]()
            {
                if (OnBakeSelectedWetnessProfileMap.IsBound())
                {
                    OnBakeSelectedWetnessProfileMap.Execute();
                }
            })));
        break;

    case EWetClothingEditorMode::WrinkleEdit:
        MenuBuilder.AddMenuEntry(
            NSLOCTEXT("WetClothingEditorCommonWidgets", "BakeAllWrinkleNormalMapsMenuItem", "Bake All Wrinkle Normal Maps"),
            NSLOCTEXT("WetClothingEditorCommonWidgets", "BakeAllWrinkleNormalMapsMenuItemTooltip", "Bake wrinkle normal maps for every material slot that contains wrinkle patches."),
            FSlateIcon(),
            FUIAction(FExecuteAction::CreateLambda([OnBakeAllWrinkleNormalMaps = Args.OnBakeAllWrinkleNormalMaps]()
            {
                if (OnBakeAllWrinkleNormalMaps.IsBound())
                {
                    OnBakeAllWrinkleNormalMaps.Execute();
                }
            })));
        MenuBuilder.AddMenuEntry(
            NSLOCTEXT("WetClothingEditorCommonWidgets", "BakeSelectedWrinkleSlotMenuItem", "Bake Selected Material Slot"),
            NSLOCTEXT("WetClothingEditorCommonWidgets", "BakeSelectedWrinkleSlotMenuItemTooltip", "Bake the wrinkle normal map for the selected material slot."),
            FSlateIcon(),
            FUIAction(FExecuteAction::CreateLambda([OnBakeSelectedWrinkleNormalMap = Args.OnBakeSelectedWrinkleNormalMap]()
            {
                if (OnBakeSelectedWrinkleNormalMap.IsBound())
                {
                    OnBakeSelectedWrinkleNormalMap.Execute();
                }
            })));
        break;

    case EWetClothingEditorMode::TransparencyBake:
        MenuBuilder.AddMenuEntry(
            NSLOCTEXT("WetClothingEditorCommonWidgets", "BakeTransparencyRevealMapsMenuItem", "Bake & Save Transparency Reveal Maps"),
            NSLOCTEXT("WetClothingEditorCommonWidgets", "BakeTransparencyRevealMapsMenuItemTooltip", "Bake reveal textures/materials for Transparency mode and save the generated assets."),
            FSlateIcon(),
            FUIAction(FExecuteAction::CreateLambda([OnBakeTransparencyRevealMaps = Args.OnBakeTransparencyRevealMaps]()
            {
                if (OnBakeTransparencyRevealMaps.IsBound())
                {
                    OnBakeTransparencyRevealMaps.Execute();
                }
            })));
        break;
    }
    MenuBuilder.EndSection();

    return MenuBuilder.MakeWidget();
}

TSharedRef<ITableRow> FWetClothingEditorCommonWidgets::GenerateMaterialSlotRow(
    TSharedPtr<FWetClothingMaterialSlotItem> Item,
    const TSharedRef<STableViewBase>& OwnerTable,
    const FWetClothingMaterialSlotRowArgs& Args)
{
    const int32 MaterialSlotIndex = Item.IsValid() ? Item->SlotIndex : INDEX_NONE;
    const bool  bIsAllSlotsRow = Item.IsValid() && MaterialSlotIndex == INDEX_NONE;
    UMaterialInterface* MaterialObject = !bIsAllSlotsRow && Item.IsValid() ? Item->Material.Get() : nullptr;
    const FText SlotTitle = bIsAllSlotsRow
                                ? NSLOCTEXT("WetClothingEditorCommonWidgets", "AllMaterialSlotsTitle", "All Slots")
                                : Item.IsValid()
                                ? FText::Format(
                                      NSLOCTEXT("WetClothingEditorCommonWidgets", "MaterialSlotThumbnailTitle", "[{0}] {1}"),
                                      FText::AsNumber(MaterialSlotIndex),
                                      FText::FromName(Item->SlotName))
                                : NSLOCTEXT("WetClothingEditorCommonWidgets", "InvalidMaterialSlotTitle", "Invalid Material Slot");

    TSharedRef<SWidget> ThumbnailWidget =
        SNew(SBorder)
        .BorderImage(FAppStyle::Get().GetBrush(TEXT("WhiteBrush")))
        .BorderBackgroundColor(FLinearColor(0.06f, 0.06f, 0.06f, 1.0f));

    if (bIsAllSlotsRow && Args.TargetMesh != nullptr && Args.ThumbnailPool.IsValid())
    {
        TSharedPtr<FAssetThumbnail> MeshThumbnail = MakeShared<FAssetThumbnail>(Args.TargetMesh, 48, 48, Args.ThumbnailPool);
        if (Args.ThumbnailSink != nullptr)
        {
            Args.ThumbnailSink->Add(MeshThumbnail);
        }

        FAssetThumbnailConfig ThumbnailConfig;
        ThumbnailConfig.bAllowFadeIn = false;
        ThumbnailWidget = MeshThumbnail->MakeThumbnailWidget(ThumbnailConfig);
    }
    else if (MaterialObject != nullptr && Args.ThumbnailPool.IsValid())
    {
        TSharedPtr<FAssetThumbnail> Thumbnail = MakeShared<FAssetThumbnail>(MaterialObject, 48, 48, Args.ThumbnailPool);
        if (Args.ThumbnailSink != nullptr)
        {
            Args.ThumbnailSink->Add(Thumbnail);
        }

        FAssetThumbnailConfig ThumbnailConfig;
        ThumbnailConfig.bAllowFadeIn = false;
        ThumbnailWidget = Thumbnail->MakeThumbnailWidget(ThumbnailConfig);
    }

    UTexture* SlotPreviewTexture = !bIsAllSlotsRow && Args.OverridePreviewTexture != nullptr && MaterialSlotIndex == Args.SelectedMaterialSlotIndex
                                       ? Args.OverridePreviewTexture
                                       : (!bIsAllSlotsRow ? FWetClothingMaterialTextureResolver::ResolveBestMaterialTexture(MaterialObject) : nullptr);

    TArray<FWetClothingAssetUVTriangle> SlotPreviewTriangles = BuildMaterialSlotPreviewTriangles(Args.TargetMesh, MaterialSlotIndex);
    TSharedRef<SWidget> SlotPreviewWidget =
        SNew(SBorder)
        .Padding(0.0f)
        .BorderImage(FAppStyle::Get().GetBrush(TEXT("Brushes.Panel")))
            [SNew(SWetClothingMaterialSlotPreview)
                 .Triangles(MoveTemp(SlotPreviewTriangles))
                 .PreviewTexture(SlotPreviewTexture)
                 .DrawWireframe(true)];

    TSharedRef<SHorizontalBox> RowContent = SNew(SHorizontalBox);

    RowContent->AddSlot()
        .AutoWidth()
        .VAlign(VAlign_Center)
        .Padding(0.0f, 0.0f, 8.0f, 0.0f)
            [SNew(SBox)
                 .WidthOverride(52.0f)
                 .HeightOverride(52.0f)
                     [ThumbnailWidget]];

    if (!bIsAllSlotsRow)
    {
        RowContent->AddSlot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            .Padding(0.0f, 0.0f, 8.0f, 0.0f)
                [SNew(SBox)
                     .WidthOverride(52.0f)
                     .HeightOverride(52.0f)
                         [SlotPreviewWidget]];
    }

    RowContent->AddSlot()
        .FillWidth(1.0f)
        .VAlign(VAlign_Center)
        .Padding(2.0f, 0.0f, 10.0f, 0.0f)
            [SNew(STextBlock)
                 .Text(SlotTitle)
                 .Font(FAppStyle::GetFontStyle(TEXT("NormalFontBold")))
                 .OverflowPolicy(ETextOverflowPolicy::Ellipsis)];

    if (!bIsAllSlotsRow)
    {
        const FText StatusText = Args.GetMaterialSlotStatusText ? Args.GetMaterialSlotStatusText(MaterialSlotIndex) : FText::GetEmpty();
        RowContent->AddSlot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            .Padding(0.0f, 0.0f, 10.0f, 0.0f)
                [SNew(STextBlock)
                     .Text(StatusText)
                     .Font(FAppStyle::GetFontStyle(TEXT("SmallFont")))
                     .ColorAndOpacity(FSlateColor(FLinearColor(0.58f, 0.58f, 0.58f, 1.0f)))];

        const bool bWettable = Item.IsValid() && Item->bIsWettableSlot;
        const FSlateColor WettableColor = bWettable
                                               ? FSlateColor(FLinearColor(0.35f, 0.85f, 1.0f, 1.0f))
                                               : FSlateColor(FLinearColor(1.0f, 0.36f, 0.36f, 1.0f));
        const FName WettableBrushName = bWettable
                                            ? TEXT("DWCEditor.Part.IsWettable.True")
                                            : TEXT("DWCEditor.Part.IsWettable.False");

        RowContent->AddSlot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            .Padding(0.0f, 0.0f, 2.0f, 0.0f)
                [SNew(SButton)
                     .ButtonStyle(FAppStyle::Get(), TEXT("NoBorder"))
                     .ContentPadding(FMargin(4.0f, 2.0f))
                     .ToolTipText(NSLOCTEXT("WetClothingEditorCommonWidgets", "WettableSlotTooltip", "Toggle whether this material slot can be wetted."))
                     .OnClicked_Lambda([OnClicked = Args.OnWettableSlotClicked, MaterialSlotIndex]()
                     {
                         return OnClicked.IsBound() ? OnClicked.Execute(MaterialSlotIndex) : FReply::Handled();
                     })
                         [SNew(SBox)
                              .WidthOverride(30.0f)
                              .HeightOverride(30.0f)
                              .HAlign(HAlign_Center)
                              .VAlign(VAlign_Center)
                                  [SNew(SImage)
                                       .DesiredSizeOverride(FVector2D(30.0f, 30.0f))
                                       .Image(FDWCEditorStyle::GetBrush(WettableBrushName))
                                       .ColorAndOpacity(WettableColor)]]];
    }

    return SNew(STableRow<TSharedPtr<FWetClothingMaterialSlotItem>>, OwnerTable)
        .Padding(4.0f)
            [RowContent];
}

TSharedRef<ITableRow> FWetClothingEditorCommonWidgets::GeneratePartMapRow(
    TSharedPtr<FWetClothingWetPartEntry> Item,
    const TSharedRef<STableViewBase>& OwnerTable)
{
    const FLinearColor Color = Item.IsValid() ? (Item->WetPartID == 0 ? FLinearColor::White : Item->Color) : FLinearColor::White;
    const FText DisplayName = Item.IsValid() ? GetWetPartDisplayName(*Item) : NSLOCTEXT("WetClothingEditorCommonWidgets", "InvalidWetPartName", "Invalid Part");
    const FText IDText = Item.IsValid()
                             ? FText::Format(NSLOCTEXT("WetClothingEditorCommonWidgets", "WetPartRowIDLabel", "ID {0}"), FText::AsNumber(Item->WetPartID))
                             : NSLOCTEXT("WetClothingEditorCommonWidgets", "InvalidWetPartIDLabel", "Invalid");

    return SNew(STableRow<TSharedPtr<FWetClothingWetPartEntry>>, OwnerTable)
        .Padding(4.0f)
            [SNew(SHorizontalBox)

             + SHorizontalBox::Slot()
                   .AutoWidth()
                   .VAlign(VAlign_Center)
                   .Padding(0.0f, 0.0f, 8.0f, 0.0f)
                       [SNew(SBox)
                            .WidthOverride(20.0f)
                            .HeightOverride(20.0f)
                                [SNew(SBorder)
                                     .BorderImage(FAppStyle::Get().GetBrush(TEXT("WhiteBrush")))
                                     .BorderBackgroundColor(Color)]]

             + SHorizontalBox::Slot()
                   .FillWidth(1.0f)
                   .VAlign(VAlign_Center)
                       [SNew(STextBlock)
                            .Text(DisplayName)
                            .OverflowPolicy(ETextOverflowPolicy::Ellipsis)]

             + SHorizontalBox::Slot()
                   .AutoWidth()
                   .VAlign(VAlign_Center)
                       [SNew(STextBlock)
                            .Text(IDText)
                            .ColorAndOpacity(FSlateColor(FLinearColor(0.72f, 0.72f, 0.72f, 1.0f)))]];
}

bool FWetClothingEditorCommonWidgets::IsMaterialSlotWettable(const UWetClothingAsset* WetClothingAsset, int32 MaterialSlotIndex)
{
    if (WetClothingAsset == nullptr || MaterialSlotIndex == INDEX_NONE)
    {
        return false;
    }

    const FWetClothingWettableMaterialSlotState* State =
        WetClothingAsset->PartData.EditableWetPartData.WettableMaterialSlots.FindByPredicate(
            [MaterialSlotIndex](const FWetClothingWettableMaterialSlotState& Candidate)
            {
                return Candidate.MaterialSlotIndex == MaterialSlotIndex;
            });

    return State != nullptr && State->bIsWettableSlot;
}

void FWetClothingEditorCommonWidgets::SetMaterialSlotWettable(UWetClothingAsset* WetClothingAsset, int32 MaterialSlotIndex, bool bIsWettableSlot)
{
    if (WetClothingAsset == nullptr || MaterialSlotIndex == INDEX_NONE)
    {
        return;
    }

    WetClothingAsset->Modify();
    FWetClothingWettableMaterialSlotState* State =
        WetClothingAsset->PartData.EditableWetPartData.WettableMaterialSlots.FindByPredicate(
            [MaterialSlotIndex](const FWetClothingWettableMaterialSlotState& Candidate)
            {
                return Candidate.MaterialSlotIndex == MaterialSlotIndex;
            });

    if (State == nullptr)
    {
        State = &WetClothingAsset->PartData.EditableWetPartData.WettableMaterialSlots.AddDefaulted_GetRef();
        State->MaterialSlotIndex = MaterialSlotIndex;
    }

    State->bIsWettableSlot = bIsWettableSlot;
    if (!bIsWettableSlot)
    {
        WetClothingAsset->PartData.GeneratedWetMaterialOverrides.RemoveAll(
            [MaterialSlotIndex](const FWetClothingGeneratedWetMaterialOverride& MaterialOverride)
            {
                return MaterialOverride.MaterialSlotIndex == MaterialSlotIndex;
            });
    }
    WetClothingAsset->MarkPackageDirty();
}

void FWetClothingEditorCommonWidgets::MarkMaterialSlotWettable(UWetClothingAsset* WetClothingAsset, int32 MaterialSlotIndex)
{
    SetMaterialSlotWettable(WetClothingAsset, MaterialSlotIndex, true);
}
