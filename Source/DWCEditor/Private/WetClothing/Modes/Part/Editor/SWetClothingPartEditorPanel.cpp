#include "SWetClothingPartEditorPanel.h"

#include "AssetThumbnail.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Core/DWCEditorUtils.h"
#include "Core/DWCEditorStyle.h"
#include "WetClothing/Modes/Part/Partition/WetPartAutoPartitioner.h"
#include "WetClothing/Modes/Part/Partition/WetPartEditingService.h"
#include "WetClothing/DerivedAssets/Materials/WCAMaterialGenerator.h"
#include "WetClothing/DerivedAssets/Textures/WetnessProfile/WetClothingWetnessProfileMapBaker.h"
#include "WetClothing/DerivedAssets/Textures/WetnessProfile/WetClothingWetnessProfileMapBakeService.h"
#include "WetClothing/Foundation/TextureAccess/WetClothingMaterialTextureResolver.h"
#include "WetClothing/Foundation/TextureAccess/WetClothingTextureReadback.h"
#include "WetClothing/WCAEditor/UI/UVView/SWCAUVView.h"
#include "WetClothing/WCAEditor/UI/Widgets/WCAEditorWidgets.h"
#include "WetClothing/Modes/Part/Widgets/SWetPartAutoPartitionControls.h"
#include "DataAssets/WetClothingAsset.h"
#include "WetClothing/Foundation/MeshAnalysis/WetClothingAssetMeshAnalyzer.h"
#include "WetClothing/WCAEditor/UI/UVView/WCAUVIslandViewCache.h"
#include "WetClothing/Modes/Part/Viewport/SDWCPartViewport.h"
#include "DataAssets/WetnessProfile.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "IDetailsView.h"
#include "FileHelpers.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/MessageDialog.h"
#include "Modules/ModuleManager.h"
#include "PropertyCustomizationHelpers.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Styling/StyleColors.h"
#include "UObject/Package.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Colors/SColorPicker.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSplitter.h"
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
    constexpr float                          AutoPartitionMaxTolerancePercent = 40.0f;
    constexpr float                          AutoPartitionDefaultTolerancePercent = 20.0f;
    constexpr EWetPartAutoPartitionColorMode AutoPartitionDefaultColorMode = EWetPartAutoPartitionColorMode::DominantColor;

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

    bool IsWetPartEntryRelevantForVisualBake(const FWetClothingWetPartEntry& Entry)
    {
        return Entry.MaterialSlotIndex != INDEX_NONE &&
               Entry.UVChannelIndex != INDEX_NONE &&
               Entry.AssignedUVIslandIDs.Num() > 0;
    }

    FString MakeTextureUvKey(const UTexture* Texture, int32 UVChannelIndex)
    {
        return Texture != nullptr && UVChannelIndex != INDEX_NONE
                   ? FString::Printf(TEXT("%s|%d"), *Texture->GetPathName(), UVChannelIndex)
                   : FString();
    }
} // namespace SWetClothingPartEditorPanelLocal

