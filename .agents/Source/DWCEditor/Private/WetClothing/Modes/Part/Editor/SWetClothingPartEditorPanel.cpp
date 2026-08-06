#include "SWetClothingPartEditorPanel.h"

#include "AssetThumbnail.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "ContentBrowserModule.h"
#include "Core/DWCEditorUtils.h"
#include "Core/DWCEditorStyle.h"
#include "WetClothing/Modes/Part/Partition/WetPartAutoPartitioner.h"
#include "WetClothing/Modes/Part/Partition/WetPartEditingService.h"
#include "WetClothing/Asset/Setup/DWCDataUVBuildService.h"
#include "WetClothing/DerivedAssets/Materials/WCAMaterialGenerator.h"
#include "WetClothing/DerivedAssets/Textures/WetnessProfile/WetClothingWetPartDataTextureBaker.h"
#include "WetClothing/DerivedAssets/Textures/WetnessProfile/WetClothingSurfaceTextureNormalizer.h"
#include "WetClothing/DerivedAssets/Textures/WetnessProfile/WetClothingRenderProfileBakeService.h"
#include "WetClothing/Foundation/TextureAccess/WetClothingMaterialTextureResolver.h"
#include "WetClothing/Foundation/TextureAccess/WetClothingTextureReadback.h"
#include "WetClothing/WCAEditor/UI/UVView/SWCAUVView.h"
#include "WetClothing/WCAEditor/UI/WCAReportDialogs.h"
#include "WetClothing/WCAEditor/UI/Widgets/WCAEditorWidgets.h"
#include "WetClothing/WCAEditor/UI/Widgets/SWCAMaterialSlotPreview.h"
#include "WetClothing/Modes/Part/Widgets/SWetPartAutoPartitionControls.h"
#include "DataAssets/WetClothingAsset.h"
#include "WetClothing/Foundation/MeshAnalysis/WetClothingAssetMeshAnalyzer.h"
#include "WetClothing/WCAEditor/UI/UVView/WCAUVIslandViewCache.h"
#include "WetClothing/Modes/Part/Viewport/SDWCPartViewport.h"
#include "DataAssets/WetnessProfile.h"
#include "WetRendering/WetMaterialParameters.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "IDetailsView.h"
#include "IContentBrowserSingleton.h"
#include "FileHelpers.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/MessageDialog.h"
#include "Misc/ScopedSlowTask.h"
#include "ScopedTransaction.h"
#include "Modules/ModuleManager.h"
#include "PropertyCustomizationHelpers.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Styling/StyleColors.h"
#include "Styling/StyleDefaults.h"
#include "UObject/Package.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Colors/SColorPicker.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBar.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/SWindow.h"

#define LOCTEXT_NAMESPACE "WCAEditorPanel"

namespace SWetClothingPartEditorPanelLocal
{
    bool IsSameMaterialFamily(UMaterialInterface* A, UMaterialInterface* B)
    {
        if (A == nullptr || B == nullptr)
        {
            return A == B;
        }

        UMaterial* ABase = A->GetMaterial();
        UMaterial* BBase = B->GetMaterial();
        return A == B || (ABase != nullptr && ABase == BBase);
    }

    constexpr float                          AutoPartitionMaxTolerancePercent = 40.0f;
    constexpr float                          AutoPartitionDefaultTolerancePercent = 20.0f;
    constexpr float                          MaterialSlotListScrollbarThickness = 12.0f;
    constexpr float                          UVIslandListHorizontalPadding = 14.0f;
    constexpr float                          UVIslandIDColumnWidth = 140.0f;
    constexpr float                          UVIslandPartIDColumnWidth = 180.0f;
    constexpr float                          UVIslandTriangleCountColumnWidth = 140.0f;
    constexpr EWetPartAutoPartitionColorMode AutoPartitionDefaultColorMode = EWetPartAutoPartitionColorMode::DominantColor;

    FLinearColor GetUnassignedPartColor()
    {
        return FLinearColor(0.32f, 0.32f, 0.32f, 1.0f);
    }

    FLinearColor GetUnassignedPartUVViewColor()
    {
        // Keep unassigned islands clearly visible in the UV view without
        // competing with assigned part colors as pure white did.
        return FLinearColor(0.62f, 0.62f, 0.62f, 1.0f);
    }

    FText GetAutoPartitionColorModeLabel(EWetPartAutoPartitionColorMode ColorMode)
    {
        switch (ColorMode)
        {
        case EWetPartAutoPartitionColorMode::AverageColor:
            return LOCTEXT("AutoPartitionColorModeAverage", "Average");

        case EWetPartAutoPartitionColorMode::MedianColor:
            return LOCTEXT("AutoPartitionColorModeMedian", "Median");

        case EWetPartAutoPartitionColorMode::KMeansColor:
            return LOCTEXT("AutoPartitionColorModeKMeans", "K-Means");

        case EWetPartAutoPartitionColorMode::DominantColor:
        default:
            return LOCTEXT("AutoPartitionColorModeDominant", "Dominant");
        }
    }

    class SUntintedColorBlockBox : public SCompoundWidget
    {
      public:
        SLATE_BEGIN_ARGS(SUntintedColorBlockBox) {}
        SLATE_DEFAULT_SLOT(FArguments, Content)
        SLATE_END_ARGS()

        void Construct(const FArguments& InArgs)
        {
            ChildSlot
                [InArgs._Content.Widget];
        }

        virtual int32 OnPaint(
            const FPaintArgs&        Args,
            const FGeometry&         AllottedGeometry,
            const FSlateRect&        MyCullingRect,
            FSlateWindowElementList& OutDrawElements,
            int32                    LayerId,
            const FWidgetStyle&      InWidgetStyle,
            bool                     bParentEnabled) const override
        {
            return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, FWidgetStyle(), true);
        }
    };

    FString MakeTextureUvKey(const UTexture* Texture, int32 UVChannelIndex)
    {
        return Texture != nullptr && UVChannelIndex != INDEX_NONE
                   ? FString::Printf(TEXT("%s|%d"), *Texture->GetPathName(), UVChannelIndex)
                   : FString();
    }

    const FDWCDataUVSlotWarning* FindDataUVSlotWarning(
        const FDWCDataUVLODMetadata& Metadata,
        const int32 MaterialSlotIndex)
    {
        return Metadata.SlotWarnings.FindByPredicate(
            [MaterialSlotIndex](const FDWCDataUVSlotWarning& Warning)
            {
                return Warning.MaterialSlotIndex == MaterialSlotIndex && Warning.HasWarnings();
            });
    }

    const FSlateBrush* GetSurfaceWaterTilingBrush()
    {
        if (const FSlateBrush* Brush = FDWCEditorStyle::GetBrush(TEXT("DWCEditor.SurfaceWaterTiling")))
        {
            return Brush;
        }

        return FAppStyle::GetBrush(TEXT("Icons.Layout"));
    }
} // namespace SWetClothingPartEditorPanelLocal

void SWetClothingPartEditorPanel::Construct(const FArguments& InArgs)
{
    WetClothingAsset = InArgs._WetClothingAsset;
    DetailsView = InArgs._DetailsView;

    const FSlateFontInfo SectionHeadingFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 13);
    const FSlateFontInfo HintTextFont = FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 10);
    static const FSlateRoundedBoxBrush DataUVActionBarBrush(
        FLinearColor(0.015f, 0.055f, 0.10f, 0.96f),
        6.0f,
        FLinearColor(0.10f, 0.45f, 1.0f, 0.90f),
        1.0f);
    static const FSlateRoundedBoxBrush DataUVActionAccentBrush(
        FLinearColor(0.00f, 0.48f, 1.0f, 1.0f),
        3.0f);

    MaterialThumbnailPool = MakeShared<FAssetThumbnailPool>(32);
    TSharedPtr<SScrollBar> MaterialSlotListScrollBar =
        SNew(SScrollBar)
        .Orientation(Orient_Vertical)
        .Thickness(FVector2D(
            SWetClothingPartEditorPanelLocal::MaterialSlotListScrollbarThickness,
            SWetClothingPartEditorPanelLocal::MaterialSlotListScrollbarThickness))
        .Padding(FMargin(0.0f))
        .AlwaysShowScrollbar(true);

    UVSelectionToolItems.Reset();
    auto AddSelectionToolItem = [this](EWCAUVSelectionTool Tool, const FText& Label, const FText& Tooltip, const FName IconBrushName)
    {
        FUVSelectionToolItemPtr ToolItem = MakeShared<FWCAUVSelectionToolItem>();
        ToolItem->Tool = Tool;
        ToolItem->Label = Label;
        ToolItem->Tooltip = Tooltip;
        ToolItem->IconBrushDisplayName = IconBrushName;
        UVSelectionToolItems.Add(ToolItem);
        return ToolItem;
    };

    AddSelectionToolItem(
        EWCAUVSelectionTool::Select,
        LOCTEXT("UVSelectionToolSelect", "Select"),
        LOCTEXT("UVSelectionToolSelectTooltip", "Click a UV island to select it. Hold Shift to add to the current selection."),
        TEXT("DWCEditor.UVTool.Select"));
    FUVSelectionToolItemPtr BoxSelectToolItem = AddSelectionToolItem(
        EWCAUVSelectionTool::BoxSelect,
        LOCTEXT("UVSelectionToolBoxSelect", "Box Select"),
        LOCTEXT("UVSelectionToolBoxSelectTooltip", "Drag a box to select UV islands. Hold Shift to add to the current selection."),
        TEXT("DWCEditor.UVTool.BoxSelect"));
    AddSelectionToolItem(
        EWCAUVSelectionTool::EllipseSelect,
        LOCTEXT("UVSelectionToolEllipseSelect", "Ellipse Select"),
        LOCTEXT("UVSelectionToolEllipseSelectTooltip", "Drag an ellipse to select UV islands. Hold Shift to add to the current selection."),
        TEXT("DWCEditor.UVTool.EllipseSelect"));
    AddSelectionToolItem(
        EWCAUVSelectionTool::LassoSelect,
        LOCTEXT("UVSelectionToolLassoSelect", "Lasso Select"),
        LOCTEXT("UVSelectionToolLassoSelectTooltip", "Draw a freeform lasso to select UV islands. Hold Shift to add to the current selection."),
        TEXT("DWCEditor.UVTool.LassoSelect"));

    SelectedUVSelectionToolItem = BoxSelectToolItem;
    CurrentUVSelectionTool = EWCAUVSelectionTool::BoxSelect;
    AutoPartitionColorModeItems.Reset();
    AutoPartitionColorModeItems.Add(MakeShared<EWetPartAutoPartitionColorMode>(EWetPartAutoPartitionColorMode::AverageColor));
    AutoPartitionColorModeItems.Add(MakeShared<EWetPartAutoPartitionColorMode>(EWetPartAutoPartitionColorMode::MedianColor));
    AutoPartitionColorModeItems.Add(MakeShared<EWetPartAutoPartitionColorMode>(EWetPartAutoPartitionColorMode::DominantColor));
    AutoPartitionColorModeItems.Add(MakeShared<EWetPartAutoPartitionColorMode>(EWetPartAutoPartitionColorMode::KMeansColor));
    SelectedAutoPartitionColorModeItem = AutoPartitionColorModeItems[2];
    AutoPartitionColorMode = EWetPartAutoPartitionColorMode::DominantColor;

    auto BuildSelectionToolButton = [this](FUVSelectionToolItemPtr ToolItem)
    {
        return SNew(SCheckBox)
            .Style(FAppStyle::Get(), TEXT("DetailsView.SectionButton"))
            .Type(ESlateCheckBoxType::ToggleButton)
            .ToolTipText(ToolItem->Tooltip)
            .IsChecked_Lambda([this, ToolItem]()
            {
                return ToolItem.IsValid() && ToolItem->Tool == CurrentUVSelectionTool
                           ? ECheckBoxState::Checked
                           : ECheckBoxState::Unchecked;
            })
            .OnCheckStateChanged_Lambda([this, ToolItem](ECheckBoxState)
            {
                if (ToolItem.IsValid())
                {
                    SetCurrentUVSelectionTool(ToolItem->Tool);
                }
            })
                [SNew(SBox)
                     .WidthOverride(28.0f)
                     .HeightOverride(24.0f)
                     .HAlign(HAlign_Center)
                     .VAlign(VAlign_Center)
                         [SNew(SImage)
                              .DesiredSizeOverride(FVector2D(18.0f, 18.0f))
                              .Image(this, &SWetClothingPartEditorPanel::GetUVSelectionToolBrush, ToolItem)
                              .ColorAndOpacity(this, &SWetClothingPartEditorPanel::GetUVSelectionToolIconColor, ToolItem)]];
    };

    TSharedRef<SHorizontalBox> SelectionToolButtonRow = SNew(SHorizontalBox);
    for (int32 ToolIndex = 0; ToolIndex < UVSelectionToolItems.Num(); ++ToolIndex)
    {
        SelectionToolButtonRow->AddSlot()
            .AutoWidth()
                [BuildSelectionToolButton(UVSelectionToolItems[ToolIndex])];
    }

    auto BuildDataUVActionBar = [this]() -> TSharedRef<SWidget>
    {
        return SNew(SBorder)
            .BorderImage_Lambda([this]() -> const FSlateBrush*
            {
                return IsDataUVOperationEnabled()
                           ? &DataUVActionBarBrush
                           : FStyleDefaults::GetNoBrush();
            })
            // Match the UV Islands assignment controls: keep the action anchored in the
            // section header, then add selection feedback without moving the button.
            .Padding(FMargin(6.0f, 3.0f))
                [SNew(SHorizontalBox)

                 + SHorizontalBox::Slot()
                       .AutoWidth()
                       .VAlign(VAlign_Center)
                       .Padding(0.0f, 0.0f, 8.0f, 0.0f)
                           [SNew(SBox)
                                .Visibility(this, &SWetClothingPartEditorPanel::GetDataUVUpdateBarVisibility)
                                .WidthOverride(4.0f)
                                .HeightOverride(28.0f)
                                    [SNew(SBorder)
                                         .BorderImage(&DataUVActionAccentBrush)
                                         .Padding(0.0f)]]

                 + SHorizontalBox::Slot()
                       .AutoWidth()
                       .VAlign(VAlign_Center)
                       .Padding(0.0f, 0.0f, 10.0f, 0.0f)
                           [SNew(SBox)
                                .Visibility(this, &SWetClothingPartEditorPanel::GetDataUVUpdateBarVisibility)
                                .MaxDesiredWidth(155.0f)
                                    [SNew(STextBlock)
                                         .Text(this, &SWetClothingPartEditorPanel::GetDataUVOperationSummaryText)
                                         .Font(FAppStyle::GetFontStyle(TEXT("NormalFontBold")))
                                         .ColorAndOpacity(FLinearColor(0.84f, 0.94f, 1.0f, 1.0f))
                                         .OverflowPolicy(ETextOverflowPolicy::Ellipsis)]]

                 + SHorizontalBox::Slot()
                       .AutoWidth()
                       .VAlign(VAlign_Center)
                           [SNew(SBox)
                                .MinDesiredWidth(150.0f)
                                .HeightOverride(30.0f)
                                    [SNew(SButton)
                                         .ButtonStyle(FAppStyle::Get(), TEXT("PrimaryButton"))
                                         .ContentPadding(FMargin(10.0f, 4.0f))
                                         .HAlign(HAlign_Center)
                                         .VAlign(VAlign_Center)
                                         .ToolTipText(this, &SWetClothingPartEditorPanel::GetDataUVOperationButtonTooltip)
                                         .IsEnabled(this, &SWetClothingPartEditorPanel::IsDataUVOperationEnabled)
                                         .OnClicked(this, &SWetClothingPartEditorPanel::HandleDataUVOperationClicked)
                                             [SNew(STextBlock)
                                                  .Text(this, &SWetClothingPartEditorPanel::GetDataUVOperationButtonText)
                                                  .Font(FAppStyle::GetFontStyle(TEXT("NormalFontBold")))
                                                  .Justification(ETextJustify::Center)]]]];
    };

    auto BuildUVIslandAssignmentBar = [this]() -> TSharedRef<SWidget>
    {
        return SNew(SBorder)
            .BorderImage_Lambda([this]() -> const FSlateBrush*
            {
                return SelectedUVIslandIDs.Num() > 0
                           ? &DataUVActionBarBrush
                           : FStyleDefaults::GetNoBrush();
            })
            // Keep the assignment controls anchored in the UV Islands header.
            // Selection feedback is added inside the same row without moving them.
            .Padding(FMargin(6.0f, 3.0f))
                [SNew(SHorizontalBox)

                 + SHorizontalBox::Slot()
                       .AutoWidth()
                       .VAlign(VAlign_Center)
                       .Padding(0.0f, 0.0f, 8.0f, 0.0f)
                           [SNew(SBox)
                                .Visibility(this, &SWetClothingPartEditorPanel::GetSelectedUVIslandTextVisibility)
                                .WidthOverride(4.0f)
                                .HeightOverride(28.0f)
                                    [SNew(SBorder)
                                         .BorderImage(&DataUVActionAccentBrush)
                                         .Padding(0.0f)]]

                 + SHorizontalBox::Slot()
                       .AutoWidth()
                       .VAlign(VAlign_Center)
                       .Padding(0.0f, 0.0f, 10.0f, 0.0f)
                           [SNew(SBox)
                                .Visibility(this, &SWetClothingPartEditorPanel::GetSelectedUVIslandTextVisibility)
                                .MaxDesiredWidth(155.0f)
                                    [SNew(STextBlock)
                                         .Text(this, &SWetClothingPartEditorPanel::GetUVIslandAssignmentSummaryText)
                                         .Font(FAppStyle::GetFontStyle(TEXT("NormalFontBold")))
                                         .ColorAndOpacity(FLinearColor(0.84f, 0.94f, 1.0f, 1.0f))
                                         .OverflowPolicy(ETextOverflowPolicy::Ellipsis)]]

                 + SHorizontalBox::Slot()
                       .AutoWidth()
                       .VAlign(VAlign_Center)
                       .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                           [SNew(SBox)
                                .WidthOverride(165.0f)
                                .HeightOverride(30.0f)
                                    [SAssignNew(AssignWetPartComboBox, SComboBox<FWetPartEntryPtr>)
                                         .OptionsSource(&CurrentWetPartItems)
                                         .OnGenerateWidget(this, &SWetClothingPartEditorPanel::GenerateAssignWetPartComboItem)
                                         .OnSelectionChanged(this, &SWetClothingPartEditorPanel::HandleAssignWetPartSelectionChanged)
                                             [SNew(SHorizontalBox)

                                              + SHorizontalBox::Slot()
                                                    .AutoWidth()
                                                    .VAlign(VAlign_Center)
                                                    .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                                                        [SNew(SBox)
                                                             .WidthOverride(14.0f)
                                                             .HeightOverride(14.0f)
                                                                 [SNew(SBorder)
                                                                      .BorderImage(FAppStyle::Get().GetBrush(TEXT("WhiteBrush")))
                                                                      .BorderBackgroundColor(this, &SWetClothingPartEditorPanel::GetSelectedAssignWetPartColor)]]

                                              + SHorizontalBox::Slot()
                                                    .FillWidth(1.0f)
                                                    .VAlign(VAlign_Center)
                                                        [SNew(STextBlock)
                                                             .Text(this, &SWetClothingPartEditorPanel::GetSelectedAssignWetPartText)
                                                             .OverflowPolicy(ETextOverflowPolicy::Ellipsis)]]]]

                 + SHorizontalBox::Slot()
                       .AutoWidth()
                       .VAlign(VAlign_Center)
                           [SNew(SBox)
                                .MinDesiredWidth(126.0f)
                                .HeightOverride(30.0f)
                                    [SNew(SButton)
                                         .ButtonStyle(FAppStyle::Get(), TEXT("PrimaryButton"))
                                         .ContentPadding(FMargin(10.0f, 4.0f))
                                         .HAlign(HAlign_Center)
                                         .VAlign(VAlign_Center)
                                         .ToolTipText(this, &SWetClothingPartEditorPanel::GetUVIslandAssignmentButtonTooltip)
                                         .IsEnabled(this, &SWetClothingPartEditorPanel::IsAssignUVIslandToWetPartEnabled)
                                         .OnClicked(this, &SWetClothingPartEditorPanel::HandleAssignSelectedUVIslandToWetPartClicked)
                                             [SNew(STextBlock)
                                                  .Text(this, &SWetClothingPartEditorPanel::GetUVIslandAssignmentButtonText)
                                                  .Font(FAppStyle::GetFontStyle(TEXT("NormalFontBold")))
                                                  .Justification(ETextJustify::Center)]]]];
    };

    ChildSlot
        [SNew(SVerticalBox)

         + SVerticalBox::Slot()
               .FillHeight(1.0f)
               .Padding(10.0f, 0.0f, 10.0f, 10.0f)
                   [SNew(SSplitter)

         // Column 1: Target Mesh / UV Channel / Material Slots / Wet Part Map.
         + SSplitter::Slot()
               .Value(0.30f)
                   [SNew(SBorder)
                        .Padding(10.0f)
                            [SNew(SVerticalBox)

                             + SVerticalBox::Slot()
                                   .FillHeight(1.0f)
                                       [SNew(SSplitter)
                                            .Orientation(Orient_Vertical)

                                        + SSplitter::Slot()
                                              .Value(0.52f)
                                                  [SNew(SVerticalBox)

                                                   + SVerticalBox::Slot()
                                                         .AutoHeight()
                                                         .Padding(0.0f, 14.0f, 0.0f, 4.0f)
                                                             [SNew(SHorizontalBox)

                                                              + SHorizontalBox::Slot()
                                                                    .FillWidth(1.0f)
                                                                    .VAlign(VAlign_Center)
                                                                        [SNew(SVerticalBox)

                                                                         + SVerticalBox::Slot()
                                                                               .AutoHeight()
                                                                                   [SNew(STextBlock)
                                                                                        .Text(LOCTEXT("MaterialSlotsLabel", "Material Slots"))
                                                                                        .Font(SectionHeadingFont)]

                                                                         + SVerticalBox::Slot()
                                                                               .AutoHeight()
                                                                               .Padding(0.0f, 2.0f, 0.0f, 0.0f)
                                                                                   [SNew(STextBlock)
                                                                                        .Text(LOCTEXT("MaterialSlotsDataUVSelectionHint", "Select one or more slots to build DWC UV."))
                                                                                        .Font(HintTextFont)
                                                                                        .ColorAndOpacity(FSlateColor::UseSubduedForeground())]]

                                                              + SHorizontalBox::Slot()
                                                                    .AutoWidth()
                                                                    .VAlign(VAlign_Center)
                                                                        [BuildDataUVActionBar()]]

                                                   + SVerticalBox::Slot()
                                                         .AutoHeight()
                                                         .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                                                             [SNew(SSeparator)
                                                                  .Orientation(Orient_Horizontal)]

                                                   + SVerticalBox::Slot()
                                                         .AutoHeight()
                                                         .Padding(0.0f, 0.0f, 0.0f, 4.0f)
                                                             [SNew(SHorizontalBox)

                                                              + SHorizontalBox::Slot()
                                                                    .FillWidth(1.0f)
                                                                        [SNew(SBorder)
                                                                             .BorderImage(FAppStyle::Get().GetBrush(TEXT("Brushes.Header")))
                                                                             .Padding(FMargin(4.0f, 3.0f))
                                                                                 [SNew(SHorizontalBox)

                                                                       + SHorizontalBox::Slot()
                                                                             .AutoWidth()
                                                                             .Padding(0.0f, 0.0f, 8.0f, 0.0f)
                                                                                 [SNew(SBox)
                                                                                      .WidthOverride(FWCAEditorWidgets::MaterialSlotSlotColumnWidth)
                                                                                      .HAlign(HAlign_Center)
                                                                                      .VAlign(VAlign_Center)
                                                                                          [SNew(STextBlock)
                                                                                               .Text(LOCTEXT("SlotColumnHeader", "Slot"))
                                                                                               .Font(FAppStyle::GetFontStyle(TEXT("SmallFont")))]]

                                                                       + SHorizontalBox::Slot()
                                                                             .FillWidth(1.0f)
                                                                             .VAlign(VAlign_Center)
                                                                             .Padding(2.0f, 0.0f, 10.0f, 0.0f)
                                                                                 [SNew(SBox)
                                                                                      .MinDesiredWidth(FWCAEditorWidgets::MaterialSlotNameColumnWidth)
                                                                                      .VAlign(VAlign_Center)
                                                                                          [SNew(STextBlock)
                                                                                               .Text(LOCTEXT("NameColumnHeader", "Name"))
                                                                                               .Font(FAppStyle::GetFontStyle(TEXT("SmallFont")))]]

                                                                       + SHorizontalBox::Slot()
                                                                             .AutoWidth()
                                                                             .Padding(0.0f, 0.0f, 10.0f, 0.0f)
                                                                                 [SNew(SBox)
                                                                                      .WidthOverride(FWCAEditorWidgets::MaterialSlotThumbnailColumnWidth)
                                                                                      .HAlign(HAlign_Left)
                                                                                      .VAlign(VAlign_Center)
                                                                                          [SNew(STextBlock)
                                                                                               .Text(LOCTEXT("ThumbnailColumnHeader", "Thumbnail"))
                                                                                               .Font(FAppStyle::GetFontStyle(TEXT("SmallFont")))]]

                                                                       + SHorizontalBox::Slot()
                                                                             .AutoWidth()
                                                                             .Padding(0.0f, 2.0f, 12.0f, 2.0f)
                                                                                 [SNew(SBox)
                                                                                      .WidthOverride(1.0f)
                                                                                          [SNew(SBorder)
                                                                                               .BorderImage(FAppStyle::Get().GetBrush(TEXT("WhiteBrush")))
                                                                                               .BorderBackgroundColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.12f))
                                                                                               .Padding(0.0f)]]

                                                                       + SHorizontalBox::Slot()
                                                                             .AutoWidth()
                                                                                 [SNew(SBox)
                                                                                      .WidthOverride(FWCAEditorWidgets::MaterialSlotDataUVColumnWidth)
                                                                                      .HAlign(HAlign_Center)
                                                                                      .VAlign(VAlign_Center)
                                                                                          [SNew(STextBlock)
                                                                                               .Text(LOCTEXT("DataUVColumnHeader", "DWC UV"))
                                                                                               .Font(FAppStyle::GetFontStyle(TEXT("SmallFont")))]]

                                                                       + SHorizontalBox::Slot()
                                                                             .AutoWidth()
                                                                                 [SNew(SBox)
                                                                                      .WidthOverride(FWCAEditorWidgets::MaterialSlotWettableColumnWidth)
                                                                                      .HAlign(HAlign_Center)
                                                                                      .VAlign(VAlign_Center)
                                                                                          [SNew(STextBlock)
                                                                                               .Text(LOCTEXT("WettableColumnHeader", "Wettable"))
                                                                                               .Font(FAppStyle::GetFontStyle(TEXT("SmallFont")))]]]]

                                                              + SHorizontalBox::Slot()
                                                                    .AutoWidth()
                                                                        [SNew(SBorder)
                                                                             .BorderImage(FAppStyle::Get().GetBrush(TEXT("Brushes.Header")))
                                                                             .Padding(0.0f)
                                                                                 [SNew(SBox)
                                                                                      .WidthOverride(SWetClothingPartEditorPanelLocal::MaterialSlotListScrollbarThickness)]]]

                                                   + SVerticalBox::Slot()
                                                         .FillHeight(1.0f)
                                                             [SNew(SHorizontalBox)

                                                              + SHorizontalBox::Slot()
                                                                    .FillWidth(1.0f)
                                                                        [SAssignNew(MaterialSlotListView, SListView<FMaterialSlotItemPtr>)
                                                                            .ListItemsSource(&MaterialSlotItems)
                                                                            .OnGenerateRow(this, &SWetClothingPartEditorPanel::GenerateMaterialSlotRow)
                                                                            .OnSelectionChanged(this, &SWetClothingPartEditorPanel::HandleMaterialSlotSelectionChanged)
                                                                            .OnMouseButtonDoubleClick(this, &SWetClothingPartEditorPanel::HandleMaterialSlotDoubleClicked)
                                                                            .ExternalScrollbar(MaterialSlotListScrollBar)
                                                                            .SelectionMode(ESelectionMode::Multi)]

                                                              + SHorizontalBox::Slot()
                                                                    .AutoWidth()
                                                                        [MaterialSlotListScrollBar.ToSharedRef()]]]

                                        + SSplitter::Slot()
                                              .Value(0.48f)
                                                  [SNew(SVerticalBox)

                                                   + SVerticalBox::Slot()
                                                         .AutoHeight()
                                                         .Padding(0.0f, 24.0f, 0.0f, 4.0f)
                                                             [SNew(SHorizontalBox)

                                                              + SHorizontalBox::Slot()
                                                                    .FillWidth(1.0f)
                                                                    .VAlign(VAlign_Center)
                                                                        [SNew(STextBlock)
                                                                             .Text(this, &SWetClothingPartEditorPanel::GetWetPartSectionText)
                                                                             .Font(SectionHeadingFont)]

                                                              + SHorizontalBox::Slot()
                                                                    .AutoWidth()
                                                                    .Padding(6.0f, 0.0f, 0.0f, 0.0f)
                                                                        [SNew(SButton)
                                                                             .Text(LOCTEXT("AddWetPartButton", "+ Add Part"))
                                                                             .IsEnabled(this, &SWetClothingPartEditorPanel::IsSelectedMaterialSlotPartEditingReady)
                                                                             .OnClicked(this, &SWetClothingPartEditorPanel::HandleAddWetPartClicked)]

                                                              + SHorizontalBox::Slot()
                                                                    .AutoWidth()
                                                                    .Padding(4.0f, 0.0f, 0.0f, 0.0f)
                                                                        [SNew(SButton)
                                                                             .Text(LOCTEXT("RemoveWetPartButton", "Remove"))
                                                                             .IsEnabled(this, &SWetClothingPartEditorPanel::IsWetPartRemoveEnabled)
                                                                             .OnClicked(this, &SWetClothingPartEditorPanel::HandleRemoveWetPartClicked)]]

                                                   + SVerticalBox::Slot()
                                                         .AutoHeight()
                                                         .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                                                             [SNew(SSeparator)
                                                                  .Orientation(Orient_Horizontal)]

                                                   + SVerticalBox::Slot()
                                                         .AutoHeight()
                                                         .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                                                             [SNew(STextBlock)
                                                                  .AutoWrapText(false)
                                                                  .OverflowPolicy(ETextOverflowPolicy::Ellipsis)
                                                                  .Text(this, &SWetClothingPartEditorPanel::GetSelectedWetPartText)
                                                                  .Visibility(this, &SWetClothingPartEditorPanel::GetSelectedWetPartTextVisibility)]

                                                   + SVerticalBox::Slot()
                                                         .FillHeight(1.0f)
                                                             [SNew(SOverlay)

                                                              + SOverlay::Slot()
                                                                    [SAssignNew(WetPartListView, SListView<FWetPartEntryPtr>)
                                                                         .ListItemsSource(&CurrentWetPartItems)
                                                                         .OnGenerateRow(this, &SWetClothingPartEditorPanel::GenerateWetPartRow)
                                                                         .OnSelectionChanged(this, &SWetClothingPartEditorPanel::HandleWetPartSelectionChanged)
                                                                         .OnMouseButtonDoubleClick(this, &SWetClothingPartEditorPanel::HandleWetPartItemDoubleClicked)
                                                                         .SelectionMode(ESelectionMode::Single)]

                                                              + SOverlay::Slot()
                                                                    .HAlign(HAlign_Center)
                                                                    .VAlign(VAlign_Center)
                                                                    .Padding(8.0f)
                                                                        [SNew(SBox)
                                                                             .MaxDesiredWidth(360.0f)
                                                                                 [SNew(STextBlock)
                                                                                      .AutoWrapText(false)
                                                                  .OverflowPolicy(ETextOverflowPolicy::Ellipsis)
                                                                                      .Justification(ETextJustify::Center)
                                                                                      .Text(this, &SWetClothingPartEditorPanel::GetWetnessProfileLibraryStatusText)
                                                                                      .ColorAndOpacity(FSlateColor::UseSubduedForeground())
                                                                                      .Visibility(this, &SWetClothingPartEditorPanel::GetWetnessProfileLibraryStatusVisibility)]]]]]]]

         // Column 2: UV View, with UV Islands directly underneath.
         + SSplitter::Slot()
               .Value(0.35f)
                   [SNew(SBorder)
                        .Padding(8.0f)
                            [SNew(SSplitter)
                                 .Orientation(Orient_Vertical)

                             + SSplitter::Slot()
                                   .Value(0.58f)
                                       [SNew(SVerticalBox)

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 8.0f, 0.0f, 5.0f)
                                                  [SNew(SHorizontalBox)

                                                   + SHorizontalBox::Slot()
                                                         .FillWidth(1.0f)
                                                         .VAlign(VAlign_Center)
                                                             [SNew(STextBlock)
                                                                  .Text(LOCTEXT("UVViewLabel", "UV View"))
                                                                  .Font(SectionHeadingFont)]]

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                                                  [SNew(SSeparator)
                                                       .Orientation(Orient_Horizontal)]

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                                                  [SNew(SBox)
                                                       .Visibility(this, &SWetClothingPartEditorPanel::GetUVEditorContentVisibility)
                                                           [FWCAEditorWidgets::BuildUVViewTextureAndViewRow(
                                                               SAssignNew(TextureSelectionContainer, SBox),
                                                               FWCAEditorWidgets::BuildUVViewOptionsButton(
                                                          TAttribute<float>::Create(TAttribute<float>::FGetter::CreateSP(this, &SWetClothingPartEditorPanel::GetUVViewBackgroundTextureOpacity)),
                                                          [this](float NewValue)
                                                          {
                                                              HandleUVViewBackgroundTextureOpacityChanged(NewValue);
                                                          },
                                                          TAttribute<float>::Create(TAttribute<float>::FGetter::CreateSP(this, &SWetClothingPartEditorPanel::GetUVViewIslandLineOpacity)),
                                                          [this](float NewValue)
                                                          {
                                                              HandleUVViewIslandLineOpacityChanged(NewValue);
                                                          },
                                                          TAttribute<float>::Create(TAttribute<float>::FGetter::CreateSP(this, &SWetClothingPartEditorPanel::GetUVViewIslandLineThicknessScale)),
                                                          [this](float NewValue)
                                                                   {
                                                                       HandleUVViewIslandLineThicknessScaleChanged(NewValue);
                                                                   }))]]

                                        + SVerticalBox::Slot()
                                              .FillHeight(1.0f)
                                                  [SNew(SOverlay)

                                                   + SOverlay::Slot()
                                                         .HAlign(HAlign_Fill)
                                                         .VAlign(VAlign_Fill)
                                                             [SNew(SVerticalBox)
                                                                  .Visibility(this, &SWetClothingPartEditorPanel::GetUVEditorContentVisibility)

                                                              + SVerticalBox::Slot()
                                                                    .AutoHeight()
                                                                        [SNew(SBorder)
                                                                             .BorderImage(FAppStyle::Get().GetBrush(TEXT("Brushes.Header")))
                                                                             .Padding(FMargin(2.0f, 2.0f, 2.0f, 2.0f))
                                                                                 [SNew(SHorizontalBox)

                                                                                  + SHorizontalBox::Slot()
                                                                                        .AutoWidth()
                                                                                            [SelectionToolButtonRow]

                                                                                  + SHorizontalBox::Slot()
                                                                                        .AutoWidth()
                                                                                        .VAlign(VAlign_Center)
                                                                                        .Padding(10.0f, 3.0f, 10.0f, 3.0f)
                                                                                            [SNew(SBox)
                                                                                                 .WidthOverride(1.0f)
                                                                                                     [SNew(SBorder)
                                                                                                          .BorderImage(FAppStyle::Get().GetBrush(TEXT("WhiteBrush")))
                                                                                                          .BorderBackgroundColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.12f))
                                                                                                          .Padding(0.0f)]]

                                                                                  + SHorizontalBox::Slot()
                                                                                        .AutoWidth()
                                                                                        .VAlign(VAlign_Center)
                                                                                            [SNew(SWetPartAutoPartitionControls)
                                                                                                 .IsAutoPartitionEnabled(this, &SWetClothingPartEditorPanel::IsAutoPartitionEnabled)
                                                                                                 .OnAutoPartitionClicked(this, &SWetClothingPartEditorPanel::HandleAutoPartitionClicked)]

                                                                                  + SHorizontalBox::Slot()
                                                                                        .FillWidth(1.0f)]]

                                                              + SVerticalBox::Slot()
                                                                    .FillHeight(1.0f)
                                                                        [SAssignNew(UVView, SWCAUVView)
                                                                             .OnIslandSelectionChanged(this, &SWetClothingPartEditorPanel::HandleUVIslandSelectionChangedFromUVView)]]

                                                   + SOverlay::Slot()
                                                         .HAlign(HAlign_Center)
                                                         .VAlign(VAlign_Center)
                                                         .Padding(18.0f)
                                                             [SNew(STextBlock)
                                                                  .AutoWrapText(false)
                                                                  .OverflowPolicy(ETextOverflowPolicy::Ellipsis)
                                                                  .Justification(ETextJustify::Center)
                                                                  .Text(this, &SWetClothingPartEditorPanel::GetUVStatusText)
                                                                  .ColorAndOpacity(FSlateColor::UseSubduedForeground())
                                                                  .Visibility(this, &SWetClothingPartEditorPanel::GetUVStatusOverlayVisibility)]]]

                             + SSplitter::Slot()
                                   .Value(0.42f)
                                       [SNew(SVerticalBox)

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 8.0f, 0.0f, 6.0f)
                                                  [SNew(SHorizontalBox)

                                                   + SHorizontalBox::Slot()
                                                         .AutoWidth()
                                                         .VAlign(VAlign_Center)
                                                             [SNew(STextBlock)
                                                                  .Text(LOCTEXT("UVIslandLabel", "UV Islands"))
                                                                  .Font(SectionHeadingFont)]

                                                   + SHorizontalBox::Slot()
                                                         .AutoWidth()
                                                         .Padding(6.0f, 1.0f, 0.0f, 0.0f)
                                                         .VAlign(VAlign_Center)
                                                             [SNew(STextBlock)
                                                                  .Text(this, &SWetClothingPartEditorPanel::GetUVIslandCountText)
                                                                  .ColorAndOpacity(FSlateColor::UseSubduedForeground())]

                                                   + SHorizontalBox::Slot()
                                                         .FillWidth(1.0f)

                                                   + SHorizontalBox::Slot()
                                                         .AutoWidth()
                                                         .VAlign(VAlign_Center)
                                                             [BuildUVIslandAssignmentBar()]]

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 0.0f, 0.0f, 4.0f)
                                                  [SNew(SBorder)
                                                       .BorderImage(FAppStyle::Get().GetBrush(TEXT("Brushes.Header")))
                                                       .Padding(FMargin(SWetClothingPartEditorPanelLocal::UVIslandListHorizontalPadding, 4.0f))
                                                           [SNew(SHorizontalBox)

                                                            + SHorizontalBox::Slot()
                                                                  .AutoWidth()
                                                                  .VAlign(VAlign_Center)
                                                                      [SNew(SBox)
                                                                           .WidthOverride(SWetClothingPartEditorPanelLocal::UVIslandIDColumnWidth)
                                                                               [SNew(STextBlock)
                                                                                    .Text(LOCTEXT("UVIslandColumnID", "Island ID"))
                                                                                    .Font(FAppStyle::GetFontStyle(TEXT("SmallFontBold")))]]

                                                            + SHorizontalBox::Slot()
                                                                  .AutoWidth()
                                                                  .VAlign(VAlign_Center)
                                                                      [SNew(SBox)
                                                                           .WidthOverride(SWetClothingPartEditorPanelLocal::UVIslandPartIDColumnWidth)
                                                                               [SNew(STextBlock)
                                                                                    .Text(LOCTEXT("UVIslandColumnPartID", "Part"))
                                                                                    .Font(FAppStyle::GetFontStyle(TEXT("SmallFontBold")))]]

                                                            + SHorizontalBox::Slot()
                                                                  .AutoWidth()
                                                                  .VAlign(VAlign_Center)
                                                                  .HAlign(HAlign_Right)
                                                                      [SNew(SBox)
                                                                           .WidthOverride(SWetClothingPartEditorPanelLocal::UVIslandTriangleCountColumnWidth)
                                                                           .HAlign(HAlign_Right)
                                                                               [SNew(STextBlock)
                                                                                    .Text(LOCTEXT("UVIslandColumnTriangles", "Triangle Count"))
                                                                                    .Font(FAppStyle::GetFontStyle(TEXT("SmallFontBold")))]]]]

                                        + SVerticalBox::Slot()
                                              .FillHeight(1.0f)
                                                  [SNew(SOverlay)

                                                   + SOverlay::Slot()
                                                         [SAssignNew(UVIslandListView, SListView<FUVIslandItemPtr>)
                                                              .ListItemsSource(&UVIslandItems)
                                                              .OnGenerateRow(this, &SWetClothingPartEditorPanel::GenerateUVIslandRow)
                                                              .OnSelectionChanged(this, &SWetClothingPartEditorPanel::HandleUVIslandSelectionChanged)
                                                              .SelectionMode(ESelectionMode::Multi)]

                                                   + SOverlay::Slot()
                                                         .HAlign(HAlign_Center)
                                                         .VAlign(VAlign_Center)
                                                         .Padding(18.0f)
                                                             [SNew(STextBlock)
                                                                  .AutoWrapText(false)
                                                                  .OverflowPolicy(ETextOverflowPolicy::Ellipsis)
                                                                  .Justification(ETextJustify::Center)
                                                                  .Text(this, &SWetClothingPartEditorPanel::GetSelectedUVIslandText)
                                                                  .ColorAndOpacity(FSlateColor::UseSubduedForeground())
                                                                  .Visibility(this, &SWetClothingPartEditorPanel::GetUVIslandStatusOverlayVisibility)]
                                                  ]]]]

         // Column 3: Preview.
         + SSplitter::Slot()
               .Value(0.35f)
                   [FWCAEditorWidgets::BuildPreviewSection(
                       SNew(SOverlay)

                       + SOverlay::Slot()
                             [SAssignNew(PreviewViewport, SDWCPartViewport)
                                  .WetClothingAsset(WetClothingAsset.Get())
                                  .SurfaceWaterTilingPreview(false)
                                  .OnIslandPicked(this, &SWetClothingPartEditorPanel::HandleUVIslandPickedFromPreview)]

                       + SOverlay::Slot()
                             .HAlign(HAlign_Left)
                             .VAlign(VAlign_Top)
                             .Padding(FMargin(14.0f, 42.0f, 14.0f, 14.0f))
                                 [BuildPartPreviewControlsPanel()],
                       FOnWetClothingPreviewFocusClicked::CreateSP(this, &SWetClothingPartEditorPanel::HandleFocusPreviewClicked))]]];

    RefreshFromAsset();
}

