#include "WCAEditorWidgets.h"

#include "AssetThumbnail.h"
#include "DataAssets/WetClothingAsset.h"
#include "Engine/SkeletalMesh.h"
#include "Core/DWCEditorStyle.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "WetClothing/WCAEditor/UI/Widgets/SWCAMaterialSlotPreview.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"

namespace
{
    FLinearColor GetUnassignedPartColor()
    {
        return FLinearColor(0.32f, 0.32f, 0.32f, 1.0f);
    }

    FText GetWetPartDisplayName(const FWetClothingWetPartEntry& Entry)
    {
        if (Entry.WetPartID == 0)
        {
            return NSLOCTEXT("WetClothingEditorCommonWidgets", "UnassignedWetPartName", "Unassigned");
        }

        const FString TrimmedName = Entry.DisplayName.TrimStartAndEnd();
        if (!TrimmedName.IsEmpty())
        {
            return FText::FromString(TrimmedName);
        }

        return FText::Format(NSLOCTEXT("WetClothingEditorCommonWidgets", "NumberedWetPartName", "Part {0}"), FText::AsNumber(Entry.WetPartID));
    }

    FText GetWettableSlotMissingProfileWarningText(const UWetClothingAsset* WetClothingAsset, int32 MaterialSlotIndex)
    {
        if (WetClothingAsset == nullptr ||
            MaterialSlotIndex == INDEX_NONE ||
            !WetClothingAsset->IsMaterialSlotWettable(MaterialSlotIndex))
        {
            return FText::GetEmpty();
        }

        int32 MissingProfilePartCount = 0;
        const FWetClothingEditableWetPartData& EditableData = WetClothingAsset->Authored.PartData.EditableWetPartData;
        if (const FWetClothingAuthoredMaterialSlot* SlotData = EditableData.FindMaterialSlot(MaterialSlotIndex))
        {
            for (const FWetClothingWetPartEntry& Entry : SlotData->WetPartEntries)
            {
                if (Entry.WetPartID == 0)
                {
                    continue;
                }

                const FWetPartProfileAssignment* Profile = EditableData.FindProfile(Entry);
                if (Profile == nullptr || !Profile->SourceProfile.IsValid())
                {
                    ++MissingProfilePartCount;
                }
            }
        }

        if (MissingProfilePartCount <= 0)
        {
            return FText::GetEmpty();
        }

        return MissingProfilePartCount == 1
                   ? NSLOCTEXT("WetClothingEditorCommonWidgets", "WettableSlotOneMissingProfileWarning", "1 part has no Wetness Profile.")
                   : FText::Format(
                         NSLOCTEXT("WetClothingEditorCommonWidgets", "WettableSlotManyMissingProfilesWarning", "{0} parts have no Wetness Profile."),
                         FText::AsNumber(MissingProfilePartCount));
    }

}

TSharedRef<SWidget> FWCAEditorWidgets::BuildSectionHeader(const TAttribute<FText>& Title, const TAttribute<FText>& Detail)
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