void SWetClothingPartEditorPanel::Construct(const FArguments& InArgs)
{
    WetClothingAsset = InArgs._WetClothingAsset;
    DetailsView = InArgs._DetailsView;

    const FSlateFontInfo PanelHeadingFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 16);
    const FSlateFontInfo SectionHeadingFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 13);
    const FSlateFontInfo AssignButtonFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 11);

    MaterialThumbnailPool = MakeShared<FAssetThumbnailPool>(32);

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
    UVDisplayModeItems.Reset();
    UVDisplayModeItems.Add(MakeShared<EWCAUVDisplayMode>(EWCAUVDisplayMode::Normal));
    UVDisplayModeItems.Add(MakeShared<EWCAUVDisplayMode>(EWCAUVDisplayMode::OutlineOnly));
    SelectedUVDisplayModeItem = UVDisplayModeItems[0];
    CurrentUVDisplayMode = EWCAUVDisplayMode::Normal;

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
            .Padding(ToolIndex + 1 < UVSelectionToolItems.Num() ? FMargin(0.0f, 0.0f, 4.0f, 0.0f) : FMargin(0.0f))
                [BuildSelectionToolButton(UVSelectionToolItems[ToolIndex])];
    }

    ChildSlot
        [SNew(SVerticalBox)

         + SVerticalBox::Slot()
               .AutoHeight()
               .Padding(10.0f, 10.0f, 10.0f, 8.0f)
                   [SNew(STextBlock)
                        .Text(LOCTEXT("EditorHeading", "Wet Part"))
                        .Font(PanelHeadingFont)]

         + SVerticalBox::Slot()
               .AutoHeight()
               .Padding(10.0f, 0.0f, 10.0f, 10.0f)
                   [SNew(SSeparator)
                        .Orientation(Orient_Horizontal)]

         + SVerticalBox::Slot()
               .FillHeight(1.0f)
               .Padding(10.0f, 0.0f, 10.0f, 10.0f)
                   [SNew(SSplitter)

         // Column 1: Target Mesh / UV Channel / Material Slots / Wet Part Map.
         + SSplitter::Slot()
               .Value(0.25f)
                   [SNew(SBorder)
                        .Padding(10.0f)
                            [SNew(SVerticalBox)

                             + SVerticalBox::Slot()
                                   .AutoHeight()
                                   .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                                       [SNew(SHorizontalBox)

                                        + SHorizontalBox::Slot()
                                              .AutoWidth()
                                              .VAlign(VAlign_Center)
                                              .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                                                  [SNew(STextBlock)
                                                       .Text(LOCTEXT("OriginalUVChannelLabel", "Original UV"))]

                                        + SHorizontalBox::Slot()
                                              .FillWidth(1.0f)
                                              .VAlign(VAlign_Center)
                                                  [SNew(SBorder)
                                                       .Padding(FMargin(8.0f, 3.0f))
                                                       .BorderImage(FAppStyle::Get().GetBrush(TEXT("ToolPanel.GroupBorder")))
                                                           [SNew(STextBlock)
                                                                .Text(this, &SWetClothingPartEditorPanel::GetOriginalUVChannelText)
                                                                .ToolTipText(LOCTEXT(
                                                                    "OriginalUVReadOnlyTooltip",
                                                                    "The Original UV channel configured in Asset Setup."))]]]

                             + SVerticalBox::Slot()
                                   .AutoHeight()
                                   .Padding(0.0f, 0.0f, 0.0f, 16.0f)
                                       [SNew(SHorizontalBox)

                                        + SHorizontalBox::Slot()
                                              .AutoWidth()
                                              .VAlign(VAlign_Center)
                                              .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                                                  [SNew(STextBlock)
                                                       .Text(LOCTEXT("WetnessProfileMapResolutionLabel", "Wetness Profile Map Resolution (Fixed)"))]

                                        + SHorizontalBox::Slot()
                                              .AutoWidth()
                                              .VAlign(VAlign_Center)
                                                  [SNew(SBorder)
                                                       .Padding(FMargin(8.0f, 3.0f))
                                                       .BorderImage(FAppStyle::Get().GetBrush(TEXT("ToolPanel.GroupBorder")))
                                                           [SNew(STextBlock)
                                                                .Text(LOCTEXT("WetnessProfileMapResolutionPlaceholder", "256"))
                                                                .ColorAndOpacity(FSlateColor(FStyleColors::ForegroundHover))]]]

                             + SVerticalBox::Slot()
                                   .FillHeight(1.0f)
                                       [SNew(SSplitter)
                                            .Orientation(Orient_Vertical)

                                        + SSplitter::Slot()
                                              .Value(0.42f)
                                                  [SNew(SVerticalBox)

                                                   + SVerticalBox::Slot()
                                                         .AutoHeight()
                                                         .Padding(0.0f, 14.0f, 0.0f, 4.0f)
                                                             [SNew(SHorizontalBox)

                                                              + SHorizontalBox::Slot()
                                                                    .FillWidth(1.0f)
                                                                    .VAlign(VAlign_Center)
                                                                        [SNew(STextBlock)
                                                                             .Text(LOCTEXT("MaterialSlotsLabel", "Material Slots"))
                                                                             .Font(SectionHeadingFont)]

                                                              + SHorizontalBox::Slot()
                                                                    .AutoWidth()
                                                                    .VAlign(VAlign_Center)
                                                                        [SNew(STextBlock)
                                                                             .Text(this, &SWetClothingPartEditorPanel::GetMaterialSlotCountText)]]

                                                   + SVerticalBox::Slot()
                                                         .AutoHeight()
                                                         .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                                                             [SNew(SSeparator)
                                                                  .Orientation(Orient_Horizontal)]

                                                   + SVerticalBox::Slot()
                                                         .FillHeight(1.0f)
                                                             [SAssignNew(MaterialSlotListView, SListView<FMaterialSlotItemPtr>)
                                                                  .ListItemsSource(&MaterialSlotItems)
                                                                  .OnGenerateRow(this, &SWetClothingPartEditorPanel::GenerateMaterialSlotRow)
                                                                  .OnSelectionChanged(this, &SWetClothingPartEditorPanel::HandleMaterialSlotSelectionChanged)
                                                                  .SelectionMode(ESelectionMode::Single)]]

                                        + SSplitter::Slot()
                                              .Value(0.58f)
                                                  [SNew(SVerticalBox)

                                                   + SVerticalBox::Slot()
                                                         .AutoHeight()
                                                         .Padding(0.0f, 8.0f, 0.0f, 4.0f)
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
                                                                  .AutoWrapText(true)
                                                                  .Text(this, &SWetClothingPartEditorPanel::GetSelectedWetPartText)]

                                                   + SVerticalBox::Slot()
                                                         .AutoHeight()
                                                         .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                                                             [SNew(STextBlock)
                                                                  .AutoWrapText(true)
                                                                  .Text(this, &SWetClothingPartEditorPanel::GetWetnessProfileLibraryStatusText)
                                                                  .ColorAndOpacity(FSlateColor(FLinearColor(0.72f, 0.72f, 0.72f, 1.0f)))]

                                                   + SVerticalBox::Slot()
                                                         .FillHeight(1.0f)
                                                             [SAssignNew(WetPartListView, SListView<FWetPartEntryPtr>)
                                                                  .ListItemsSource(&CurrentWetPartItems)
                                                                  .OnGenerateRow(this, &SWetClothingPartEditorPanel::GenerateWetPartRow)
                                                                  .OnSelectionChanged(this, &SWetClothingPartEditorPanel::HandleWetPartSelectionChanged)
                                                                  .OnMouseButtonDoubleClick(this, &SWetClothingPartEditorPanel::HandleWetPartItemDoubleClicked)
                                                                  .SelectionMode(ESelectionMode::Single)]]]]]

         // Column 2: UV View, with UV Islands directly underneath.
         + SSplitter::Slot()
               .Value(0.375f)
                   [SNew(SBorder)
                        .Padding(8.0f)
                            [SNew(SSplitter)
                                 .Orientation(Orient_Vertical)

                             + SSplitter::Slot()
                                   .Value(0.58f)
                                       [SNew(SVerticalBox)

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 10.0f, 0.0f, 4.0f)
                                                  [SNew(STextBlock)
                                                       .Text(LOCTEXT("UVViewLabel", "UV View"))
                                                       .Font(SectionHeadingFont)]

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                                                  [SNew(SSeparator)
                                                       .Orientation(Orient_Horizontal)]

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                                                  [SNew(STextBlock)
                                                       .AutoWrapText(true)
                                                       .Text(this, &SWetClothingPartEditorPanel::GetUVStatusText)]

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                                                  [FWCAEditorWidgets::BuildUVViewTextureAndViewRow(
                                                      SAssignNew(TextureSelectionContainer, SBox),
                                                      FWCAEditorWidgets::BuildUVViewOptionsButton(
                                                                 &UVDisplayModeItems,
                                                                 SelectedUVDisplayModeItem,
                                                                 TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &SWetClothingPartEditorPanel::GetSelectedUVDisplayModeText)),
                                                                 [this](FUVDisplayModeItemPtr Item)
                                                                 {
                                                                     HandleUVDisplayModeSelectionChanged(Item, ESelectInfo::Direct);
                                                                 },
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
                                                                 }))]

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                                                  [SNew(SHorizontalBox)

                                                   + SHorizontalBox::Slot()
                                                         .AutoWidth()
                                                         .VAlign(VAlign_Center)
                                                         .Padding(0.0f, 0.0f, 4.0f, 0.0f)
                                                             [SNew(STextBlock)
                                                                  .Text(LOCTEXT("UVSelectionToolLabel", "Tool:"))]

                                                   + SHorizontalBox::Slot()
                                                         .AutoWidth()
                                                             [SelectionToolButtonRow]

                                                   + SHorizontalBox::Slot()
                                                         .AutoWidth()
                                                         .VAlign(VAlign_Center)
                                                         .Padding(10.0f, 0.0f, 0.0f, 0.0f)
                                                             [SNew(SWetPartAutoPartitionControls)
                                                                  .IsAutoPartitionEnabled(this, &SWetClothingPartEditorPanel::IsAutoPartitionEnabled)
                                                                  .OnAutoPartitionClicked(this, &SWetClothingPartEditorPanel::HandleAutoPartitionClicked)]]

                                        + SVerticalBox::Slot()
                                              .FillHeight(1.0f)
                                                  [SAssignNew(UVView, SWCAUVView)
                                                       .OnIslandSelectionChanged(this, &SWetClothingPartEditorPanel::HandleUVIslandSelectionChangedFromUVView)]]

                             + SSplitter::Slot()
                                   .Value(0.42f)
                                       [SNew(SVerticalBox)

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 10.0f, 0.0f, 4.0f)
                                                  [SNew(SHorizontalBox)

                                                   + SHorizontalBox::Slot()
                                                         .AutoWidth()
                                                         .VAlign(VAlign_Center)
                                                             [SNew(STextBlock)
                                                                  .Text(LOCTEXT("UVIslandLabel", "UV Islands"))
                                                                  .Font(SectionHeadingFont)]

                                                   + SHorizontalBox::Slot()
                                                         .AutoWidth()
                                                         .Padding(8.0f, 0.0f, 0.0f, 0.0f)
                                                         .VAlign(VAlign_Center)
                                                             [SNew(STextBlock)
                                                                  .Text(LOCTEXT("AssignTargetLabel", "Target:"))]

                                                   + SHorizontalBox::Slot()
                                                         .AutoWidth()
                                                         .Padding(6.0f, 0.0f, 0.0f, 0.0f)
                                                         .VAlign(VAlign_Center)
                                                             [SNew(SBox)
                                                                  .WidthOverride(220.0f)
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
                                                                                               .Text(this, &SWetClothingPartEditorPanel::GetSelectedAssignWetPartText)]]]]

                                                   + SHorizontalBox::Slot()
                                                         .AutoWidth()
                                                         .Padding(8.0f, 0.0f, 0.0f, 0.0f)
                                                         .VAlign(VAlign_Center)
                                                             [SNew(SButton)
                                                                  .ContentPadding(FMargin(10.0f, 4.0f))
                                                                  .HAlign(HAlign_Center)
                                                                  .VAlign(VAlign_Center)
                                                                  .OnClicked(this, &SWetClothingPartEditorPanel::HandleAssignSelectedUVIslandToWetPartClicked)
                                                                      [SNew(STextBlock)
                                                                           .Text(this, &SWetClothingPartEditorPanel::GetAssignUVIslandToWetPartText)
                                                                           .Font(AssignButtonFont)]]]

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                                                  [SNew(SSeparator)
                                                       .Orientation(Orient_Horizontal)]

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 0.0f, 0.0f, 4.0f)
                                                  [SNew(STextBlock)
                                                       .Text(this, &SWetClothingPartEditorPanel::GetUVIslandCountText)]

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                                                  [SNew(STextBlock)
                                                       .AutoWrapText(true)
                                                       .Text(this, &SWetClothingPartEditorPanel::GetSelectedUVIslandText)]

                                        + SVerticalBox::Slot()
                                              .FillHeight(1.0f)
                                                  [SAssignNew(UVIslandListView, SListView<FUVIslandItemPtr>)
                                                       .ListItemsSource(&UVIslandItems)
                                                       .OnGenerateRow(this, &SWetClothingPartEditorPanel::GenerateUVIslandRow)
                                                       .OnSelectionChanged(this, &SWetClothingPartEditorPanel::HandleUVIslandSelectionChanged)
                                                       .SelectionMode(ESelectionMode::Multi)]]]]

         // Column 3: Preview.
         + SSplitter::Slot()
               .Value(0.375f)
                   [FWCAEditorWidgets::BuildPreviewSection(
                       SAssignNew(PreviewViewport, SDWCPartViewport)
                           .WetClothingAsset(WetClothingAsset.Get())
                           .OnIslandPicked(this, &SWetClothingPartEditorPanel::HandleUVIslandPickedFromPreview),
                       FOnWetClothingPreviewFocusClicked::CreateSP(this, &SWetClothingPartEditorPanel::HandleFocusPreviewClicked),
                       SNew(SHorizontalBox)
                           + SHorizontalBox::Slot()
                                 .AutoWidth()
                                 .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                                 .VAlign(VAlign_Center)
                                     [SNew(STextBlock)
                                          .Text(LOCTEXT("SelectionLineThicknessLabel", "Selection Line"))]

                           + SHorizontalBox::Slot()
                                 .AutoWidth()
                                 .VAlign(VAlign_Center)
                                     [SNew(SBox)
                                          .WidthOverride(88.0f)
                                              [SNew(SSpinBox<float>)
                                                   .MinValue(0.25f)
                                                   .MaxValue(4.0f)
                                                   .MinSliderValue(0.25f)
                                                   .MaxSliderValue(4.0f)
                                                   .Delta(0.05f)
                                                   .Value(this, &SWetClothingPartEditorPanel::GetSelectionLineThicknessScale)
                                                   .OnValueChanged(this, &SWetClothingPartEditorPanel::HandleSelectionLineThicknessChanged)]])]]];

    RefreshFromAsset();
}