void SWetClothingPartEditorPanel::RefreshFromAsset()
{
    RestorePersistedDataUVFailureState();
    RefreshMaterialSlotItems();
    RefreshOriginalUVChannel();
    RefreshMaterialTextures();

    if (PreviewViewport.IsValid())
    {
        PreviewViewport->RefreshPreviewMesh();
        PreviewViewport->SetShowWetPartColors(bShowPartColorsInPreview);
        PreviewViewport->SetWetPartColorIntensity(PartColorIntensity);

        if (SelectedMaterialSlotIndex != INDEX_NONE)
        {
            PreviewViewport->SetHighlightedMaterialSlot(SelectedMaterialSlotIndex);
        }
        else
        {
            PreviewViewport->ClearMaterialSlotHighlight();
        }
    }

    if (SurfaceWaterTilingPreviewViewport.IsValid())
    {
        SurfaceWaterTilingPreviewViewport->RefreshPreviewMesh();
    }

    RefreshPreviewIslandHighlight();
    RefreshPreviewWetPartOverlay();
}

void SWetClothingPartEditorPanel::RefreshMaterialSlotItems()
{
    const int32 PreviousSelection = SelectedMaterialSlotIndex;

    MaterialSlotItems.Reset();
    MaterialSlotThumbnails.Reset();

    FMaterialSlotItemPtr AllSlotsItem = MakeShared<FWCAMaterialSlotItem>();
    AllSlotsItem->SlotIndex = INDEX_NONE;
    AllSlotsItem->SlotName = TEXT("All Slots");
    MaterialSlotItems.Add(AllSlotsItem);

    if (const UWetClothingAsset* WetClothingAssetPtr = WetClothingAsset.Get())
    {
        const USkeletalMesh* PreviewMesh = WetClothingAssetPtr->GetRuntimeSkeletalMesh();
        if (PreviewMesh == nullptr)
        {
            PreviewMesh = WetClothingAssetPtr->GetSourceSkeletalMesh();
        }
        if (PreviewMesh != nullptr)
        {
            const TArray<FSkeletalMaterial>& Materials = PreviewMesh->GetMaterials();

            for (int32 MaterialIndex = 0; MaterialIndex < Materials.Num(); ++MaterialIndex)
            {
                const FSkeletalMaterial& SkeletalMaterial = Materials[MaterialIndex];

                FMaterialSlotItemPtr Item = MakeShared<FWCAMaterialSlotItem>();
                Item->SlotIndex = MaterialIndex;
                Item->SlotName = SkeletalMaterial.MaterialSlotName;
                Item->Material = SkeletalMaterial.MaterialInterface;
                Item->bIsWettableSlot = FWCAEditorWidgets::IsMaterialSlotWettable(WetClothingAssetPtr, MaterialIndex);
                MaterialSlotItems.Add(Item);
            }
        }
    }

    SelectedMaterialSlotIndex = FindMaterialSlotItem(PreviousSelection).IsValid() ? PreviousSelection : INDEX_NONE;
    SyncDataUVOperationSelection();

    if (MaterialSlotListView.IsValid())
    {
        MaterialSlotListView->RequestListRefresh();
        TGuardValue<bool> SelectionGuard(bSynchronizingMaterialSlotSelection, true);
        MaterialSlotListView->ClearSelection();

        for (const FMaterialSlotItemPtr& MaterialSlotItem : MaterialSlotItems)
        {
            if (MaterialSlotItem.IsValid() &&
                MaterialSlotItem->SlotIndex != INDEX_NONE &&
                (SelectedDataUVOperationSlotIndices.Contains(MaterialSlotItem->SlotIndex) ||
                 MaterialSlotItem->SlotIndex == SelectedMaterialSlotIndex))
            {
                MaterialSlotListView->SetItemSelection(MaterialSlotItem, true, ESelectInfo::Direct);
            }
        }
    }
}

void SWetClothingPartEditorPanel::RefreshMaterialTextures(bool bRefreshUVView)
{
    TextureThumbnails.Reset();
    FWetClothingMaterialTextureResolver::BuildTextureItemsForMaterialSlot(
        WetClothingAsset.Get(),
        SelectedMaterialSlotIndex,
        TextureItems,
        SelectedTextureItem);

    bShowMaterialTextureInUVView = SelectedTextureItem.IsValid() && SelectedTextureItem->Texture.IsValid();

    RefreshTextureToggleWidgets();
    if (bRefreshUVView)
    {
        RefreshUVView();
    }
}

void SWetClothingPartEditorPanel::RefreshTextureToggleWidgets()
{
    TextureThumbnails.Reset();

    if (!TextureSelectionContainer.IsValid())
    {
        return;
    }

    if (SelectedMaterialSlotIndex == INDEX_NONE)
    {
        TextureSelectionContainer->SetContent(
            SNew(STextBlock)
                .Text(LOCTEXT("AllSlotsPreviewOnlyTextureNotice", "All Slots is preview-only. Select a single material slot to choose its texture."))
                .ColorAndOpacity(FSlateColor(FStyleColors::ForegroundHover)));
        return;
    }

    if (!FindMaterialSlotItem(SelectedMaterialSlotIndex).IsValid())
    {
        TextureSelectionContainer->SetContent(
            SNew(STextBlock)
                .Text(LOCTEXT("SelectMaterialSlotForTextures", "Select a material slot to choose its texture."))
                .ColorAndOpacity(FSlateColor(FStyleColors::ForegroundHover)));
        return;
    }

    const bool bHasActualTexture = TextureItems.ContainsByPredicate([](const FTextureItemPtr& TextureItem)
                                                                    { return TextureItem.IsValid() && TextureItem->Texture.IsValid(); });

    if (!bHasActualTexture)
    {
        TextureSelectionContainer->SetContent(
            SNew(STextBlock)
                .Text(LOCTEXT("NoMaterialTextures", "No textures were found on this material slot."))
                .ColorAndOpacity(FSlateColor(FStyleColors::ForegroundHover)));
        return;
    }

    TextureSelectionContainer->SetContent(
        FWCAEditorWidgets::BuildUVViewTextureSelector(
            &TextureItems,
            SelectedTextureItem,
            MaterialThumbnailPool,
            &TextureThumbnails,
            &TextureComboBox,
            &SelectedTextureComboContentBox,
            [this](FTextureItemPtr Item, ESelectInfo::Type SelectInfo)
            {
                HandleTextureSelectionChanged(Item, SelectInfo);
            }));
}

void SWetClothingPartEditorPanel::RefreshOriginalUVChannel()
{
    // Part Edit is permanently bound to the Original UV channel configured in Asset Setup.
    RefreshUVIslandList();

    if (PreviewViewport.IsValid())
    {
        if (SelectedMaterialSlotIndex != INDEX_NONE)
        {
            PreviewViewport->SetHighlightedMaterialSlot(SelectedMaterialSlotIndex);
        }
        else
        {
            PreviewViewport->ClearMaterialSlotHighlight();
        }
    }
}

void SWetClothingPartEditorPanel::RefreshUVIslandList()
{
    const int32       PreviousPrimaryUVIslandID = SelectedUVIslandID;
    const TSet<int32> PreviousSelectedUVIslandIDs = SelectedUVIslandIDs;

    UVIslandItems.Reset();
    ResetIslandSelection();
    UVStatusMessage = TEXT("Select a material slot.");

    const UWetClothingAsset* WetClothingAssetPtr = WetClothingAsset.Get();
    if (SelectedMaterialSlotIndex == INDEX_NONE)
    {
        UVStatusMessage = TEXT("Select a material slot.");
    }
    else if (!IsSelectedMaterialSlotWettable())
    {
        UVStatusMessage = TEXT("Enable Wettable for this slot.");
    }
    else if (WetClothingAssetPtr == nullptr ||
             WetClothingAssetPtr->GetRuntimeSkeletalMesh() == nullptr ||
             !IsMaterialSlotDataUVReadyForEditing(SelectedMaterialSlotIndex))
    {
        UVStatusMessage = TEXT("UV editing is unavailable for this slot.");
    }
    else if (!HasValidOriginalUVChannel())
    {
        UVStatusMessage = TEXT("No UV data found.");
    }
    else
    {
        const int32 UVChannelIndex = GetOriginalUVChannelIndex();
        FString ErrorMessage;
        const bool bBuiltIslands = FWCAUVIslandViewCache::GetMaterialSlotUVIslands(
            WetClothingAssetPtr,
            UVChannelIndex,
            SelectedMaterialSlotIndex,
            UVIslandItems,
            &ErrorMessage);
        if (!bBuiltIslands)
        {
            UVStatusMessage = TEXT("UV data could not be loaded.");
        }
        else if (UVIslandItems.IsEmpty())
        {
            UVStatusMessage = TEXT("No UV islands found.");
        }
        else
        {
            UVStatusMessage = FString::Printf(
                TEXT("LOD0 / Original UV%d / Slot %d / %d islands"),
                UVChannelIndex,
                SelectedMaterialSlotIndex,
                UVIslandItems.Num());
        }
    }

    for (const FUVIslandItemPtr& IslandItem : UVIslandItems)
    {
        if (IslandItem.IsValid() && PreviousSelectedUVIslandIDs.Contains(IslandItem->UVIslandID))
        {
            SelectedUVIslandIDs.Add(IslandItem->UVIslandID);
        }
    }
    if (SelectedUVIslandIDs.Contains(PreviousPrimaryUVIslandID))
    {
        SelectedUVIslandID = PreviousPrimaryUVIslandID;
    }
    else if (SelectedUVIslandIDs.Num() > 0)
    {
        SelectedUVIslandID = *SelectedUVIslandIDs.CreateConstIterator();
    }

    if (UVIslandListView.IsValid())
    {
        UVIslandListView->RequestListRefresh();
        SyncUVIslandListSelectionToState();
    }

    RefreshWetPartList(false);
    RefreshUVView();
    RefreshPreviewIslandHighlight();
}

void SWetClothingPartEditorPanel::RefreshUVView()
{
    if (!UVView.IsValid())
    {
        return;
    }

    UVView->SetNormalizeToContentBounds(true);
    UVView->SetSelectionTool(CurrentUVSelectionTool);
    UVView->SetDisplayMode(EWCAUVDisplayMode::Normal);
    UVView->SetBackgroundTextureOpacity(UVViewBackgroundTextureOpacity);
    UVView->SetUVIslandLineOpacity(UVViewIslandLineOpacity);
    UVView->SetUVIslandLineThicknessScale(UVViewIslandLineThicknessScale);

    if (SelectedMaterialSlotIndex == INDEX_NONE)
    {
        UVView->SetBackgroundTexture(nullptr);
        UVView->SetDrawBackgroundTexture(false);
        UVView->SetIslands(TArray<FUVIslandItemPtr>());
        UVView->SetIslandColors(TMap<int32, FLinearColor>());
        UVView->SetHiddenUVIslandIDs(TSet<int32>());
        UVView->SetSelectedIslands(TSet<int32>());
        RefreshPreviewWetPartOverlay();
        return;
    }

    if (!IsSelectedMaterialSlotPartEditingReady())
    {
        UVView->SetBackgroundTexture(nullptr);
        UVView->SetDrawBackgroundTexture(false);
        UVView->SetIslands(TArray<FUVIslandItemPtr>());
        UVView->SetIslandColors(TMap<int32, FLinearColor>());
        UVView->SetHiddenUVIslandIDs(TSet<int32>());
        UVView->SetSelectedIslands(TSet<int32>());
        RefreshPreviewWetPartOverlay();
        return;
    }

    UVView->SetBackgroundTexture(ResolveTextureAddressTexture());
    UVView->SetDrawBackgroundTexture(bShowMaterialTextureInUVView && ResolveSelectedMaterialTexture() != nullptr);
    UVView->SetIslands(UVIslandItems);
    UVView->SetIslandColors(BuildUVIslandColorMap());
    UVView->SetHiddenUVIslandIDs(BuildHiddenUVIslandIDSet());
    UVView->SetSelectedIslands(SelectedUVIslandIDs);

    RefreshPreviewWetPartOverlay();
}

void SWetClothingPartEditorPanel::RefreshPreviewIslandHighlight()
{
    if (!PreviewViewport.IsValid())
    {
        return;
    }

    PreviewViewport->SetSelectableIslands(UVIslandItems);
    PreviewViewport->SetHighlightedUVIslandIDs(SelectedUVIslandIDs);
}

void SWetClothingPartEditorPanel::RefreshWetPartList(bool bRefreshUVView)
{
    const int32 PreviousSelectedWetPart = SelectedWetPartID;
    const int32 PreviousAssignWetPartID = SelectedAssignWetPartID;

    CurrentWetPartItems.Reset();
    SelectedWetPartID = INDEX_NONE;
    SelectedAssignWetPartID = INDEX_NONE;
    WetPartInlineRenameWidgets.Reset();

    if (IsSelectedMaterialSlotPartEditingReady())
    {
        EnsureDefaultWetPartForSelectedScope();

        if (const UWetClothingAsset* WetClothingAssetPtr = WetClothingAsset.Get())
        {
            FWetPartEditingService::BuildWetPartItemsForScope(
                WetClothingAssetPtr,
                FWetPartEditingService::MakeScope(SelectedMaterialSlotIndex),
                CurrentWetPartItems);
        }
    }

    CurrentWetPartItems.Sort([](const FWetPartEntryPtr& A, const FWetPartEntryPtr& B)
                             { return A.IsValid() && B.IsValid() ? A->WetPartID < B->WetPartID : A.IsValid(); });

    for (const FWetPartEntryPtr& Item : CurrentWetPartItems)
    {
        if (Item.IsValid() && Item->WetPartID == PreviousSelectedWetPart)
        {
            SelectedWetPartID = PreviousSelectedWetPart;
        }

        if (Item.IsValid() && Item->WetPartID == PreviousAssignWetPartID)
        {
            SelectedAssignWetPartID = PreviousAssignWetPartID;
        }
    }

    if (SelectedAssignWetPartID == INDEX_NONE)
    {
        for (const FWetPartEntryPtr& Item : CurrentWetPartItems)
        {
            if (Item.IsValid() && Item->WetPartID == 0)
            {
                SelectedAssignWetPartID = 0;
                break;
            }
        }
    }

    if (SelectedAssignWetPartID == INDEX_NONE && CurrentWetPartItems.Num() > 0 && CurrentWetPartItems[0].IsValid())
    {
        SelectedAssignWetPartID = CurrentWetPartItems[0]->WetPartID;
    }

    if (WetPartListView.IsValid())
    {
        WetPartListView->RequestListRefresh();

        if (SelectedWetPartID != INDEX_NONE)
        {
            for (const FWetPartEntryPtr& Item : CurrentWetPartItems)
            {
                if (Item.IsValid() && Item->WetPartID == SelectedWetPartID)
                {
                    WetPartListView->SetSelection(Item, ESelectInfo::Direct);
                    break;
                }
            }
        }
        else
        {
            WetPartListView->ClearSelection();
        }
    }

    if (AssignWetPartComboBox.IsValid())
    {
        AssignWetPartComboBox->RefreshOptions();
        AssignWetPartComboBox->SetSelectedItem(FindWetPartItemByID(SelectedAssignWetPartID));
    }

    if (bRefreshUVView)
    {
        RefreshUVView();
    }
}

void SWetClothingPartEditorPanel::RefreshPreviewWetPartOverlay()
{
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->SetSelectableIslands(UVIslandItems);
        PreviewViewport->SetWetPartIslandAssignments(BuildUVIslandWetPartIDMap(), BuildPreviewUVIslandColorMap());
        PreviewViewport->SetShowWetPartColors(bShowPartColorsInPreview);
    }

    RefreshSurfaceWaterTilingPreview();
}

void SWetClothingPartEditorPanel::RefreshSurfaceWaterTilingPreview()
{
    if (!SurfaceWaterTilingPreviewViewport.IsValid())
    {
        return;
    }

    SurfaceWaterTilingPreviewViewport->SetSelectableIslands(UVIslandItems);
    SurfaceWaterTilingPreviewViewport->SetWetPartIslandAssignments(
        BuildUVIslandWetPartIDMap(),
        BuildPreviewUVIslandColorMap());
    SurfaceWaterTilingPreviewViewport->SetShowWetPartColors(false);
    SurfaceWaterTilingPreviewViewport->SetPreviewWetPart(
        SelectedMaterialSlotIndex,
        SelectedWetPartID);
    SurfaceWaterTilingPreviewViewport->SetSurfaceWaterTilingPreviewCoverageMode(SurfaceWaterPreviewCoverageMode);
    SurfaceWaterTilingPreviewViewport->SetSurfaceWaterTilingPreviewDisplayMode(SurfaceWaterPreviewDisplayMode);
    SurfaceWaterTilingPreviewViewport->SetPreviewWetness(0.0f, 1.0f);
    SurfaceWaterTilingPreviewViewport->SetSurfaceWaterPreviewDropletsEnabled(true);
    SurfaceWaterTilingPreviewViewport->SetSurfaceWaterPreviewNormalFlip(false, false);

    // The tiling popup renders only the actual material result. Editor Part
    // boundaries remain exclusive to the main Part-edit viewport.
    SurfaceWaterTilingPreviewViewport->ClearHighlightedIsland();

    if (SelectedMaterialSlotIndex != INDEX_NONE)
    {
        SurfaceWaterTilingPreviewViewport->SetHighlightedMaterialSlot(SelectedMaterialSlotIndex);
    }
    else
    {
        SurfaceWaterTilingPreviewViewport->ClearMaterialSlotHighlight();
    }
}

void SWetClothingPartEditorPanel::RefreshIslandSelectionViews()
{
    if (UVView.IsValid())
    {
        UVView->SetSelectedIslands(SelectedUVIslandIDs);
    }

    RefreshPreviewIslandHighlight();
}