TSharedRef<SWidget> FWCAEditorWidgets::BuildTextureComboContent(
    TSharedPtr<FWCATextureItem> Item,
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

TSharedRef<SWidget> FWCAEditorWidgets::GenerateTextureComboItem(
    TSharedPtr<FWCATextureItem> Item,
    TSharedPtr<FAssetThumbnailPool> ThumbnailPool,
    TArray<TSharedPtr<FAssetThumbnail>>* ThumbnailSink)
{
    return BuildTextureComboContent(Item, 36.0f, false, ThumbnailPool, ThumbnailSink);
}

TSharedRef<SWidget> FWCAEditorWidgets::BuildUVViewTextureSelector(
    TArray<TSharedPtr<FWCATextureItem>>* TextureItems,
    TSharedPtr<FWCATextureItem> SelectedTextureItem,
    TSharedPtr<FAssetThumbnailPool> ThumbnailPool,
    TArray<TSharedPtr<FAssetThumbnail>>* ThumbnailSink,
    TSharedPtr<SComboBox<TSharedPtr<FWCATextureItem>>>* OutComboBox,
    TSharedPtr<SBox>* OutSelectedContentBox,
    TFunction<void(TSharedPtr<FWCATextureItem>, ESelectInfo::Type)> OnSelectionChanged)
{
    TSharedPtr<SComboBox<TSharedPtr<FWCATextureItem>>> LocalComboBox;
    TSharedPtr<SBox> LocalSelectedContentBox;

    TSharedRef<SWidget> ComboWidget =
        SAssignNew(LocalComboBox, SComboBox<TSharedPtr<FWCATextureItem>>)
        .OptionsSource(TextureItems)
        .InitiallySelectedItem(SelectedTextureItem)
        .OnGenerateWidget_Lambda([ThumbnailPool, ThumbnailSink](TSharedPtr<FWCATextureItem> Item)
        {
            return FWCAEditorWidgets::GenerateTextureComboItem(Item, ThumbnailPool, ThumbnailSink);
        })
        .OnSelectionChanged_Lambda([OnSelectionChanged](TSharedPtr<FWCATextureItem> Item, ESelectInfo::Type SelectInfo)
        {
            if (OnSelectionChanged)
            {
                OnSelectionChanged(Item, SelectInfo);
            }
        })
        .ComboBoxStyle(&FAppStyle::Get().GetWidgetStyle<FComboBoxStyle>(TEXT("ComboBox")))
        .ButtonStyle(FAppStyle::Get(), TEXT("Button"))
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

TSharedRef<SWidget> FWCAEditorWidgets::BuildUVViewTextureAndViewRow(
    const TSharedRef<SWidget>& TextureSelector,
    const TSharedRef<SWidget>& ViewOptionsButton)
{
    return SNew(SHorizontalBox)
        + SHorizontalBox::Slot()
        .AutoWidth()
        .VAlign(VAlign_Center)
        .Padding(0.0f, 0.0f, 8.0f, 0.0f)
        [
            SNew(STextBlock)
            .Text(NSLOCTEXT("WetClothingEditorCommonWidgets", "UVViewTextureLabel", "Background"))
            .Font(FAppStyle::GetFontStyle(TEXT("SmallFont")))
            .ColorAndOpacity(FSlateColor::UseSubduedForeground())
        ]

        + SHorizontalBox::Slot()
        .FillWidth(1.0f)
        .VAlign(VAlign_Center)
        [
            SNew(SBox)
            .MinDesiredHeight(32.0f)
            .VAlign(VAlign_Center)
            [
                TextureSelector
            ]
        ]

        + SHorizontalBox::Slot()
        .AutoWidth()
        .VAlign(VAlign_Center)
        .Padding(4.0f, 0.0f, 0.0f, 0.0f)
        [
            ViewOptionsButton
        ];
}

TSharedRef<SWidget> FWCAEditorWidgets::BuildUVViewOptionsButton(
    TAttribute<float> BackgroundTextureOpacity,
    TFunction<void(float)> OnBackgroundTextureOpacityChanged,
    TAttribute<float> UVIslandLineOpacity,
    TFunction<void(float)> OnUVIslandLineOpacityChanged,
    TAttribute<float> UVIslandLineThicknessScale,
    TFunction<void(float)> OnUVIslandLineThicknessScaleChanged,
    const bool bShowBackgroundTextureControls)
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

    TSharedRef<SVerticalBox> OptionsPanel = SNew(SVerticalBox);

    if (bShowBackgroundTextureControls)
    {
        OptionsPanel->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 0.0f, 0.0f, 4.0f)
            [
                SNew(SHorizontalBox)

                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                .VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(NSLOCTEXT("WetClothingEditorCommonWidgets", "UVViewBackgroundOpacityLabel", "Background Texture Opacity"))
                    .Font(LabelFont)
                ]

                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .Text(BuildPercentText(BackgroundTextureOpacity))
                    .Font(LabelFont)
                ]
            ];

        OptionsPanel->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 0.0f, 0.0f, 12.0f)
            [
                SNew(SSlider)
                .MinValue(0.0f)
                .MaxValue(1.0f)
                .Value(BackgroundTextureOpacity)
                .OnValueChanged_Lambda([OnBackgroundTextureOpacityChanged](float NewValue)
                {
                    if (OnBackgroundTextureOpacityChanged)
                    {
                        OnBackgroundTextureOpacityChanged(NewValue);
                    }
                })
            ];
    }

    OptionsPanel->AddSlot()
        .AutoHeight()
        .Padding(0.0f, 0.0f, 0.0f, 4.0f)
        [
            SNew(SHorizontalBox)

            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            .VAlign(VAlign_Center)
            [
                SNew(STextBlock)
                .Text(NSLOCTEXT("WetClothingEditorCommonWidgets", "UVViewIslandLineOpacityLabel", "UV Line Opacity"))
                .Font(LabelFont)
            ]

            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            [
                SNew(STextBlock)
                .Text(BuildPercentText(UVIslandLineOpacity))
                .Font(LabelFont)
            ]
        ];

    OptionsPanel->AddSlot()
        .AutoHeight()
        .Padding(0.0f, 0.0f, 0.0f, 12.0f)
        [
            SNew(SSlider)
            .MinValue(0.0f)
            .MaxValue(1.0f)
            .Value(UVIslandLineOpacity)
            .OnValueChanged_Lambda([OnUVIslandLineOpacityChanged](float NewValue)
            {
                if (OnUVIslandLineOpacityChanged)
                {
                    OnUVIslandLineOpacityChanged(NewValue);
                }
            })
        ];

    OptionsPanel->AddSlot()
        .AutoHeight()
        .Padding(0.0f, 0.0f, 0.0f, 4.0f)
        [
            SNew(SHorizontalBox)

            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            .VAlign(VAlign_Center)
            [
                SNew(STextBlock)
                .Text(NSLOCTEXT("WetClothingEditorCommonWidgets", "UVViewLineWeightLabel", "UV Line Thickness"))
                .Font(LabelFont)
            ]

            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            [
                SNew(STextBlock)
                .Text(BuildLineWeightText(UVIslandLineThicknessScale))
                .Font(LabelFont)
            ]
        ];

    OptionsPanel->AddSlot()
        .AutoHeight()
        [
            SNew(SSlider)
            .MinValue(0.25f)
            .MaxValue(6.0f)
            .Value(UVIslandLineThicknessScale)
            .OnValueChanged_Lambda([OnUVIslandLineThicknessScaleChanged](float NewValue)
            {
                if (OnUVIslandLineThicknessScaleChanged)
                {
                    OnUVIslandLineThicknessScaleChanged(NewValue);
                }
            })
        ];

    return SNew(SComboButton)
        .HasDownArrow(true)
        .ButtonStyle(FAppStyle::Get(), TEXT("Button"))
        .ContentPadding(FMargin(6.0f, 3.0f))
        .ToolTipText(NSLOCTEXT("WetClothingEditorCommonWidgets", "UVViewOptionsButtonTooltip", "View options"))
        .ButtonContent()
        [
            SNew(SBox)
            .WidthOverride(20.0f)
            .HeightOverride(18.0f)
            .HAlign(HAlign_Center)
            .VAlign(VAlign_Center)
            [
                SNew(SImage)
                .DesiredSizeOverride(FVector2D(16.0f, 16.0f))
                .Image(FAppStyle::GetBrush(TEXT("Icons.Visible")))
            ]
        ]
        .MenuContent()
        [
            SNew(SBox)
            .WidthOverride(270.0f)
            [
                SNew(SBorder)
                .BorderImage(FAppStyle::Get().GetBrush(TEXT("NoBorder")))
                .Padding(10.0f)
                [
                    OptionsPanel
                ]
            ]
        ];
}