void SWetClothingPartEditorPanel::RefreshFromAsset()
{
    RefreshMaterialSlotItems();
    RefreshOriginalUVChannel();
    RefreshMaterialTextures();

    if (PreviewViewport.IsValid())
    {
        PreviewViewport->RefreshPreviewMesh();

        if (SelectedMaterialSlotIndex != INDEX_NONE)
        {
            PreviewViewport->SetHighlightedMaterialSlot(SelectedMaterialSlotIndex);
        }
        else
        {
            PreviewViewport->ClearMaterialSlotHighlight();
        }
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
        if (const USkeletalMesh* GeneratedDataUV = WetClothingAssetPtr->GetRuntimeSkeletalMesh())
        {
            const TArray<FSkeletalMaterial>& Materials = GeneratedDataUV->GetMaterials();

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

    if (MaterialSlotListView.IsValid())
    {
        MaterialSlotListView->RequestListRefresh();

        const FMaterialSlotItemPtr SelectedItem = FindMaterialSlotItem(SelectedMaterialSlotIndex);
        if (SelectedItem.IsValid())
        {
            MaterialSlotListView->SetSelection(SelectedItem, ESelectInfo::Direct);
        }
        else
        {
            MaterialSlotListView->ClearSelection();
        }
    }
}

void SWetClothingPartEditorPanel::RefreshMaterialTextures(bool bRefreshUVView)
{
    const int32 UVChannelIndex = GetOriginalUVChannelIndex();

    TextureThumbnails.Reset();
    FWetClothingMaterialTextureResolver::BuildTextureItemsForMaterialSlot(
        WetClothingAsset.Get(),
        SelectedMaterialSlotIndex,
        UVChannelIndex,
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
    UVStatusMessage = TEXT("Select a material slot to inspect its UV islands.");

    const UWetClothingAsset* WetClothingAssetPtr = WetClothingAsset.Get();
    if (WetClothingAssetPtr == nullptr || WetClothingAssetPtr->GetRuntimeSkeletalMesh() == nullptr)
    {
        UVStatusMessage = TEXT("Generate the DWC Data UV to inspect UV islands.");
    }
    else if (SelectedMaterialSlotIndex == INDEX_NONE)
    {
        UVStatusMessage = TEXT("Select a material slot to inspect its UV islands.");
    }
    else if (!HasValidOriginalUVChannel())
    {
        UVStatusMessage = TEXT("No UV channels are available on LOD 0.");
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
            UVStatusMessage = ErrorMessage;
        }
        else if (UVIslandItems.IsEmpty())
        {
            UVStatusMessage = TEXT("No UV islands were found for the selected slot in LOD0.");
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
    UVView->SetDisplayMode(CurrentUVDisplayMode);
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

    EnsureDefaultWetPartForSelectedScope();

    if (const UWetClothingAsset* WetClothingAssetPtr = WetClothingAsset.Get())
    {
        const int32 UVChannelIndex = GetOriginalUVChannelIndex();
        for (const FWetClothingWetPartEntry& Entry : WetClothingAssetPtr->Authored.PartData.EditableWetPartData.WetPartEntries)
        {
            if (Entry.MaterialSlotIndex == SelectedMaterialSlotIndex && Entry.UVChannelIndex == UVChannelIndex)
            {
                CurrentWetPartItems.Add(MakeShared<FWetClothingWetPartEntry>(Entry));
            }
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
        PreviewViewport->SetWetPartIslandAssignments(BuildUVIslandWetPartIDMap(), BuildUVIslandColorMap());
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
    TSet<int32>               HiddenIslandIDs;

    const FWetClothingWetPartEntry* DefaultEntry = nullptr;
    TMap<int32, const FWetClothingWetPartEntry*> ExplicitEntryByIslandID;

    if (const UWetClothingAsset* WetClothingAssetPtr = WetClothingAsset.Get())
    {
        const int32 UVChannelIndex = GetOriginalUVChannelIndex();
        for (const FWetClothingWetPartEntry& Entry : WetClothingAssetPtr->Authored.PartData.EditableWetPartData.WetPartEntries)
        {
            if (Entry.MaterialSlotIndex != SelectedMaterialSlotIndex || Entry.UVChannelIndex != UVChannelIndex)
            {
                continue;
            }

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

        FLinearColor Color = Entry->WetPartID == 0 ? FLinearColor::White : Entry->Color;
        Color.A = 1.0f;
        IslandColors.Add(IslandItem->UVIslandID, Color);
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
        PreviewViewport->SetWetPartIslandAssignments(IslandWetPartIDs, IslandColors);
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
    if (WetClothingAssetPtr == nullptr || SelectedMaterialSlotIndex == INDEX_NONE || !HasValidOriginalUVChannel())
    {
        return;
    }

    const FWetPartScope Scope = FWetPartEditingService::MakeScope(SelectedMaterialSlotIndex, GetOriginalUVChannelIndex());
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
    int32 MaxWetPartID = 0;
    if (const UWetClothingAsset* WetClothingAssetPtr = WetClothingAsset.Get())
    {
        const int32 UVChannelIndex = GetOriginalUVChannelIndex();
        for (const FWetClothingWetPartEntry& Entry : WetClothingAssetPtr->Authored.PartData.EditableWetPartData.WetPartEntries)
        {
            if (Entry.MaterialSlotIndex == SelectedMaterialSlotIndex && Entry.UVChannelIndex == UVChannelIndex)
            {
                MaxWetPartID = FMath::Max(MaxWetPartID, Entry.WetPartID);
            }
        }
    }

    return MaxWetPartID + 1;
}

FWetClothingWetPartEntry* SWetClothingPartEditorPanel::FindMutableWetPartEntry(int32 WetPartID) const
{
    UWetClothingAsset* WetClothingAssetPtr = WetClothingAsset.Get();
    if (WetClothingAssetPtr == nullptr)
    {
        return nullptr;
    }

    const int32 UVChannelIndex = GetOriginalUVChannelIndex();
    for (FWetClothingWetPartEntry& Entry : WetClothingAssetPtr->Authored.PartData.EditableWetPartData.WetPartEntries)
    {
        if (Entry.MaterialSlotIndex == SelectedMaterialSlotIndex && Entry.UVChannelIndex == UVChannelIndex && Entry.WetPartID == WetPartID)
        {
            return &Entry;
        }
    }

    return nullptr;
}

const FWetClothingWetPartEntry* SWetClothingPartEditorPanel::FindWetPartEntry(int32 WetPartID) const
{
    return FindMutableWetPartEntry(WetPartID);
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
    if (const UWetClothingAsset* WetClothingAssetPtr = WetClothingAsset.Get())
    {
        const int32 UVChannelIndex = GetOriginalUVChannelIndex();
        for (const FWetClothingWetPartEntry& Entry : WetClothingAssetPtr->Authored.PartData.EditableWetPartData.WetPartEntries)
        {
            if (Entry.MaterialSlotIndex == SelectedMaterialSlotIndex && Entry.UVChannelIndex == UVChannelIndex && Entry.AssignedUVIslandIDs.Contains(UVIslandID))
            {
                return &Entry;
            }
        }
    }
    return nullptr;
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
        return FLinearColor::White;
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
    return WetPartID == 0 ? TEXT("Part Default") : FString::Printf(TEXT("Part %d"), WetPartID);
}

FString SWetClothingPartEditorPanel::GetWetPartDisplayName(const FWetClothingWetPartEntry& Entry) const
{
    const FString TrimmedName = Entry.DisplayName.TrimStartAndEnd();
    if (!TrimmedName.IsEmpty())
    {
        return TrimmedName;
    }

    return GetDefaultWetPartName(Entry.WetPartID);
}

FString SWetClothingPartEditorPanel::GetAssignedProfileLabel(const FWetClothingWetPartEntry& Entry) const
{
    const FString TrimmedLabel = Entry.ProfileAssignment.SourceProfileName.TrimStartAndEnd();
    return TrimmedLabel.IsEmpty() ? TEXT("Select Profile") : TrimmedLabel;
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
        if (EffectiveWetPartID == 0)
        {
            continue;
        }

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

            FLinearColor Color = Entry->WetPartID == 0 ? FLinearColor::White : Entry->Color;
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
    const UWetClothingAsset* WetClothingAssetPtr = WetClothingAsset.Get();
    if (WetClothingAssetPtr == nullptr || MaterialSlotIndex == INDEX_NONE)
    {
        return FText::GetEmpty();
    }

    int32 PartCount = 0;
    for (const FWetClothingWetPartEntry& Entry : WetClothingAssetPtr->Authored.PartData.EditableWetPartData.WetPartEntries)
    {
        if (Entry.MaterialSlotIndex == MaterialSlotIndex && Entry.AssignedUVIslandIDs.Num() > 0)
        {
            ++PartCount;
        }
    }

    if (PartCount <= 0)
    {
        return NSLOCTEXT("SWetClothingPartEditorPanel", "MaterialSlotStatusNoParts", "No Parts");
    }

    return PartCount == 1
        ? NSLOCTEXT("SWetClothingPartEditorPanel", "MaterialSlotStatusOnePart", "1 Part")
        : FText::Format(NSLOCTEXT("SWetClothingPartEditorPanel", "MaterialSlotStatusManyParts", "{0} Parts"), FText::AsNumber(PartCount));
}

TSharedRef<ITableRow> SWetClothingPartEditorPanel::GenerateMaterialSlotRow(FMaterialSlotItemPtr Item, const TSharedRef<STableViewBase>& OwnerTable)
{
    FWCAMaterialSlotRowArgs Args;
    Args.WetClothingAsset = WetClothingAsset.Get();
    Args.GeneratedDataUV = WetClothingAsset.IsValid() ? WetClothingAsset->GetRuntimeSkeletalMesh() : nullptr;
    Args.ThumbnailPool = MaterialThumbnailPool;
    Args.ThumbnailSink = &MaterialSlotThumbnails;
    Args.OnWettableSlotClicked = FOnWettableMaterialSlotClicked::CreateSP(this, &SWetClothingPartEditorPanel::HandleWettableMaterialSlotClicked);
    Args.GetMaterialSlotStatusText = [this](const int32 MaterialSlotIndex)
    {
        return GetMaterialSlotStatusText(MaterialSlotIndex);
    };

    return FWCAEditorWidgets::GenerateMaterialSlotRow(Item, OwnerTable, Args);
}

void SWetClothingPartEditorPanel::HandleMaterialSlotSelectionChanged(FMaterialSlotItemPtr Item, ESelectInfo::Type)
{
    SelectedMaterialSlotIndex = Item.IsValid() ? Item->SlotIndex : INDEX_NONE;
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

FReply SWetClothingPartEditorPanel::HandleWettableMaterialSlotClicked(int32 MaterialSlotIndex)
{
    UWetClothingAsset* WetClothingAssetPtr = WetClothingAsset.Get();
    if (WetClothingAssetPtr == nullptr || MaterialSlotIndex == INDEX_NONE)
    {
        return FReply::Handled();
    }

    const bool bNewWettable = !FWCAEditorWidgets::IsMaterialSlotWettable(WetClothingAssetPtr, MaterialSlotIndex);
    FWCAEditorWidgets::SetMaterialSlotWettable(WetClothingAssetPtr, MaterialSlotIndex, bNewWettable);

    if (FMaterialSlotItemPtr SlotItem = FindMaterialSlotItem(MaterialSlotIndex))
    {
        SlotItem->bIsWettableSlot = bNewWettable;
    }
    if (MaterialSlotListView.IsValid())
    {
        MaterialSlotListView->Invalidate(EInvalidateWidget::Paint);
    }
    if (MaterialSlotIndex == SelectedMaterialSlotIndex)
    {
        RefreshWetPartList(true);
        RefreshWetPartAssignmentViews();
    }

    return FReply::Handled();
}

void SWetClothingPartEditorPanel::MarkSelectedMaterialSlotWettable()
{
    UWetClothingAsset* WetClothingAssetPtr = WetClothingAsset.Get();
    if (WetClothingAssetPtr == nullptr || SelectedMaterialSlotIndex == INDEX_NONE)
    {
        return;
    }

    FWCAEditorWidgets::MarkMaterialSlotWettable(WetClothingAssetPtr, SelectedMaterialSlotIndex);
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
    MarkSelectedMaterialSlotWettable();

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
        FWCAMaterialGenerator::MakeOptionsForAsset(WetClothingAssetPtr, EDWCSimulationMode::VertexCPU);
    const FWetClothingUnifiedMaterialSetupResult MaterialSet =
        FWCAMaterialGenerator::CreateOrUpdateUnifiedMaterialSet(SourceMaterial, MaterialSetupOptions);

    FString ResultMessage = MaterialSet.Message;
    const bool bSucceeded =
        MaterialSet.bSucceeded && MaterialSet.GeneratedMaterial != nullptr &&
        MaterialSet.CPUMaterialInstance != nullptr && MaterialSet.GPUMaterialInstance != nullptr;

    if (bSucceeded && SourceMaterial != nullptr)
    {
        if (WetClothingAssetPtr != nullptr)
        {
            if (const USkeletalMesh* GeneratedDataUV = WetClothingAssetPtr->GetRuntimeSkeletalMesh())
            {
                const TArray<FSkeletalMaterial>& Materials = GeneratedDataUV->GetMaterials();
                TArray<int32>                    AssignedSlotIndices;
                for (int32 MaterialIndex = 0; MaterialIndex < Materials.Num(); ++MaterialIndex)
                {
                    if (Materials[MaterialIndex].MaterialInterface == SourceMaterial)
                    {
                        AssignedSlotIndices.Add(MaterialIndex);
                    }
                }

                if (AssignedSlotIndices.Num() > 0)
                {
                    WetClothingAssetPtr->Modify();
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
                        ExistingOverride->CPUMaterialInstance = MaterialSet.CPUMaterialInstance;
                        ExistingOverride->GPUMaterialInstance = MaterialSet.GPUMaterialInstance;
                        FWCAEditorWidgets::MarkMaterialSlotWettable(WetClothingAssetPtr, MaterialIndex);
                    }
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
                        *MaterialSet.CPUMaterialInstance->GetName(),
                        *MaterialSet.GPUMaterialInstance->GetName(),
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

TSharedRef<SWidget> SWetClothingPartEditorPanel::GenerateUVDisplayModeComboItem(FUVDisplayModeItemPtr Item)
{
    return FWCAEditorWidgets::GenerateUVDisplayModeComboItem(Item);
}

void SWetClothingPartEditorPanel::HandleUVDisplayModeSelectionChanged(FUVDisplayModeItemPtr Item, ESelectInfo::Type SelectInfo)
{
    if (!Item.IsValid())
    {
        return;
    }

    SelectedUVDisplayModeItem = Item;
    CurrentUVDisplayMode = *Item;

    if (UVView.IsValid())
    {
        UVView->SetDisplayMode(CurrentUVDisplayMode);
    }
}

TSharedRef<ITableRow> SWetClothingPartEditorPanel::GenerateUVIslandRow(FUVIslandItemPtr Item, const TSharedRef<STableViewBase>& OwnerTable)
{
    return SNew(STableRow<FUVIslandItemPtr>, OwnerTable)
        .Padding(4.0f)
            [SNew(SHorizontalBox) + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)[SNew(SBox).WidthOverride(18.0f).HeightOverride(18.0f)[SNew(SBorder).BorderImage(FAppStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor_Lambda(
                [this, Item]() -> FSlateColor
                {
                    FLinearColor SwatchColor(0.06f, 0.06f, 0.06f, 1.0f);
                    if (Item.IsValid())
                    {
                        if (const FWetClothingWetPartEntry* EffectiveEntry = FindEffectiveWetPartEntryForUVIsland(Item->UVIslandID))
                        {
                            SwatchColor = EffectiveEntry->WetPartID == 0 ? FLinearColor::White : EffectiveEntry->Color;
                            SwatchColor.A = 1.0f;
                        }
                    }
                    return FSlateColor(SwatchColor);
                })]] + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[SNew(STextBlock).AutoWrapText(true).Text_Lambda(
                [this, Item]()
                {
                    FText RowText = LOCTEXT("InvalidUVIsland", "Invalid UV island");
                    if (Item.IsValid())
                    {
                        if (const FWetClothingWetPartEntry* EffectiveEntry = FindEffectiveWetPartEntryForUVIsland(Item->UVIslandID))
                        {
                            RowText = FText::Format(LOCTEXT("UVIslandAssignedRow", "Island {0} | {1} tris | ID {2}"), FText::AsNumber(Item->UVIslandID), FText::AsNumber(Item->TriangleCount), FText::AsNumber(EffectiveEntry->WetPartID));
                        }
                    }
                    return RowText;
                })]];
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
    const FLinearColor                   Color = Item.IsValid() ? (Item->WetPartID == 0 ? FLinearColor::White : Item->Color) : FLinearColor::White;
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
                                                                              .Text_Lambda([this, Item]()
                                                                                           { return Item.IsValid()
                                                                                                        ? FText::FromString(GetWetPartDisplayName(*Item))
                                                                                                        : LOCTEXT("InvalidWetPartName", "Invalid Part"); })
                                                                              .OnTextCommitted(this, &SWetClothingPartEditorPanel::HandleWetPartNameCommitted, Item)]

                                                               + SHorizontalBox::Slot()
                                                                     .AutoWidth()
                                                                     .VAlign(VAlign_Center)
                                                                     .Padding(8.0f, 0.0f, 0.0f, 0.0f)
                                                                         [SNew(STextBlock)
                                                                              .Text(Item.IsValid()
                                                                                        ? FText::Format(LOCTEXT("WetPartRowIDLabel", "| ID {0}"), FText::AsNumber(Item->WetPartID))
                                                                                        : LOCTEXT("InvalidWetPartIDLabel", "| Invalid"))
                                                                              .ColorAndOpacity(FSlateColor(FLinearColor(0.72f, 0.72f, 0.72f, 1.0f)))]]

                                                    + SVerticalBox::Slot()
                                                          .AutoHeight()
                                                          .Padding(0.0f, 6.0f, 0.0f, 0.0f)
                                                              [SNew(SHorizontalBox)

                                                               + SHorizontalBox::Slot()
                                                                     .FillWidth(1.0f)
                                                                     .VAlign(VAlign_Center)
                                                                         [SNew(SObjectPropertyEntryBox)
                                                                              .AllowedClass(UWetnessProfile::StaticClass())
                                                                              .AllowClear(true)
                                                                              .AllowCreate(false)
                                                                              .DisplayThumbnail(false)
                                                                              .ObjectPath_Lambda([this, Item]()
                                                                                                 {
                                                                                                      const FWetClothingWetPartEntry* Entry = Item.IsValid() ? FindWetPartEntry(Item->WetPartID) : nullptr;
                                                                                                      return Entry != nullptr ? Entry->ProfileAssignment.SourceProfile.ToString() : FString(); })
                                                                              .OnObjectChanged_Lambda([this, Item](const FAssetData& AssetData)
                                                                                                      { HandleWetnessProfilePicked(Item, AssetData); })]]]];

    if (Item.IsValid() && InlineTextBlock.IsValid())
    {
        WetPartInlineRenameWidgets.Add(Item->WetPartID, InlineTextBlock);
    }

    return Row;
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
        return;
    }

    RefreshIslandSelectionViews();
}

void SWetClothingPartEditorPanel::HandleWetPartItemDoubleClicked(FWetPartEntryPtr Item)
{
    if (!Item.IsValid())
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
    if (!Item.IsValid())
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
    MarkSelectedMaterialSlotWettable();

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
    MarkSelectedMaterialSlotWettable();

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
        MarkSelectedMaterialSlotWettable();
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
    if (!Item.IsValid())
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

    if (ProfileAssetData.IsValid())
    {
        if (const UWetnessProfile* SourceProfile = Cast<UWetnessProfile>(ProfileAssetData.GetAsset()))
        {
            Entry->ProfileAssignment.SourceProfile = FSoftObjectPath(SourceProfile);
            Entry->ProfileAssignment.SourceProfileName = SourceProfile->GetName();
            Entry->ProfileAssignment.Parameters = SourceProfile->Parameters;
        }
    }
    else
    {
        Entry->ProfileAssignment.SourceProfile = FSoftObjectPath();
        Entry->ProfileAssignment.SourceProfileName.Reset();
        Entry->ProfileAssignment.Parameters = FWetnessProfileParameters();
    }

    WetClothingAssetPtr->MarkPackageDirty();
    MarkSelectedMaterialSlotWettable();

    RefreshWetPartList(false);
}

TSharedRef<SWidget> SWetClothingPartEditorPanel::GenerateAssignWetPartComboItem(FWetPartEntryPtr Item)
{
    const FLinearColor Color = Item.IsValid() ? (Item->WetPartID == 0 ? FLinearColor::White : Item->Color) : FLinearColor::White;

    return SNew(SHorizontalBox) + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 6.0f, 0.0f)[SNew(SBox).WidthOverride(14.0f).HeightOverride(14.0f)[SNew(SBorder).BorderImage(FAppStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(Color)]]

           + SHorizontalBox::Slot()
                 .FillWidth(1.0f)
                 .VAlign(VAlign_Center)
                     [SNew(STextBlock)
                          .Text(Item.IsValid()
                                    ? FText::Format(
                                          LOCTEXT("AssignWetPartOption", "{0} | ID {1}"),
                                          FText::FromString(GetWetPartDisplayName(*Item)),
                                          FText::AsNumber(Item->WetPartID))
                                    : LOCTEXT("AssignWetPartInvalid", "Invalid Part"))];
}

void SWetClothingPartEditorPanel::HandleAssignWetPartSelectionChanged(FWetPartEntryPtr Item, ESelectInfo::Type SelectInfo)
{
    SelectedAssignWetPartID = Item.IsValid() ? Item->WetPartID : INDEX_NONE;
}

FReply SWetClothingPartEditorPanel::HandleAddWetPartClicked()
{
    UWetClothingAsset* WetClothingAssetPtr = WetClothingAsset.Get();
    if (WetClothingAssetPtr == nullptr || SelectedMaterialSlotIndex == INDEX_NONE || !HasValidOriginalUVChannel())
    {
        return FReply::Handled();
    }

    const int32 NewWetPartID = FindNextWetPartForSelectedScope();
    WetClothingAssetPtr->Modify();

    FWetClothingWetPartEntry NewEntry;
    NewEntry.MaterialSlotIndex = SelectedMaterialSlotIndex;
    NewEntry.UVChannelIndex = GetOriginalUVChannelIndex();
    NewEntry.WetPartID = NewWetPartID;
    NewEntry.DisplayName = GetDefaultWetPartName(NewWetPartID);
    NewEntry.Color = GetDefaultWetPartColor(NewWetPartID);
    NewEntry.bViewEnabled = true;

    WetClothingAssetPtr->Authored.PartData.EditableWetPartData.WetPartEntries.Add(NewEntry);
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
    const int32 UVChannelIndex = GetOriginalUVChannelIndex();
    WetClothingAssetPtr->Modify();
    for (int32 Index = WetClothingAssetPtr->Authored.PartData.EditableWetPartData.WetPartEntries.Num() - 1; Index >= 0; --Index)
    {
        const FWetClothingWetPartEntry& Entry = WetClothingAssetPtr->Authored.PartData.EditableWetPartData.WetPartEntries[Index];
        if (Entry.MaterialSlotIndex == SelectedMaterialSlotIndex && Entry.UVChannelIndex == UVChannelIndex && Entry.WetPartID == SelectedWetPartID)
        {
            WetClothingAssetPtr->Authored.PartData.EditableWetPartData.WetPartEntries.RemoveAt(Index);
            break;
        }
    }
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
    return SelectedWetPartID != INDEX_NONE && SelectedWetPartID != 0;
}

bool SWetClothingPartEditorPanel::IsAutoPartitionEnabled() const
{
    return WetClothingAsset.IsValid() && SelectedMaterialSlotIndex != INDEX_NONE && HasValidOriginalUVChannel() && UVIslandItems.Num() > 0;
}

bool SWetClothingPartEditorPanel::HasAutoPartitionDataToReplace() const
{
    if (const UWetClothingAsset* WetClothingAssetPtr = WetClothingAsset.Get())
    {
        const int32 UVChannelIndex = GetOriginalUVChannelIndex();
        for (const FWetClothingWetPartEntry& Entry : WetClothingAssetPtr->Authored.PartData.EditableWetPartData.WetPartEntries)
        {
            if (Entry.MaterialSlotIndex == SelectedMaterialSlotIndex && Entry.UVChannelIndex == UVChannelIndex && Entry.WetPartID != 0)
            {
                return true;
            }
        }
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

    const int32 UVChannelIndex = GetOriginalUVChannelIndex();
    WetClothingAssetPtr->Modify();
    MarkSelectedMaterialSlotWettable();

    for (int32 EntryIndex = WetClothingAssetPtr->Authored.PartData.EditableWetPartData.WetPartEntries.Num() - 1; EntryIndex >= 0; --EntryIndex)
    {
        const FWetClothingWetPartEntry& Entry = WetClothingAssetPtr->Authored.PartData.EditableWetPartData.WetPartEntries[EntryIndex];
        if (Entry.MaterialSlotIndex == SelectedMaterialSlotIndex && Entry.UVChannelIndex == UVChannelIndex && Entry.WetPartID != 0)
        {
            WetClothingAssetPtr->Authored.PartData.EditableWetPartData.WetPartEntries.RemoveAt(EntryIndex);
        }
    }

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

        FWetClothingWetPartEntry NewEntry;
        NewEntry.MaterialSlotIndex = SelectedMaterialSlotIndex;
        NewEntry.UVChannelIndex = UVChannelIndex;
        NewEntry.WetPartID = NewWetPartID;
        NewEntry.DisplayName = GetDefaultWetPartName(NewWetPartID);
        NewEntry.Color = GetDefaultWetPartColor(NewWetPartID);
        NewEntry.bViewEnabled = true;
        NewEntry.AssignedUVIslandIDs = Clusters[ClusterIndex].UVIslandIDs;
        WetClothingAssetPtr->Authored.PartData.EditableWetPartData.WetPartEntries.Add(NewEntry);
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
    if (WetClothingAssetPtr == nullptr || SelectedMaterialSlotIndex == INDEX_NONE || SelectedUVIslandIDs.Num() == 0 || SelectedAssignWetPartID == INDEX_NONE)
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

    const int32 UVChannelIndex = GetOriginalUVChannelIndex();
    WetClothingAssetPtr->Modify();
    MarkSelectedMaterialSlotWettable();
    for (FWetClothingWetPartEntry& Entry : WetClothingAssetPtr->Authored.PartData.EditableWetPartData.WetPartEntries)
    {
        if (Entry.MaterialSlotIndex == SelectedMaterialSlotIndex && Entry.UVChannelIndex == UVChannelIndex)
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

FText SWetClothingPartEditorPanel::GetMaterialSlotCountText() const
{
    int32 SlotCount = 0;
    for (const FMaterialSlotItemPtr& Item : MaterialSlotItems)
    {
        if (Item.IsValid() && Item->SlotIndex != INDEX_NONE)
        {
            ++SlotCount;
        }
    }

    return FText::Format(
        LOCTEXT("MaterialSlotCount", "{0} Slots"),
        FText::AsNumber(SlotCount));
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

FText SWetClothingPartEditorPanel::GetSelectedUVDisplayModeText() const
{
    return FWCAEditorWidgets::GetUVDisplayModeLabel(
        SelectedUVDisplayModeItem.IsValid() ? *SelectedUVDisplayModeItem : EWCAUVDisplayMode::Normal);
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

FText SWetClothingPartEditorPanel::GetWetnessProfileMapBakeSourceText() const
{
    UTexture* SourceTexture = ResolveSelectedMaterialTexture();
    if (SourceTexture == nullptr)
    {
        return LOCTEXT("WetnessProfileMapBakeNoSource", "Source Texture: None");
    }

    return FText::Format(
        LOCTEXT("WetnessProfileMapBakeSource", "Source Texture: {0} / UV Channel {1}"),
        FText::FromString(SourceTexture->GetName()),
        FText::AsNumber(GetOriginalUVChannelIndex()));
}

FText SWetClothingPartEditorPanel::GetWetnessProfileMapBakeSlotsText() const
{
    TArray<int32> MaterialSlotIndices;
    CollectMaterialSlotsForWetnessProfileMap(ResolveSelectedMaterialTexture(), GetOriginalUVChannelIndex(), MaterialSlotIndices);

    if (MaterialSlotIndices.Num() == 0)
    {
        return LOCTEXT("WetnessProfileMapBakeNoSlots", "Material Slots: None");
    }

    TArray<FString> SlotLabels;
    SlotLabels.Reserve(MaterialSlotIndices.Num());
    for (const int32 MaterialSlotIndex : MaterialSlotIndices)
    {
        SlotLabels.Add(FString::Printf(TEXT("%d"), MaterialSlotIndex));
    }

    return FText::Format(
        LOCTEXT("WetnessProfileMapBakeSlots", "Material Slots: {0}"),
        FText::FromString(FString::Join(SlotLabels, TEXT(", "))));
}

FText SWetClothingPartEditorPanel::GetWetnessProfileMapBakeStatusText() const
{
    UTexture* SourceTexture = ResolveSelectedMaterialTexture();
    if (SourceTexture == nullptr)
    {
        return LOCTEXT("WetnessProfileMapBakeStatusNoSource", "Select a material texture to prepare a texture-level Wetness Profile Map.");
    }

    const FWetClothingBakedWetnessProfileMap* BakedWetnessProfileMap = FindBakedWetnessProfileMap(SourceTexture, GetOriginalUVChannelIndex());
    if (BakedWetnessProfileMap == nullptr)
    {
        return LOCTEXT("WetnessProfileMapBakeStatusNotBaked", "Status: Not baked yet. Phase 2 will generate Wetness Profile Map 0 for this texture.");
    }

    if (BakedWetnessProfileMap->WetnessProfileMap0 == nullptr)
    {
        return LOCTEXT("WetnessProfileMapBakeStatusMissingTexture", "Status: Bake entry exists, but Wetness Profile Map 0 is missing.");
    }

    return FText::Format(
        LOCTEXT("WetnessProfileMapBakeStatusReady", "Status: {0}"),
        FText::FromString(BakedWetnessProfileMap->WetnessProfileMap0->GetName()));
}

FText SWetClothingPartEditorPanel::GetWetnessProfileMapBakeSettingsText() const
{
    const FWetClothingBakedWetnessProfileMap* BakedWetnessProfileMap =
        FindBakedWetnessProfileMap(ResolveSelectedMaterialTexture(), GetOriginalUVChannelIndex());
    const int32 Resolution = DWCWetnessProfileMapBake::Resolution;
    const int32 PaddingPixels = DWCWetnessProfileMapBake::PaddingPixels;

    if (BakedWetnessProfileMap != nullptr && BakedWetnessProfileMap->WetnessProfileMap0 != nullptr)
    {
        return FText::Format(
            LOCTEXT("WetnessProfileMapBakeSettingsWithSize", "Settings: Max {0} px / Output {1}x{2} / Padding {3} px"),
            FText::AsNumber(Resolution),
            FText::AsNumber(BakedWetnessProfileMap->WetnessProfileMap0->GetSurfaceWidth()),
            FText::AsNumber(BakedWetnessProfileMap->WetnessProfileMap0->GetSurfaceHeight()),
            FText::AsNumber(PaddingPixels));
    }

    return FText::Format(
        LOCTEXT("WetnessProfileMapBakeSettings", "Settings: Max {0} px / Padding {1} px"),
        FText::AsNumber(Resolution),
        FText::AsNumber(PaddingPixels));
}

FText SWetClothingPartEditorPanel::GetUVIslandCountText() const
{
    return FText::Format(
        LOCTEXT("UVIslandCount", "Island Count: {0}"),
        FText::AsNumber(UVIslandItems.Num()));
}

FText SWetClothingPartEditorPanel::GetSelectedUVIslandText() const
{
    if (SelectedUVIslandIDs.Num() == 0)
    {
        return LOCTEXT("NoUVIslandSelected", "Select UV islands from the list, UV view, or 3D preview. Hold Shift for multi-select.");
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

FText SWetClothingPartEditorPanel::GetUVStatusText() const
{
    return FText::FromString(UVStatusMessage);
}

FText SWetClothingPartEditorPanel::GetWetPartSectionText() const
{
    if (SelectedMaterialSlotIndex == INDEX_NONE)
    {
        return LOCTEXT("WetPartSectionNoSlot", "Part Map: No slot");
    }

    return FText::Format(
        LOCTEXT("WetPartSection", "Part Map / Slot {0}"),
        FText::AsNumber(SelectedMaterialSlotIndex));
}

FText SWetClothingPartEditorPanel::GetAssignUVIslandToWetPartText() const
{
    return LOCTEXT("AssignSelectedIslands", "Assign");
}

FText SWetClothingPartEditorPanel::GetSelectedAssignWetPartText() const
{
    if (const FWetPartEntryPtr Item = FindWetPartItemByID(SelectedAssignWetPartID))
    {
        return FText::Format(
            LOCTEXT("SelectedAssignWetPart", "{0} | ID {1}"),
            FText::FromString(GetWetPartDisplayName(*Item)),
            FText::AsNumber(Item->WetPartID));
    }

    return LOCTEXT("SelectedAssignWetPartNone", "Select Part");
}

FSlateColor SWetClothingPartEditorPanel::GetSelectedAssignWetPartColor() const
{
    if (const FWetPartEntryPtr Item = FindWetPartItemByID(SelectedAssignWetPartID))
    {
        return FSlateColor(Item->WetPartID == 0 ? FLinearColor::White : Item->Color);
    }

    return FSlateColor(FLinearColor(0.08f, 0.08f, 0.08f, 1.0f));
}

FText SWetClothingPartEditorPanel::GetSelectedWetPartText() const
{
    if (SelectedWetPartID == INDEX_NONE)
    {
        return LOCTEXT("SelectedWetPartNone", "Selected Part: None. Pick a row to select all islands assigned to that ID.");
    }

    if (const FWetClothingWetPartEntry* Entry = FindWetPartEntry(SelectedWetPartID))
    {
        return FText::Format(
            LOCTEXT("SelectedWetPart", "Selected Part: {0}  |  Double-click name to rename"),
            FText::FromString(GetWetPartDisplayName(*Entry)));
    }

    return LOCTEXT("SelectedWetPartInvalid", "Selected Part: Invalid");
}

FText SWetClothingPartEditorPanel::GetWetnessProfileLibraryStatusText() const
{
    return LOCTEXT("WetnessProfileLibraryStatus", "Select any Wetness Profile from project or enabled plugin content.");
}

FText SWetClothingPartEditorPanel::GetBlendModeText(FWetPartEntryPtr Item) const
{
    if (!Item.IsValid())
    {
        return LOCTEXT("InvalidBlendMode", "Standard");
    }

    const UEnum* BlendModeEnum = StaticEnum<EWetPartProfileBlendMode>();
    return BlendModeEnum != nullptr
               ? BlendModeEnum->GetDisplayNameTextByValue(static_cast<int64>(Item->ProfileAssignment.BlendMode))
               : LOCTEXT("BlendModeFallback", "Standard");
}

FText SWetClothingPartEditorPanel::GetWetnessProfileButtonText(FWetPartEntryPtr Item) const
{
    return Item.IsValid()
               ? FText::FromString(GetAssignedProfileLabel(*Item))
               : LOCTEXT("NoProfileSelected", "Select Profile");
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
    return HandleBakeAllWetnessProfileMapsClicked();
}

bool SWetClothingPartEditorPanel::IsWetnessProfileMapBakeSourceValid() const
{
    return WetClothingAsset.IsValid() && WetClothingAsset->GetRuntimeSkeletalMesh() != nullptr &&
           ResolveSelectedMaterialTexture() != nullptr && HasValidOriginalUVChannel();
}

bool SWetClothingPartEditorPanel::CanBakeAnyWetnessProfileMap() const
{
    TArray<UTexture*> SourceTextures;
    CollectWetnessProfileMapSourceTextures(GetOriginalUVChannelIndex(), SourceTextures);
    return WetClothingAsset.IsValid() && WetClothingAsset->GetRuntimeSkeletalMesh() != nullptr &&
           HasValidOriginalUVChannel() && SourceTextures.Num() > 0;
}

FReply SWetClothingPartEditorPanel::HandleBakeSelectedWetnessProfileMapClicked()
{
    UWetClothingAsset* WetClothingAssetPtr = WetClothingAsset.Get();
    UTexture*          SourceTexture = ResolveSelectedMaterialTexture();
    if (WetClothingAssetPtr == nullptr || SourceTexture == nullptr)
    {
        return FReply::Handled();
    }

    const int32 UVChannelIndex = GetOriginalUVChannelIndex();

    TArray<int32> MaterialSlotIndices;
    CollectMaterialSlotsForWetnessProfileMap(SourceTexture, UVChannelIndex, MaterialSlotIndices);
    if (MaterialSlotIndices.Num() == 0)
    {
        FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("NoWetnessProfileMapBakeSlots", "No material slots were found for the selected texture."));
        return FReply::Handled();
    }

    const FWetClothingWetnessProfileMapBakeSettings Settings;

    FWetClothingWetnessProfileMapBakeResult Result;
    FString                                 ErrorMessage;
    if (!FWetClothingWetnessProfileMapBaker::BakeWetnessProfileMap0(
            WetClothingAssetPtr,
            SourceTexture,
            UVChannelIndex,
            MaterialSlotIndices,
            Settings,
            Result,
            ErrorMessage))
    {
        FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(ErrorMessage));
        return FReply::Handled();
    }

    UE_LOG(
        LogTemp,
        Display,
        TEXT("DWC: Baked Wetness Profile Map 0 '%s' for texture '%s' (%d painted pixels)."),
        *GetNameSafe(Result.WetnessProfileMap0.Get()),
        *GetNameSafe(SourceTexture),
        Result.PaintedPixelCount);

    if (DetailsView.IsValid())
    {
        DetailsView->ForceRefresh();
    }

    return FReply::Handled();
}

FReply SWetClothingPartEditorPanel::HandleBakeAllWetnessProfileMapsClicked()
{
    UWetClothingAsset* WetClothingAssetPtr = WetClothingAsset.Get();
    if (WetClothingAssetPtr == nullptr)
    {
        return FReply::Handled();
    }

    const int32 UVChannelIndex = GetOriginalUVChannelIndex();

    TArray<UTexture*> SourceTextures;
    CollectWetnessProfileMapSourceTextures(UVChannelIndex, SourceTextures);
    if (SourceTextures.Num() == 0)
    {
        FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("NoWetnessProfileMapBakeSources", "No source textures were found for the selected UV channel."));
        return FReply::Handled();
    }

    int32 BakedCount = 0;
    for (UTexture* SourceTexture : SourceTextures)
    {
        if (SourceTexture == nullptr)
        {
            continue;
        }

        TArray<int32> MaterialSlotIndices;
        CollectMaterialSlotsForWetnessProfileMap(SourceTexture, UVChannelIndex, MaterialSlotIndices);
        if (MaterialSlotIndices.Num() == 0)
        {
            continue;
        }

        const FWetClothingWetnessProfileMapBakeSettings Settings;

        FWetClothingWetnessProfileMapBakeResult Result;
        FString                                 ErrorMessage;
        if (!FWetClothingWetnessProfileMapBaker::BakeWetnessProfileMap0(
                WetClothingAssetPtr,
                SourceTexture,
                UVChannelIndex,
                MaterialSlotIndices,
                Settings,
                Result,
                ErrorMessage))
        {
            FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(ErrorMessage));
            return FReply::Handled();
        }

        ++BakedCount;
    }

    UE_LOG(LogTemp, Display, TEXT("DWC: Baked %d Wetness Profile Map texture(s)."), BakedCount);

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

UTexture* SWetClothingPartEditorPanel::FindSavedTextureSelection(int32 MaterialSlotIndex, int32 UVChannelIndex) const
{
    return FWetClothingMaterialTextureResolver::FindSavedTextureSelection(WetClothingAsset.Get(), MaterialSlotIndex, UVChannelIndex);
}

UTexture* SWetClothingPartEditorPanel::ResolveOrSaveTextureSelection(int32 MaterialSlotIndex, int32 UVChannelIndex)
{
    return FWetClothingMaterialTextureResolver::ResolveOrSaveTextureSelection(WetClothingAsset.Get(), MaterialSlotIndex, UVChannelIndex);
}

bool SWetClothingPartEditorPanel::HasSavedTextureSelection(int32 MaterialSlotIndex, int32 UVChannelIndex) const
{
    return FWetClothingMaterialTextureResolver::HasSavedTextureSelection(WetClothingAsset.Get(), MaterialSlotIndex, UVChannelIndex);
}

void SWetClothingPartEditorPanel::SaveTextureSelection(int32 MaterialSlotIndex, int32 UVChannelIndex, UTexture* Texture)
{
    FWetClothingMaterialTextureResolver::SaveTextureSelection(WetClothingAsset.Get(), MaterialSlotIndex, UVChannelIndex, Texture);
}

bool SWetClothingPartEditorPanel::HasPendingVisualBakeTasks(FString* OutSummary) const
{
    return FWetClothingWetnessProfileMapBakeService::HasPendingVisualBakeTasks(WetClothingAsset.Get(), OutSummary);
}

bool SWetClothingPartEditorPanel::BakeWetnessProfileMapsAndUpdateMaterials(FString& OutSummary, bool* OutHadWarnings)
{
    const bool bSucceeded = FWetClothingWetnessProfileMapBakeService::BakeWetnessProfileMapsAndUpdateMaterials(WetClothingAsset.Get(), OutSummary, OutHadWarnings);
    if (DetailsView.IsValid())
    {
        DetailsView->ForceRefresh();
    }

    return bSucceeded;
}

bool SWetClothingPartEditorPanel::SaveBakedWetnessAssets() const
{
    return FWetClothingWetnessProfileMapBakeService::SaveBakedWetnessAssets(WetClothingAsset.Get());
}

const FWetClothingBakedWetnessProfileMap* SWetClothingPartEditorPanel::FindBakedWetnessProfileMap(UTexture* SourceTexture, int32 UVChannelIndex) const
{
    const UWetClothingAsset* WetClothingAssetPtr = WetClothingAsset.Get();
    if (WetClothingAssetPtr == nullptr || SourceTexture == nullptr || UVChannelIndex == INDEX_NONE)
    {
        return nullptr;
    }

    return WetClothingAssetPtr->Derived.Inline.BakedWetnessProfileMaps.FindByPredicate(
        [SourceTexture, UVChannelIndex](const FWetClothingBakedWetnessProfileMap& BakedWetnessProfileMap)
        {
            return BakedWetnessProfileMap.SourceTexture == SourceTexture &&
                   BakedWetnessProfileMap.UVChannelIndex == UVChannelIndex;
        });
}

void SWetClothingPartEditorPanel::CollectMaterialSlotsForWetnessProfileMap(UTexture* SourceTexture, int32 UVChannelIndex, TArray<int32>& OutMaterialSlotIndices) const
{
    OutMaterialSlotIndices.Reset();

    const UWetClothingAsset* WetClothingAssetPtr = WetClothingAsset.Get();
    if (WetClothingAssetPtr == nullptr || SourceTexture == nullptr || UVChannelIndex == INDEX_NONE)
    {
        return;
    }

    for (const FWetClothingSourceTextureSelection& Selection : WetClothingAssetPtr->Authored.PartData.EditableWetPartData.SourceTextureSelections)
    {
        if (Selection.Texture == SourceTexture &&
            Selection.UVChannelIndex == UVChannelIndex &&
            Selection.MaterialSlotIndex != INDEX_NONE &&
            FWCAEditorWidgets::IsMaterialSlotWettable(WetClothingAssetPtr, Selection.MaterialSlotIndex))
        {
            OutMaterialSlotIndices.AddUnique(Selection.MaterialSlotIndex);
        }
    }

    OutMaterialSlotIndices.Sort();
}

void SWetClothingPartEditorPanel::CollectWetnessProfileMapSourceTextures(int32 UVChannelIndex, TArray<UTexture*>& OutSourceTextures) const
{
    OutSourceTextures.Reset();

    if (UVChannelIndex == INDEX_NONE)
    {
        return;
    }

    const UWetClothingAsset* WetClothingAssetPtr = WetClothingAsset.Get();
    if (WetClothingAssetPtr == nullptr)
    {
        return;
    }

    for (const FWetClothingSourceTextureSelection& Selection : WetClothingAssetPtr->Authored.PartData.EditableWetPartData.SourceTextureSelections)
    {
        if (Selection.UVChannelIndex == UVChannelIndex && Selection.Texture != nullptr)
        {
            OutSourceTextures.AddUnique(Selection.Texture);
        }
    }
}

void SWetClothingPartEditorPanel::SaveSelectedTexture()
{
    SaveTextureSelection(SelectedMaterialSlotIndex, GetOriginalUVChannelIndex(), ResolveSelectedMaterialTexture());
}

#undef LOCTEXT_NAMESPACE