void SWetClothingPartEditorPanel::RefreshWetPartAssignmentViews()
{
    TMap<int32, int32>        IslandWetPartIDs;
    TMap<int32, FLinearColor> IslandColors;
    TMap<int32, FLinearColor> PreviewIslandColors;
    TSet<int32>               HiddenIslandIDs;

    const FWetClothingWetPartEntry* DefaultEntry = nullptr;
    TMap<int32, const FWetClothingWetPartEntry*> ExplicitEntryByIslandID;

    if (const UWetClothingAsset* WetClothingAssetPtr = WetClothingAsset.Get())
    {
        const FWetClothingAuthoredMaterialSlot* SlotData =
            WetClothingAssetPtr->Authored.PartData.EditableWetPartData.FindMaterialSlot(SelectedMaterialSlotIndex);
        if (SlotData != nullptr)
        {
            for (const FWetClothingWetPartEntry& Entry : SlotData->WetPartEntries)
            {
                if (Entry.WetPartID == 0)
                {
                    DefaultEntry = &Entry;
                }

                for (const int32 UVIslandID : Entry.AssignedUVIslandIDs)
                {
                    if (!ExplicitEntryByIslandID.Contains(UVIslandID))
                    {
                        ExplicitEntryByIslandID.Add(UVIslandID, &Entry);
                    }
                }
            }
        }
    }

    for (const FUVIslandItemPtr& IslandItem : UVIslandItems)
    {
        if (!IslandItem.IsValid())
        {
            continue;
        }

        const FWetClothingWetPartEntry* const* ExplicitEntry = ExplicitEntryByIslandID.Find(IslandItem->UVIslandID);
        const FWetClothingWetPartEntry*        Entry = ExplicitEntry != nullptr ? *ExplicitEntry : DefaultEntry;
        if (Entry == nullptr)
        {
            continue;
        }

        if (Entry->WetPartID != 0)
        {
            IslandWetPartIDs.Add(IslandItem->UVIslandID, Entry->WetPartID);
            if (!Entry->bViewEnabled)
            {
                HiddenIslandIDs.Add(IslandItem->UVIslandID);
                continue;
            }
        }

        FLinearColor Color = Entry->WetPartID == 0 ? SWetClothingPartEditorPanelLocal::GetUnassignedPartUVViewColor() : Entry->Color;
        Color.A = 1.0f;
        IslandColors.Add(IslandItem->UVIslandID, Color);
        if (Entry->WetPartID != 0)
        {
            PreviewIslandColors.Add(IslandItem->UVIslandID, Color);
        }
    }

    if (UVView.IsValid())
    {
        UVView->SetIslandColors(IslandColors);
        UVView->SetHiddenUVIslandIDs(HiddenIslandIDs);
        UVView->SetSelectedIslands(SelectedUVIslandIDs);
    }

    if (UVIslandListView.IsValid())
    {
        UVIslandListView->RequestListRefresh();
        UVIslandListView->Invalidate(EInvalidateWidget::Paint);
    }

    if (PreviewViewport.IsValid())
    {
        PreviewViewport->SetWetPartIslandAssignments(IslandWetPartIDs, PreviewIslandColors);
    }
}

void SWetClothingPartEditorPanel::RefreshWetPartWidgets()
{
    WetPartInlineRenameWidgets.Reset();

    if (WetPartListView.IsValid())
    {
        WetPartListView->RequestListRefresh();
    }
}

void SWetClothingPartEditorPanel::EnsureDefaultWetPartForSelectedScope()
{
    UWetClothingAsset* WetClothingAssetPtr = WetClothingAsset.Get();
    if (WetClothingAssetPtr == nullptr ||
        SelectedMaterialSlotIndex == INDEX_NONE ||
        !IsSelectedMaterialSlotPartEditingReady() ||
        !HasValidOriginalUVChannel())
    {
        return;
    }

    const FWetPartScope Scope = FWetPartEditingService::MakeScope(SelectedMaterialSlotIndex);
    FWetPartEditingService::EnsureDefaultWetPartForScope(WetClothingAssetPtr, Scope);
}

int32 SWetClothingPartEditorPanel::GetOriginalUVChannelIndex() const
{
    return WetClothingAsset.IsValid() ? WetClothingAsset->GetOriginalUVChannelIndex() : 0;
}

bool SWetClothingPartEditorPanel::HasValidOriginalUVChannel() const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr)
    {
        return false;
    }

    const USkeletalMesh* PreparedMesh = Asset->GetRuntimeSkeletalMesh();
    const int32 UVChannelIndex = Asset->GetOriginalUVChannelIndex();
    return PreparedMesh != nullptr &&
           UVChannelIndex >= 0 &&
           UVChannelIndex < FWetClothingAssetMeshAnalyzer::GetNumUVChannels(PreparedMesh, 0);
}

int32 SWetClothingPartEditorPanel::FindNextWetPartForSelectedScope() const
{
    return FWetPartEditingService::FindNextWetPartIDForScope(
        WetClothingAsset.Get(),
        FWetPartEditingService::MakeScope(SelectedMaterialSlotIndex));
}

FWetClothingWetPartEntry* SWetClothingPartEditorPanel::FindMutableWetPartEntry(int32 WetPartID) const
{
    return FWetPartEditingService::FindMutableEntry(
        WetClothingAsset.Get(),
        FWetPartEditingService::MakeScope(SelectedMaterialSlotIndex),
        WetPartID);
}

const FWetClothingWetPartEntry* SWetClothingPartEditorPanel::FindWetPartEntry(int32 WetPartID) const
{
    return FWetPartEditingService::FindEntry(
        WetClothingAsset.Get(),
        FWetPartEditingService::MakeScope(SelectedMaterialSlotIndex),
        WetPartID);
}

SWetClothingPartEditorPanel::FWetPartEntryPtr SWetClothingPartEditorPanel::FindWetPartItemByID(int32 WetPartID) const
{
    for (const FWetPartEntryPtr& Item : CurrentWetPartItems)
    {
        if (Item.IsValid() && Item->WetPartID == WetPartID)
        {
            return Item;
        }
    }

    return nullptr;
}

SWetClothingPartEditorPanel::FMaterialSlotItemPtr SWetClothingPartEditorPanel::FindMaterialSlotItem(int32 MaterialSlotIndex) const
{
    for (const FMaterialSlotItemPtr& Item : MaterialSlotItems)
    {
        if (Item.IsValid() && Item->SlotIndex == MaterialSlotIndex)
        {
            return Item;
        }
    }

    return nullptr;
}

const FWetClothingWetPartEntry* SWetClothingPartEditorPanel::FindWetPartEntryForUVIsland(int32 UVIslandID) const
{
    return FWetPartEditingService::FindEntryForUVIsland(
        WetClothingAsset.Get(),
        FWetPartEditingService::MakeScope(SelectedMaterialSlotIndex),
        UVIslandID);
}

const FWetClothingWetPartEntry* SWetClothingPartEditorPanel::FindEffectiveWetPartEntryForUVIsland(int32 UVIslandID) const
{
    if (const FWetClothingWetPartEntry* AssignedEntry = FindWetPartEntryForUVIsland(UVIslandID))
    {
        return AssignedEntry;
    }
    return FindWetPartEntry(0);
}

TSet<int32> SWetClothingPartEditorPanel::GetUVIslandIDsForWetPart(int32 WetPartID) const
{
    TSet<int32> Result;

    for (const FUVIslandItemPtr& IslandItem : UVIslandItems)
    {
        if (IslandItem.IsValid() && GetEffectiveWetPartForUVIsland(IslandItem->UVIslandID) == WetPartID)
        {
            Result.Add(IslandItem->UVIslandID);
        }
    }

    return Result;
}

int32 SWetClothingPartEditorPanel::GetEffectiveWetPartForUVIsland(int32 UVIslandID) const
{
    if (const FWetClothingWetPartEntry* EffectiveEntry = FindEffectiveWetPartEntryForUVIsland(UVIslandID))
    {
        return EffectiveEntry->WetPartID;
    }
    return 0;
}

FLinearColor SWetClothingPartEditorPanel::GetDefaultWetPartColor(int32 WetPartID) const
{
    if (WetPartID == 0)
    {
        return SWetClothingPartEditorPanelLocal::GetUnassignedPartColor();
    }

    static const FLinearColor Palette[] = {
        FLinearColor(1.00f, 0.00f, 1.00f, 1.0f),
        FLinearColor(0.00f, 0.25f, 1.00f, 1.0f),
        FLinearColor(0.00f, 1.00f, 0.05f, 1.0f),
        FLinearColor(1.00f, 1.00f, 0.00f, 1.0f),
        FLinearColor(1.00f, 0.35f, 0.00f, 1.0f),
        FLinearColor(0.55f, 0.00f, 1.00f, 1.0f),
        FLinearColor(0.00f, 1.00f, 1.00f, 1.0f),
        FLinearColor(1.00f, 0.00f, 0.20f, 1.0f),
        FLinearColor(0.35f, 1.00f, 0.00f, 1.0f),
        FLinearColor(1.00f, 0.00f, 0.65f, 1.0f),
        FLinearColor(0.00f, 0.65f, 1.00f, 1.0f),
        FLinearColor(0.75f, 1.00f, 0.00f, 1.0f),
        FLinearColor(1.00f, 0.60f, 0.00f, 1.0f),
        FLinearColor(0.35f, 0.00f, 1.00f, 1.0f),
        FLinearColor(0.00f, 1.00f, 0.55f, 1.0f),
        FLinearColor(1.00f, 0.00f, 0.00f, 1.0f)
    };

    const int32 PaletteIndex = FMath::Abs(WetPartID - 1) % UE_ARRAY_COUNT(Palette);
    return Palette[PaletteIndex];
}

FString SWetClothingPartEditorPanel::GetDefaultWetPartName(int32 WetPartID) const
{
    return WetPartID == 0 ? TEXT("Unassigned") : FString::Printf(TEXT("Part %d"), WetPartID);
}

FString SWetClothingPartEditorPanel::GetWetPartDisplayName(const FWetClothingWetPartEntry& Entry) const
{
    if (Entry.WetPartID == 0)
    {
        return TEXT("Unassigned");
    }

    const FString TrimmedName = Entry.DisplayName.TrimStartAndEnd();
    if (!TrimmedName.IsEmpty())
    {
        return TrimmedName;
    }

    return GetDefaultWetPartName(Entry.WetPartID);
}

FString SWetClothingPartEditorPanel::GetAssignedProfileLabel(const FWetClothingWetPartEntry& Entry) const
{
    return FWetPartEditingService::GetAssignedProfileLabel(WetClothingAsset.Get(), Entry);
}

TMap<int32, int32> SWetClothingPartEditorPanel::BuildUVIslandWetPartIDMap() const
{
    TMap<int32, int32> Result;
    for (const FUVIslandItemPtr& IslandItem : UVIslandItems)
    {
        if (!IslandItem.IsValid())
        {
            continue;
        }

        const int32 EffectiveWetPartID = GetEffectiveWetPartForUVIsland(IslandItem->UVIslandID);
        if (FindWetPartEntry(EffectiveWetPartID) != nullptr)
        {
            Result.Add(IslandItem->UVIslandID, EffectiveWetPartID);
        }
    }
    return Result;
}

TMap<int32, FLinearColor> SWetClothingPartEditorPanel::BuildUVIslandColorMap() const
{
    TMap<int32, FLinearColor> Result;

    for (const FUVIslandItemPtr& IslandItem : UVIslandItems)
    {
        if (!IslandItem.IsValid())
        {
            continue;
        }

        if (const FWetClothingWetPartEntry* Entry = FindEffectiveWetPartEntryForUVIsland(IslandItem->UVIslandID))
        {
            if (Entry->WetPartID != 0 && !Entry->bViewEnabled)
            {
                continue;
            }

            FLinearColor Color = Entry->WetPartID == 0 ? SWetClothingPartEditorPanelLocal::GetUnassignedPartUVViewColor() : Entry->Color;
            Color.A = 1.0f;
            Result.Add(IslandItem->UVIslandID, Color);
        }
    }

    return Result;
}

TMap<int32, FLinearColor> SWetClothingPartEditorPanel::BuildPreviewUVIslandColorMap() const
{
    TMap<int32, FLinearColor> Result;

    for (const FUVIslandItemPtr& IslandItem : UVIslandItems)
    {
        if (!IslandItem.IsValid())
        {
            continue;
        }

        if (const FWetClothingWetPartEntry* Entry = FindEffectiveWetPartEntryForUVIsland(IslandItem->UVIslandID))
        {
            if (Entry->WetPartID == 0 || !Entry->bViewEnabled)
            {
                continue;
            }

            FLinearColor Color = Entry->Color;
            Color.A = 1.0f;
            Result.Add(IslandItem->UVIslandID, Color);
        }
    }

    return Result;
}

TSet<int32> SWetClothingPartEditorPanel::BuildHiddenUVIslandIDSet() const
{
    TSet<int32> Result;

    for (const FUVIslandItemPtr& IslandItem : UVIslandItems)
    {
        if (!IslandItem.IsValid())
        {
            continue;
        }

        if (const FWetClothingWetPartEntry* Entry = FindEffectiveWetPartEntryForUVIsland(IslandItem->UVIslandID))
        {
            if (Entry->WetPartID != 0 && !Entry->bViewEnabled)
            {
                Result.Add(IslandItem->UVIslandID);
            }
        }
    }

    return Result;
}

FText SWetClothingPartEditorPanel::GetMaterialSlotStatusText(const int32 MaterialSlotIndex) const
{
    if (MaterialSlotIndex == INDEX_NONE)
    {
        return LOCTEXT("DataUVStatusAllDetails", "DWC UV Details");
    }

    // The table answers one question only: can this slot's DWC UV be used now?
    // Diagnostics remain available through the status cell and details report.
    if (IsMaterialSlotDataUVReadyForEditing(MaterialSlotIndex))
    {
        return LOCTEXT("DataUVStatusReady", "Ready");
    }

    if (IsMaterialSlotIncludedInDataUVLayout(MaterialSlotIndex) ||
        FailedDataUVSlotIndices.Contains(MaterialSlotIndex))
    {
        return LOCTEXT("DataUVStatusFailed", "Failed");
    }

    return FText::FromString(TEXT("-"));
}

FSlateColor SWetClothingPartEditorPanel::GetMaterialSlotStatusColor(const int32 MaterialSlotIndex) const
{
    if (MaterialSlotIndex == INDEX_NONE)
    {
        return FSlateColor(FStyleColors::Foreground);
    }

    if (IsMaterialSlotDataUVReadyForEditing(MaterialSlotIndex))
    {
        return FSlateColor(FLinearColor(0.24f, 0.78f, 0.38f, 1.0f));
    }
    if (IsMaterialSlotIncludedInDataUVLayout(MaterialSlotIndex) ||
        FailedDataUVSlotIndices.Contains(MaterialSlotIndex))
    {
        return FSlateColor(FStyleColors::Error);
    }
    return FSlateColor(FStyleColors::Foreground);
}

FText SWetClothingPartEditorPanel::GetMaterialSlotStatusTooltip(const int32 MaterialSlotIndex) const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr)
    {
        return FText::GetEmpty();
    }

    if (MaterialSlotIndex == INDEX_NONE)
    {
        return LOCTEXT(
            "DataUVAllSlotsDetailsTooltip",
            "View DWC UV generation details and diagnostics for all material slots.");
    }

    if (IsMaterialSlotDataUVReadyForEditing(MaterialSlotIndex))
    {
        return DoesMaterialSlotHaveDataUVWarnings(MaterialSlotIndex)
            ? LOCTEXT(
                "DataUVReadyWithDiagnosticsTooltip",
                "Ready\nDiagnostics are available.\nClick to view details.")
            : LOCTEXT(
                "DataUVReadyTooltip",
                "Ready\nClick to view generation details.");
    }

    if (IsMaterialSlotIncludedInDataUVLayout(MaterialSlotIndex))
    {
        return LOCTEXT(
            "DataUVNotUsableTooltip",
            "Failed\nThe current DWC UV is not usable for editing.\nClick to view details.");
    }

    if (FailedDataUVSlotIndices.Contains(MaterialSlotIndex))
    {
        return LOCTEXT(
            "DataUVFailedTooltip",
            "Failed\nClick to view the build error and diagnostics.");
    }

    return LOCTEXT(
        "DataUVNotGeneratedTooltip",
        "DWC UV has not been built for this material slot.");
}

bool SWetClothingPartEditorPanel::ShouldShowMaterialSlotStatusInfo(const int32 MaterialSlotIndex) const
{
    if (MaterialSlotIndex == INDEX_NONE)
    {
        return true;
    }

    return IsMaterialSlotIncludedInDataUVLayout(MaterialSlotIndex) ||
        FailedDataUVSlotIndices.Contains(MaterialSlotIndex);
}

const FSlateBrush* SWetClothingPartEditorPanel::GetMaterialSlotStatusInfoBrush(const int32 MaterialSlotIndex) const
{
    if (MaterialSlotIndex == INDEX_NONE)
    {
        return FAppStyle::GetBrush(TEXT("Icons.InfoWithColor"));
    }

    if (FailedDataUVSlotIndices.Contains(MaterialSlotIndex) ||
        (IsMaterialSlotIncludedInDataUVLayout(MaterialSlotIndex) &&
         !IsMaterialSlotDataUVReadyForEditing(MaterialSlotIndex)))
    {
        return FAppStyle::GetBrush(TEXT("Icons.ErrorWithColor"));
    }

    return FAppStyle::GetBrush(TEXT("Icons.SuccessWithColor"));
}

FSlateColor SWetClothingPartEditorPanel::GetMaterialSlotStatusInfoColor(const int32 MaterialSlotIndex) const
{
    if (MaterialSlotIndex == INDEX_NONE)
    {
        return FSlateColor(FLinearColor(0.45f, 0.72f, 0.95f, 1.0f));
    }

    // The AppStyle "WithColor" brushes already contain the intended status colors.
    return FSlateColor(FLinearColor::White);
}

FReply SWetClothingPartEditorPanel::HandleMaterialSlotStatusInfoClicked(const int32 MaterialSlotIndex)
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr)
    {
        return FReply::Handled();
    }

    const USkeletalMesh* PreparedMesh = Asset->GetRuntimeSkeletalMesh();
    if (PreparedMesh == nullptr)
    {
        PreparedMesh = Asset->GetSourceSkeletalMesh();
    }

    if (MaterialSlotIndex == INDEX_NONE)
    {
        WCAReportDialogs::OpenDWCDataUVAllSlotsDetailsDialog(
            *Asset,
            PreparedMesh,
            FailedDataUVSlotIndices,
            LastDataUVUpdateError);
    }
    else
    {
        WCAReportDialogs::OpenDWCDataUVSlotDetailsDialog(
            *Asset,
            PreparedMesh,
            MaterialSlotIndex,
            FailedDataUVSlotIndices,
            LastDataUVUpdateError);
    }
    return FReply::Handled();
}

FSlateColor SWetClothingPartEditorPanel::GetMaterialSlotRowBackgroundColor(const int32 MaterialSlotIndex) const
{
    if (!MaterialSlotListView.IsValid())
    {
        return FSlateColor(FLinearColor::Transparent);
    }

    const FMaterialSlotItemPtr Item = FindMaterialSlotItem(MaterialSlotIndex);
    if (!Item.IsValid() || !MaterialSlotListView->IsItemSelected(Item))
    {
        return FSlateColor(FLinearColor::Transparent);
    }

    // Keep the row driving the preview/details visually dominant. Additional
    // selected rows use Unreal's standard inactive-selection tint so they stay
    // recognizable as batch-build targets without competing with the primary row.
    return MaterialSlotIndex == SelectedMaterialSlotIndex
        ? FSlateColor(FStyleColors::Select)
        : FSlateColor(FStyleColors::SelectInactive);
}

FSlateColor SWetClothingPartEditorPanel::GetMaterialSlotRowAccentColor(const int32 MaterialSlotIndex) const
{
    if (IsMaterialSlotPartMapComplete(MaterialSlotIndex))
    {
        return FSlateColor(FLinearColor(0.24f, 0.78f, 0.38f, 1.0f));
    }
    if (DoesMaterialSlotNeedPartMapAttention(MaterialSlotIndex))
    {
        return FSlateColor(FLinearColor(1.0f, 0.78f, 0.18f, 1.0f));
    }
    return FSlateColor(FLinearColor::Transparent);
}

bool SWetClothingPartEditorPanel::IsMaterialSlotIncludedInDataUVLayout(const int32 MaterialSlotIndex) const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || !Asset->HasLockedDataUVLayout() || MaterialSlotIndex == INDEX_NONE)
    {
        return false;
    }
    for (const FDWCDataUVLODMetadata& Metadata : Asset->GetDataUVMetadata())
    {
        // Legacy layouts did not serialize a slot list and generated every material slot.
        if (Metadata.GeneratedMaterialSlotIndices.IsEmpty() ||
            Metadata.GeneratedMaterialSlotIndices.Contains(MaterialSlotIndex))
        {
            return true;
        }
    }
    return false;
}

bool SWetClothingPartEditorPanel::DoesMaterialSlotHaveDataUVWarnings(const int32 MaterialSlotIndex) const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr)
    {
        return false;
    }
    for (const FDWCDataUVLODMetadata& Metadata : Asset->GetDataUVMetadata())
    {
        if (SWetClothingPartEditorPanelLocal::FindDataUVSlotWarning(Metadata, MaterialSlotIndex) != nullptr)
        {
            return true;
        }
    }
    return false;
}

bool SWetClothingPartEditorPanel::IsMaterialSlotPartMapComplete(const int32 MaterialSlotIndex) const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr ||
        MaterialSlotIndex == INDEX_NONE ||
        !FWCAEditorWidgets::IsMaterialSlotWettable(Asset, MaterialSlotIndex) ||
        !IsMaterialSlotDataUVReadyForEditing(MaterialSlotIndex))
    {
        return false;
    }

    const FWetClothingEditableWetPartData& EditableData = Asset->Authored.PartData.EditableWetPartData;
    const FWetClothingAuthoredMaterialSlot* SlotData = EditableData.FindMaterialSlot(MaterialSlotIndex);
    if (SlotData == nullptr)
    {
        return false;
    }

    TSet<int32> AssignedRealPartIslandIDs;
    for (const FWetClothingWetPartEntry& Entry : SlotData->WetPartEntries)
    {
        if (Entry.WetPartID == 0 || Entry.AssignedUVIslandIDs.IsEmpty())
        {
            continue;
        }

        const FWetPartProfileAssignment* Profile = EditableData.FindProfile(Entry);
        if (Profile == nullptr || !Profile->SourceProfile.IsValid())
        {
            return false;
        }

        for (const int32 UVIslandID : Entry.AssignedUVIslandIDs)
        {
            AssignedRealPartIslandIDs.Add(UVIslandID);
        }
    }

    TArray<FUVIslandItemPtr> SlotIslands;
    if (!FWCAUVIslandViewCache::GetMaterialSlotUVIslands(
            Asset,
            Asset->GetOriginalUVChannelIndex(),
            MaterialSlotIndex,
            SlotIslands))
    {
        return false;
    }

    if (SlotIslands.IsEmpty())
    {
        return false;
    }

    for (const FUVIslandItemPtr& IslandItem : SlotIslands)
    {
        if (!IslandItem.IsValid() ||
            !AssignedRealPartIslandIDs.Contains(IslandItem->UVIslandID))
        {
            return false;
        }
    }

    return true;
}

bool SWetClothingPartEditorPanel::DoesMaterialSlotNeedPartMapAttention(const int32 MaterialSlotIndex) const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    return Asset != nullptr &&
           MaterialSlotIndex != INDEX_NONE &&
           FWCAEditorWidgets::IsMaterialSlotWettable(Asset, MaterialSlotIndex) &&
           !IsMaterialSlotPartMapComplete(MaterialSlotIndex);
}

FText SWetClothingPartEditorPanel::GetMaterialSlotPartMapWarningText(const int32 MaterialSlotIndex) const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr ||
        MaterialSlotIndex == INDEX_NONE ||
        !FWCAEditorWidgets::IsMaterialSlotWettable(Asset, MaterialSlotIndex) ||
        IsMaterialSlotPartMapComplete(MaterialSlotIndex))
    {
        return FText::GetEmpty();
    }

    TArray<FText> Reasons;
    if (!IsMaterialSlotDataUVReadyForEditing(MaterialSlotIndex))
    {
        Reasons.Add(LOCTEXT("PartMapWarningDataUVNotReady", "DWC UV Channel is not ready"));
    }

    const FWetClothingEditableWetPartData& EditableData = Asset->Authored.PartData.EditableWetPartData;
    const FWetClothingAuthoredMaterialSlot* SlotData = EditableData.FindMaterialSlot(MaterialSlotIndex);

    int32 MissingProfilePartCount = 0;
    TSet<int32> AssignedRealPartIslandIDs;
    if (SlotData != nullptr)
    {
        for (const FWetClothingWetPartEntry& Entry : SlotData->WetPartEntries)
        {
            if (Entry.WetPartID == 0 || Entry.AssignedUVIslandIDs.IsEmpty())
            {
                continue;
            }

            const FWetPartProfileAssignment* Profile = EditableData.FindProfile(Entry);
            if (Profile == nullptr || !Profile->SourceProfile.IsValid())
            {
                ++MissingProfilePartCount;
            }

            for (const int32 UVIslandID : Entry.AssignedUVIslandIDs)
            {
                AssignedRealPartIslandIDs.Add(UVIslandID);
            }
        }
    }

    TArray<FUVIslandItemPtr> SlotIslands;
    if (IsMaterialSlotDataUVReadyForEditing(MaterialSlotIndex) &&
        FWCAUVIslandViewCache::GetMaterialSlotUVIslands(
            Asset,
            Asset->GetOriginalUVChannelIndex(),
            MaterialSlotIndex,
            SlotIslands))
    {
        int32 UnassignedIslandCount = 0;
        for (const FUVIslandItemPtr& IslandItem : SlotIslands)
        {
            if (!IslandItem.IsValid() ||
                !AssignedRealPartIslandIDs.Contains(IslandItem->UVIslandID))
            {
                ++UnassignedIslandCount;
            }
        }

        if (UnassignedIslandCount > 0)
        {
            Reasons.Add(FText::Format(
                LOCTEXT("PartMapWarningUnassignedIslands", "{0} island(s) need a Part"),
                FText::AsNumber(UnassignedIslandCount)));
        }
    }

    if (MissingProfilePartCount > 0)
    {
        Reasons.Add(MissingProfilePartCount == 1
                        ? LOCTEXT("PartMapWarningOneMissingProfile", "1 part needs a Wetness Profile")
                        : FText::Format(
                              LOCTEXT("PartMapWarningManyMissingProfiles", "{0} parts need a Wetness Profile"),
                              FText::AsNumber(MissingProfilePartCount)));
    }

    if (Reasons.IsEmpty())
    {
            return LOCTEXT("PartMapWarningIncomplete", "Part Map setup is incomplete.");
    }

    TArray<FString> ReasonStrings;
    ReasonStrings.Reserve(Reasons.Num());
    for (const FText& Reason : Reasons)
    {
        ReasonStrings.Add(Reason.ToString());
    }
    return FText::Format(
        LOCTEXT("PartMapWarningReasons", "{0}."),
        FText::FromString(FString::Join(ReasonStrings, TEXT("; "))));
}

TSet<int32> SWetClothingPartEditorPanel::CollectExistingDataUVSlotIndices() const
{
    TSet<int32> Result;
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || !Asset->HasLockedDataUVLayout())
    {
        return Result;
    }

    bool bHasLegacyAllSlotsMetadata = false;
    for (const FDWCDataUVLODMetadata& Metadata : Asset->GetDataUVMetadata())
    {
        if (Metadata.GeneratedMaterialSlotIndices.IsEmpty())
        {
            bHasLegacyAllSlotsMetadata = true;
            continue;
        }
        for (const int32 MaterialSlotIndex : Metadata.GeneratedMaterialSlotIndices)
        {
            if (MaterialSlotIndex != INDEX_NONE)
            {
                Result.Add(MaterialSlotIndex);
            }
        }
    }

    if (!bHasLegacyAllSlotsMetadata)
    {
        return Result;
    }

    const USkeletalMesh* Mesh = Asset->GetRuntimeSkeletalMesh();
    if (Mesh == nullptr)
    {
        Mesh = Asset->GetSourceSkeletalMesh();
    }
    if (Mesh != nullptr)
    {
        for (int32 MaterialSlotIndex = 0; MaterialSlotIndex < Mesh->GetMaterials().Num(); ++MaterialSlotIndex)
        {
            Result.Add(MaterialSlotIndex);
        }
    }
    return Result;
}

TSet<int32> SWetClothingPartEditorPanel::CollectSelectableDataUVOperationSlotIndices() const
{
    TSet<int32> Result;
    for (const FMaterialSlotItemPtr& Item : MaterialSlotItems)
    {
        if (Item.IsValid() &&
            Item->SlotIndex != INDEX_NONE &&
            !IsMaterialSlotIncludedInDataUVLayout(Item->SlotIndex))
        {
            Result.Add(Item->SlotIndex);
        }
    }
    return Result;
}

TSet<int32> SWetClothingPartEditorPanel::CollectSelectedGenerateDataUVSlotIndices() const
{
    TSet<int32> Result;
    const TSet<int32> SelectableSlots = CollectSelectableDataUVOperationSlotIndices();
    for (const int32 MaterialSlotIndex : SelectedDataUVOperationSlotIndices)
    {
        if (SelectableSlots.Contains(MaterialSlotIndex) &&
            !IsMaterialSlotIncludedInDataUVLayout(MaterialSlotIndex))
        {
            Result.Add(MaterialSlotIndex);
        }
    }
    return Result;
}

TSet<int32> SWetClothingPartEditorPanel::CollectSelectedUpdateDataUVSlotIndices() const
{
    return TSet<int32>();
}

bool SWetClothingPartEditorPanel::IsDataUVOperationSelectable(const int32 MaterialSlotIndex) const
{
    return MaterialSlotIndex != INDEX_NONE &&
           CollectSelectableDataUVOperationSlotIndices().Contains(MaterialSlotIndex);
}

ECheckBoxState SWetClothingPartEditorPanel::GetDataUVOperationCheckState(const int32 MaterialSlotIndex) const
{
    return SelectedDataUVOperationSlotIndices.Contains(MaterialSlotIndex)
        ? ECheckBoxState::Checked
        : ECheckBoxState::Unchecked;
}