TSharedRef<SWidget> FWCAEditorWidgets::BuildUVViewOptionsButton(
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
    const bool bShowBackgroundTextureControls)
{
    TSharedRef<SWidget> ViewOptionsButton = BuildUVViewOptionsButton(
        BackgroundTextureOpacity,
        MoveTemp(OnBackgroundTextureOpacityChanged),
        UVIslandLineOpacity,
        MoveTemp(OnUVIslandLineOpacityChanged),
        UVIslandLineThicknessScale,
        MoveTemp(OnUVIslandLineThicknessScaleChanged),
        bShowBackgroundTextureControls);

    TSharedRef<SWidget> DisplayModeCombo = SNew(SComboBox<TSharedPtr<EWCAUVDisplayMode>>)
        .OptionsSource(DisplayModeItems)
        .InitiallySelectedItem(SelectedDisplayModeItem)
        .OnGenerateWidget_Lambda([](TSharedPtr<EWCAUVDisplayMode> Item)
        {
            return GenerateUVDisplayModeComboItem(Item);
        })
        .OnSelectionChanged_Lambda([OnDisplayModeChanged](TSharedPtr<EWCAUVDisplayMode> Item, ESelectInfo::Type)
        {
            if (OnDisplayModeChanged)
            {
                OnDisplayModeChanged(Item);
            }
        })
        .ContentPadding(FMargin(6.0f, 3.0f))
        [SNew(STextBlock)
             .Text(SelectedDisplayModeText)];

    return SNew(SHorizontalBox)
        + SHorizontalBox::Slot()
              .AutoWidth()
              .VAlign(VAlign_Center)
                  [DisplayModeCombo]
        + SHorizontalBox::Slot()
              .AutoWidth()
              .VAlign(VAlign_Center)
              .Padding(4.0f, 0.0f, 0.0f, 0.0f)
                  [ViewOptionsButton];
}

TSharedRef<SWidget> FWCAEditorWidgets::GenerateUVDisplayModeComboItem(TSharedPtr<EWCAUVDisplayMode> Item)
{
    return SNew(STextBlock)
        .Text(Item.IsValid() ? GetUVDisplayModeLabel(*Item) : FText::GetEmpty());
}

FText FWCAEditorWidgets::GetUVDisplayModeLabel(EWCAUVDisplayMode DisplayMode)
{
    return DisplayMode == EWCAUVDisplayMode::OutlineOnly
        ? NSLOCTEXT("WetClothingEditorCommonWidgets", "UVDisplayModeOutlineOnly", "Outline Only")
        : NSLOCTEXT("WetClothingEditorCommonWidgets", "UVDisplayModeNormal", "Normal");
}

TSharedRef<SWidget> FWCAEditorWidgets::BuildPreviewSection(
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

TSharedRef<SWidget> FWCAEditorWidgets::BuildRuntimeBuildMenu(const FWCARuntimeBuildMenuArgs& Args)
{
    FMenuBuilder MenuBuilder(true, nullptr);

    MenuBuilder.BeginSection(TEXT("BuildForRuntimeAll"));
    MenuBuilder.AddMenuEntry(
        NSLOCTEXT("WetClothingEditorCommonWidgets", "BuildAllRequiredMenuItem", "Build All Required"),
        NSLOCTEXT("WetClothingEditorCommonWidgets", "BuildAllRequiredMenuItemTooltip", "Build and save every currently required runtime output in dependency order."),
        FSlateIcon(FDWCEditorStyle::GetStyleSetName(), TEXT("DWCEditor.BuildForRuntime"), TEXT("DWCEditor.BuildForRuntime.Small")),
        FUIAction(FExecuteAction::CreateLambda([OnBuildAllRequired = Args.OnBuildAllRequired]()
        {
            if (OnBuildAllRequired.IsBound()) OnBuildAllRequired.Execute();
        }), Args.CanBuildAllRequired));
    MenuBuilder.EndSection();

    MenuBuilder.BeginSection(TEXT("BuildForRuntimeData"), NSLOCTEXT("WetClothingEditorCommonWidgets", "BuildForRuntimeDataMenuSection", "RUNTIME DATA"));
    MenuBuilder.AddMenuEntry(
        NSLOCTEXT("WetClothingEditorCommonWidgets", "BuildCPURuntimeDataMenuItem", "Build CPU Runtime Data"),
        NSLOCTEXT("WetClothingEditorCommonWidgets", "BuildCPURuntimeDataMenuItemTooltip", "Build and save the CPU vertex-simulation runtime payload."),
        FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("ClassIcon.DataAsset")),
        FUIAction(FExecuteAction::CreateLambda([OnBuildCPURuntimeData = Args.OnBuildCPURuntimeData]()
        {
            if (OnBuildCPURuntimeData.IsBound()) OnBuildCPURuntimeData.Execute();
        }), Args.CanBuildCPURuntimeData));
    MenuBuilder.AddMenuEntry(
        NSLOCTEXT("WetClothingEditorCommonWidgets", "BuildGPURuntimeDataMenuItem", "Build GPU Runtime Data"),
        NSLOCTEXT("WetClothingEditorCommonWidgets", "BuildGPURuntimeDataMenuItemTooltip", "Build and save the GPU runtime payload together with its resolution-dependent simulation lookup data."),
        FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("ClassIcon.DataAsset")),
        FUIAction(FExecuteAction::CreateLambda([OnBuildGPURuntimeData = Args.OnBuildGPURuntimeData]()
        {
            if (OnBuildGPURuntimeData.IsBound()) OnBuildGPURuntimeData.Execute();
        }), Args.CanBuildGPURuntimeData));
    MenuBuilder.EndSection();

    MenuBuilder.BeginSection(TEXT("BuildForRuntimeAssets"), NSLOCTEXT("WetClothingEditorCommonWidgets", "BuildForRuntimeAssetsMenuSection", "GENERATED ASSETS"));
    MenuBuilder.AddMenuEntry(
        NSLOCTEXT("WetClothingEditorCommonWidgets", "GenerateMaterialsMenuItem", "Generate Materials"),
        NSLOCTEXT("WetClothingEditorCommonWidgets", "GenerateMaterialsMenuItemTooltip", "Generate or update the unified DWC material and CPU/GPU material instances."),
        FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("ClassIcon.Material")),
        FUIAction(FExecuteAction::CreateLambda([OnGenerateMaterials = Args.OnGenerateMaterials]()
        {
            if (OnGenerateMaterials.IsBound()) OnGenerateMaterials.Execute();
        }), Args.CanGenerateMaterials));
    MenuBuilder.AddMenuEntry(
        NSLOCTEXT("WetClothingEditorCommonWidgets", "BakeRenderProfileDataMenuItem", "Bake Render Profile Lookup Texture"),
        NSLOCTEXT("WetClothingEditorCommonWidgets", "BakeRenderProfileDataMenuItemTooltip", "Bake the Render Profile Lookup Texture and associated local profile data used by CPU and GPU wetness rendering."),
        FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("ClassIcon.Texture2D")),
        FUIAction(FExecuteAction::CreateLambda([OnBuildRenderProfileData = Args.OnBuildRenderProfileData]()
        {
            if (OnBuildRenderProfileData.IsBound()) OnBuildRenderProfileData.Execute();
        }), Args.CanBuildRenderProfileData));
    MenuBuilder.AddMenuEntry(
        NSLOCTEXT("WetClothingEditorCommonWidgets", "BakeWrinkleTexturesMenuItem", "Bake Wrinkle Textures"),
        NSLOCTEXT("WetClothingEditorCommonWidgets", "BakeWrinkleTexturesMenuItemTooltip", "Bake all required wrinkle normal and mask textures."),
        FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("ClassIcon.Texture2D")),
        FUIAction(FExecuteAction::CreateLambda([OnBakeWrinkleTextures = Args.OnBakeWrinkleTextures]()
        {
            if (OnBakeWrinkleTextures.IsBound()) OnBakeWrinkleTextures.Execute();
        }), Args.CanBakeWrinkleTextures));
    MenuBuilder.AddMenuEntry(
        NSLOCTEXT("WetClothingEditorCommonWidgets", "BakeTransparencyTexturesMenuItem", "Bake Transparency Textures"),
        NSLOCTEXT("WetClothingEditorCommonWidgets", "BakeTransparencyTexturesMenuItemTooltip", "Generate and bake packed transparency textures for required transparency layers."),
        FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("ClassIcon.Texture2D")),
        FUIAction(FExecuteAction::CreateLambda([OnBakeTransparencyTextures = Args.OnBakeTransparencyTextures]()
        {
            if (OnBakeTransparencyTextures.IsBound()) OnBakeTransparencyTextures.Execute();
        }), Args.CanBakeTransparencyTextures));
    MenuBuilder.EndSection();

    return MenuBuilder.MakeWidget();
}