void SWetClothingPartEditorPanel::HandleDataUVOperationCheckStateChanged(
    const ECheckBoxState NewState,
    const int32 MaterialSlotIndex)
{
    if (!IsDataUVOperationSelectable(MaterialSlotIndex))
    {
        SelectedDataUVOperationSlotIndices.Remove(MaterialSlotIndex);
        return;
    }

    if (NewState == ECheckBoxState::Checked)
    {
        SelectedDataUVOperationSlotIndices.Add(MaterialSlotIndex);
    }
    else
    {
        SelectedDataUVOperationSlotIndices.Remove(MaterialSlotIndex);
    }

    if (MaterialSlotListView.IsValid())
    {
        MaterialSlotListView->RequestListRefresh();
    }
}

ECheckBoxState SWetClothingPartEditorPanel::GetAllDataUVOperationCheckState() const
{
    const TSet<int32> SelectableSlots = CollectSelectableDataUVOperationSlotIndices();
    if (SelectableSlots.IsEmpty())
    {
        return ECheckBoxState::Unchecked;
    }

    int32 SelectedCount = 0;
    for (const int32 MaterialSlotIndex : SelectableSlots)
    {
        if (SelectedDataUVOperationSlotIndices.Contains(MaterialSlotIndex))
        {
            ++SelectedCount;
        }
    }

    if (SelectedCount == 0)
    {
        return ECheckBoxState::Unchecked;
    }
    return SelectedCount == SelectableSlots.Num()
        ? ECheckBoxState::Checked
        : ECheckBoxState::Undetermined;
}

void SWetClothingPartEditorPanel::HandleAllDataUVOperationCheckStateChanged(const ECheckBoxState NewState)
{
    if (NewState == ECheckBoxState::Checked)
    {
        SelectedDataUVOperationSlotIndices = CollectSelectableDataUVOperationSlotIndices();
    }
    else
    {
        SelectedDataUVOperationSlotIndices.Reset();
    }

    if (MaterialSlotListView.IsValid())
    {
        MaterialSlotListView->RequestListRefresh();
    }
}

void SWetClothingPartEditorPanel::SyncDataUVOperationSelection()
{
    const TSet<int32> SelectableSlots = CollectSelectableDataUVOperationSlotIndices();
    TArray<int32> SelectedSlotsToRemove;
    for (const int32 MaterialSlotIndex : SelectedDataUVOperationSlotIndices)
    {
        if (!SelectableSlots.Contains(MaterialSlotIndex))
        {
            SelectedSlotsToRemove.Add(MaterialSlotIndex);
        }
    }
    for (const int32 MaterialSlotIndex : SelectedSlotsToRemove)
    {
        SelectedDataUVOperationSlotIndices.Remove(MaterialSlotIndex);
    }
}

FDWCDataUVBuildResult SWetClothingPartEditorPanel::GenerateDataUVForTargetSlots(
    const TSet<int32>& TargetMaterialSlotIndices,
    const TSet<int32>* ConfirmedVisibleExclusionMaterialSlotIndices)
{
    FDWCDataUVBuildResult Result;
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || TargetMaterialSlotIndices.IsEmpty())
    {
        Result.Message = TEXT("No material slots were selected for the DWC UV build.");
        return Result;
    }

    // Existing generated slots are included so their per-slot DWC UV payloads are refreshed
    // alongside the requested rows. Each slot is committed independently by the build service.
    TSet<int32> BuildMaterialSlotIndices = CollectExistingDataUVSlotIndices();
    BuildMaterialSlotIndices.Append(TargetMaterialSlotIndices);

    FWetClothingEditableWetPartData& EditableData = Asset->Authored.PartData.EditableWetPartData;
    TArray<FWetClothingAuthoredMaterialSlot> OriginalMaterialSlots = EditableData.MaterialSlots;

    for (FWetClothingAuthoredMaterialSlot& Slot : EditableData.MaterialSlots)
    {
        Slot.bIsWettableSlot = BuildMaterialSlotIndices.Contains(Slot.MaterialSlotIndex);
    }
    for (const int32 MaterialSlotIndex : BuildMaterialSlotIndices)
    {
        EditableData.FindOrAddMaterialSlot(MaterialSlotIndex).bIsWettableSlot = true;
    }

    FDWCDataUVBuildOptions BuildOptions;
    BuildOptions.bMergeWithExistingLayout = true;
    BuildOptions.bRequireAllMaterialSlots = true;
    if (ConfirmedVisibleExclusionMaterialSlotIndices != nullptr)
    {
        BuildOptions.ConfirmedVisibleExclusionMaterialSlotIndices =
            *ConfirmedVisibleExclusionMaterialSlotIndices;
    }

    Result = FDWCDataUVBuildService::Generate(
        *Asset,
        false,
        Asset->GetSetupSettings().bAllowOverwritePreferredDWCDataUVChannel,
        true,
        &BuildOptions);

    EditableData.MaterialSlots = MoveTemp(OriginalMaterialSlots);
    return Result;
}

void SWetClothingPartEditorPanel::RestorePersistedDataUVFailureState()
{
    FailedDataUVSlotIndices.Reset();
    LastDataUVUpdateError.Reset();

    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr)
    {
        return;
    }

#if WITH_EDITORONLY_DATA
    for (const int32 MaterialSlotIndex : Asset->Derived.Inline.FailedDataUVMaterialSlotIndices)
    {
        // A committed DWC UV Channel is authoritative and takes precedence over stale failure history.
        if (MaterialSlotIndex != INDEX_NONE && !IsMaterialSlotIncludedInDataUVLayout(MaterialSlotIndex))
        {
            FailedDataUVSlotIndices.Add(MaterialSlotIndex);
        }
    }
    LastDataUVUpdateError = Asset->Derived.Inline.LastDataUVGenerationFailure;
#endif
}

void SWetClothingPartEditorPanel::PersistDataUVFailureState()
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr)
    {
        return;
    }

#if WITH_EDITORONLY_DATA
    TArray<int32> FailedSlots = FailedDataUVSlotIndices.Array();
    FailedSlots.Sort();
    Asset->Modify();
    Asset->Derived.Inline.FailedDataUVMaterialSlotIndices = MoveTemp(FailedSlots);
    Asset->Derived.Inline.LastDataUVGenerationFailure = FailedDataUVSlotIndices.IsEmpty()
        ? FString()
        : LastDataUVUpdateError;
    Asset->MarkPackageDirty();
#endif
}

EVisibility SWetClothingPartEditorPanel::GetDataUVUpdateBarVisibility() const
{
    return IsDataUVOperationEnabled()
        ? EVisibility::Visible
        : EVisibility::Collapsed;
}

FText SWetClothingPartEditorPanel::GetDataUVOperationSummaryText() const
{
    const int32 GenerateCount = CollectSelectedGenerateDataUVSlotIndices().Num();
    if (GenerateCount > 0)
    {
        return FText::Format(
            LOCTEXT("SelectedGenerateDataUVText", "{0} selected"),
            FText::AsNumber(GenerateCount));
    }

    return LOCTEXT("SelectDataUVTasksText", "Select material slots in the left column to build DWC UV.");
}

FText SWetClothingPartEditorPanel::GetDataUVOperationButtonText() const
{
    const int32 GenerateCount = CollectSelectedGenerateDataUVSlotIndices().Num();
    if (GenerateCount > 0)
    {
        return FText::Format(
            LOCTEXT("GenerateDataUVButtonText", "Build DWC UV ({0})"),
            FText::AsNumber(GenerateCount));
    }
    return LOCTEXT("GenerateOrUpdateDataUVButtonText", "Build DWC UV");
}

FText SWetClothingPartEditorPanel::GetDataUVOperationButtonTooltip() const
{
    if (!CollectSelectedGenerateDataUVSlotIndices().IsEmpty())
    {
        return LOCTEXT("GenerateDataUVTooltip", "Build DWC UV for the selected material slots that do not have DWC UV data.");
    }
    return LOCTEXT("SelectDataUVOperationTooltip", "Select at least one material slot without DWC UV data.");
}

bool SWetClothingPartEditorPanel::IsDataUVOperationEnabled() const
{
    return !CollectSelectedGenerateDataUVSlotIndices().IsEmpty();
}

FReply SWetClothingPartEditorPanel::HandleDataUVOperationClicked()
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    const TSet<int32> GenerateSlots = CollectSelectedGenerateDataUVSlotIndices();
    TSet<int32> TargetSlots = GenerateSlots;
    if (Asset == nullptr || TargetSlots.IsEmpty())
    {
        return FReply::Handled();
    }

    const FText ProgressText = LOCTEXT("GenerateDataUVProgress", "Building DWC UV for selected material slots...");

    FScopedSlowTask SlowTask(1.0f, ProgressText);
    SlowTask.MakeDialog(false);
    SlowTask.EnterProgressFrame(1.0f);

    const FDWCDataUVBuildResult BuildResult = GenerateDataUVForTargetSlots(TargetSlots);
    for (const int32 MaterialSlotIndex : TargetSlots)
    {
        SelectedDataUVOperationSlotIndices.Remove(MaterialSlotIndex);
    }

    if (!BuildResult.bSucceeded)
    {
        Asset->SetLastBakeFailure(BuildResult.Message);

        TSet<int32> NewlyFailedSlots;
        for (const int32 FailedMaterialSlotIndex : BuildResult.FailedMaterialSlotIndices)
        {
            if (TargetSlots.Contains(FailedMaterialSlotIndex) &&
                !IsMaterialSlotIncludedInDataUVLayout(FailedMaterialSlotIndex))
            {
                NewlyFailedSlots.Add(FailedMaterialSlotIndex);
            }
        }
        // Global failures cannot identify a single slot. Only targets without committed DWC UV Channel
        // become Failed; existing Warning data remains available and can be selected again.
        if (NewlyFailedSlots.IsEmpty())
        {
            for (const int32 MaterialSlotIndex : TargetSlots)
            {
                if (!IsMaterialSlotIncludedInDataUVLayout(MaterialSlotIndex))
                {
                    NewlyFailedSlots.Add(MaterialSlotIndex);
                }
            }
        }
        FailedDataUVSlotIndices.Append(NewlyFailedSlots);
        LastDataUVUpdateError = BuildResult.Message;
        PersistDataUVFailureState();
        SyncDataUVOperationSelection();

        WCAReportDialogs::OpenDWCDataUVBuildFailureDialog(
            BuildResult,
            Asset,
            BuildResult.PreparedMesh != nullptr ? BuildResult.PreparedMesh : Asset->GetRuntimeSkeletalMesh(),
            TargetSlots);
        if (MaterialSlotListView.IsValid())
        {
            MaterialSlotListView->RequestListRefresh();
        }
        return FReply::Handled();
    }

    for (const int32 MaterialSlotIndex : BuildResult.GeneratedMaterialSlotIndices)
    {
        FailedDataUVSlotIndices.Remove(MaterialSlotIndex);
    }
    for (const int32 MaterialSlotIndex : BuildResult.FailedMaterialSlotIndices)
    {
        FailedDataUVSlotIndices.Add(MaterialSlotIndex);
    }
    if (FailedDataUVSlotIndices.IsEmpty())
    {
        LastDataUVUpdateError.Reset();
        Asset->SetLastBakeFailure(FString());
    }
    else
    {
        LastDataUVUpdateError = BuildResult.Message;
    }
    PersistDataUVFailureState();
    Asset->MarkPackageDirty();
    RefreshFromAsset();
    if ((BuildResult.bGeneratedWithWarnings || !BuildResult.FailedMaterialSlotIndices.IsEmpty()) && !BuildResult.Message.IsEmpty())
    {
        TSet<int32> ReportSlots = TargetSlots;
        ReportSlots.Append(BuildResult.FailedMaterialSlotIndices);
        WCAReportDialogs::OpenDWCDataUVBuildResultDialog(
            BuildResult,
            Asset,
            BuildResult.PreparedMesh != nullptr ? BuildResult.PreparedMesh : Asset->GetRuntimeSkeletalMesh(),
            ReportSlots.IsEmpty() ? TargetSlots : ReportSlots);
    }
    return FReply::Handled();
}

TSharedRef<ITableRow> SWetClothingPartEditorPanel::GenerateMaterialSlotRow(FMaterialSlotItemPtr Item, const TSharedRef<STableViewBase>& OwnerTable)
{
    FWCAMaterialSlotRowArgs Args;
    Args.AllSlotsTitle = FText::Format(
        LOCTEXT("AllMaterialSlotsWithCount", "All Slots ({0})"),
        FText::AsNumber(FMath::Max(0, MaterialSlotItems.Num() - 1)));
    Args.WetClothingAsset = WetClothingAsset.Get();
    Args.GeneratedDataUV = WetClothingAsset.IsValid() ? WetClothingAsset->GetRuntimeSkeletalMesh() : nullptr;
    if (Args.GeneratedDataUV == nullptr && WetClothingAsset.IsValid())
    {
        Args.GeneratedDataUV = WetClothingAsset->GetSourceSkeletalMesh();
    }
    Args.ThumbnailPool = MaterialThumbnailPool;
    Args.ThumbnailSink = &MaterialSlotThumbnails;
    Args.OnWettableSlotClicked = FOnWettableMaterialSlotClicked::CreateSP(this, &SWetClothingPartEditorPanel::HandleWettableMaterialSlotClicked);
    Args.IsWettableToggleEnabled = [this](const int32 MaterialSlotIndex)
    {
        return true;
    };
    Args.GetMaterialSlotStatusText = [this](const int32 MaterialSlotIndex)
    {
        return GetMaterialSlotStatusText(MaterialSlotIndex);
    };
    Args.GetMaterialSlotStatusColor = [this](const int32 MaterialSlotIndex)
    {
        return GetMaterialSlotStatusColor(MaterialSlotIndex);
    };
    Args.GetMaterialSlotStatusTooltip = [this](const int32 MaterialSlotIndex)
    {
        return GetMaterialSlotStatusTooltip(MaterialSlotIndex);
    };
    Args.ShouldShowMaterialSlotStatusInfo = [this](const int32 MaterialSlotIndex)
    {
        return ShouldShowMaterialSlotStatusInfo(MaterialSlotIndex);
    };
    Args.GetMaterialSlotStatusInfoBrush = [this](const int32 MaterialSlotIndex)
    {
        return GetMaterialSlotStatusInfoBrush(MaterialSlotIndex);
    };
    Args.GetMaterialSlotStatusInfoColor = [this](const int32 MaterialSlotIndex)
    {
        return GetMaterialSlotStatusInfoColor(MaterialSlotIndex);
    };
    Args.OnMaterialSlotStatusInfoClicked = [this](const int32 MaterialSlotIndex)
    {
        return HandleMaterialSlotStatusInfoClicked(MaterialSlotIndex);
    };
    Args.GetMaterialSlotWarningText = [this](const int32 MaterialSlotIndex)
    {
        return GetMaterialSlotPartMapWarningText(MaterialSlotIndex);
    };
    Args.GetMaterialSlotRowBackgroundColor = [this](const int32 MaterialSlotIndex)
    {
        return GetMaterialSlotRowBackgroundColor(MaterialSlotIndex);
    };
    Args.GetMaterialSlotRowAccentColor = [this](const int32 MaterialSlotIndex)
    {
        return GetMaterialSlotRowAccentColor(MaterialSlotIndex);
    };
    Args.BuildThumbnailWidget = [this](const int32 MaterialSlotIndex) -> TSharedRef<SWidget>
    {
        return BuildMaterialSlotPreviewWidget(MaterialSlotIndex);
    };

    return FWCAEditorWidgets::GenerateMaterialSlotRow(Item, OwnerTable, Args);
}

TSharedRef<SWidget> SWetClothingPartEditorPanel::BuildMaterialSlotPreviewWidget(const int32 MaterialSlotIndex) const
{
    TArray<FWetClothingAssetUVTriangle> PreviewTriangles;
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const USkeletalMesh* RuntimeMesh = Asset != nullptr ? Asset->GetRuntimeSkeletalMesh() : nullptr;
    const USkeletalMesh* SourceMesh = Asset != nullptr ? Asset->GetSourceSkeletalMesh() : nullptr;
    const int32 PreferredLODIndex = Asset != nullptr ? Asset->GetSimulationLODIndex() : 0;
    const int32 PreferredUVChannelIndex = Asset != nullptr ? Asset->GetOriginalUVChannelIndex() : 0;

    FWCAUVIslandViewCache::BuildMaterialSlotPreviewTriangles(
        Asset,
        MaterialSlotIndex,
        PreviewTriangles);

    const auto TryUVPreview = [&PreviewTriangles, MaterialSlotIndex](
                                  const USkeletalMesh* Mesh,
                                  const int32 LODIndex,
                                  const int32 UVChannelIndex)
    {
        if (PreviewTriangles.IsEmpty() && Mesh != nullptr)
        {
            FWCAUVIslandViewCache::BuildMaterialSlotPreviewTriangles(
                Mesh,
                LODIndex,
                UVChannelIndex,
                MaterialSlotIndex,
                PreviewTriangles);
        }
    };

    // Before the DWC UV build there may be no Prepared/Runtime mesh yet. In that
    // state the compact preview must still use the source mesh instead of becoming blank.
    TryUVPreview(SourceMesh, PreferredLODIndex, PreferredUVChannelIndex);
    if (PreferredLODIndex != 0)
    {
        TryUVPreview(RuntimeMesh, 0, PreferredUVChannelIndex);
        TryUVPreview(SourceMesh, 0, PreferredUVChannelIndex);
    }

    const auto TryGeometryPreview = [&PreviewTriangles, MaterialSlotIndex](
                                        const USkeletalMesh* Mesh,
                                        const int32 LODIndex,
                                        const int32 UVChannelIndex)
    {
        if (PreviewTriangles.IsEmpty() && Mesh != nullptr)
        {
            FWCAUVIslandViewCache::BuildMaterialSlotGeometryPreviewTriangles(
                Mesh,
                LODIndex,
                UVChannelIndex,
                MaterialSlotIndex,
                PreviewTriangles);
        }
    };

    // UV-island analysis intentionally rejects invalid/zero-area UV triangles. The
    // thumbnail is a geometry preview, so fall back to valid 3D triangles in that case.
    TryGeometryPreview(RuntimeMesh, PreferredLODIndex, PreferredUVChannelIndex);
    TryGeometryPreview(SourceMesh, PreferredLODIndex, PreferredUVChannelIndex);
    if (PreferredLODIndex != 0)
    {
        TryGeometryPreview(RuntimeMesh, 0, PreferredUVChannelIndex);
        TryGeometryPreview(SourceMesh, 0, PreferredUVChannelIndex);
    }

    TArray<FTextureItemPtr> LocalTextureItems;
    FTextureItemPtr LocalSelectedTextureItem;
    FWetClothingMaterialTextureResolver::BuildTextureItemsForMaterialSlot(
        WetClothingAsset.Get(),
        MaterialSlotIndex,
        LocalTextureItems,
        LocalSelectedTextureItem);

    UTexture* PreviewTexture = LocalSelectedTextureItem.IsValid() ? LocalSelectedTextureItem->Texture.Get() : nullptr;
    if (Cast<UTexture2D>(PreviewTexture) == nullptr)
    {
        const auto ResolveFallbackTexture = [MaterialSlotIndex](const USkeletalMesh* Mesh) -> UTexture*
        {
            if (Mesh != nullptr)
            {
                const TArray<FSkeletalMaterial>& Materials = Mesh->GetMaterials();
                if (Materials.IsValidIndex(MaterialSlotIndex))
                {
                    return FWetClothingMaterialTextureResolver::ResolveBestMaterialTexture(
                        Materials[MaterialSlotIndex].MaterialInterface);
                }
            }
            return nullptr;
        };

        PreviewTexture = ResolveFallbackTexture(RuntimeMesh);
        if (Cast<UTexture2D>(PreviewTexture) == nullptr)
        {
            PreviewTexture = ResolveFallbackTexture(SourceMesh);
        }
    }

    return SNew(SWCAMaterialSlotPreview)
        .Triangles(PreviewTriangles)
        .PreviewTexture(PreviewTexture)
        .DrawWireframe(true);
}

void SWetClothingPartEditorPanel::HandleMaterialSlotSelectionChanged(FMaterialSlotItemPtr Item, ESelectInfo::Type)
{
    if (bSynchronizingMaterialSlotSelection)
    {
        return;
    }

    TArray<FMaterialSlotItemPtr> SelectedItems;
    SelectedDataUVOperationSlotIndices.Reset();
    if (MaterialSlotListView.IsValid())
    {
        MaterialSlotListView->GetSelectedItems(SelectedItems);
        for (const FMaterialSlotItemPtr& SelectedItem : SelectedItems)
        {
            if (SelectedItem.IsValid() && IsDataUVOperationSelectable(SelectedItem->SlotIndex))
            {
                SelectedDataUVOperationSlotIndices.Add(SelectedItem->SlotIndex);
            }
        }
    }

    const bool bChangedItemIsStillSelected =
        Item.IsValid() &&
        MaterialSlotListView.IsValid() &&
        MaterialSlotListView->IsItemSelected(Item);

    if (bChangedItemIsStillSelected)
    {
        // The most recently selected row becomes the primary row used by the
        // preview and details panels. Other selected rows remain batch targets.
        SelectedMaterialSlotIndex = Item->SlotIndex;
    }
    else
    {
        const bool bPrimaryStillSelected = SelectedItems.ContainsByPredicate(
            [this](const FMaterialSlotItemPtr& SelectedItem)
            {
                return SelectedItem.IsValid() &&
                       SelectedItem->SlotIndex == SelectedMaterialSlotIndex;
            });

        if (!bPrimaryStillSelected)
        {
            SelectedMaterialSlotIndex = INDEX_NONE;
            for (const FMaterialSlotItemPtr& SelectedItem : SelectedItems)
            {
                if (SelectedItem.IsValid())
                {
                    SelectedMaterialSlotIndex = SelectedItem->SlotIndex;
                    break;
                }
            }
        }
    }

    SelectedWetPartID = INDEX_NONE;
    ResetIslandSelection();
    RefreshMaterialTextures(false);
    if (MaterialSlotListView.IsValid())
    {
        MaterialSlotListView->RequestListRefresh();
    }
    RefreshUVIslandList();

    if (PreviewViewport.IsValid())
    {
        if (SelectedMaterialSlotIndex != INDEX_NONE)
        {
            PreviewViewport->SetHighlightedMaterialSlot(SelectedMaterialSlotIndex);
        }
        else
        {
            PreviewViewport->ClearMaterialSlotHighlight();
        }
    }
}

void SWetClothingPartEditorPanel::HandleMaterialSlotDoubleClicked(FMaterialSlotItemPtr Item)
{
    if (!Item.IsValid() || !ShouldShowMaterialSlotStatusInfo(Item->SlotIndex))
    {
        return;
    }

    HandleMaterialSlotStatusInfoClicked(Item->SlotIndex);
}

FReply SWetClothingPartEditorPanel::HandleWettableMaterialSlotClicked(int32 MaterialSlotIndex)
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || MaterialSlotIndex == INDEX_NONE)
    {
        return FReply::Handled();
    }

    const bool bWasWettable = FWCAEditorWidgets::IsMaterialSlotWettable(Asset, MaterialSlotIndex);
    const bool bNewWettable = !bWasWettable;
    const bool bWasSelectedMaterialSlot = MaterialSlotIndex == SelectedMaterialSlotIndex;
    FWCAEditorWidgets::SetMaterialSlotWettable(Asset, MaterialSlotIndex, bNewWettable);
    if (FMaterialSlotItemPtr SlotItem = FindMaterialSlotItem(MaterialSlotIndex))
    {
        SlotItem->bIsWettableSlot = bNewWettable;
    }

    const bool bHasDataUV = IsMaterialSlotIncludedInDataUVLayout(MaterialSlotIndex);
    if (bNewWettable && !bHasDataUV)
    {
        SelectedDataUVOperationSlotIndices.Add(MaterialSlotIndex);
        SelectedMaterialSlotIndex = MaterialSlotIndex;
        if (MaterialSlotListView.IsValid())
        {
            if (const FMaterialSlotItemPtr SlotItem = FindMaterialSlotItem(MaterialSlotIndex))
            {
                TGuardValue<bool> SelectionGuard(bSynchronizingMaterialSlotSelection, true);
                MaterialSlotListView->SetItemSelection(SlotItem, true, ESelectInfo::Direct);
            }
        }
    }
    else if (!bNewWettable)
    {
        SelectedDataUVOperationSlotIndices.Remove(MaterialSlotIndex);
        if (MaterialSlotListView.IsValid())
        {
            if (const FMaterialSlotItemPtr SlotItem = FindMaterialSlotItem(MaterialSlotIndex))
            {
                TGuardValue<bool> SelectionGuard(bSynchronizingMaterialSlotSelection, true);
                MaterialSlotListView->SetItemSelection(SlotItem, false, ESelectInfo::Direct);
            }
        }
        if (SelectedMaterialSlotIndex == MaterialSlotIndex)
        {
            SelectedMaterialSlotIndex = INDEX_NONE;
            for (const int32 SelectedSlotIndex : SelectedDataUVOperationSlotIndices)
            {
                SelectedMaterialSlotIndex = SelectedSlotIndex;
                break;
            }
        }
    }

    if (MaterialSlotListView.IsValid())
    {
        MaterialSlotListView->RequestListRefresh();
    }
    if (bWasSelectedMaterialSlot || MaterialSlotIndex == SelectedMaterialSlotIndex)
    {
        RefreshWetPartList(true);
        RefreshWetPartAssignmentViews();
        RefreshMaterialTextures(false);
        RefreshUVIslandList();
    }

    return FReply::Handled();
}

void SWetClothingPartEditorPanel::MarkSelectedMaterialSlotWettable(bool bInvalidateBake)
{
    UWetClothingAsset* WetClothingAssetPtr = WetClothingAsset.Get();
    if (WetClothingAssetPtr == nullptr || SelectedMaterialSlotIndex == INDEX_NONE)
    {
        return;
    }

    const bool bWasWettable = FWCAEditorWidgets::IsMaterialSlotWettable(WetClothingAssetPtr, SelectedMaterialSlotIndex);
    FWCAEditorWidgets::MarkMaterialSlotWettable(WetClothingAssetPtr, SelectedMaterialSlotIndex);
    if (bInvalidateBake && bWasWettable)
    {
        WetClothingAssetPtr->MarkSimulationBakeOutOfDate();
        WetClothingAssetPtr->MarkVisualBakeOutOfDate();
        WetClothingAssetPtr->RefreshBakeState(false);
        WetClothingAssetPtr->MarkPackageDirty();
    }
    EnsureDefaultWetPartForSelectedScope();
    if (FMaterialSlotItemPtr SelectedItem = FindMaterialSlotItem(SelectedMaterialSlotIndex))
    {
        SelectedItem->bIsWettableSlot = true;
    }

    if (MaterialSlotListView.IsValid())
    {
        MaterialSlotListView->RequestListRefresh();
    }
}

TSharedRef<SWidget> SWetClothingPartEditorPanel::GenerateTextureComboItem(FTextureItemPtr Item)
{
    return FWCAEditorWidgets::GenerateTextureComboItem(Item, MaterialThumbnailPool, &TextureThumbnails);
}

void SWetClothingPartEditorPanel::HandleTextureSelectionChanged(FTextureItemPtr Item, ESelectInfo::Type SelectInfo)
{
    SelectedTextureItem = Item;
    bShowMaterialTextureInUVView = SelectedTextureItem.IsValid() && SelectedTextureItem->Texture.IsValid();
    SaveSelectedTexture();

    if (SelectedTextureComboContentBox.IsValid())
    {
        SelectedTextureComboContentBox->SetContent(
            FWCAEditorWidgets::BuildTextureComboContent(SelectedTextureItem, 24.0f, true, MaterialThumbnailPool, &TextureThumbnails));
    }

    if (MaterialSlotListView.IsValid())
    {
        MaterialSlotListView->RequestListRefresh();
    }

    RefreshUVView();
}

TSharedRef<SWidget> SWetClothingPartEditorPanel::BuildTextureComboContent(FTextureItemPtr Item, float ThumbnailSize, bool bCompactLayout)
{
    return FWCAEditorWidgets::BuildTextureComboContent(Item, ThumbnailSize, bCompactLayout, MaterialThumbnailPool, &TextureThumbnails);
}