TSharedRef<ITableRow> FWCAEditorWidgets::GenerateMaterialSlotRow(
    TSharedPtr<FWCAMaterialSlotItem> Item,
    const TSharedRef<STableViewBase>& OwnerTable,
    const FWCAMaterialSlotRowArgs& Args)
{
    const int32 MaterialSlotIndex = Item.IsValid() ? Item->SlotIndex : INDEX_NONE;
    const bool  bIsAllSlotsRow = Item.IsValid() && MaterialSlotIndex == INDEX_NONE;
    const bool bIsRowEnabled = bIsAllSlotsRow || !Args.IsMaterialSlotEnabled ||
        Args.IsMaterialSlotEnabled(MaterialSlotIndex);
    const FText RowTooltip = bIsAllSlotsRow
        ? Args.AllSlotsTooltip
        : Args.GetMaterialSlotTooltipText
            ? Args.GetMaterialSlotTooltipText(MaterialSlotIndex)
            : FText::GetEmpty();
    UMaterialInterface* MaterialObject = !bIsAllSlotsRow && Item.IsValid() ? Item->Material.Get() : nullptr;
    const FText SlotIDText = bIsAllSlotsRow
                                 ? NSLOCTEXT("WetClothingEditorCommonWidgets", "AllMaterialSlotsSlotID", "All")
                                 : Item.IsValid()
                                 ? FText::AsNumber(MaterialSlotIndex)
                                 : FText::FromString(TEXT("-"));
    const FText SlotTitle = bIsAllSlotsRow
                                ? (Args.AllSlotsTitle.IsEmpty()
                                       ? NSLOCTEXT("WetClothingEditorCommonWidgets", "AllMaterialSlotsTitle", "All Slots")
                                       : Args.AllSlotsTitle)
                                : Item.IsValid()
                                ? FText::FromName(Item->SlotName)
                                : NSLOCTEXT("WetClothingEditorCommonWidgets", "InvalidMaterialSlotTitle", "Invalid Material Slot");
    const TSharedRef<SWidget> EmptyMaterialThumbnailWidget =
        SNew(SBorder)
        .BorderImage(FAppStyle::Get().GetBrush(TEXT("WhiteBrush")))
        .BorderBackgroundColor(FLinearColor(0.06f, 0.06f, 0.06f, 1.0f));
    TSharedRef<SWidget> ThumbnailWidget = EmptyMaterialThumbnailWidget;
    TSharedPtr<SWidget> MaterialThumbnailWidget;

    if (MaterialObject != nullptr && Args.ThumbnailPool.IsValid())
    {
        TSharedPtr<FAssetThumbnail> Thumbnail = MakeShared<FAssetThumbnail>(MaterialObject, 48, 48, Args.ThumbnailPool);
        if (Args.ThumbnailSink != nullptr)
        {
            Args.ThumbnailSink->Add(Thumbnail);
        }

        FAssetThumbnailConfig ThumbnailConfig;
        ThumbnailConfig.bAllowFadeIn = false;
        MaterialThumbnailWidget = Thumbnail->MakeThumbnailWidget(ThumbnailConfig);
    }

    if (!bIsAllSlotsRow && Args.BuildThumbnailWidget)
    {
        const TSharedRef<SWidget> SlotPreviewWidget = Args.BuildThumbnailWidget(MaterialSlotIndex);
        ThumbnailWidget =
            SNew(SHorizontalBox)

            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            .Padding(0.0f, 0.0f, 12.0f, 0.0f)
            [
                SNew(SBox)
                .WidthOverride(48.0f)
                .HeightOverride(48.0f)
                [
                    MaterialThumbnailWidget.IsValid()
                        ? MaterialThumbnailWidget.ToSharedRef()
                        : EmptyMaterialThumbnailWidget
                ]
            ]

            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            [
                SNew(SBox)
                .WidthOverride(48.0f)
                .HeightOverride(48.0f)
                [
                    SlotPreviewWidget
                ]
            ];
    }
    else if (MaterialThumbnailWidget.IsValid())
    {
        ThumbnailWidget = MaterialThumbnailWidget.ToSharedRef();
    }

    TSharedRef<SHorizontalBox> RowContent = SNew(SHorizontalBox);

    if (Args.BuildLeadingWidget)
    {
        RowContent->AddSlot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            [
                SNew(SBox)
                .WidthOverride(48.0f)
                .HAlign(HAlign_Center)
                .VAlign(VAlign_Center)
                [
                    Args.BuildLeadingWidget(MaterialSlotIndex)
                ]
            ];
    }

    RowContent->AddSlot()
        .AutoWidth()
        .VAlign(VAlign_Center)
        .Padding(0.0f, 0.0f, 8.0f, 0.0f)
            [SNew(SBox)
                 .WidthOverride(FWCAEditorWidgets::MaterialSlotSlotColumnWidth)
                 .HAlign(HAlign_Center)
                 .VAlign(VAlign_Center)
                     [SNew(STextBlock)
                          .Text(SlotIDText)
                          .Font(FAppStyle::GetFontStyle(TEXT("NormalFontBold")))]];

    RowContent->AddSlot()
        .FillWidth(1.0f)
        .VAlign(VAlign_Center)
        .Padding(2.0f, 0.0f, 10.0f, 0.0f)
            [SNew(SBox)
                 .MinDesiredWidth(FWCAEditorWidgets::MaterialSlotNameColumnWidth)
                 .VAlign(VAlign_Center)
                     [SNew(SVerticalBox)

                      + SVerticalBox::Slot()
                            .AutoHeight()
                                [SNew(STextBlock)
                                     .Text(SlotTitle)
                                     .Font(FAppStyle::GetFontStyle(TEXT("NormalFontBold")))
                                     .OverflowPolicy(ETextOverflowPolicy::Ellipsis)]

                      + SVerticalBox::Slot()
                            .AutoHeight()
                            .Padding(0.0f, 3.0f, 0.0f, 0.0f)
                                [SNew(SHorizontalBox)
                            .Visibility_Lambda([GetWarningText = Args.GetMaterialSlotWarningText, WetClothingAsset = Args.WetClothingAsset, MaterialSlotIndex, bIsAllSlotsRow]()
                            {
                                if (bIsAllSlotsRow)
                                {
                                    return EVisibility::Collapsed;
                                }
                                const FText WarningText = GetWarningText
                                    ? GetWarningText(MaterialSlotIndex)
                                    : GetWettableSlotMissingProfileWarningText(WetClothingAsset, MaterialSlotIndex);
                                return WarningText.IsEmpty()
                                    ? EVisibility::Collapsed
                                    : EVisibility::Visible;
                            })

                        + SHorizontalBox::Slot()
                              .AutoWidth()
                              .VAlign(VAlign_Center)
                              .Padding(0.0f, 0.0f, 4.0f, 0.0f)
                                  [SNew(SImage)
                                       .DesiredSizeOverride(FVector2D(13.0f, 13.0f))
                                       .Image(FAppStyle::GetBrush(TEXT("Icons.WarningWithColor")))]

                        + SHorizontalBox::Slot()
                              .FillWidth(1.0f)
                              .VAlign(VAlign_Center)
                                  [SNew(STextBlock)
                                       .Text_Lambda([GetWarningText = Args.GetMaterialSlotWarningText, WetClothingAsset = Args.WetClothingAsset, MaterialSlotIndex, bIsAllSlotsRow]()
                                       {
                                           if (bIsAllSlotsRow)
                                           {
                                               return FText::GetEmpty();
                                           }
                                           return GetWarningText
                                               ? GetWarningText(MaterialSlotIndex)
                                               : GetWettableSlotMissingProfileWarningText(WetClothingAsset, MaterialSlotIndex);
                                       })
                                       .Font(FAppStyle::GetFontStyle(TEXT("SmallFont")))
                                       .ColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.78f, 0.18f, 1.0f)))
                                       .OverflowPolicy(ETextOverflowPolicy::Ellipsis)]]]];

    RowContent->AddSlot()
        .AutoWidth()
        .VAlign(VAlign_Center)
        .Padding(0.0f, 0.0f, 10.0f, 0.0f)
            [SNew(SBox)
                 .WidthOverride(FWCAEditorWidgets::MaterialSlotThumbnailColumnWidth)
                 .HeightOverride(52.0f)
                 .HAlign(HAlign_Center)
                 .VAlign(VAlign_Center)
                     [bIsAllSlotsRow
                         ? StaticCastSharedRef<SWidget>(SNew(STextBlock)
                             .Text(FText::FromString(TEXT("-")))
                             .Font(FAppStyle::GetFontStyle(TEXT("SmallFont"))))
                         : ThumbnailWidget]];

    RowContent->AddSlot()
        .AutoWidth()
        .VAlign(VAlign_Fill)
        .Padding(0.0f, 4.0f, 12.0f, 4.0f)
            [SNew(SBox)
                 .WidthOverride(1.0f)
                     [SNew(SBorder)
                          .BorderImage(FAppStyle::Get().GetBrush(TEXT("WhiteBrush")))
                          .BorderBackgroundColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.12f))
                          .Padding(0.0f)]];

    RowContent->AddSlot()
        .AutoWidth()
        .VAlign(VAlign_Center)
            [SNew(SBox)
                 .WidthOverride(FWCAEditorWidgets::MaterialSlotDataUVColumnWidth)
                 .HAlign(HAlign_Center)
                 .VAlign(VAlign_Center)
                     [SNew(SButton)
                          .ButtonStyle(FAppStyle::Get(), TEXT("NoBorder"))
                          .ContentPadding(FMargin(2.0f, 1.0f))
                          .IsEnabled_Lambda([ShouldShowInfo = Args.ShouldShowMaterialSlotStatusInfo, MaterialSlotIndex]()
                          {
                              return ShouldShowInfo && ShouldShowInfo(MaterialSlotIndex);
                          })
                          .ToolTipText_Lambda([GetTooltip = Args.GetMaterialSlotStatusTooltip, MaterialSlotIndex]()
                          {
                              return GetTooltip ? GetTooltip(MaterialSlotIndex) : FText::GetEmpty();
                          })
                          .OnClicked_Lambda([OnClicked = Args.OnMaterialSlotStatusInfoClicked, MaterialSlotIndex]()
                          {
                              return OnClicked
                                  ? OnClicked(MaterialSlotIndex)
                                  : FReply::Handled();
                          })
                              [SNew(SHorizontalBox)

                               + SHorizontalBox::Slot()
                                     .AutoWidth()
                                     .VAlign(VAlign_Center)
                                         [SNew(SBox)
                                              .WidthOverride(18.0f)
                                              .HAlign(HAlign_Center)
                                              .VAlign(VAlign_Center)
                                                  [SNew(SImage)
                                                       .DesiredSizeOverride(FVector2D(14.0f, 14.0f))
                                                       .Visibility_Lambda([ShouldShowInfo = Args.ShouldShowMaterialSlotStatusInfo, MaterialSlotIndex]()
                                                       {
                                                           return ShouldShowInfo && ShouldShowInfo(MaterialSlotIndex)
                                                               ? EVisibility::Visible
                                                               : EVisibility::Hidden;
                                                       })
                                                       .Image_Lambda([GetBrush = Args.GetMaterialSlotStatusInfoBrush, MaterialSlotIndex]()
                                                       {
                                                           return GetBrush
                                                               ? GetBrush(MaterialSlotIndex)
                                                               : FAppStyle::GetBrush(TEXT("Icons.InfoWithColor"));
                                                       })
                                                       .ColorAndOpacity_Lambda([GetColor = Args.GetMaterialSlotStatusInfoColor, MaterialSlotIndex]()
                                                       {
                                                           return GetColor
                                                               ? GetColor(MaterialSlotIndex)
                                                               : FSlateColor(FLinearColor::White);
                                                       })]]

                               + SHorizontalBox::Slot()
                                     .AutoWidth()
                                     .VAlign(VAlign_Center)
                                     .Padding(5.0f, 0.0f, 0.0f, 0.0f)
                                         [SNew(STextBlock)
                                              .Text_Lambda([GetStatusText = Args.GetMaterialSlotStatusText, MaterialSlotIndex]()
                                              {
                                                  return GetStatusText
                                                      ? GetStatusText(MaterialSlotIndex)
                                                      : FText::GetEmpty();
                                              })
                                              .Font(FAppStyle::GetFontStyle(TEXT("SmallFont")))
                                              .ColorAndOpacity_Lambda([GetStatusColor = Args.GetMaterialSlotStatusColor, MaterialSlotIndex]()
                                              {
                                                  return GetStatusColor
                                                      ? GetStatusColor(MaterialSlotIndex)
                                                      : FSlateColor(FStyleColors::Foreground);
                                              })]]]];

    if (Args.bShowWettableToggle)
    {
        RowContent->AddSlot()
            .AutoWidth()
            .VAlign(VAlign_Center)
                [SNew(SBox)
                     .WidthOverride(FWCAEditorWidgets::MaterialSlotWettableColumnWidth)
                     .HAlign(HAlign_Center)
                     .VAlign(VAlign_Center)
                         [bIsAllSlotsRow
                             ? StaticCastSharedRef<SWidget>(SNew(STextBlock)
                                 .Text(FText::FromString(TEXT("-")))
                                 .Font(FAppStyle::GetFontStyle(TEXT("SmallFont"))))
                             : StaticCastSharedRef<SWidget>(SNew(SButton)
                          .ButtonStyle(FAppStyle::Get(), TEXT("NoBorder"))
                          .ContentPadding(FMargin(4.0f, 2.0f))
                          .ToolTipText(NSLOCTEXT("WetClothingEditorCommonWidgets", "WettableSlotTooltip", "Toggle whether this material slot can be wetted."))
                          .IsEnabled_Lambda([IsToggleEnabled = Args.IsWettableToggleEnabled, MaterialSlotIndex]()
                         {
                             return !IsToggleEnabled || IsToggleEnabled(MaterialSlotIndex);
                         })
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
                                           .Image_Lambda([Item]()
                                           {
                                               const bool bWettable = Item.IsValid() && Item->bIsWettableSlot;
                                               return FDWCEditorStyle::GetBrush(
                                                   bWettable ? TEXT("DWCEditor.Part.IsWettable.True") : TEXT("DWCEditor.Part.IsWettable.False"));
                                           })
                                           .ColorAndOpacity_Lambda([Item]()
                                           {
                                               const bool bWettable = Item.IsValid() && Item->bIsWettableSlot;
                                           return bWettable
                                                          ? FSlateColor(FLinearColor(0.35f, 0.85f, 1.0f, 1.0f))
                                                          : FSlateColor(FLinearColor(1.0f, 0.36f, 0.36f, 1.0f));
                                           })]])]];
    }

    if (!bIsAllSlotsRow && Args.BuildTrailingWidget)
    {
        RowContent->AddSlot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            [
                Args.BuildTrailingWidget(MaterialSlotIndex)
            ];
    }

    TSharedRef<SOverlay> DecoratedRowContent = SNew(SOverlay)

        + SOverlay::Slot()
        [
            RowContent
        ]

        + SOverlay::Slot()
        .HAlign(HAlign_Left)
        .VAlign(VAlign_Fill)
        [
            SNew(SBox)
            .WidthOverride(3.0f)
            [
                SNew(SBorder)
                .BorderImage(FAppStyle::Get().GetBrush(TEXT("WhiteBrush")))
                .BorderBackgroundColor_Lambda(
                    [GetAccentColor = Args.GetMaterialSlotRowAccentColor, MaterialSlotIndex, bIsAllSlotsRow]()
                    {
                        return !bIsAllSlotsRow && GetAccentColor
                            ? GetAccentColor(MaterialSlotIndex)
                            : FSlateColor(FLinearColor::Transparent);
                    })
                .Padding(0.0f)
            ]
        ]

        + SOverlay::Slot()
        .HAlign(HAlign_Fill)
        .VAlign(VAlign_Bottom)
        [
            SNew(SBox)
            .HeightOverride(1.0f)
            [
                SNew(SBorder)
                .BorderImage(FAppStyle::Get().GetBrush(TEXT("WhiteBrush")))
                .BorderBackgroundColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.10f))
                .Padding(0.0f)
            ]
        ];

    return SNew(STableRow<TSharedPtr<FWCAMaterialSlotItem>>, OwnerTable)
        .IsEnabled(bIsRowEnabled)
        .ToolTipText(RowTooltip)
        .Padding(0.0f)
        [
            SNew(SBorder)
            .BorderImage(FAppStyle::Get().GetBrush(TEXT("WhiteBrush")))
            .BorderBackgroundColor_Lambda(
                [GetBackgroundColor = Args.GetMaterialSlotRowBackgroundColor, MaterialSlotIndex, bIsAllSlotsRow]()
                {
                    return !bIsAllSlotsRow && GetBackgroundColor
                        ? GetBackgroundColor(MaterialSlotIndex)
                        : FSlateColor(FLinearColor::Transparent);
                })
            .Padding(4.0f)
            [
                DecoratedRowContent
            ]
        ];
}