FReply SWetClothingPartEditorPanel::HandleApplyMaterialSetupClicked()
{
    UMaterialInterface* SelectedMaterial = nullptr;
    for (const FMaterialSlotItemPtr& MaterialSlotItem : MaterialSlotItems)
    {
        if (MaterialSlotItem.IsValid() && MaterialSlotItem->SlotIndex == SelectedMaterialSlotIndex)
        {
            SelectedMaterial = MaterialSlotItem->Material.Get();
            break;
        }
    }

    UMaterialInterface* SourceMaterial = SelectedMaterial;
    UWetClothingAsset*  WetClothingAssetPtr = WetClothingAsset.Get();

    const FWCAMaterialGenerator::FOptions MaterialSetupOptions =
        FWCAMaterialGenerator::MakeOptionsForAsset(
            WetClothingAssetPtr,
            EDWCSimulationMode::VertexCPU,
            SelectedMaterialSlotIndex);
    const FWetClothingUnifiedMaterialSetupResult MaterialSet =
        FWCAMaterialGenerator::CreateOrUpdateUnifiedMaterialSet(SourceMaterial, MaterialSetupOptions);

    FString ResultMessage = MaterialSet.Message;
    const bool bSucceeded =
        MaterialSet.bSucceeded && MaterialSet.GeneratedMaterial != nullptr &&
        MaterialSet.GeneratedMaterialInstance != nullptr;

    if (bSucceeded && SourceMaterial != nullptr)
    {
        if (WetClothingAssetPtr != nullptr)
        {
            if (USkeletalMesh* GeneratedDataUV = WetClothingAssetPtr->GetRuntimeSkeletalMesh())
            {
                const TArray<FSkeletalMaterial>& Materials = GeneratedDataUV->GetMaterials();
                TArray<int32>                    AssignedSlotIndices;
                for (int32 MaterialIndex = 0; MaterialIndex < Materials.Num(); ++MaterialIndex)
                {
                    if (SWetClothingPartEditorPanelLocal::IsSameMaterialFamily(
                            Materials[MaterialIndex].MaterialInterface,
                            SourceMaterial))
                    {
                        AssignedSlotIndices.Add(MaterialIndex);
                    }
                }

                if (AssignedSlotIndices.Num() > 0)
                {
                    WetClothingAssetPtr->Modify();
                    GeneratedDataUV->Modify();
                    for (const int32 MaterialIndex : AssignedSlotIndices)
                    {
                        FWetClothingGeneratedWetMaterialOverride* ExistingOverride = WetClothingAssetPtr->Derived.Inline.GeneratedWetMaterialOverrides.FindByPredicate(
                            [MaterialIndex](const FWetClothingGeneratedWetMaterialOverride& MaterialOverride)
                            {
                                return MaterialOverride.MaterialSlotIndex == MaterialIndex;
                            });

                        if (ExistingOverride == nullptr)
                        {
                            ExistingOverride = &WetClothingAssetPtr->Derived.Inline.GeneratedWetMaterialOverrides.AddDefaulted_GetRef();
                            ExistingOverride->MaterialSlotIndex = MaterialIndex;
                        }

                        ExistingOverride->SourceMaterial = SourceMaterial;
                        ExistingOverride->GeneratedMaterial = MaterialSet.GeneratedMaterial;
                        ExistingOverride->GeneratedMaterialInstance = MaterialSet.GeneratedMaterialInstance;
                        GeneratedDataUV->GetMaterials()[MaterialIndex].MaterialInterface =
                            MaterialSet.GeneratedMaterialInstance;
                        FWCAEditorWidgets::MarkMaterialSlotWettable(WetClothingAssetPtr, MaterialIndex);
                    }
                    GeneratedDataUV->MarkPackageDirty();
                    WetClothingAssetPtr->MarkPackageDirty();
                    RefreshMaterialSlotItems();

                    FString AssignedSlotText;
                    for (int32 Index = 0; Index < AssignedSlotIndices.Num(); ++Index)
                    {
                        if (Index > 0)
                        {
                            AssignedSlotText += TEXT(", ");
                        }
                        AssignedSlotText += FString::FromInt(AssignedSlotIndices[Index]);
                    }

                    ResultMessage += FString::Printf(
                        TEXT("\nStored shared '%s', CPU '%s', and GPU '%s' as runtime wet material overrides for %d material slot(s) on '%s': %s."),
                        *MaterialSet.GeneratedMaterial->GetName(),
                        *MaterialSet.GeneratedMaterialInstance->GetName(),
                        *MaterialSet.GeneratedMaterialInstance->GetName(),
                        AssignedSlotIndices.Num(),
                        *GeneratedDataUV->GetName(),
                        *AssignedSlotText);

                    if (PreviewViewport.IsValid())
                    {
                        PreviewViewport->SetHighlightedMaterialSlot(SelectedMaterialSlotIndex);
                    }
                    if (DetailsView.IsValid())
                    {
                        DetailsView->ForceRefresh();
                    }
                }
            }
        }
    }

    const EAppMsgCategory MessageCategory = bSucceeded ? EAppMsgCategory::Success : EAppMsgCategory::Error;
    FMessageDialog::Open(MessageCategory, EAppMsgType::Ok, FText::FromString(ResultMessage));

    return FReply::Handled();
}

bool SWetClothingPartEditorPanel::IsApplyMaterialSetupEnabled() const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr ||
        Asset->GetRuntimeSkeletalMesh() == nullptr ||
        SelectedMaterialSlotIndex == INDEX_NONE)
    {
        return false;
    }

    if (Asset->GetSetupSettings().bBuildGPUWetnessMapSimulationData)
    {
        if (!Asset->HasValidDataUVForLOD(Asset->GetSimulationLODIndex()))
        {
            return false;
        }
    }

    for (const FMaterialSlotItemPtr& MaterialSlotItem : MaterialSlotItems)
    {
        if (MaterialSlotItem.IsValid() && MaterialSlotItem->SlotIndex == SelectedMaterialSlotIndex)
        {
            return MaterialSlotItem->Material.IsValid();
        }
    }

    return false;
}

TSharedRef<ITableRow> SWetClothingPartEditorPanel::GenerateUVIslandRow(FUVIslandItemPtr Item, const TSharedRef<STableViewBase>& OwnerTable)
{
    return SNew(STableRow<FUVIslandItemPtr>, OwnerTable)
        .Padding(FMargin(SWetClothingPartEditorPanelLocal::UVIslandListHorizontalPadding, 3.0f))
            [SNew(SHorizontalBox)

             + SHorizontalBox::Slot()
                   .AutoWidth()
                   .VAlign(VAlign_Center)
                   .Padding(0.0f, 0.0f, 12.0f, 0.0f)
                       [SNew(SBox)
                            .WidthOverride(SWetClothingPartEditorPanelLocal::UVIslandIDColumnWidth - 12.0f)
                                [SNew(STextBlock)
                                     .Text_Lambda([Item]()
                                     {
                                         return Item.IsValid()
                                                    ? FText::Format(LOCTEXT("UVIslandRowID", "#{0}"), FText::AsNumber(Item->UVIslandID))
                                                    : LOCTEXT("InvalidUVIsland", "Invalid");
                                     })
                                     .OverflowPolicy(ETextOverflowPolicy::Ellipsis)]]

             + SHorizontalBox::Slot()
                   .AutoWidth()
                   .VAlign(VAlign_Center)
                   .Padding(0.0f, 0.0f, 12.0f, 0.0f)
                       [SNew(SBox)
                            .WidthOverride(SWetClothingPartEditorPanelLocal::UVIslandPartIDColumnWidth - 12.0f)
                                [SNew(SHorizontalBox)

                                 + SHorizontalBox::Slot()
                                       .AutoWidth()
                                       .VAlign(VAlign_Center)
                                       .Padding(0.0f, 0.0f, 7.0f, 0.0f)
                                           [SNew(SBox)
                                                .WidthOverride(16.0f)
                                                .HeightOverride(16.0f)
                                                    [SNew(SBorder)
                                                         .BorderImage(FAppStyle::Get().GetBrush(TEXT("WhiteBrush")))
                                                         .BorderBackgroundColor_Lambda(
                                    [this, Item]() -> FSlateColor
                                    {
                                        FLinearColor SwatchColor(0.22f, 0.22f, 0.22f, 1.0f);
                                        if (Item.IsValid())
                                        {
                                            if (const FWetClothingWetPartEntry* AssignedEntry = FindWetPartEntryForUVIsland(Item->UVIslandID))
                                            {
                                                if (AssignedEntry->WetPartID != 0)
                                                {
                                                    SwatchColor = AssignedEntry->Color;
                                                    SwatchColor.A = 1.0f;
                                                }
                                            }
                                        }
                                        return FSlateColor(SwatchColor);
                                    })]]

                                 + SHorizontalBox::Slot()
                                       .FillWidth(1.0f)
                                       .VAlign(VAlign_Center)
                                           [SNew(STextBlock)
                                                .Text_Lambda([this, Item]()
                                                {
                                                    if (!Item.IsValid())
                                                    {
                                                        return LOCTEXT("InvalidUVIslandPartID", "Invalid");
                                                    }
                                                    if (const FWetClothingWetPartEntry* AssignedEntry = FindWetPartEntryForUVIsland(Item->UVIslandID))
                                                    {
                                                        if (AssignedEntry->WetPartID != 0)
                                                        {
                                                            return FText::Format(
                                                                LOCTEXT("UVIslandPartNameWithID", "{0} (ID {1})"),
                                                                FText::FromString(GetWetPartDisplayName(*AssignedEntry)),
                                                                FText::AsNumber(AssignedEntry->WetPartID));
                                                        }
                                                        return FText::FromString(GetWetPartDisplayName(*AssignedEntry));
                                                    }
                                                    return LOCTEXT("UVIslandPartUnassigned", "Unassigned");
                                                })
                                                .ColorAndOpacity_Lambda([this, Item]()
                                                {
                                                    if (!Item.IsValid())
                                                    {
                                                        return FSlateColor::UseSubduedForeground();
                                                    }
                                                    const FWetClothingWetPartEntry* AssignedEntry = FindWetPartEntryForUVIsland(Item->UVIslandID);
                                                    return AssignedEntry != nullptr && AssignedEntry->WetPartID != 0
                                                        ? FSlateColor::UseForeground()
                                                        : FSlateColor::UseSubduedForeground();
                                                })
                                                .OverflowPolicy(ETextOverflowPolicy::Ellipsis)]]
                       ]

             + SHorizontalBox::Slot()
                   .AutoWidth()
                   .VAlign(VAlign_Center)
                   .HAlign(HAlign_Right)
                       [SNew(SBox)
                            .WidthOverride(SWetClothingPartEditorPanelLocal::UVIslandTriangleCountColumnWidth)
                            .HAlign(HAlign_Right)
                                [SNew(STextBlock)
                                     .Text_Lambda([Item]()
                                     {
                                         return Item.IsValid()
                                                    ? FText::Format(
                                                          LOCTEXT("UVIslandTriangleCountText", "{0} tris"),
                                                          FText::AsNumber(Item->TriangleCount))
                                                    : FText::GetEmpty();
                                     })
                                     .Justification(ETextJustify::Right)]]];
}

void SWetClothingPartEditorPanel::HandleUVIslandSelectionChanged(FUVIslandItemPtr Item, ESelectInfo::Type SelectInfo)
{
    if (bSyncingUVIslandListSelection || !UVIslandListView.IsValid())
    {
        return;
    }
    TArray<FUVIslandItemPtr> SelectedItems;
    UVIslandListView->GetSelectedItems(SelectedItems);
    TSet<int32> NewSelectedIDs;
    for (const FUVIslandItemPtr& SelectedItem : SelectedItems)
    {
        if (SelectedItem.IsValid())
        {
            NewSelectedIDs.Add(SelectedItem->UVIslandID);
        }
    }
    const int32 NewPrimaryID = Item.IsValid() ? Item->UVIslandID : (NewSelectedIDs.Num() > 0 ? *NewSelectedIDs.CreateConstIterator() : INDEX_NONE);
    SetSelectedUVIslandIDs(NewSelectedIDs, NewPrimaryID, false);
}

void SWetClothingPartEditorPanel::HandleUVIslandSelectionChangedFromUVView(const TArray<int32>& UVIslandIDs, EWCAUVSelectionOp SelectionOp)
{
    ApplyIslandSelection(UVIslandIDs, SelectionOp == EWCAUVSelectionOp::Add);
}

void SWetClothingPartEditorPanel::HandleUVIslandPickedFromPreview(int32 UVIslandID, bool bAppendSelection)
{
    if (UVIslandID == INDEX_NONE)
    {
        if (!bAppendSelection)
        {
            SetSelectedUVIslandIDs(TSet<int32>(), INDEX_NONE);
        }
        return;
    }
    ApplyIslandSelection({ UVIslandID }, bAppendSelection);
}

void SWetClothingPartEditorPanel::ApplyIslandSelection(const TArray<int32>& HitUVIslandIDs, bool bAppendSelection)
{
    TSet<int32> NewSelection = bAppendSelection ? SelectedUVIslandIDs : TSet<int32>();
    for (int32 UVIslandID : HitUVIslandIDs)
    {
        if (UVIslandID != INDEX_NONE)
        {
            NewSelection.Add(UVIslandID);
        }
    }
    const int32 NewPrimaryID = HitUVIslandIDs.Num() > 0 ? HitUVIslandIDs.Last() : INDEX_NONE;
    SetSelectedUVIslandIDs(NewSelection, NewPrimaryID);
}

void SWetClothingPartEditorPanel::SetSelectedUVIslandIDs(const TSet<int32>& InSelectedUVIslandIDs, int32 InPrimarySelectedUVIslandID, bool bSyncListSelection)
{
    SelectedUVIslandIDs = InSelectedUVIslandIDs;
    SelectedUVIslandID = SelectedUVIslandIDs.Contains(InPrimarySelectedUVIslandID) ? InPrimarySelectedUVIslandID : (SelectedUVIslandIDs.Num() > 0 ? *SelectedUVIslandIDs.CreateConstIterator() : INDEX_NONE);

    bool bKeepSelectedWetPart = SelectedWetPartID == INDEX_NONE;
    if (!bKeepSelectedWetPart)
    {
        const TSet<int32> WetPartUVIslandIDs = GetUVIslandIDsForWetPart(SelectedWetPartID);
        bKeepSelectedWetPart = WetPartUVIslandIDs.Num() == SelectedUVIslandIDs.Num();
        if (bKeepSelectedWetPart)
        {
            for (int32 UVIslandID : WetPartUVIslandIDs)
            {
                if (!SelectedUVIslandIDs.Contains(UVIslandID))
                {
                    bKeepSelectedWetPart = false;
                    break;
                }
            }
        }
    }

    if (!bKeepSelectedWetPart)
    {
        SelectedWetPartID = INDEX_NONE;
        if (WetPartListView.IsValid())
        {
            WetPartListView->ClearSelection();
        }
    }

    if (bSyncListSelection)
    {
        SyncUVIslandListSelectionToState();
    }
    RefreshIslandSelectionViews();
}

void SWetClothingPartEditorPanel::SyncUVIslandListSelectionToState()
{
    if (!UVIslandListView.IsValid())
    {
        return;
    }
    bSyncingUVIslandListSelection = true;
    UVIslandListView->ClearSelection();
    for (const FUVIslandItemPtr& IslandItem : UVIslandItems)
    {
        if (IslandItem.IsValid() && SelectedUVIslandIDs.Contains(IslandItem->UVIslandID))
        {
            UVIslandListView->SetItemSelection(IslandItem, true, ESelectInfo::Direct);
            if (IslandItem->UVIslandID == SelectedUVIslandID)
            {
                UVIslandListView->RequestScrollIntoView(IslandItem);
            }
        }
    }
    bSyncingUVIslandListSelection = false;
}

void SWetClothingPartEditorPanel::ResetIslandSelection()
{
    SelectedUVIslandID = INDEX_NONE;
    SelectedUVIslandIDs.Reset();
    if (UVIslandListView.IsValid())
    {
        UVIslandListView->ClearSelection();
    }
}

TSharedRef<ITableRow> SWetClothingPartEditorPanel::GenerateWetPartRow(FWetPartEntryPtr Item, const TSharedRef<STableViewBase>& OwnerTable)
{
    const FLinearColor                   Color = Item.IsValid() ? (Item->WetPartID == 0 ? SWetClothingPartEditorPanelLocal::GetUnassignedPartColor() : Item->Color) : SWetClothingPartEditorPanelLocal::GetUnassignedPartColor();
    const float                          ProfileControlHeight = 26.0f;
    const float                          WetnessProfilePickerWidth = 220.0f;
    TSharedPtr<SInlineEditableTextBlock> InlineTextBlock;

    TSharedRef<ITableRow> Row = SNew(STableRow<FWetPartEntryPtr>, OwnerTable)
                                    .Padding(4.0f)
                                        [SNew(SHorizontalBox)

                                         + SHorizontalBox::Slot()
                                               .AutoWidth()
                                               .VAlign(VAlign_Center)
                                               .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                                                   [SNew(SButton)
                                                        .ContentPadding(FMargin(2.0f))
                                                        .OnClicked(this, &SWetClothingPartEditorPanel::HandleToggleWetPartViewClicked, Item)
                                                        .IsEnabled_Lambda([Item]()
                                                                          { return Item.IsValid() && Item->WetPartID != 0; })
                                                            [SNew(SImage)
                                                                 .Image(this, &SWetClothingPartEditorPanel::GetWetPartVisibilityBrush, Item)]]

                                         + SHorizontalBox::Slot()
                                               .AutoWidth()
                                               .VAlign(VAlign_Center)
                                               .Padding(0.0f, 0.0f, 8.0f, 0.0f)
                                                   [SNew(SBox)
                                                        .WidthOverride(30.0f)
                                                        .HeightOverride(30.0f)
                                                            [SNew(SButton)
                                                                 .ButtonStyle(FAppStyle::Get(), TEXT("NoBorder"))
                                                                 .ContentPadding(0.0f)
                                                                 .ToolTipText(LOCTEXT("WetPartColorTooltip", "Edit wet part debug color."))
                                                                 .IsEnabled_Lambda([Item]()
                                                                                   { return Item.IsValid() && Item->WetPartID != 0; })
                                                                 .OnClicked(this, &SWetClothingPartEditorPanel::HandleWetPartColorClicked, Item)
                                                                     [SNew(SWetClothingPartEditorPanelLocal::SUntintedColorBlockBox)
                                                                          [SNew(SColorBlock)
                                                                               .Color(Color)
                                                                               .Size(FVector2D(30.0f, 30.0f))
                                                                               .ShowBackgroundForAlpha(false)]]]]

                                         + SHorizontalBox::Slot()
                                               .FillWidth(1.0f)
                                               .VAlign(VAlign_Center)
                                                   [SNew(SVerticalBox)

                                                    + SVerticalBox::Slot()
                                                          .AutoHeight()
                                                              [SNew(SHorizontalBox)

                                                               + SHorizontalBox::Slot()
                                                                     .AutoWidth()
                                                                     .VAlign(VAlign_Center)
                                                                         [SAssignNew(InlineTextBlock, SInlineEditableTextBlock)
                                                                              .IsReadOnly_Lambda([Item]()
                                                                                                 { return !Item.IsValid() || Item->WetPartID == 0; })
                                                                              .Text_Lambda([this, Item]()
                                                                                           { return Item.IsValid()
                                                                                                        ? FText::FromString(GetWetPartDisplayName(*Item))
                                                                                                        : LOCTEXT("InvalidWetPartName", "Invalid Part"); })
                                                                              .OnTextCommitted(this, &SWetClothingPartEditorPanel::HandleWetPartNameCommitted, Item)]

                                                               + SHorizontalBox::Slot()
                                                                     .AutoWidth()
                                                                     .VAlign(VAlign_Center)
                                                                     .Padding(4.0f, 0.0f, 0.0f, 0.0f)
                                                                         [SNew(STextBlock)
                                                                              .Text(Item.IsValid()
                                                                                        ? FText::Format(LOCTEXT("WetPartRowIDLabel", "(ID {0})"), FText::AsNumber(Item->WetPartID))
                                                                                        : LOCTEXT("InvalidWetPartIDLabel", "(Invalid)"))
                                                                              .Visibility(Item.IsValid() && Item->WetPartID == 0 ? EVisibility::Collapsed : EVisibility::Visible)
                                                                              .ColorAndOpacity(FSlateColor(FLinearColor(0.62f, 0.62f, 0.62f, 1.0f)))]]

                                                    + SVerticalBox::Slot()
                                                          .AutoHeight()
                                                          .Padding(0.0f, 6.0f, 0.0f, 0.0f)
                                                              [SNew(SHorizontalBox)

                                                               + SHorizontalBox::Slot()
                                                                     .AutoWidth()
                                                                     .VAlign(VAlign_Center)
                                                                     .Padding(0.0f, 0.0f, 0.0f, 0.0f)
                                                                         [SNew(SBox)
                                                                              .WidthOverride(WetnessProfilePickerWidth + ProfileControlHeight + 6.0f)
                                                                              .HeightOverride(ProfileControlHeight)
                                                                                  [SNew(SHorizontalBox)

                                                                                   + SHorizontalBox::Slot()
                                                                                         .FillWidth(1.0f)
                                                                                         .VAlign(VAlign_Center)
                                                                                             [SNew(SObjectPropertyEntryBox)
                                                                                                  .AllowedClass(UWetnessProfile::StaticClass())
                                                                                                  .AllowClear(true)
                                                                                                  .AllowCreate(false)
                                                                                                  .DisplayThumbnail(false)
                                                                                                  .DisplayUseSelected(false)
                                                                                                  .DisplayBrowse(false)
                                                                                                  .IsEnabled(this, &SWetClothingPartEditorPanel::IsWetnessProfileControlEnabled, Item)
                                                                                                  .ObjectPath(this, &SWetClothingPartEditorPanel::GetWetnessProfileObjectPath, Item)
                                                                                                  .OnObjectChanged_Lambda([this, Item](const FAssetData& AssetData)
                                                                                                                          { HandleWetnessProfilePicked(Item, AssetData); })]

                                                                                   + SHorizontalBox::Slot()
                                                                                         .AutoWidth()
                                                                                         .VAlign(VAlign_Center)
                                                                                         .Padding(6.0f, 0.0f, 0.0f, 0.0f)
                                                                                             [SNew(SBox)
                                                                                                  .WidthOverride(ProfileControlHeight)
                                                                                                  .HeightOverride(ProfileControlHeight)
                                                                                                      [SNew(SButton)
                                                                                                           .ButtonStyle(FAppStyle::Get(), TEXT("NoBorder"))
                                                                                                           .ContentPadding(FMargin(3.0f))
                                                                                                           .ToolTipText(LOCTEXT("SurfaceWaterTilingButtonTooltip", "Edit Part-local Surface Water size values and preview levels. Empty Parts will show a warning instead of opening the preview."))
                                                                                                           .IsEnabled_Lambda([this, Item]()
                                                                                                                             { return IsSurfaceWaterTilingEnabled(Item); })
                                                                                                           .OnClicked(this, &SWetClothingPartEditorPanel::HandleOpenSurfaceWaterTilingClicked, Item)
                                                                                                               [SNew(SBox)
                                                                                                                    .WidthOverride(16.0f)
                                                                                                                    .HeightOverride(16.0f)
                                                                                                                        [SNew(SImage)
                                                                                                                             .Image(SWetClothingPartEditorPanelLocal::GetSurfaceWaterTilingBrush())
                                                                                                                             .ColorAndOpacity(FSlateColor::UseForeground())]]]]]]

                                                               + SHorizontalBox::Slot()
                                                                     .FillWidth(1.0f)

                                                               + SHorizontalBox::Slot()
                                                                     .AutoWidth()
                                                                     .VAlign(VAlign_Center)
                                                                         [SNew(SBox)
                                                                              .HeightOverride(ProfileControlHeight)
                                                                              .VAlign(VAlign_Center)
                                                                                  [SNew(SHorizontalBox)

                                                                                   + SHorizontalBox::Slot()
                                                                                         .AutoWidth()
                                                                                         .VAlign(VAlign_Center)
                                                                                             [PropertyCustomizationHelpers::MakeUseSelectedButton(
                                                                                                  FSimpleDelegate::CreateSP(this, &SWetClothingPartEditorPanel::HandleUseSelectedWetnessProfileClicked, Item),
                                                                                                  LOCTEXT("UseSelectedWetnessProfileTooltip", "Use the selected Wetness Profile from the Content Browser."),
                                                                                                  TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateSP(this, &SWetClothingPartEditorPanel::IsWetnessProfileControlEnabled, Item)))]

                                                                                   + SHorizontalBox::Slot()
                                                                                         .AutoWidth()
                                                                                         .VAlign(VAlign_Center)
                                                                                         .Padding(2.0f, 0.0f, 0.0f, 0.0f)
                                                                                             [PropertyCustomizationHelpers::MakeBrowseButton(
                                                                                                  FSimpleDelegate::CreateSP(this, &SWetClothingPartEditorPanel::HandleBrowseWetnessProfileClicked, Item),
                                                                                                  LOCTEXT("BrowseWetnessProfileTooltip", "Find the assigned Wetness Profile in the Content Browser."),
                                                                                                  TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateSP(this, &SWetClothingPartEditorPanel::IsWetnessProfileBrowseEnabled, Item)))]]]]]

                                         + SHorizontalBox::Slot()
                                               .AutoWidth()
                                               .VAlign(VAlign_Center)
                                               .Padding(8.0f, 0.0f, 0.0f, 0.0f)
                                                   [SNew(SButton)
                                                        .ButtonStyle(FAppStyle::Get(), TEXT("NoBorder"))
                                                        .ContentPadding(FMargin(3.0f))
                                                        .ToolTipText(LOCTEXT("ResetWetPartTooltip", "Reset this Part's editable settings to their defaults."))
                                                        .IsEnabled_Lambda([Item]()
                                                                          { return Item.IsValid() && Item->WetPartID != 0; })
                                                        .OnClicked(this, &SWetClothingPartEditorPanel::HandleResetWetPartClicked, Item)
                                                            [SNew(SImage)
                                                                 .Image(FAppStyle::Get().GetBrush(TEXT("PropertyWindow.DiffersFromDefault")))]]];

    if (Item.IsValid() && InlineTextBlock.IsValid())
    {
        WetPartInlineRenameWidgets.Add(Item->WetPartID, InlineTextBlock);
    }

    return Row;
}

FReply SWetClothingPartEditorPanel::HandleResetWetPartClicked(FWetPartEntryPtr Item)
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    FWetClothingWetPartEntry* Entry = Item.IsValid() ? FindMutableWetPartEntry(Item->WetPartID) : nullptr;
    if (Asset == nullptr || Entry == nullptr)
    {
        return FReply::Handled();
    }

    const FScopedTransaction Transaction(LOCTEXT("ResetWetPartTransaction", "Reset Wet Part Settings"));
    Asset->Modify();
    Entry->DisplayName = GetDefaultWetPartName(Entry->WetPartID);
    Entry->Color = GetDefaultWetPartColor(Entry->WetPartID);
    Entry->bViewEnabled = true;
    Entry->ProfileIndex = 0;
    Entry->SurfaceWater = FWetPartSurfaceWaterSettings();
    Asset->MarkPackageDirty();

    if (Item.IsValid())
    {
        *Item = *Entry;
    }
    RefreshWetPartList(false);
    RefreshWetPartAssignmentViews();
    RefreshPreviewWetPartOverlay();
    return FReply::Handled();
}

void SWetClothingPartEditorPanel::HandleWetPartSelectionChanged(FWetPartEntryPtr Item, ESelectInfo::Type SelectInfo)
{
    SelectedWetPartID = Item.IsValid() ? Item->WetPartID : INDEX_NONE;

    if (WetPartListView.IsValid() && Item.IsValid() && !WetPartListView->IsItemSelected(Item))
    {
        WetPartListView->SetSelection(Item);
    }

    if (Item.IsValid())
    {
        const TSet<int32> IslandsForWetPart = GetUVIslandIDsForWetPart(Item->WetPartID);
        const int32       PrimaryUVIslandID = IslandsForWetPart.Num() > 0 ? *IslandsForWetPart.CreateConstIterator() : INDEX_NONE;
        SetSelectedUVIslandIDs(IslandsForWetPart, PrimaryUVIslandID);
        RefreshPreviewWetPartOverlay();
        return;
    }

    RefreshIslandSelectionViews();
    RefreshPreviewWetPartOverlay();
}

void SWetClothingPartEditorPanel::HandleWetPartItemDoubleClicked(FWetPartEntryPtr Item)
{
    if (!Item.IsValid() || Item->WetPartID == 0)
    {
        return;
    }

    HandleWetPartSelectionChanged(Item, ESelectInfo::Direct);

    if (const TWeakPtr<SInlineEditableTextBlock>* InlineWidget = WetPartInlineRenameWidgets.Find(Item->WetPartID))
    {
        if (InlineWidget->IsValid())
        {
            InlineWidget->Pin()->EnterEditingMode();
        }
    }
}

void SWetClothingPartEditorPanel::HandleWetPartNameCommitted(const FText& InText, ETextCommit::Type CommitType, FWetPartEntryPtr Item)
{
    if (!Item.IsValid() || Item->WetPartID == 0)
    {
        return;
    }

    UWetClothingAsset*             WetClothingAssetPtr = WetClothingAsset.Get();
    FWetClothingWetPartEntry* Entry = FindMutableWetPartEntry(Item->WetPartID);
    if (WetClothingAssetPtr == nullptr || Entry == nullptr)
    {
        return;
    }

    const FString TrimmedName = InText.ToString().TrimStartAndEnd();
    const FString NewDisplayName = TrimmedName.IsEmpty() ? GetDefaultWetPartName(Entry->WetPartID) : TrimmedName;
    if (Entry->DisplayName == NewDisplayName)
    {
        return;
    }

    WetClothingAssetPtr->Modify();
    Entry->DisplayName = NewDisplayName;
    WetClothingAssetPtr->MarkPackageDirty();
    MarkSelectedMaterialSlotWettable(false);

    RefreshWetPartList(false);
}

FReply SWetClothingPartEditorPanel::HandleWetPartColorClicked(FWetPartEntryPtr Item)
{
    if (!Item.IsValid() || Item->WetPartID == 0)
    {
        return FReply::Handled();
    }

    FColorPickerArgs PickerArgs;
    PickerArgs.InitialColor = Item->Color;
    PickerArgs.bUseAlpha = false;
    PickerArgs.bOnlyRefreshOnMouseUp = true;
    PickerArgs.ParentWidget = AsShared();
    PickerArgs.OnColorCommitted = FOnLinearColorValueChanged::CreateSP(this, &SWetClothingPartEditorPanel::HandleWetPartColorCommitted, Item);
    OpenColorPicker(PickerArgs);

    return FReply::Handled();
}

void SWetClothingPartEditorPanel::HandleWetPartColorCommitted(FLinearColor NewColor, FWetPartEntryPtr Item)
{
    if (!Item.IsValid() || Item->WetPartID == 0)
    {
        return;
    }

    UWetClothingAsset*             WetClothingAssetPtr = WetClothingAsset.Get();
    FWetClothingWetPartEntry* Entry = FindMutableWetPartEntry(Item->WetPartID);
    if (WetClothingAssetPtr == nullptr || Entry == nullptr)
    {
        return;
    }

    NewColor.A = 1.0f;
    if (Entry->Color.Equals(NewColor))
    {
        return;
    }

    WetClothingAssetPtr->Modify();
    Entry->Color = NewColor;
    Item->Color = NewColor;
    WetClothingAssetPtr->MarkPackageDirty();
    MarkSelectedMaterialSlotWettable(false);

    RefreshWetPartList(false);
    RefreshWetPartAssignmentViews();
}

FReply SWetClothingPartEditorPanel::HandleToggleWetPartViewClicked(FWetPartEntryPtr Item)
{
    if (!Item.IsValid() || Item->WetPartID == 0)
    {
        return FReply::Handled();
    }

    UWetClothingAsset*             WetClothingAssetPtr = WetClothingAsset.Get();
    FWetClothingWetPartEntry* Entry = FindMutableWetPartEntry(Item->WetPartID);
    if (WetClothingAssetPtr != nullptr && Entry != nullptr)
    {
        WetClothingAssetPtr->Modify();
        Entry->bViewEnabled = !Entry->bViewEnabled;
        WetClothingAssetPtr->MarkPackageDirty();
        MarkSelectedMaterialSlotWettable(false);
    }

    RefreshWetPartList(false);
    RefreshWetPartAssignmentViews();
    return FReply::Handled();
}

const FSlateBrush* SWetClothingPartEditorPanel::GetWetPartVisibilityBrush(FWetPartEntryPtr Item) const
{
    const bool bVisible = Item.IsValid() ? Item->bViewEnabled : false;
    return FAppStyle::Get().GetBrush(bVisible ? TEXT("Icons.Visible") : TEXT("Icons.Hidden"));
}

void SWetClothingPartEditorPanel::HandleWetnessProfilePicked(FWetPartEntryPtr Item, const FAssetData& ProfileAssetData)
{
    if (!Item.IsValid() || Item->WetPartID == 0)
    {
        return;
    }

    UWetClothingAsset*             WetClothingAssetPtr = WetClothingAsset.Get();
    FWetClothingWetPartEntry* Entry = FindMutableWetPartEntry(Item->WetPartID);
    if (WetClothingAssetPtr == nullptr || Entry == nullptr)
    {
        return;
    }

    WetClothingAssetPtr->Modify();

    FWetClothingEditableWetPartData& EditableData = WetClothingAssetPtr->Authored.PartData.EditableWetPartData;
    if (ProfileAssetData.IsValid())
    {
        if (const UWetnessProfile* SourceProfile = Cast<UWetnessProfile>(ProfileAssetData.GetAsset()))
        {
            Entry->ProfileIndex = EditableData.FindOrAddProfile(
                FSoftObjectPath(SourceProfile),
                SourceProfile->Parameters);
        }
    }
    else
    {
        EditableData.EnsureDefaultProfile();
        Entry->ProfileIndex = 0;
    }
    EditableData.CompactProfiles();

    WetClothingAssetPtr->MarkPackageDirty();
    MarkSelectedMaterialSlotWettable();

    RefreshWetPartList(false);
}

TSharedRef<SWidget> SWetClothingPartEditorPanel::GenerateAssignWetPartComboItem(FWetPartEntryPtr Item)
{
    const FLinearColor Color = Item.IsValid() ? (Item->WetPartID == 0 ? SWetClothingPartEditorPanelLocal::GetUnassignedPartColor() : Item->Color) : SWetClothingPartEditorPanelLocal::GetUnassignedPartColor();

    return SNew(SHorizontalBox) + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 6.0f, 0.0f)[SNew(SBox).WidthOverride(14.0f).HeightOverride(14.0f)[SNew(SBorder).BorderImage(FAppStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(Color)]]

           + SHorizontalBox::Slot()
                 .FillWidth(1.0f)
                 .VAlign(VAlign_Center)
                     [SNew(SVerticalBox)

                      + SVerticalBox::Slot()
                            .AutoHeight()
                                [SNew(STextBlock)
                                     .Text(Item.IsValid()
                                               ? FText::FromString(GetWetPartDisplayName(*Item))
                                               : LOCTEXT("AssignWetPartInvalid", "Invalid Part"))
                                     .ColorAndOpacity(Item.IsValid() && Item->WetPartID == 0
                                         ? FSlateColor::UseSubduedForeground()
                                         : FSlateColor::UseForeground())]

                      + SVerticalBox::Slot()
                            .AutoHeight()
                                [SNew(STextBlock)
                                     .Text(Item.IsValid() && Item->WetPartID != 0
                                               ? FText::Format(LOCTEXT("AssignWetPartOptionID", "ID {0}"), FText::AsNumber(Item->WetPartID))
                                               : FText::GetEmpty())
                                     .Font(FAppStyle::GetFontStyle(TEXT("SmallFont")))
                                     .ColorAndOpacity(FSlateColor::UseSubduedForeground())
                                     .Visibility(Item.IsValid() && Item->WetPartID != 0 ? EVisibility::Visible : EVisibility::Collapsed)]];
}

void SWetClothingPartEditorPanel::HandleAssignWetPartSelectionChanged(FWetPartEntryPtr Item, ESelectInfo::Type SelectInfo)
{
    SelectedAssignWetPartID = Item.IsValid() ? Item->WetPartID : INDEX_NONE;
}

FReply SWetClothingPartEditorPanel::HandleAddWetPartClicked()
{
    UWetClothingAsset* WetClothingAssetPtr = WetClothingAsset.Get();
    if (WetClothingAssetPtr == nullptr ||
        SelectedMaterialSlotIndex == INDEX_NONE ||
        !IsSelectedMaterialSlotPartEditingReady() ||
        !HasValidOriginalUVChannel())
    {
        return FReply::Handled();
    }

    const int32 NewWetPartID = FindNextWetPartForSelectedScope();
    WetClothingAssetPtr->Modify();

    FWetClothingEditableWetPartData& EditableData = WetClothingAssetPtr->Authored.PartData.EditableWetPartData;
    EditableData.EnsureDefaultProfile();
    FWetClothingAuthoredMaterialSlot& SlotData = EditableData.FindOrAddMaterialSlot(SelectedMaterialSlotIndex);
    FWetClothingWetPartEntry& NewEntry = SlotData.WetPartEntries.AddDefaulted_GetRef();
    NewEntry.WetPartID = NewWetPartID;
    NewEntry.DisplayName = GetDefaultWetPartName(NewWetPartID);
    NewEntry.Color = GetDefaultWetPartColor(NewWetPartID);
    NewEntry.bViewEnabled = true;
    NewEntry.ProfileIndex = 0;
    WetClothingAssetPtr->MarkPackageDirty();
    MarkSelectedMaterialSlotWettable();
    SelectedWetPartID = INDEX_NONE;
    SelectedAssignWetPartID = NewWetPartID;

    RefreshWetPartList(false);
    return FReply::Handled();
}

FReply SWetClothingPartEditorPanel::HandleRemoveWetPartClicked()
{
    UWetClothingAsset* WetClothingAssetPtr = WetClothingAsset.Get();
    if (WetClothingAssetPtr == nullptr || SelectedMaterialSlotIndex == INDEX_NONE || SelectedWetPartID == INDEX_NONE || SelectedWetPartID == 0)
    {
        return FReply::Handled();
    }

    const int32 RemovedWetPartID = SelectedWetPartID;
    WetClothingAssetPtr->Modify();
    FWetClothingEditableWetPartData& EditableData = WetClothingAssetPtr->Authored.PartData.EditableWetPartData;
    if (FWetClothingAuthoredMaterialSlot* SlotData = EditableData.FindMaterialSlot(SelectedMaterialSlotIndex))
    {
        SlotData->WetPartEntries.RemoveAll(
            [this](const FWetClothingWetPartEntry& Entry)
            {
                return Entry.WetPartID == SelectedWetPartID;
            });
    }
    EditableData.CompactProfiles();
    SelectedWetPartID = INDEX_NONE;
    if (SelectedAssignWetPartID == RemovedWetPartID)
    {
        SelectedAssignWetPartID = INDEX_NONE;
    }
    WetClothingAssetPtr->MarkPackageDirty();
    MarkSelectedMaterialSlotWettable();
    EnsureDefaultWetPartForSelectedScope();
    RefreshWetPartList(false);
    RefreshWetPartAssignmentViews();
    return FReply::Handled();
}

bool SWetClothingPartEditorPanel::IsWetPartRemoveEnabled() const
{
    return IsSelectedMaterialSlotPartEditingReady() && SelectedWetPartID != INDEX_NONE && SelectedWetPartID != 0;
}

bool SWetClothingPartEditorPanel::IsAutoPartitionEnabled() const
{
    return WetClothingAsset.IsValid() &&
           SelectedMaterialSlotIndex != INDEX_NONE &&
           IsSelectedMaterialSlotPartEditingReady() &&
           HasValidOriginalUVChannel() &&
           UVIslandItems.Num() > 0;
}

bool SWetClothingPartEditorPanel::HasAutoPartitionDataToReplace() const
{
    if (const UWetClothingAsset* WetClothingAssetPtr = WetClothingAsset.Get())
    {
        const FWetClothingAuthoredMaterialSlot* SlotData =
            WetClothingAssetPtr->Authored.PartData.EditableWetPartData.FindMaterialSlot(SelectedMaterialSlotIndex);
        return SlotData != nullptr && SlotData->WetPartEntries.ContainsByPredicate(
            [](const FWetClothingWetPartEntry& Entry)
            {
                return Entry.WetPartID != 0;
            });
    }

    return false;
}

FReply SWetClothingPartEditorPanel::HandleAutoPartitionClicked()
{
    UWetClothingAsset* WetClothingAssetPtr = WetClothingAsset.Get();
    if (WetClothingAssetPtr == nullptr || !IsAutoPartitionEnabled())
    {
        return FReply::Handled();
    }

    AutoPartitionTolerancePercent = SWetClothingPartEditorPanelLocal::AutoPartitionDefaultTolerancePercent;
    AutoPartitionColorMode = SWetClothingPartEditorPanelLocal::AutoPartitionDefaultColorMode;
    SelectedAutoPartitionColorModeItem.Reset();
    for (const FAutoPartitionColorModeItemPtr& ColorModeItem : AutoPartitionColorModeItems)
    {
        if (ColorModeItem.IsValid() && *ColorModeItem == AutoPartitionColorMode)
        {
            SelectedAutoPartitionColorModeItem = ColorModeItem;
            break;
        }
    }

    if (HasAutoPartitionDataToReplace())
    {
        FMessageDialog::Open(
            EAppMsgType::Ok,
            LOCTEXT("AutoPartitionExistingDataWarning", "This material slot already has authored part data. Auto Partition preview can be inspected safely, but applying it will ask before replacing the existing data."));
    }

    UTexture2D*                 PartitionTexture = ResolveAutoPartitionTexture();
    FWetClothingTextureReadback TextureData;
    FString                     TextureErrorMessage;
    if (!FWetClothingTextureReadbackUtils::TryReadTextureSourceData(PartitionTexture, TextureData, TextureErrorMessage))
    {
        FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TextureErrorMessage));
        return FReply::Handled();
    }

    TSharedRef<TArray<FWetPartAutoPartitionCluster>> PreviewClusters = MakeShared<TArray<FWetPartAutoPartitionCluster>>();
    FString                                          AutoPartitionErrorMessage;
    if (!FWetPartAutoPartitioner::BuildClusters(UVIslandItems, TextureData, AutoPartitionTolerancePercent, AutoPartitionColorMode, *PreviewClusters, &AutoPartitionErrorMessage))
    {
        FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(AutoPartitionErrorMessage));
        return FReply::Handled();
    }

    TSharedRef<FWetClothingTextureReadback> PreviewTextureData = MakeShared<FWetClothingTextureReadback>(TextureData);
    TSharedPtr<SWCAUVView>     BeforePreviewView;
    TSharedPtr<SWCAUVView>     AfterPreviewView;
    const FSlateFontInfo                    AutoPartitionDialogTextFont = FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 13);
    const FSlateFontInfo                    AutoPartitionDialogHeadingFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 14);

    const TSharedRef<SWindow> PreviewWindow = SNew(SWindow)
                                                  .Title(LOCTEXT("AutoPartitionPreviewTitle", "Auto Partition Preview"))
                                                  .ClientSize(FVector2D(980.0f, 680.0f))
                                                  .SupportsMinimize(false)
                                                  .SupportsMaximize(false);

    TWeakPtr<SWindow>                             WeakPreviewWindow = PreviewWindow.ToSharedPtr();
    TSharedRef<TWeakPtr<SWCAUVView>> WeakAfterPreviewView = MakeShared<TWeakPtr<SWCAUVView>>();
    const auto                                    RefreshAutoPartitionPreview = [this, PreviewTextureData, PreviewClusters, WeakAfterPreviewView]()
    {
        FString PreviewErrorMessage;
        if (FWetPartAutoPartitioner::BuildClusters(UVIslandItems, *PreviewTextureData, AutoPartitionTolerancePercent, AutoPartitionColorMode, *PreviewClusters, &PreviewErrorMessage))
        {
            if (TSharedPtr<SWCAUVView> PinnedAfterView = WeakAfterPreviewView->Pin())
            {
                PinnedAfterView->SetIslandColors(BuildAutoPartitionPreviewColorMap(*PreviewClusters));
            }
        }
    };

    PreviewWindow->SetContent(
        SNew(SBorder)
            .Padding(12.0f)
                [SNew(SVerticalBox)

                 + SVerticalBox::Slot()
                       .FillHeight(1.0f)
                           [SNew(SSplitter)

                            + SSplitter::Slot()
                                  .Value(0.5f)
                                      [SNew(SVerticalBox)

                                       + SVerticalBox::Slot()
                                             .AutoHeight()
                                             .Padding(0.0f, 0.0f, 8.0f, 6.0f)
                                                 [SNew(STextBlock)
                                                      .Text(LOCTEXT("AutoPartitionBeforeLabel", "Before"))
                                                      .Font(AutoPartitionDialogHeadingFont)]

                                       + SVerticalBox::Slot()
                                             .FillHeight(1.0f)
                                             .Padding(0.0f, 0.0f, 8.0f, 0.0f)
                                                 [SAssignNew(BeforePreviewView, SWCAUVView)]]

                            + SSplitter::Slot()
                                  .Value(0.5f)
                                      [SNew(SVerticalBox)

                                       + SVerticalBox::Slot()
                                             .AutoHeight()
                                             .Padding(8.0f, 0.0f, 0.0f, 6.0f)
                                                 [SNew(STextBlock)
                                                      .Text(LOCTEXT("AutoPartitionAfterLabel", "After"))
                                                      .Font(AutoPartitionDialogHeadingFont)]

                                       + SVerticalBox::Slot()
                                             .FillHeight(1.0f)
                                             .Padding(8.0f, 0.0f, 0.0f, 0.0f)
                                                 [SAssignNew(AfterPreviewView, SWCAUVView)]]]

                 + SVerticalBox::Slot()
                       .AutoHeight()
                       .HAlign(HAlign_Center)
                       .Padding(0.0f, 12.0f, 0.0f, 0.0f)
                           [SNew(SHorizontalBox)

                            + SHorizontalBox::Slot()
                                  .AutoWidth()
                                  .VAlign(VAlign_Center)
                                  .Padding(0.0f, 0.0f, 10.0f, 0.0f)
                                      [SNew(STextBlock)
                                           .Text(LOCTEXT("AutoPartitionPreviewColorModeLabel", "Representative Color"))
                                           .Font(AutoPartitionDialogTextFont)]

                            + SHorizontalBox::Slot()
                                  .AutoWidth()
                                  .VAlign(VAlign_Center)
                                  .Padding(0.0f, 0.0f, 24.0f, 0.0f)
                                      [SNew(SBox)
                                           .WidthOverride(150.0f)
                                               [SNew(SComboBox<FAutoPartitionColorModeItemPtr>)
                                                    .OptionsSource(&AutoPartitionColorModeItems)
                                                    .InitiallySelectedItem(SelectedAutoPartitionColorModeItem)
                                                    .OnGenerateWidget(this, &SWetClothingPartEditorPanel::GenerateAutoPartitionColorModeComboItem)
                                                    .OnSelectionChanged_Lambda(
                                                        [this, RefreshAutoPartitionPreview](FAutoPartitionColorModeItemPtr Item, ESelectInfo::Type SelectInfo)
                                                        {
                                                            HandleAutoPartitionColorModeSelectionChanged(Item, SelectInfo);
                                                            RefreshAutoPartitionPreview();
                                                        })
                                                        [SNew(STextBlock)
                                                             .Text(this, &SWetClothingPartEditorPanel::GetAutoPartitionColorModeText)
                                                             .Font(AutoPartitionDialogTextFont)]]]

                            + SHorizontalBox::Slot()
                                  .AutoWidth()
                                  .VAlign(VAlign_Center)
                                  .Padding(0.0f, 0.0f, 10.0f, 0.0f)
                                      [SNew(STextBlock)
                                           .Text(LOCTEXT("AutoPartitionPreviewToleranceLabel", "Color Tolerance"))
                                           .Font(AutoPartitionDialogTextFont)]

                            + SHorizontalBox::Slot()
                                  .AutoWidth()
                                  .VAlign(VAlign_Center)
                                      [SNew(SBox)
                                           .WidthOverride(160.0f)
                                               [SNew(SSpinBox<float>)
                                                    .MinValue(0.0f)
                                                    .MaxValue(SWetClothingPartEditorPanelLocal::AutoPartitionMaxTolerancePercent)
                                                    .MinSliderValue(0.0f)
                                                    .MaxSliderValue(SWetClothingPartEditorPanelLocal::AutoPartitionMaxTolerancePercent)
                                                    .Delta(0.1f)
                                                    .Value(this, &SWetClothingPartEditorPanel::GetAutoPartitionTolerance)
                                                    .OnValueChanged_Lambda(
                                                        [this, RefreshAutoPartitionPreview](float InValue)
                                                        {
                                                            HandleAutoPartitionToleranceChanged(InValue);
                                                            RefreshAutoPartitionPreview();
                                                        })]]]

                 + SVerticalBox::Slot()
                       .AutoHeight()
                       .HAlign(HAlign_Center)
                       .Padding(0.0f, 8.0f, 0.0f, 0.0f)
                           [SNew(STextBlock)
                                .Text_Lambda(
                                    [PreviewClusters]()
                                    {
                                        return FText::Format(
                                            LOCTEXT("AutoPartitionPreviewClusterCount", "{0} parts will be generated."),
                                            FText::AsNumber(PreviewClusters->Num()));
                                    })
                                .Font(AutoPartitionDialogTextFont)]

                 + SVerticalBox::Slot()
                       .AutoHeight()
                       .Padding(0.0f, 12.0f, 0.0f, 0.0f)
                           [SNew(SHorizontalBox)

                            + SHorizontalBox::Slot()
                                  .FillWidth(1.0f)
                                      [SNullWidget::NullWidget]

                            + SHorizontalBox::Slot()
                                  .AutoWidth()
                                  .Padding(0.0f, 0.0f, 8.0f, 0.0f)
                                      [SNew(SButton)
                                           .OnClicked_Lambda(
                                               [WeakPreviewWindow]()
                                               {
                                                   if (TSharedPtr<SWindow> PinnedWindow = WeakPreviewWindow.Pin())
                                                   {
                                                       PinnedWindow->RequestDestroyWindow();
                                                   }
                                                   return FReply::Handled();
                                               })
                                               [SNew(STextBlock)
                                                    .Text(LOCTEXT("AutoPartitionPreviewCancel", "Cancel"))
                                                    .Font(AutoPartitionDialogTextFont)]]

                            + SHorizontalBox::Slot()
                                  .AutoWidth()
                                      [SNew(SButton)
                                           .OnClicked_Lambda(
                                               [this, PreviewClusters, WeakPreviewWindow]()
                                               {
                                                   if (HasAutoPartitionDataToReplace())
                                                   {
                                                       const EAppReturnType::Type Response = FMessageDialog::Open(
                                                           EAppMsgType::YesNo,
                                                           LOCTEXT("ConfirmAutoPartitionReplace", "This material slot already has authored part data. Delete it all and regenerate parts automatically?"));

                                                       if (Response != EAppReturnType::Yes)
                                                       {
                                                           return FReply::Handled();
                                                       }
                                                   }

                                                   ApplyAutoPartitionClusters(*PreviewClusters);

                                                   if (TSharedPtr<SWindow> PinnedWindow = WeakPreviewWindow.Pin())
                                                   {
                                                       PinnedWindow->RequestDestroyWindow();
                                                   }
                                                   return FReply::Handled();
                                               })
                                               [SNew(STextBlock)
                                                    .Text(LOCTEXT("AutoPartitionPreviewApply", "Apply"))
                                                    .Font(AutoPartitionDialogTextFont)]]]]);

    *WeakAfterPreviewView = AfterPreviewView;

    if (BeforePreviewView.IsValid())
    {
        BeforePreviewView->SetBackgroundTexture(PartitionTexture);
        BeforePreviewView->SetDisplayMode(EWCAUVDisplayMode::Normal);
    }

    if (AfterPreviewView.IsValid())
    {
        AfterPreviewView->SetBackgroundTexture(PartitionTexture);
        AfterPreviewView->SetIslands(UVIslandItems);
        AfterPreviewView->SetIslandColors(BuildAutoPartitionPreviewColorMap(*PreviewClusters));
        AfterPreviewView->SetDisplayMode(EWCAUVDisplayMode::Normal);
    }

    FSlateApplication::Get().AddModalWindow(PreviewWindow, FSlateApplication::Get().GetActiveTopLevelWindow());
    return FReply::Handled();
}

UTexture2D* SWetClothingPartEditorPanel::ResolveAutoPartitionTexture() const
{
    if (UTexture2D* SelectedTexture = Cast<UTexture2D>(ResolveSelectedMaterialTexture()))
    {
        return SelectedTexture;
    }

    for (const FTextureItemPtr& TextureItem : TextureItems)
    {
        if (TextureItem.IsValid())
        {
            if (UTexture2D* Texture2D = Cast<UTexture2D>(TextureItem->Texture.Get()))
            {
                return Texture2D;
            }
        }
    }

    return nullptr;
}

TMap<int32, FLinearColor> SWetClothingPartEditorPanel::BuildAutoPartitionPreviewColorMap(const TArray<FWetPartAutoPartitionCluster>& Clusters) const
{
    TMap<int32, FLinearColor> Result;

    for (int32 ClusterIndex = 0; ClusterIndex < Clusters.Num(); ++ClusterIndex)
    {
        FLinearColor ClusterColor = GetDefaultWetPartColor(ClusterIndex + 1);
        ClusterColor.A = 1.0f;

        for (const int32 UVIslandID : Clusters[ClusterIndex].UVIslandIDs)
        {
            Result.Add(UVIslandID, ClusterColor);
        }
    }

    return Result;
}

void SWetClothingPartEditorPanel::ApplyAutoPartitionClusters(const TArray<FWetPartAutoPartitionCluster>& Clusters)
{
    UWetClothingAsset* WetClothingAssetPtr = WetClothingAsset.Get();
    if (WetClothingAssetPtr == nullptr || !IsAutoPartitionEnabled())
    {
        return;
    }

    WetClothingAssetPtr->Modify();
    MarkSelectedMaterialSlotWettable();

    FWetClothingEditableWetPartData& EditableData = WetClothingAssetPtr->Authored.PartData.EditableWetPartData;
    FWetClothingAuthoredMaterialSlot& SlotData = EditableData.FindOrAddMaterialSlot(SelectedMaterialSlotIndex);
    SlotData.WetPartEntries.RemoveAll(
        [](const FWetClothingWetPartEntry& Entry)
        {
            return Entry.WetPartID != 0;
        });
    EditableData.CompactProfiles();

    EnsureDefaultWetPartForSelectedScope();
    if (FWetClothingWetPartEntry* DefaultEntry = FindMutableWetPartEntry(0))
    {
        DefaultEntry->AssignedUVIslandIDs.Reset();
        DefaultEntry->DisplayName = GetDefaultWetPartName(0);
        DefaultEntry->Color = GetDefaultWetPartColor(0);
        DefaultEntry->bViewEnabled = true;
    }

    for (int32 ClusterIndex = 0; ClusterIndex < Clusters.Num(); ++ClusterIndex)
    {
        const int32 NewWetPartID = ClusterIndex + 1;

        FWetClothingWetPartEntry& NewEntry = SlotData.WetPartEntries.AddDefaulted_GetRef();
        NewEntry.WetPartID = NewWetPartID;
        NewEntry.DisplayName = GetDefaultWetPartName(NewWetPartID);
        NewEntry.Color = GetDefaultWetPartColor(NewWetPartID);
        NewEntry.bViewEnabled = true;
        NewEntry.AssignedUVIslandIDs = Clusters[ClusterIndex].UVIslandIDs;
        NewEntry.ProfileIndex = 0;
    }

    WetClothingAssetPtr->MarkPackageDirty();

    SelectedWetPartID = Clusters.Num() > 0 ? 1 : 0;
    SelectedAssignWetPartID = SelectedWetPartID;

    if (MaterialSlotListView.IsValid())
    {
        MaterialSlotListView->SetSelection(FindMaterialSlotItem(SelectedMaterialSlotIndex), ESelectInfo::Direct);
    }

    RefreshWetPartList(false);
    RefreshWetPartAssignmentViews();
}

FReply SWetClothingPartEditorPanel::HandleAssignSelectedUVIslandToWetPartClicked()
{
    UWetClothingAsset* WetClothingAssetPtr = WetClothingAsset.Get();
    if (WetClothingAssetPtr == nullptr ||
        SelectedMaterialSlotIndex == INDEX_NONE ||
        !IsSelectedMaterialSlotPartEditingReady() ||
        SelectedUVIslandIDs.Num() == 0 ||
        SelectedAssignWetPartID == INDEX_NONE)
    {
        return FReply::Handled();
    }

    bool bHasAssignmentChange = false;
    for (int32 UVIslandID : SelectedUVIslandIDs)
    {
        if (GetEffectiveWetPartForUVIsland(UVIslandID) != SelectedAssignWetPartID)
        {
            bHasAssignmentChange = true;
            break;
        }
    }

    if (!bHasAssignmentChange)
    {
        return FReply::Handled();
    }

    WetClothingAssetPtr->Modify();
    MarkSelectedMaterialSlotWettable();
    if (FWetClothingAuthoredMaterialSlot* SlotData =
            WetClothingAssetPtr->Authored.PartData.EditableWetPartData.FindMaterialSlot(SelectedMaterialSlotIndex))
    {
        for (FWetClothingWetPartEntry& Entry : SlotData->WetPartEntries)
        {
            for (int32 UVIslandID : SelectedUVIslandIDs)
            {
                Entry.AssignedUVIslandIDs.Remove(UVIslandID);
            }
        }
    }
    if (FWetClothingWetPartEntry* SelectedEntry = FindMutableWetPartEntry(SelectedAssignWetPartID))
    {
        for (int32 UVIslandID : SelectedUVIslandIDs)
        {
            if (SelectedAssignWetPartID != 0)
            {
                SelectedEntry->AssignedUVIslandIDs.AddUnique(UVIslandID);
            }
        }
    }
    WetClothingAssetPtr->MarkPackageDirty();
    RefreshWetPartAssignmentViews();
    return FReply::Handled();
}

FText SWetClothingPartEditorPanel::GetSelectedMaterialSlotText() const
{
    const FMaterialSlotItemPtr Item = FindMaterialSlotItem(SelectedMaterialSlotIndex);
    if (!Item.IsValid())
    {
        return LOCTEXT("NoMaterialSlotSelected", "Select a material slot to isolate its coverage on the preview mesh.");
    }

    if (Item->SlotIndex == INDEX_NONE)
    {
        return LOCTEXT("SelectedAllMaterialSlots", "Selected: All Slots (preview only)");
    }

    const FString MaterialName = Item->Material.IsValid() ? Item->Material->GetName() : TEXT("None");

    return FText::Format(
        LOCTEXT("SelectedMaterialSlot", "Selected: [{0}] {1} ({2})"),
        FText::AsNumber(Item->SlotIndex),
        FText::FromName(Item->SlotName),
        FText::FromString(MaterialName));
}

FText SWetClothingPartEditorPanel::GetOriginalUVChannelText() const
{
    if (!HasValidOriginalUVChannel())
    {
        return LOCTEXT("NoOriginalUVChannel", "Unavailable");
    }

    return FText::Format(
        LOCTEXT("SelectedOriginalUVChannel", "UV {0}"),
        FText::AsNumber(GetOriginalUVChannelIndex()));
}

float SWetClothingPartEditorPanel::GetUVViewBackgroundTextureOpacity() const
{
    return UVViewBackgroundTextureOpacity;
}

float SWetClothingPartEditorPanel::GetUVViewIslandLineOpacity() const
{
    return UVViewIslandLineOpacity;
}

float SWetClothingPartEditorPanel::GetUVViewIslandLineThicknessScale() const
{
    return UVViewIslandLineThicknessScale;
}

void SWetClothingPartEditorPanel::HandleUVViewBackgroundTextureOpacityChanged(float NewValue)
{
    UVViewBackgroundTextureOpacity = FMath::Clamp(NewValue, 0.0f, 1.0f);
    if (UVView.IsValid())
    {
        UVView->SetBackgroundTextureOpacity(UVViewBackgroundTextureOpacity);
    }
}

void SWetClothingPartEditorPanel::HandleUVViewIslandLineOpacityChanged(float NewValue)
{
    UVViewIslandLineOpacity = FMath::Clamp(NewValue, 0.0f, 1.0f);
    if (UVView.IsValid())
    {
        UVView->SetUVIslandLineOpacity(UVViewIslandLineOpacity);
    }
}

void SWetClothingPartEditorPanel::HandleUVViewIslandLineThicknessScaleChanged(float NewValue)
{
    UVViewIslandLineThicknessScale = FMath::Clamp(NewValue, 0.25f, 6.0f);
    if (UVView.IsValid())
    {
        UVView->SetUVIslandLineThicknessScale(UVViewIslandLineThicknessScale);
    }
}

FText SWetClothingPartEditorPanel::GetSelectedTextureText() const
{
    if (!SelectedTextureItem.IsValid())
    {
        return LOCTEXT("NoTextureSelected", "No Texture");
    }

    return FText::FromString(SelectedTextureItem->Label);
}

FText SWetClothingPartEditorPanel::GetRenderProfileBakeSourceText() const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    return Asset != nullptr
        ? FText::Format(
            LOCTEXT("WetPartDataBakeSource", "Output UV: DWC UV Channel {0}"),
            FText::AsNumber(Asset->GetDWCDataUVChannelIndex()))
        : LOCTEXT("WetPartDataBakeNoAsset", "Output UV: unavailable");
}

FText SWetClothingPartEditorPanel::GetRenderProfileBakeSlotsText() const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr)
    {
        return LOCTEXT("WetPartDataBakeNoSlots", "Material Slots: None");
    }

    TArray<int32> Slots;
    for (const FWetClothingAuthoredMaterialSlot& SlotData : Asset->Authored.PartData.EditableWetPartData.MaterialSlots)
    {
        const bool bHasRuntimePart = SlotData.WetPartEntries.ContainsByPredicate(
            [](const FWetClothingWetPartEntry& Entry)
            {
                return Entry.WetPartID != 0 && !Entry.AssignedUVIslandIDs.IsEmpty();
            });
        if (SlotData.MaterialSlotIndex != INDEX_NONE && SlotData.bIsWettableSlot && bHasRuntimePart)
        {
            Slots.AddUnique(SlotData.MaterialSlotIndex);
        }
    }
    Slots.Sort();
    TArray<FString> Labels;
    for (const int32 Slot : Slots)
    {
        Labels.Add(FString::FromInt(Slot));
    }
    return Labels.IsEmpty()
        ? LOCTEXT("WetPartDataBakeNoSlots2", "Material Slots: None")
        : FText::Format(
            LOCTEXT("WetPartDataBakeSlots", "Material Slots: {0}"),
            FText::FromString(FString::Join(Labels, TEXT(", "))));
}