TSharedRef<ITableRow> FWCAEditorWidgets::GeneratePartMapRow(
    TSharedPtr<FWetClothingWetPartEntry> Item,
    const TSharedRef<STableViewBase>& OwnerTable)
{
    const FLinearColor Color = Item.IsValid() ? (Item->WetPartID == 0 ? GetUnassignedPartColor() : Item->Color) : GetUnassignedPartColor();
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
                            .Font(FAppStyle::GetFontStyle(TEXT("NormalFontBold")))
                            .ColorAndOpacity(Item.IsValid() && Item->WetPartID == 0
                                ? FSlateColor::UseSubduedForeground()
                                : FSlateColor::UseForeground())
                            .OverflowPolicy(ETextOverflowPolicy::Ellipsis)]

             + SHorizontalBox::Slot()
                   .AutoWidth()
                   .VAlign(VAlign_Center)
                       [SNew(STextBlock)
                            .Text(IDText)
                            .Visibility(Item.IsValid() && Item->WetPartID == 0 ? EVisibility::Collapsed : EVisibility::Visible)
                            .ColorAndOpacity(FSlateColor(FLinearColor(0.72f, 0.72f, 0.72f, 1.0f)))]];
}

bool FWCAEditorWidgets::IsMaterialSlotWettable(const UWetClothingAsset* WetClothingAsset, int32 MaterialSlotIndex)
{
    if (WetClothingAsset == nullptr || MaterialSlotIndex == INDEX_NONE)
    {
        return false;
    }

    const FWetClothingAuthoredMaterialSlot* State =
        WetClothingAsset->Authored.PartData.EditableWetPartData.FindMaterialSlot(MaterialSlotIndex);
    return State != nullptr && State->bIsWettableSlot;
}