FText SWetClothingPartEditorPanel::GetRenderProfileBakeStatusText() const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr)
    {
        return LOCTEXT("WetPartDataBakeNoAssetStatus", "Status: No WCA.");
    }

    const FWetClothingBakedWetPartData& Baked = Asset->Derived.Inline.BakedWetPartData;
    if (!Baked.IsValid())
    {
        return LOCTEXT("WetPartDataBakeRequired", "Status: Wet Part Data Texture bake required.");
    }
    return FText::Format(
        LOCTEXT("WetPartDataBakeReady", "Status: {0} slot textures / {1} local profiles"),
        FText::AsNumber(Baked.SlotTextures.Num()),
        FText::AsNumber(Baked.LocalProfiles.Num()));
}

FText SWetClothingPartEditorPanel::GetRenderProfileBakeSettingsText() const
{
    return FText::Format(
        LOCTEXT("WetPartDataBakeSettings", "Wet Part Data: {0}x{0} / Padding {1} px / Point Sample / DWC UV Channel  |  Surface Textures: {2}x{2}"),
        FText::AsNumber(DWCWetPartDataTextureBake::Resolution),
        FText::AsNumber(DWCWetPartDataTextureBake::PaddingPixels),
        FText::AsNumber(DWCSurfaceTextureNormalization::Resolution));
}

FText SWetClothingPartEditorPanel::GetUVIslandCountText() const
{
    return FText::Format(
        LOCTEXT("UVIslandCount", "{0} islands"),
        FText::AsNumber(UVIslandItems.Num()));
}

FText SWetClothingPartEditorPanel::GetSelectedUVIslandText() const
{
    const FText EmptyState = GetCommonPartEditingEmptyStateText();
    if (!EmptyState.IsEmpty())
    {
        return EmptyState;
    }
    if (UVIslandItems.IsEmpty())
    {
        return GetUVStatusText();
    }
    if (SelectedUVIslandIDs.Num() == 0)
    {
        return LOCTEXT("NoUVIslandSelected", "Select UV islands from the list, UV view, or 3D preview.");
    }
    if (SelectedUVIslandIDs.Num() > 1)
    {
        return FText::Format(LOCTEXT("SelectedUVIslandMulti", "Selected Islands: {0}  |  Primary: {1}"), FText::AsNumber(SelectedUVIslandIDs.Num()), FText::AsNumber(SelectedUVIslandID));
    }
    for (const FUVIslandItemPtr& IslandItem : UVIslandItems)
    {
        if (IslandItem.IsValid() && IslandItem->UVIslandID == SelectedUVIslandID)
        {
            return FText::Format(LOCTEXT("SelectedUVIsland", "Selected Island: {0}  |  Bounds Min({1}, {2}) Max({3}, {4})"), FText::AsNumber(IslandItem->UVIslandID), FText::AsNumber(IslandItem->UVBounds.Min.X), FText::AsNumber(IslandItem->UVBounds.Min.Y), FText::AsNumber(IslandItem->UVBounds.Max.X), FText::AsNumber(IslandItem->UVBounds.Max.Y));
        }
    }
    return LOCTEXT("NoUVIslandSelectedFallback", "Select UV islands from the list, UV view, or 3D preview.");
}

EVisibility SWetClothingPartEditorPanel::GetSelectedUVIslandTextVisibility() const
{
    return SelectedUVIslandIDs.Num() > 0
               ? EVisibility::Visible
               : EVisibility::Collapsed;
}

FText SWetClothingPartEditorPanel::GetUVIslandAssignmentSummaryText() const
{
    const int32 SelectedIslandCount = SelectedUVIslandIDs.Num();
    if (SelectedIslandCount == 0)
    {
        return FText::GetEmpty();
    }

    return SelectedIslandCount == 1
               ? FText::Format(
                     LOCTEXT("SelectedUVIslandAssignmentSingle", "{0} UV island selected"),
                     FText::AsNumber(SelectedIslandCount))
               : FText::Format(
                     LOCTEXT("SelectedUVIslandAssignmentMulti", "{0} UV islands selected"),
                     FText::AsNumber(SelectedIslandCount));
}

FText SWetClothingPartEditorPanel::GetUVIslandAssignmentButtonText() const
{
    return FText::Format(
        LOCTEXT("AssignSelectedIslandsWetPart", "Assign Part ({0})"),
        FText::AsNumber(SelectedUVIslandIDs.Num()));
}

FText SWetClothingPartEditorPanel::GetUVIslandAssignmentButtonTooltip() const
{
    if (!IsSelectedMaterialSlotPartEditingReady())
    {
        return LOCTEXT("ApplySelectedIslandsNotReadyTooltip", "The selected material slot must be Wettable and have a ready DWC UV Channel.");
    }
    if (SelectedUVIslandIDs.IsEmpty())
    {
        return LOCTEXT("ApplySelectedIslandsNoSelectionTooltip", "Select at least one UV island.");
    }
    if (SelectedAssignWetPartID == INDEX_NONE)
    {
        return LOCTEXT("ApplySelectedIslandsNoPartTooltip", "Choose a wet part target.");
    }

    return LOCTEXT("AssignSelectedIslandsTooltip", "Assign the selected wet part to the selected UV islands.");
}

FText SWetClothingPartEditorPanel::GetUVStatusText() const
{
    const FText EmptyState = GetCommonPartEditingEmptyStateText();
    return EmptyState.IsEmpty()
        ? FText::FromString(UVStatusMessage)
        : EmptyState;
}

EVisibility SWetClothingPartEditorPanel::GetUVStatusOverlayVisibility() const
{
    return UVIslandItems.IsEmpty()
               ? EVisibility::HitTestInvisible
               : EVisibility::Collapsed;
}

EVisibility SWetClothingPartEditorPanel::GetUVEditorContentVisibility() const
{
    return UVIslandItems.IsEmpty()
        ? EVisibility::Collapsed
        : EVisibility::Visible;
}

FText SWetClothingPartEditorPanel::GetCommonPartEditingEmptyStateText() const
{
    if (SelectedMaterialSlotIndex == INDEX_NONE)
    {
        return LOCTEXT("CommonEmptyStateSelectSlot", "Select a material slot.");
    }
    if (!IsSelectedMaterialSlotWettable())
    {
        return LOCTEXT("CommonEmptyStateEnableWettable", "Enable Wettable for this slot.");
    }
    if (!IsMaterialSlotDataUVReadyForEditing(SelectedMaterialSlotIndex))
    {
        return LOCTEXT("CommonEmptyStateUVUnavailable", "UV editing is unavailable for this slot.");
    }
    return FText::GetEmpty();
}

EVisibility SWetClothingPartEditorPanel::GetUVIslandStatusOverlayVisibility() const
{
    return UVIslandItems.IsEmpty()
               ? EVisibility::HitTestInvisible
               : EVisibility::Collapsed;
}

FText SWetClothingPartEditorPanel::GetWetPartSectionText() const
{
    if (SelectedMaterialSlotIndex == INDEX_NONE)
    {
        return LOCTEXT("WetPartSectionNoSlot", "Part Map");
    }

    return FText::Format(
        LOCTEXT("WetPartSection", "Part Map / Slot {0}"),
        FText::AsNumber(SelectedMaterialSlotIndex));
}

FText SWetClothingPartEditorPanel::GetSelectedAssignWetPartText() const
{
    if (const FWetPartEntryPtr Item = FindWetPartItemByID(SelectedAssignWetPartID))
    {
        return FText::FromString(GetWetPartDisplayName(*Item));
    }

    return LOCTEXT("SelectedAssignWetPartNone", "Select Part");
}

FSlateColor SWetClothingPartEditorPanel::GetSelectedAssignWetPartColor() const
{
    if (const FWetPartEntryPtr Item = FindWetPartItemByID(SelectedAssignWetPartID))
    {
        return FSlateColor(Item->WetPartID == 0 ? SWetClothingPartEditorPanelLocal::GetUnassignedPartColor() : Item->Color);
    }

    return FSlateColor(FLinearColor(0.08f, 0.08f, 0.08f, 1.0f));
}

FText SWetClothingPartEditorPanel::GetSelectedWetPartText() const
{
    if (SelectedWetPartID == INDEX_NONE)
    {
        return FText::GetEmpty();
    }

    if (const FWetClothingWetPartEntry* Entry = FindWetPartEntry(SelectedWetPartID))
    {
        return FText::Format(
            LOCTEXT("SelectedWetPart", "Selected Part: {0}  |  Double-click name to rename"),
            FText::FromString(GetWetPartDisplayName(*Entry)));
    }

    return LOCTEXT("SelectedWetPartInvalid", "Selected Part: Invalid");
}

EVisibility SWetClothingPartEditorPanel::GetSelectedWetPartTextVisibility() const
{
    if (!GetCommonPartEditingEmptyStateText().IsEmpty() || CurrentWetPartItems.IsEmpty())
    {
        return EVisibility::Collapsed;
    }
    return GetSelectedWetPartText().IsEmpty()
        ? EVisibility::Collapsed
        : EVisibility::Visible;
}

FText SWetClothingPartEditorPanel::GetWetnessProfileLibraryStatusText() const
{
    const FText EmptyState = GetCommonPartEditingEmptyStateText();
    if (!EmptyState.IsEmpty())
    {
        return EmptyState;
    }
    if (CurrentWetPartItems.IsEmpty())
    {
        return LOCTEXT("WetnessProfileNoParts", "No parts found.");
    }
    if (SelectedWetPartID == 0)
    {
        return LOCTEXT("DefaultPartProfileDisabled", "Unassigned uses no Wetness Profile.");
    }

    return LOCTEXT("WetnessProfileLibraryStatus", "Choose a Wetness Profile from project or plugin content.");
}

EVisibility SWetClothingPartEditorPanel::GetWetnessProfileLibraryStatusVisibility() const
{
    return CurrentWetPartItems.IsEmpty()
               ? EVisibility::HitTestInvisible
               : EVisibility::Collapsed;
}

bool SWetClothingPartEditorPanel::IsSelectedMaterialSlotWettable() const
{
    const UWetClothingAsset* WetClothingAssetPtr = WetClothingAsset.Get();
    return WetClothingAssetPtr != nullptr &&
           SelectedMaterialSlotIndex != INDEX_NONE &&
           FWCAEditorWidgets::IsMaterialSlotWettable(WetClothingAssetPtr, SelectedMaterialSlotIndex);
}

bool SWetClothingPartEditorPanel::IsMaterialSlotDataUVReadyForEditing(const int32 MaterialSlotIndex) const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr ||
        MaterialSlotIndex == INDEX_NONE ||
        Asset->GetRuntimeSkeletalMesh() == nullptr ||
        !IsMaterialSlotIncludedInDataUVLayout(MaterialSlotIndex))
    {
        return false;
    }

    const FDWCDataUVLODMetadata* Metadata = Asset->FindDataUVMetadataForLOD(0);
    return Metadata != nullptr &&
           Metadata->bIsValid &&
           Metadata->UVChannelIndex == Asset->GetDWCDataUVChannelIndex() &&
           Metadata->GeneratorVersion == DWCGeneratedDataVersion::DataUV;
}

bool SWetClothingPartEditorPanel::IsSelectedMaterialSlotPartEditingReady() const
{
    return IsSelectedMaterialSlotWettable() &&
           IsMaterialSlotDataUVReadyForEditing(SelectedMaterialSlotIndex);
}

bool SWetClothingPartEditorPanel::IsAssignUVIslandToWetPartEnabled() const
{
    return IsSelectedMaterialSlotPartEditingReady() &&
           SelectedUVIslandIDs.Num() > 0 &&
           SelectedAssignWetPartID != INDEX_NONE;
}

FText SWetClothingPartEditorPanel::GetBlendModeText(FWetPartEntryPtr Item) const
{
    if (!Item.IsValid())
    {
        return LOCTEXT("InvalidBlendMode", "Standard");
    }

    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const FWetPartProfileAssignment* Profile = Asset != nullptr
        ? Asset->Authored.PartData.EditableWetPartData.FindProfile(*Item)
        : nullptr;
    const UEnum* BlendModeEnum = StaticEnum<EWetPartProfileBlendMode>();
    return BlendModeEnum != nullptr && Profile != nullptr
               ? BlendModeEnum->GetDisplayNameTextByValue(static_cast<int64>(Profile->BlendMode))
               : LOCTEXT("BlendModeFallback", "Standard");
}

FText SWetClothingPartEditorPanel::GetWetnessProfileButtonText(FWetPartEntryPtr Item) const
{
    return Item.IsValid()
               ? FText::FromString(GetAssignedProfileLabel(*Item))
               : LOCTEXT("NoProfileSelected", "Select Profile");
}

FString SWetClothingPartEditorPanel::GetWetnessProfileObjectPath(FWetPartEntryPtr Item) const
{
    if (!Item.IsValid() || Item->WetPartID == 0)
    {
        return FString();
    }

    const FWetClothingWetPartEntry* Entry = FindWetPartEntry(Item->WetPartID);
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const FWetPartProfileAssignment* Profile = Asset != nullptr && Entry != nullptr
        ? Asset->Authored.PartData.EditableWetPartData.FindProfile(*Entry)
        : nullptr;
    return Profile != nullptr ? Profile->SourceProfile.ToString() : FString();
}

bool SWetClothingPartEditorPanel::IsWetnessProfileControlEnabled(FWetPartEntryPtr Item) const
{
    return Item.IsValid() && Item->WetPartID != 0;
}

bool SWetClothingPartEditorPanel::IsWetnessProfileBrowseEnabled(FWetPartEntryPtr Item) const
{
    return IsWetnessProfileControlEnabled(Item) && !GetWetnessProfileObjectPath(Item).IsEmpty();
}

void SWetClothingPartEditorPanel::HandleUseSelectedWetnessProfileClicked(FWetPartEntryPtr Item)
{
    if (!IsWetnessProfileControlEnabled(Item))
    {
        return;
    }

    FContentBrowserModule& ContentBrowserModule =
        FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

    TArray<FAssetData> SelectedAssets;
    ContentBrowserModule.Get().GetSelectedAssets(SelectedAssets);
    for (const FAssetData& SelectedAsset : SelectedAssets)
    {
        if (SelectedAsset.IsValid() && Cast<UWetnessProfile>(SelectedAsset.GetAsset()) != nullptr)
        {
            HandleWetnessProfilePicked(Item, SelectedAsset);
            return;
        }
    }
}

void SWetClothingPartEditorPanel::HandleBrowseWetnessProfileClicked(FWetPartEntryPtr Item)
{
    if (GEditor == nullptr || !IsWetnessProfileBrowseEnabled(Item))
    {
        return;
    }

    UObject* ProfileObject = FSoftObjectPath(GetWetnessProfileObjectPath(Item)).TryLoad();
    if (ProfileObject != nullptr)
    {
        TArray<UObject*> ObjectsToSync{ProfileObject};
        GEditor->SyncBrowserToObjects(ObjectsToSync);
    }
}

void SWetClothingPartEditorPanel::SetCurrentUVSelectionTool(EWCAUVSelectionTool InTool)
{
    CurrentUVSelectionTool = InTool;
    SelectedUVSelectionToolItem.Reset();

    for (const FUVSelectionToolItemPtr& ToolItem : UVSelectionToolItems)
    {
        if (ToolItem.IsValid() && ToolItem->Tool == InTool)
        {
            SelectedUVSelectionToolItem = ToolItem;
            break;
        }
    }

    if (UVView.IsValid())
    {
        UVView->SetSelectionTool(CurrentUVSelectionTool);
    }
}

const FSlateBrush* SWetClothingPartEditorPanel::GetUVSelectionToolBrush(FUVSelectionToolItemPtr Item) const
{
    if (Item.IsValid() && Item->IconBrushDisplayName != NAME_None)
    {
        return FDWCEditorStyle::GetBrush(Item->IconBrushDisplayName);
    }

    return FAppStyle::GetBrush(TEXT("ClassIcon.Default"));
}

FSlateColor SWetClothingPartEditorPanel::GetUVSelectionToolIconColor(FUVSelectionToolItemPtr Item) const
{
    const bool bIsSelected = Item.IsValid() && Item->Tool == CurrentUVSelectionTool;
    return FSlateColor(bIsSelected
                           ? FLinearColor::White
                           : FStyleColors::Foreground);
}

TSharedRef<SWidget> SWetClothingPartEditorPanel::GenerateAutoPartitionColorModeComboItem(FAutoPartitionColorModeItemPtr Item) const
{
    return SNew(STextBlock)
        .Text(Item.IsValid()
                  ? SWetClothingPartEditorPanelLocal::GetAutoPartitionColorModeLabel(*Item)
                  : LOCTEXT("AutoPartitionColorModeInvalid", "Unknown"))
        .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 12));
}

void SWetClothingPartEditorPanel::HandleAutoPartitionColorModeSelectionChanged(FAutoPartitionColorModeItemPtr Item, ESelectInfo::Type SelectInfo)
{
    if (Item.IsValid())
    {
        SelectedAutoPartitionColorModeItem = Item;
        AutoPartitionColorMode = *Item;
    }
}

FText SWetClothingPartEditorPanel::GetAutoPartitionColorModeText() const
{
    return SWetClothingPartEditorPanelLocal::GetAutoPartitionColorModeLabel(AutoPartitionColorMode);
}

float SWetClothingPartEditorPanel::GetAutoPartitionTolerance() const
{
    return AutoPartitionTolerancePercent;
}

void SWetClothingPartEditorPanel::HandleAutoPartitionToleranceChanged(float InValue)
{
    AutoPartitionTolerancePercent = FMath::Clamp(InValue, 0.0f, SWetClothingPartEditorPanelLocal::AutoPartitionMaxTolerancePercent);
}

bool SWetClothingPartEditorPanel::IsSurfaceWaterTilingEnabled(const FWetPartEntryPtr Item) const
{
    if (!Item.IsValid() || Item->WetPartID <= 0)
    {
        return false;
    }

    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const FWetClothingWetPartEntry* Entry = FindWetPartEntry(Item->WetPartID);
    const FWetPartProfileAssignment* Assignment = Asset != nullptr && Entry != nullptr
        ? Asset->Authored.PartData.EditableWetPartData.FindProfile(*Entry)
        : nullptr;
    if (Assignment == nullptr)
    {
        return false;
    }

    const FWetnessProfileParameters* Parameters = &Assignment->Parameters;
    if (const UWetnessProfile* SourceProfile = Cast<UWetnessProfile>(Assignment->SourceProfile.ResolveObject()))
    {
        Parameters = &SourceProfile->GetParameters();
    }
    return Parameters->SurfaceWater.bEnabled;
}

bool SWetClothingPartEditorPanel::IsSelectedWetPartSurfaceSettingsEnabled() const
{
    return SelectedMaterialSlotIndex != INDEX_NONE &&
           IsSurfaceWaterTilingEnabled(FindWetPartItemByID(SelectedWetPartID));
}

FReply SWetClothingPartEditorPanel::HandleOpenSurfaceWaterTilingClicked(FWetPartEntryPtr Item)
{
    if (!Item.IsValid() || !IsSurfaceWaterTilingEnabled(Item))
    {
        return FReply::Handled();
    }

    if (GetUVIslandIDsForWetPart(Item->WetPartID).IsEmpty())
    {
        FMessageDialog::Open(
            EAppMsgType::Ok,
            LOCTEXT(
                "SurfaceWaterTilingNoPartTriangles",
                "The selected Wet Part does not contain any UV-island triangles."));
        return FReply::Handled();
    }

    HandleWetPartSelectionChanged(Item, ESelectInfo::Direct);
    ResetSurfaceWaterTilingPreviewState();
    if (const TSharedPtr<SWindow> ExistingWindow = SurfaceWaterTilingWindow.Pin())
    {
        RefreshSurfaceWaterTilingPreview();
        ExistingWindow->BringToFront(true);
        return FReply::Handled();
    }

    const TSharedRef<SWindow> Window = SNew(SWindow)
        .Title(LOCTEXT("SurfaceWaterTilingWindowTitle", "Surface Water Tiling"))
        .ClientSize(FVector2D(1120.0f, 760.0f))
        .SupportsMinimize(false)
        .SupportsMaximize(true)
        .SizingRule(ESizingRule::UserSized);
    Window->SetContent(BuildSurfaceWaterTilingWindowContent());
    Window->SetOnWindowClosed(FOnWindowClosed::CreateSP(
        this, &SWetClothingPartEditorPanel::HandleSurfaceWaterTilingWindowClosed));
    SurfaceWaterTilingWindow = Window;

    if (const TSharedPtr<SWindow> ParentWindow = FSlateApplication::Get().FindWidgetWindow(AsShared()))
    {
        FSlateApplication::Get().AddWindowAsNativeChild(Window, ParentWindow.ToSharedRef());
    }
    else
    {
        FSlateApplication::Get().AddWindow(Window);
    }

    RefreshSurfaceWaterTilingPreview();
    if (SurfaceWaterTilingPreviewViewport.IsValid())
    {
        SurfaceWaterTilingPreviewViewport->FocusOnPreviewMesh(true);
    }
    return FReply::Handled();
}

void SWetClothingPartEditorPanel::ResetSurfaceWaterTilingPreviewState()
{
}

TSharedRef<SWidget> SWetClothingPartEditorPanel::BuildSurfaceWaterTilingWindowContent()
{
    const auto BuildScaleRow = [this](
        const FText& Label,
        const FText& ToolTip,
        const TAttribute<bool>& IsEnabled,
        const TAttribute<float>& Value,
        TFunction<void(float)> OnValueChanged) -> TSharedRef<SWidget>
    {
        return SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
                  .FillWidth(1.0f)
                  .VAlign(VAlign_Center)
                      [SNew(STextBlock).Text(Label).ToolTipText(ToolTip)]
            + SHorizontalBox::Slot()
                  .AutoWidth()
                  .Padding(12.0f, 0.0f, 0.0f, 0.0f)
                      [SNew(SBox)
                           .WidthOverride(104.0f)
                               [SNew(SSpinBox<float>)
                                    .MinValue(0.25f).MaxValue(4.0f)
                                    .MinSliderValue(0.25f).MaxSliderValue(4.0f)
                                    .Delta(0.05f)
                                    .Value(Value)
                                    .IsEnabled(IsEnabled)
                                    .OnValueChanged_Lambda([OnValueChanged = MoveTemp(OnValueChanged)](const float NewValue)
                                    {
                                        OnValueChanged(NewValue);
                                    })]];
    };

    const auto BuildPreviewToggle = [](
        const FText& Label,
        const FText& ToolTip,
        TAttribute<ECheckBoxState> CheckState,
        FOnCheckStateChanged OnChanged) -> TSharedRef<SWidget>
    {
        return SNew(SCheckBox)
            .ToolTipText(ToolTip)
            .IsChecked(CheckState)
            .OnCheckStateChanged(OnChanged)
            [SNew(STextBlock).Text(Label)];
    };

    const auto BuildPreviewSlider = [this](
        const FText& Label,
        const TAttribute<float>& Value,
        TFunction<void(float)> OnValueChanged,
        const TAttribute<FText>& ValueText) -> TSharedRef<SWidget>
    {
        return SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
                  .AutoWidth()
                  .VAlign(VAlign_Center)
                      [SNew(SBox).WidthOverride(126.0f)[SNew(STextBlock).Text(Label)]]
            + SHorizontalBox::Slot()
                  .FillWidth(1.0f)
                  .VAlign(VAlign_Center)
                      [SNew(SSlider)
                           .Value(Value)
                           .OnValueChanged_Lambda([OnValueChanged = MoveTemp(OnValueChanged)](const float NewValue)
                           {
                               OnValueChanged(NewValue);
                           })]
            + SHorizontalBox::Slot()
                  .AutoWidth()
                  .VAlign(VAlign_Center)
                  .Padding(10.0f, 0.0f, 0.0f, 0.0f)
                      [SNew(SBox)
                           .WidthOverride(48.0f)
                               [SNew(STextBlock).Justification(ETextJustify::Right).Text(ValueText)]];
    };

    const auto BuildDetailSizeSlider = [this, &BuildPreviewSlider](
        const FText& Label,
        const TAttribute<float>& Value,
        TFunction<void(float)> OnValueChanged,
        const TAttribute<FText>& ValueText) -> TSharedRef<SWidget>
    {
        return BuildPreviewSlider(
            Label,
            Value,
            [OnValueChanged = MoveTemp(OnValueChanged)](const float NewSliderValue)
            {
                OnValueChanged(FMath::Clamp(NewSliderValue, 0.0f, 1.0f) * 4.0f);
            },
            ValueText);
    };

    const auto BuildCoverageModeRadio = [this](
        const FText& Label,
        const FText& ToolTip,
        const EDWCSurfaceWaterTilingPreviewCoverageMode Mode) -> TSharedRef<SWidget>
    {
        return SNew(SCheckBox)
            .Style(FAppStyle::Get(), TEXT("RadioButton"))
            .ToolTipText(ToolTip)
            .IsChecked(this, &SWetClothingPartEditorPanel::GetSurfaceWaterPreviewCoverageModeState, Mode)
            .OnCheckStateChanged(this, &SWetClothingPartEditorPanel::HandleSurfaceWaterPreviewCoverageModeChanged, Mode)
            [SNew(STextBlock).Text(Label)];
    };

    const auto BuildDisplayModeRadio = [this](
        const FText& Label,
        const FText& ToolTip,
        const EDWCSurfaceWaterTilingPreviewDisplayMode Mode) -> TSharedRef<SWidget>
    {
        return SNew(SCheckBox)
            .Style(FAppStyle::Get(), TEXT("RadioButton"))
            .ToolTipText(ToolTip)
            .IsChecked(this, &SWetClothingPartEditorPanel::GetSurfaceWaterPreviewDisplayModeState, Mode)
            .OnCheckStateChanged(this, &SWetClothingPartEditorPanel::HandleSurfaceWaterPreviewDisplayModeChanged, Mode)
            [SNew(STextBlock).Text(Label)];
    };

    return SNew(SBorder)
        .Padding(10.0f)
        .BorderImage(FAppStyle::Get().GetBrush(TEXT("ToolPanel.GroupBorder")))
            [SNew(SVerticalBox)

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(4.0f, 2.0f, 4.0f, 8.0f)
                       [SNew(SHorizontalBox)
                        + SHorizontalBox::Slot()
                              .FillWidth(1.0f)
                              .VAlign(VAlign_Center)
                                  [SNew(SVerticalBox)
                                   + SVerticalBox::Slot().AutoHeight()
                                         [SNew(STextBlock)
                                              .Text_Lambda([this]()
                                              {
                                                  if (const FWetClothingWetPartEntry* Entry = FindWetPartEntry(SelectedWetPartID))
                                                  {
                                                      return FText::Format(
                                                          LOCTEXT("SurfaceWaterTilingSelectedPart", "Selected Part: {0}"),
                                                          FText::FromString(GetWetPartDisplayName(*Entry)));
                                                  }
                                                  return LOCTEXT("SurfaceWaterTilingNoPart", "No Wet Part selected.");
                                              })
                                              .Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.BoldFont")))]
                                   + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f, 0.0f, 0.0f)
                                         [SNew(STextBlock)
                                              .Text(LOCTEXT(
                                                  "SurfaceWaterTilingGPUOnly",
                                                  "GPU Simulation Only · Uses the selected slot's generated GPU material on the original mesh."))
                                              .ColorAndOpacity(FSlateColor(FStyleColors::ForegroundHover))]]
                        + SHorizontalBox::Slot()
                              .AutoWidth()
                              .VAlign(VAlign_Center)
                              .Padding(8.0f, 0.0f, 0.0f, 0.0f)
                                  [SNew(SButton)
                                       .Text(LOCTEXT("SurfaceWaterTilingFocusMesh", "Focus Mesh"))
                                       .OnClicked_Lambda([this]()
                                           {
                                               if (SurfaceWaterTilingPreviewViewport.IsValid())
                                               {
                                                   SurfaceWaterTilingPreviewViewport->FocusOnPreviewMesh();
                                               }
                                               return FReply::Handled();
                                       })]]

             + SVerticalBox::Slot()
                   .FillHeight(1.0f)
                       [SNew(SSplitter)

                        + SSplitter::Slot()
                              .Value(0.72f)
                                  [SNew(SBorder)
                                       .Padding(0.0f)
                                       .BorderImage(FAppStyle::Get().GetBrush(TEXT("ToolPanel.DarkGroupBorder")))
                                           [SAssignNew(SurfaceWaterTilingPreviewViewport, SDWCPartViewport)
                                                .WetClothingAsset(WetClothingAsset.Get())
                                                .SurfaceWaterTilingPreview(true)]]

                        + SSplitter::Slot()
                              .Value(0.28f)
                                  [SNew(SBorder)
                                       .Padding(12.0f)
                                       .BorderImage(FAppStyle::Get().GetBrush(TEXT("ToolPanel.GroupBorder")))
                                           [SNew(SScrollBox)
                                            + SScrollBox::Slot()
                                                  [SNew(SVerticalBox)
                                                   + SVerticalBox::Slot().AutoHeight()
                                                         [SNew(STextBlock)
                                                              .Text(LOCTEXT("SurfaceWaterTilingSettingsHeader", "Surface Water Droplet Sizes"))
                                                              .Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.BoldFont")))]
                                                   + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 8.0f)
                                                         [SNew(SSeparator)]
                                                   + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f)
                                                         [SNew(STextBlock)
                                                              .Text(LOCTEXT("SurfaceWaterCoverageMode", "Coverage Mode"))
                                                              .Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))]
                                                   + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 3.0f)
                                                         [BuildCoverageModeRadio(
                                                             LOCTEXT("SurfaceWaterCoverageModeFullPart", "Full Part"),
                                                             LOCTEXT("SurfaceWaterCoverageModeFullPartTooltip", "Fill the selected Wet Part with Surface Water to judge droplet detail normal tiling."),
                                                             EDWCSurfaceWaterTilingPreviewCoverageMode::FullPart)]
                                                   + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f)
                                                         [BuildCoverageModeRadio(
                                                             LOCTEXT("SurfaceWaterCoverageModeSingleCircle", "Single Circle"),
                                                             LOCTEXT("SurfaceWaterCoverageModeSingleCircleTooltip", "Render one contact-sized Surface Water circle to judge Droplet Stamp Size."),
                                                             EDWCSurfaceWaterTilingPreviewCoverageMode::SingleCircle)]
                                                   + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 8.0f)
                                                         [SNew(SSeparator)]
                                                   + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f)
                                                         [SNew(STextBlock)
                                                              .Text(LOCTEXT("SurfaceWaterDisplayMode", "Display Mode"))
                                                              .Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))]
                                                   + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 3.0f)
                                                         [BuildDisplayModeRadio(
                                                             LOCTEXT("SurfaceWaterDisplayModeLit", "Lit"),
                                                             LOCTEXT("SurfaceWaterDisplayModeLitTooltip", "Render the generated material normally."),
                                                             EDWCSurfaceWaterTilingPreviewDisplayMode::Lit)]
                                                   + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f)
                                                         [BuildDisplayModeRadio(
                                                             LOCTEXT("SurfaceWaterDisplayModeDropletNormal", "Droplet Normal"),
                                                             LOCTEXT("SurfaceWaterDisplayModeDropletNormalTooltip", "Show the evaluated droplet normal color like the Wetness Profile preview."),
                                                             EDWCSurfaceWaterTilingPreviewDisplayMode::DropletNormal)]
                                                   + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 8.0f)
                                                         [SNew(SSeparator)]
                                                   + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f)
                                                         [SNew(SBox)
                                                              .Visibility(this, &SWetClothingPartEditorPanel::GetSingleCirclePreviewVisibility)
                                                                  [BuildPreviewToggle(
                                                                      LOCTEXT("PopupOverrideDropletStampSize", "Override Droplet1 Stamp Size"),
                                                                      LOCTEXT("PopupOverrideDropletStampSizeTooltip", "Use a part-local scale for this Wet Part instead of the Wetness Profile Droplet1 Stamp Size."),
                                                                      TAttribute<ECheckBoxState>::Create(TAttribute<ECheckBoxState>::FGetter::CreateSP(this, &SWetClothingPartEditorPanel::GetSelectedDropletStampSizeOverrideCheckState)),
                                                                      FOnCheckStateChanged::CreateSP(this, &SWetClothingPartEditorPanel::HandleSelectedDropletStampSizeOverrideChanged))]]
                                                   + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f)
                                                          [SNew(SBox)
                                                               .Visibility(this, &SWetClothingPartEditorPanel::GetSingleCirclePreviewVisibility)
                                                                   [BuildScaleRow(
                                                                       LOCTEXT("PopupDropletStampSizeScale", "Droplet1 Stamp Size Scale"),
                                                                       LOCTEXT("PopupDropletStampSizeScaleTooltip", "Multiplies the Wetness Profile Droplet1 Stamp Size for this Wet Part."),
                                                                       TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateSP(this, &SWetClothingPartEditorPanel::IsSelectedDropletStampSizeOverrideEnabled)),
                                                                       TAttribute<float>::Create(TAttribute<float>::FGetter::CreateSP(this, &SWetClothingPartEditorPanel::GetSelectedDropletRadiusScale)),
                                                                       [this](const float NewValue) { HandleSelectedDropletRadiusScaleChanged(NewValue); })]]
                                                   + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 3.0f)
                                                         [SNew(SBox)
                                                              .Visibility(this, &SWetClothingPartEditorPanel::GetSingleCirclePreviewVisibility)
                                                                  [BuildPreviewToggle(
                                                                      LOCTEXT("PopupOverrideDropletFlowStampSize", "Override Droplet2 Stamp Size"),
                                                                      LOCTEXT("PopupOverrideDropletFlowStampSizeTooltip", "Use a separate part-local scale for Droplet2 stamps instead of the Wetness Profile Droplet2 Stamp Size."),
                                                                      TAttribute<ECheckBoxState>::Create(TAttribute<ECheckBoxState>::FGetter::CreateSP(this, &SWetClothingPartEditorPanel::GetSelectedDropletFlowStampSizeOverrideCheckState)),
                                                                      FOnCheckStateChanged::CreateSP(this, &SWetClothingPartEditorPanel::HandleSelectedDropletFlowStampSizeOverrideChanged))]]
                                                   + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f)
                                                         [SNew(SBox)
                                                              .Visibility(this, &SWetClothingPartEditorPanel::GetSingleCirclePreviewVisibility)
                                                                  [BuildScaleRow(
                                                                      LOCTEXT("PopupDropletFlowStampSizeScale", "Droplet2 Stamp Size Scale"),
                                                                      LOCTEXT("PopupDropletFlowStampSizeScaleTooltip", "Multiplies the Wetness Profile Droplet2 Stamp Width and Height for this Wet Part."),
                                                                      TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateSP(this, &SWetClothingPartEditorPanel::IsSelectedDropletFlowStampSizeOverrideEnabled)),
                                                                      TAttribute<float>::Create(TAttribute<float>::FGetter::CreateSP(this, &SWetClothingPartEditorPanel::GetSelectedDropletFlowSizeScale)),
                                                                      [this](const float NewValue) { HandleSelectedDropletFlowSizeScaleChanged(NewValue); })]]
                                                    + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 12.0f, 0.0f, 0.0f)
                                                          [SNew(STextBlock)
                                                               .Text_Lambda([this]()
                                                              {
                                                                  return SurfaceWaterTilingPreviewViewport.IsValid()
                                                                      ? SurfaceWaterTilingPreviewViewport->GetSurfaceWaterPreviewStatusText()
                                                                      : LOCTEXT("SurfaceWaterTilingPreviewInitializing", "Initializing preview...");
                                                              })
                                                              .AutoWrapText(true)
                                                              .ColorAndOpacity_Lambda([this]()
                                                              {
                                                                  return SurfaceWaterTilingPreviewViewport.IsValid()
                                                                      ? SurfaceWaterTilingPreviewViewport->GetSurfaceWaterPreviewStatusColor()
                                                                      : FSlateColor(FStyleColors::ForegroundHover);
                                                              })]]]]]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(8.0f, 10.0f, 8.0f, 4.0f)
                       [SNew(SBorder)
                           .Padding(FMargin(12.0f, 10.0f))
                           .BorderImage(FAppStyle::Get().GetBrush(TEXT("ToolPanel.GroupBorder")))
                                [SNew(SVerticalBox)
                                 + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f)
                                        [BuildDetailSizeSlider(
                                            LOCTEXT("PopupDropletDetailSize", "Droplet1 Detail Size"),
                                            TAttribute<float>::Create(TAttribute<float>::FGetter::CreateLambda([this]()
                                            {
                                                return FMath::Clamp(GetSelectedDropletDetailSize() / 4.0f, 0.0f, 1.0f);
                                            })),
                                            [this](const float NewValue) { HandleSelectedDropletDetailSizeChanged(NewValue); },
                                            TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &SWetClothingPartEditorPanel::GetSelectedDropletDetailSizeText)))]
                                  + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 3.0f)
                                        [BuildDetailSizeSlider(
                                            LOCTEXT("PopupDropletFlowDetailSize", "Droplet2 Detail Size"),
                                            TAttribute<float>::Create(TAttribute<float>::FGetter::CreateLambda([this]()
                                            {
                                                return FMath::Clamp(GetSelectedDropletFlowDetailSize() / 4.0f, 0.0f, 1.0f);
                                            })),
                                            [this](const float NewValue) { HandleSelectedDropletFlowDetailSizeChanged(NewValue); },
                                            TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &SWetClothingPartEditorPanel::GetSelectedDropletFlowDetailSizeText)))]]]];
}