void FWCAEditorWidgets::SetMaterialSlotWettable(UWetClothingAsset* WetClothingAsset, int32 MaterialSlotIndex, bool bIsWettableSlot)
{
    if (WetClothingAsset == nullptr || MaterialSlotIndex == INDEX_NONE)
    {
        return;
    }

    FWetClothingEditableWetPartData& EditableData = WetClothingAsset->Authored.PartData.EditableWetPartData;
    FWetClothingAuthoredMaterialSlot* State = EditableData.FindMaterialSlot(MaterialSlotIndex);

    const bool bHasStaleMaterialOverrides =
        !bIsWettableSlot &&
        WetClothingAsset->Derived.Inline.GeneratedWetMaterialOverrides.ContainsByPredicate(
            [MaterialSlotIndex](const FWetClothingGeneratedWetMaterialOverride& MaterialOverride)
            {
                return MaterialOverride.MaterialSlotIndex == MaterialSlotIndex;
            });
    if (State != nullptr && State->bIsWettableSlot == bIsWettableSlot && !bHasStaleMaterialOverrides)
    {
        return;
    }

    WetClothingAsset->Modify();
    if (State == nullptr)
    {
        State = &EditableData.FindOrAddMaterialSlot(MaterialSlotIndex);
    }

    State->bIsWettableSlot = bIsWettableSlot;
    if (!bIsWettableSlot)
    {
        WetClothingAsset->Derived.Inline.GeneratedWetMaterialOverrides.RemoveAll(
            [MaterialSlotIndex](const FWetClothingGeneratedWetMaterialOverride& MaterialOverride)
            {
                return MaterialOverride.MaterialSlotIndex == MaterialSlotIndex;
            });
    }
    WetClothingAsset->MarkSimulationBakeOutOfDate();
    WetClothingAsset->MarkVisualBakeOutOfDate();
    WetClothingAsset->RefreshBakeState(false);
    WetClothingAsset->MarkPackageDirty();
}

void FWCAEditorWidgets::MarkMaterialSlotWettable(UWetClothingAsset* WetClothingAsset, int32 MaterialSlotIndex)
{
    SetMaterialSlotWettable(WetClothingAsset, MaterialSlotIndex, true);
}