void SWetClothingPartEditorPanel::HandleSurfaceWaterTilingWindowClosed(const TSharedRef<SWindow>& /*Window*/)
{
    SurfaceWaterTilingPreviewViewport.Reset();
    SurfaceWaterTilingWindow.Reset();
}

float SWetClothingPartEditorPanel::GetSelectedDropletRadiusScale() const
{
    const FWetClothingWetPartEntry* Entry = FindWetPartEntry(SelectedWetPartID);
    return Entry != nullptr ? FMath::Clamp(Entry->SurfaceWater.DropletRadiusScale, 0.25f, 4.0f) : 1.0f;
}

float SWetClothingPartEditorPanel::GetSelectedDropletFlowSizeScale() const
{
    const FWetClothingWetPartEntry* Entry = FindWetPartEntry(SelectedWetPartID);
    return Entry != nullptr ? FMath::Clamp(Entry->SurfaceWater.DropletFlowSizeScale, 0.25f, 4.0f) : 1.0f;
}

ECheckBoxState SWetClothingPartEditorPanel::GetSelectedDropletStampSizeOverrideCheckState() const
{
    const FWetClothingWetPartEntry* Entry = FindWetPartEntry(SelectedWetPartID);
    return Entry != nullptr && Entry->SurfaceWater.bOverrideDropletStampSize
               ? ECheckBoxState::Checked
               : ECheckBoxState::Unchecked;
}

ECheckBoxState SWetClothingPartEditorPanel::GetSelectedDropletFlowStampSizeOverrideCheckState() const
{
    const FWetClothingWetPartEntry* Entry = FindWetPartEntry(SelectedWetPartID);
    return Entry != nullptr && Entry->SurfaceWater.bOverrideDropletFlowStampSize
               ? ECheckBoxState::Checked
               : ECheckBoxState::Unchecked;
}

void SWetClothingPartEditorPanel::HandleSelectedDropletStampSizeOverrideChanged(const ECheckBoxState NewState)
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    FWetClothingWetPartEntry* Entry = FindMutableWetPartEntry(SelectedWetPartID);
    if (Asset == nullptr || Entry == nullptr || Entry->WetPartID < 0)
    {
        return;
    }

    const bool bNewValue = NewState == ECheckBoxState::Checked;
    if (Entry->SurfaceWater.bOverrideDropletStampSize == bNewValue)
    {
        return;
    }

    Asset->Modify();
    Entry->SurfaceWater.bOverrideDropletStampSize = bNewValue;
    Asset->MarkRuntimeBakeOutputsDirty(DWCBakeOutput::GPURuntimeData | DWCBakeOutput::GPUMaps);
    Asset->MarkPackageDirty();
    if (SurfaceWaterTilingPreviewViewport.IsValid())
    {
        SurfaceWaterTilingPreviewViewport->RefreshSurfaceWaterPreviewDynamicTextures();
    }
}

void SWetClothingPartEditorPanel::HandleSelectedDropletFlowStampSizeOverrideChanged(
    const ECheckBoxState NewState)
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    FWetClothingWetPartEntry* Entry = FindMutableWetPartEntry(SelectedWetPartID);
    if (Asset == nullptr || Entry == nullptr || Entry->WetPartID < 0)
    {
        return;
    }

    const bool bNewValue = NewState == ECheckBoxState::Checked;
    if (Entry->SurfaceWater.bOverrideDropletFlowStampSize == bNewValue)
    {
        return;
    }

    Asset->Modify();
    Entry->SurfaceWater.bOverrideDropletFlowStampSize = bNewValue;
    Asset->MarkRuntimeBakeOutputsDirty(DWCBakeOutput::GPURuntimeData | DWCBakeOutput::GPUMaps);
    Asset->MarkPackageDirty();
    if (SurfaceWaterTilingPreviewViewport.IsValid())
    {
        SurfaceWaterTilingPreviewViewport->RefreshSurfaceWaterPreviewDynamicTextures();
    }
}

bool SWetClothingPartEditorPanel::IsSelectedDropletStampSizeOverrideEnabled() const
{
    const FWetClothingWetPartEntry* Entry = FindWetPartEntry(SelectedWetPartID);
    return IsSelectedWetPartSurfaceSettingsEnabled() &&
           Entry != nullptr &&
           Entry->SurfaceWater.bOverrideDropletStampSize;
}

bool SWetClothingPartEditorPanel::IsSelectedDropletFlowStampSizeOverrideEnabled() const
{
    const FWetClothingWetPartEntry* Entry = FindWetPartEntry(SelectedWetPartID);
    return IsSelectedWetPartSurfaceSettingsEnabled() &&
           Entry != nullptr &&
           Entry->SurfaceWater.bOverrideDropletFlowStampSize;
}

float SWetClothingPartEditorPanel::GetSelectedDropletDetailSize() const
{
    const FWetClothingWetPartEntry* Entry = FindWetPartEntry(SelectedWetPartID);
    return Entry != nullptr ? FMath::Clamp(Entry->SurfaceWater.DropletDetailSize, 0.0f, 4.0f) : 1.0f;
}

FText SWetClothingPartEditorPanel::GetSelectedDropletDetailSizeText() const
{
    FNumberFormattingOptions Options;
    Options.MinimumFractionalDigits = 2;
    Options.MaximumFractionalDigits = 2;
    return FText::AsNumber(GetSelectedDropletDetailSize(), &Options);
}

float SWetClothingPartEditorPanel::GetSelectedDropletFlowDetailSize() const
{
    const FWetClothingWetPartEntry* Entry = FindWetPartEntry(SelectedWetPartID);
    return Entry != nullptr ? FMath::Clamp(Entry->SurfaceWater.DropletFlowDetailSize, 0.0f, 4.0f) : 1.0f;
}

FText SWetClothingPartEditorPanel::GetSelectedDropletFlowDetailSizeText() const
{
    FNumberFormattingOptions Options;
    Options.MinimumFractionalDigits = 2;
    Options.MaximumFractionalDigits = 2;
    return FText::AsNumber(GetSelectedDropletFlowDetailSize(), &Options);
}

void SWetClothingPartEditorPanel::HandleSelectedDropletRadiusScaleChanged(const float InValue)
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    FWetClothingWetPartEntry* Entry = FindMutableWetPartEntry(SelectedWetPartID);
    if (Asset == nullptr || Entry == nullptr || Entry->WetPartID < 0)
    {
        return;
    }

    const float NewValue = FMath::Clamp(InValue, 0.25f, 4.0f);
    if (FMath::IsNearlyEqual(Entry->SurfaceWater.DropletRadiusScale, NewValue))
    {
        return;
    }

    Asset->Modify();
    Entry->SurfaceWater.DropletRadiusScale = NewValue;
    Asset->MarkRuntimeBakeOutputsDirty(DWCBakeOutput::GPURuntimeData | DWCBakeOutput::GPUMaps);
    Asset->MarkPackageDirty();
    if (SurfaceWaterTilingPreviewViewport.IsValid())
    {
        SurfaceWaterTilingPreviewViewport->RefreshSurfaceWaterPreviewDynamicTextures();
    }
}

void SWetClothingPartEditorPanel::HandleSelectedDropletFlowSizeScaleChanged(const float InValue)
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    FWetClothingWetPartEntry* Entry = FindMutableWetPartEntry(SelectedWetPartID);
    if (Asset == nullptr || Entry == nullptr || Entry->WetPartID < 0)
    {
        return;
    }

    const float NewValue = FMath::Clamp(InValue, 0.25f, 4.0f);
    if (FMath::IsNearlyEqual(Entry->SurfaceWater.DropletFlowSizeScale, NewValue))
    {
        return;
    }

    Asset->Modify();
    Entry->SurfaceWater.DropletFlowSizeScale = NewValue;
    Asset->MarkRuntimeBakeOutputsDirty(DWCBakeOutput::GPURuntimeData | DWCBakeOutput::GPUMaps);
    Asset->MarkPackageDirty();
    if (SurfaceWaterTilingPreviewViewport.IsValid())
    {
        SurfaceWaterTilingPreviewViewport->RefreshSurfaceWaterPreviewDynamicTextures();
    }
}

void SWetClothingPartEditorPanel::HandleSelectedDropletDetailSizeChanged(const float InValue)
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    FWetClothingWetPartEntry* Entry = FindMutableWetPartEntry(SelectedWetPartID);
    if (Asset == nullptr || Entry == nullptr || Entry->WetPartID < 0)
    {
        return;
    }

    const float NewValue = FMath::Clamp(InValue, 0.0f, 4.0f);
    if (FMath::IsNearlyEqual(Entry->SurfaceWater.DropletDetailSize, NewValue))
    {
        return;
    }

    Asset->Modify();
    Entry->SurfaceWater.DropletDetailSize = NewValue;
    Asset->MarkRuntimeBakeOutputsDirty(DWCBakeOutput::GPUMaps);
    Asset->MarkPackageDirty();
    if (SurfaceWaterTilingPreviewViewport.IsValid())
    {
        SurfaceWaterTilingPreviewViewport->RefreshSurfaceWaterPreviewDynamicTextures();
    }
}

void SWetClothingPartEditorPanel::HandleSelectedDropletFlowDetailSizeChanged(const float InValue)
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    FWetClothingWetPartEntry* Entry = FindMutableWetPartEntry(SelectedWetPartID);
    if (Asset == nullptr || Entry == nullptr || Entry->WetPartID < 0)
    {
        return;
    }

    const float NewValue = FMath::Clamp(InValue, 0.0f, 4.0f);
    if (FMath::IsNearlyEqual(Entry->SurfaceWater.DropletFlowDetailSize, NewValue))
    {
        return;
    }

    Asset->Modify();
    Entry->SurfaceWater.DropletFlowDetailSize = NewValue;
    Asset->MarkRuntimeBakeOutputsDirty(DWCBakeOutput::GPUMaps);
    Asset->MarkPackageDirty();
    if (SurfaceWaterTilingPreviewViewport.IsValid())
    {
        SurfaceWaterTilingPreviewViewport->RefreshSurfaceWaterPreviewDynamicTextures();
    }
}

ECheckBoxState SWetClothingPartEditorPanel::GetSurfaceWaterPreviewCoverageModeState(
    const EDWCSurfaceWaterTilingPreviewCoverageMode Mode) const
{
    return SurfaceWaterPreviewCoverageMode == Mode
        ? ECheckBoxState::Checked
        : ECheckBoxState::Unchecked;
}

void SWetClothingPartEditorPanel::HandleSurfaceWaterPreviewCoverageModeChanged(
    const ECheckBoxState NewState,
    const EDWCSurfaceWaterTilingPreviewCoverageMode Mode)
{
    if (NewState != ECheckBoxState::Checked || SurfaceWaterPreviewCoverageMode == Mode)
    {
        return;
    }

    SurfaceWaterPreviewCoverageMode = Mode;
    if (SurfaceWaterTilingPreviewViewport.IsValid())
    {
        SurfaceWaterTilingPreviewViewport->SetSurfaceWaterTilingPreviewCoverageMode(SurfaceWaterPreviewCoverageMode);
    }
}

ECheckBoxState SWetClothingPartEditorPanel::GetSurfaceWaterPreviewDisplayModeState(
    const EDWCSurfaceWaterTilingPreviewDisplayMode Mode) const
{
    return SurfaceWaterPreviewDisplayMode == Mode
        ? ECheckBoxState::Checked
        : ECheckBoxState::Unchecked;
}

void SWetClothingPartEditorPanel::HandleSurfaceWaterPreviewDisplayModeChanged(
    const ECheckBoxState NewState,
    const EDWCSurfaceWaterTilingPreviewDisplayMode Mode)
{
    if (NewState != ECheckBoxState::Checked || SurfaceWaterPreviewDisplayMode == Mode)
    {
        return;
    }

    SurfaceWaterPreviewDisplayMode = Mode;
    if (SurfaceWaterTilingPreviewViewport.IsValid())
    {
        SurfaceWaterTilingPreviewViewport->SetSurfaceWaterTilingPreviewDisplayMode(SurfaceWaterPreviewDisplayMode);
    }
}

EVisibility SWetClothingPartEditorPanel::GetSingleCirclePreviewVisibility() const
{
    return SurfaceWaterPreviewCoverageMode == EDWCSurfaceWaterTilingPreviewCoverageMode::SingleCircle
        ? EVisibility::Visible
        : EVisibility::Collapsed;
}

ECheckBoxState SWetClothingPartEditorPanel::GetShowPartColorsCheckState() const
{
    return bShowPartColorsInPreview ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SWetClothingPartEditorPanel::HandleShowPartColorsChanged(const ECheckBoxState NewState)
{
    bShowPartColorsInPreview = NewState == ECheckBoxState::Checked;
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->SetShowWetPartColors(bShowPartColorsInPreview);
    }
}

float SWetClothingPartEditorPanel::GetPartColorIntensity() const
{
    return PartColorIntensity;
}

void SWetClothingPartEditorPanel::HandlePartColorIntensityChanged(const float InValue)
{
    PartColorIntensity = FMath::Clamp(InValue, 0.0f, 1.0f);
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->SetWetPartColorIntensity(PartColorIntensity);
    }
}

float SWetClothingPartEditorPanel::GetSelectionLineThicknessScale() const
{
    return PreviewViewport.IsValid() ? PreviewViewport->GetSelectionOverlayThicknessScale() : 1.0f;
}

void SWetClothingPartEditorPanel::HandleSelectionLineThicknessChanged(float InValue)
{
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->SetSelectionOverlayThicknessScale(InValue);
    }
}

TSharedRef<SWidget> SWetClothingPartEditorPanel::BuildPartPreviewControlsPanel()
{
    static const FSlateRoundedBoxBrush PreviewControlsBackgroundBrush(
        FLinearColor(0.025f, 0.025f, 0.025f, 0.86f),
        6.0f,
        FLinearColor(0.22f, 0.22f, 0.22f, 0.9f),
        1.0f);

    return SNew(SBox)
        .WidthOverride(330.0f)
            [SNew(SBorder)
                 .BorderImage(&PreviewControlsBackgroundBrush)
                 .Padding(8.0f)
                     [SNew(SVerticalBox)

                      + SVerticalBox::Slot()
                            .AutoHeight()
                            .Padding(0.0f, 0.0f, 0.0f, 4.0f)
                                [SNew(SBorder)
                                     .BorderImage(FAppStyle::Get().GetBrush(TEXT("Brushes.Header")))
                                     .Padding(FMargin(8.0f, 6.0f))
                                         [SNew(SHorizontalBox)

                                          + SHorizontalBox::Slot()
                                                .AutoWidth()
                                                .VAlign(VAlign_Center)
                                                    [SNew(SBox)
                                                         .WidthOverride(112.0f)
                                                             [SNew(STextBlock)
                                                                  .Text(LOCTEXT("MainPreviewShowPartColors", "Part Colors"))]]

                                          + SHorizontalBox::Slot()
                                                .FillWidth(1.0f)
                                                .VAlign(VAlign_Center)
                                                    [SNew(SCheckBox)
                                                         .IsChecked(this, &SWetClothingPartEditorPanel::GetShowPartColorsCheckState)
                                                         .OnCheckStateChanged(this, &SWetClothingPartEditorPanel::HandleShowPartColorsChanged)
                                                             [SNew(STextBlock)
                                                                  .Text(LOCTEXT("PartColorsEnabledLabel", "Enabled"))]]]]

                      + SVerticalBox::Slot()
                            .AutoHeight()
                            .Padding(0.0f, 0.0f, 0.0f, 4.0f)
                                [SNew(SBorder)
                                     .BorderImage(FAppStyle::Get().GetBrush(TEXT("Brushes.Header")))
                                     .Padding(FMargin(8.0f, 6.0f))
                                         [SNew(SHorizontalBox)

                                          + SHorizontalBox::Slot()
                                                .AutoWidth()
                                                .VAlign(VAlign_Center)
                                                    [SNew(SBox)
                                                         .WidthOverride(112.0f)
                                                             [SNew(STextBlock)
                                                                  .Text(LOCTEXT("PartColorIntensityLabel", "Intensity"))]]

                                          + SHorizontalBox::Slot()
                                                .FillWidth(1.0f)
                                                .VAlign(VAlign_Center)
                                                    [SNew(SSlider)
                                                         .MinValue(0.0f)
                                                         .MaxValue(1.0f)
                                                         .Value(this, &SWetClothingPartEditorPanel::GetPartColorIntensity)
                                                         .OnValueChanged(this, &SWetClothingPartEditorPanel::HandlePartColorIntensityChanged)]

                                          + SHorizontalBox::Slot()
                                                .AutoWidth()
                                                .VAlign(VAlign_Center)
                                                .Padding(8.0f, 0.0f, 0.0f, 0.0f)
                                                    [SNew(SBox)
                                                         .WidthOverride(42.0f)
                                                             [SNew(STextBlock)
                                                                  .Justification(ETextJustify::Right)
                                                                  .Text_Lambda([this]()
                                                                  {
                                                                      return FText::AsPercent(PartColorIntensity);
                                                                  })]]]]

                      + SVerticalBox::Slot()
                            .AutoHeight()
                                [SNew(SBorder)
                                     .BorderImage(FAppStyle::Get().GetBrush(TEXT("Brushes.Header")))
                                     .Padding(FMargin(8.0f, 6.0f))
                                         [SNew(SHorizontalBox)

                                          + SHorizontalBox::Slot()
                                                .AutoWidth()
                                                .VAlign(VAlign_Center)
                                                    [SNew(SBox)
                                                         .WidthOverride(112.0f)
                                                             [SNew(STextBlock)
                                                                  .Text(LOCTEXT("SelectionLineThicknessLabel", "Selection Line"))]]

                                          + SHorizontalBox::Slot()
                                                .FillWidth(1.0f)
                                                .VAlign(VAlign_Center)
                                                    [SNew(SSlider)
                                                         .MinValue(0.25f)
                                                         .MaxValue(4.0f)
                                                         .Value(this, &SWetClothingPartEditorPanel::GetSelectionLineThicknessScale)
                                                         .OnValueChanged(this, &SWetClothingPartEditorPanel::HandleSelectionLineThicknessChanged)]

                                          + SHorizontalBox::Slot()
                                                .AutoWidth()
                                                .VAlign(VAlign_Center)
                                                .Padding(8.0f, 0.0f, 0.0f, 0.0f)
                                                    [SNew(SBox)
                                                         .WidthOverride(42.0f)
                                                             [SNew(STextBlock)
                                                                  .Justification(ETextJustify::Right)
                                                                  .Text_Lambda([this]()
                                                                  {
                                                                      FNumberFormattingOptions Options;
                                                                      Options.MinimumFractionalDigits = 1;
                                                                      Options.MaximumFractionalDigits = 1;
                                                                      return FText::Format(
                                                                          LOCTEXT("SelectionLineWeightValue", "{0}x"),
                                                                          FText::AsNumber(GetSelectionLineThicknessScale(), &Options));
                                                                  })]]]]]];
}

FReply SWetClothingPartEditorPanel::HandleFocusPreviewClicked()
{
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->FocusOnPreviewMesh();
    }

    return FReply::Handled();
}

FReply SWetClothingPartEditorPanel::HandleSaveAssetClicked()
{
    DWCEditorUtils::SaveAsset(WetClothingAsset.Get());
    return FReply::Handled();
}

FReply SWetClothingPartEditorPanel::HandleBakeAllMapsClicked()
{
    return HandleBakeRenderProfileDataClicked();
}

bool SWetClothingPartEditorPanel::IsRenderProfileBakeSourceValid() const
{
    return WetClothingAsset.IsValid() &&
           WetClothingAsset->GetRuntimeSkeletalMesh() != nullptr &&
           WetClothingAsset->HasValidDataUVForLOD(WetClothingAsset->GetSimulationLODIndex());
}

bool SWetClothingPartEditorPanel::CanBakeAnyRenderProfileData() const
{
    return IsRenderProfileBakeSourceValid() &&
           WetClothingAsset->Authored.PartData.EditableWetPartData.MaterialSlots.ContainsByPredicate(
               [](const FWetClothingAuthoredMaterialSlot& SlotData)
               {
                   return SlotData.bIsWettableSlot && !SlotData.WetPartEntries.IsEmpty();
               });
}

FReply SWetClothingPartEditorPanel::HandleBakeRenderProfileDataClicked()
{
    FString Summary;
    bool bHadWarnings = false;
    if (!BakeRenderProfileDataAndUpdateMaterials(Summary, &bHadWarnings))
    {
        FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Summary));
        return FReply::Handled();
    }

    UE_LOG(LogTemp, Display, TEXT("DWC: %s"), *Summary);
    if (DetailsView.IsValid())
    {
        DetailsView->ForceRefresh();
    }
    return FReply::Handled();
}

UTexture* SWetClothingPartEditorPanel::ResolveSelectedMaterialTexture() const
{
    return SelectedTextureItem.IsValid() ? SelectedTextureItem->Texture.Get() : nullptr;
}

UTexture* SWetClothingPartEditorPanel::ResolveTextureAddressTexture() const
{
    if (UTexture* SelectedTexture = ResolveSelectedMaterialTexture())
    {
        return SelectedTexture;
    }

    for (const FTextureItemPtr& TextureItem : TextureItems)
    {
        if (TextureItem.IsValid() && TextureItem->Texture.IsValid())
        {
            return TextureItem->Texture.Get();
        }
    }

    return nullptr;
}

void SWetClothingPartEditorPanel::SaveTextureSelection(int32 MaterialSlotIndex, UTexture* Texture)
{
    FWetClothingMaterialTextureResolver::SaveTextureSelection(WetClothingAsset.Get(), MaterialSlotIndex, Texture);
}

bool SWetClothingPartEditorPanel::HasPendingVisualBakeTasks(FString* OutSummary) const
{
    return FWetClothingRenderProfileBakeService::HasPendingVisualBakeTasks(WetClothingAsset.Get(), OutSummary);
}

bool SWetClothingPartEditorPanel::BakeRenderProfileDataAndUpdateMaterials(FString& OutSummary, bool* OutHadWarnings)
{
    const bool bSucceeded = FWetClothingRenderProfileBakeService::BakeRenderProfileDataAndUpdateMaterials(WetClothingAsset.Get(), OutSummary, OutHadWarnings);
    if (DetailsView.IsValid())
    {
        DetailsView->ForceRefresh();
    }

    return bSucceeded;
}

bool SWetClothingPartEditorPanel::SaveBakedRenderProfileAssets() const
{
    return FWetClothingRenderProfileBakeService::SaveBakedRenderProfileAssets(WetClothingAsset.Get());
}

void SWetClothingPartEditorPanel::SaveSelectedTexture()
{
    SaveTextureSelection(SelectedMaterialSlotIndex, ResolveSelectedMaterialTexture());
}

#undef LOCTEXT_NAMESPACE
