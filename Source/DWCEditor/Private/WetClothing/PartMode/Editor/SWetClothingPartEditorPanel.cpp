#include "SWetClothingPartEditorPanel.h"

#include "AssetThumbnail.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Core/DWCEditorUtils.h"
#include "Core/DWCEditorStyle.h"
#include "WetClothing/PartMode/Partition/WetPartAutoPartitioner.h"
#include "WetClothing/Common/Material/WetClothingMaterialSetup.h"
#include "WetClothing/PartMode/WetnessProfileMap/WetClothingWetnessProfileMapBaker.h"
#include "WetClothing/Common/Texture/WetClothingMaterialTextureResolver.h"
#include "WetClothing/Common/Texture/WetClothingTextureReadback.h"
#include "WetClothing/Common/Widgets/SWetClothingAssetUVView.h"
#include "WetClothing/Common/Widgets/WetClothingEditorCommonWidgets.h"
#include "WetClothing/PartMode/Widgets/SWetPartAutoPartitionControls.h"
#include "DataAssets/WetClothingAsset.h"
#include "WetClothing/Common/Analysis/WetClothingAssetMeshAnalyzer.h"
#include "WetClothing/Common/Viewport/WetClothingAssetViewport.h"
#include "DataAssets/WetnessProfile.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "IDetailsView.h"
#include "FileHelpers.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
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

#define LOCTEXT_NAMESPACE "WetClothingAssetEditorPanel"

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

    TArray<FWetClothingAssetUVTriangle> BuildMaterialSlotPreviewTriangles(const USkeletalMesh* SkeletalMesh, int32 MaterialSlotIndex)
    {
        TArray<FWetClothingAssetUVTriangle> PreviewTriangles;

        if (SkeletalMesh == nullptr || FWetClothingAssetMeshAnalyzer::GetNumUVChannels(SkeletalMesh, 0) <= 0)
        {
            return PreviewTriangles;
        }

        TArray<FWetClothingAssetUVIsland> BuiltIslands;
        if (!FWetClothingAssetMeshAnalyzer::BuildMaterialSlotUVIslands(SkeletalMesh, 0, 0, MaterialSlotIndex, BuiltIslands, nullptr))
        {
            return PreviewTriangles;
        }

        for (const FWetClothingAssetUVIsland& Island : BuiltIslands)
        {
            PreviewTriangles.Append(Island.UVTriangles);
        }

        return PreviewTriangles;
    }

    bool IsWetPartEntryRelevantForWetSetup(const FWetClothingWetPartEntry& Entry)
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
    auto AddSelectionToolItem = [this](EWetClothingAssetUVSelectionTool Tool, const FText& Label, const FText& Tooltip, const FName IconBrushName)
    {
        FUVSelectionToolItemPtr ToolItem = MakeShared<FWetClothingUVSelectionToolItem>();
        ToolItem->Tool = Tool;
        ToolItem->Label = Label;
        ToolItem->Tooltip = Tooltip;
        ToolItem->IconBrushDisplayName = IconBrushName;
        UVSelectionToolItems.Add(ToolItem);
        return ToolItem;
    };

    FUVSelectionToolItemPtr SelectToolItem = AddSelectionToolItem(
        EWetClothingAssetUVSelectionTool::Select,
        LOCTEXT("UVSelectionToolSelect", "Select"),
        LOCTEXT("UVSelectionToolSelectTooltip", "Click a UV island to select it. Hold Shift to add to the current selection."),
        TEXT("DWCEditor.UVTool.Select"));
    AddSelectionToolItem(
        EWetClothingAssetUVSelectionTool::BoxSelect,
        LOCTEXT("UVSelectionToolBoxSelect", "Box Select"),
        LOCTEXT("UVSelectionToolBoxSelectTooltip", "Drag a box to select UV islands. Hold Shift to add to the current selection."),
        TEXT("DWCEditor.UVTool.BoxSelect"));
    AddSelectionToolItem(
        EWetClothingAssetUVSelectionTool::EllipseSelect,
        LOCTEXT("UVSelectionToolEllipseSelect", "Ellipse Select"),
        LOCTEXT("UVSelectionToolEllipseSelectTooltip", "Drag an ellipse to select UV islands. Hold Shift to add to the current selection."),
        TEXT("DWCEditor.UVTool.EllipseSelect"));
    AddSelectionToolItem(
        EWetClothingAssetUVSelectionTool::LassoSelect,
        LOCTEXT("UVSelectionToolLassoSelect", "Lasso Select"),
        LOCTEXT("UVSelectionToolLassoSelectTooltip", "Draw a freeform lasso to select UV islands. Hold Shift to add to the current selection."),
        TEXT("DWCEditor.UVTool.LassoSelect"));

    SelectedUVSelectionToolItem = SelectToolItem;
    CurrentUVSelectionTool = EWetClothingAssetUVSelectionTool::Select;
    UVDisplayModeItems.Reset();
    UVDisplayModeItems.Add(MakeShared<EWetClothingAssetUVDisplayMode>(EWetClothingAssetUVDisplayMode::Normal));
    UVDisplayModeItems.Add(MakeShared<EWetClothingAssetUVDisplayMode>(EWetClothingAssetUVDisplayMode::OutlineOnly));
    SelectedUVDisplayModeItem = UVDisplayModeItems[0];
    CurrentUVDisplayMode = EWetClothingAssetUVDisplayMode::Normal;

    AutoPartitionColorModeItems.Reset();
    AutoPartitionColorModeItems.Add(MakeShared<EWetPartAutoPartitionColorMode>(EWetPartAutoPartitionColorMode::AverageColor));
    AutoPartitionColorModeItems.Add(MakeShared<EWetPartAutoPartitionColorMode>(EWetPartAutoPartitionColorMode::MedianColor));
    AutoPartitionColorModeItems.Add(MakeShared<EWetPartAutoPartitionColorMode>(EWetPartAutoPartitionColorMode::DominantColor));
    AutoPartitionColorModeItems.Add(MakeShared<EWetPartAutoPartitionColorMode>(EWetPartAutoPartitionColorMode::KMeansColor));
    SelectedAutoPartitionColorModeItem = AutoPartitionColorModeItems[2];
    AutoPartitionColorMode = EWetPartAutoPartitionColorMode::DominantColor;

    auto BuildSelectionToolButton = [this](FUVSelectionToolItemPtr ToolItem)
    {
        return SNew(SButton)
            .ButtonColorAndOpacity(this, &SWetClothingPartEditorPanel::GetUVSelectionToolButtonColor, ToolItem)
            .ContentPadding(FMargin(2.0f))
            .HAlign(HAlign_Center)
            .VAlign(VAlign_Center)
            .OnClicked(this, &SWetClothingPartEditorPanel::HandleUVSelectionToolButtonClicked, ToolItem)
            .ToolTipText(ToolItem->Tooltip)
                [SNew(SBox)
                     .WidthOverride(18.0f)
                     .HeightOverride(18.0f)
                     .HAlign(HAlign_Center)
                     .VAlign(VAlign_Center)
                         [SNew(SImage)
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
                                   .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                                       [DetailsView.IsValid()
                                            ? StaticCastSharedRef<SWidget>(DetailsView.ToSharedRef())
                                            : StaticCastSharedRef<SWidget>(
                                                  SNew(STextBlock)
                                                      .Text(LOCTEXT("MissingDetails", "Details view is unavailable.")))]

                             + SVerticalBox::Slot()
                                   .AutoHeight()
                                   .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                                       [SAssignNew(UVChannelComboBox, SComboBox<FUVChannelItemPtr>)
                                            .OptionsSource(&UVChannelItems)
                                            .OnGenerateWidget(this, &SWetClothingPartEditorPanel::GenerateUVChannelComboItem)
                                            .OnSelectionChanged(this, &SWetClothingPartEditorPanel::HandleUVChannelSelectionChanged)
                                                [SNew(STextBlock)
                                                     .Text(this, &SWetClothingPartEditorPanel::GetSelectedUVChannelText)]]

                             + SVerticalBox::Slot()
                                   .AutoHeight()
                                   .Padding(0.0f, 0.0f, 0.0f, 16.0f)
                                       [SNew(SHorizontalBox)

                                        + SHorizontalBox::Slot()
                                              .AutoWidth()
                                              .VAlign(VAlign_Center)
                                              .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                                                  [SNew(STextBlock)
                                                       .Text(LOCTEXT("WetnessProfileMapResolutionLabel", "Wetness Profile Map Resolution"))]

                                        + SHorizontalBox::Slot()
                                              .AutoWidth()
                                              .VAlign(VAlign_Center)
                                                  [SNew(SBorder)
                                                       .Padding(FMargin(8.0f, 3.0f))
                                                       .BorderImage(FAppStyle::Get().GetBrush(TEXT("ToolPanel.GroupBorder")))
                                                           [SNew(STextBlock)
                                                                .Text(LOCTEXT("WetnessProfileMapResolutionPlaceholder", "512 x 512"))
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
                                                  [SNew(SHorizontalBox)

                                                   + SHorizontalBox::Slot()
                                                         .FillWidth(1.0f)
                                                             [SNew(SBorder)
                                                                  .Padding(6.0f)
                                                                  .BorderImage(FAppStyle::Get().GetBrush(TEXT("ToolPanel.GroupBorder")))
                                                                      [SAssignNew(TextureSelectionContainer, SBox)]]

                                                   + SHorizontalBox::Slot()
                                                         .AutoWidth()
                                                         .VAlign(VAlign_Center)
                                                         .Padding(10.0f, 0.0f, 4.0f, 0.0f)
                                                             [SNew(STextBlock)
                                                                  .Text(LOCTEXT("UVSelectionToolLabel", "Tool:"))]

                                                   + SHorizontalBox::Slot()
                                                         .AutoWidth()
                                                             [SelectionToolButtonRow]

                                                   + SHorizontalBox::Slot()
                                                         .AutoWidth()
                                                         .VAlign(VAlign_Center)
                                                         .Padding(10.0f, 0.0f, 4.0f, 0.0f)
                                                             [SNew(STextBlock)
                                                                  .Text(LOCTEXT("UVDisplayModeLabel", "View:"))]

                                                   + SHorizontalBox::Slot()
                                                         .AutoWidth()
                                                             [SAssignNew(UVDisplayModeComboBox, SComboBox<FUVDisplayModeItemPtr>)
                                                                  .OptionsSource(&UVDisplayModeItems)
                                                                  .InitiallySelectedItem(SelectedUVDisplayModeItem)
                                                                  .OnGenerateWidget(this, &SWetClothingPartEditorPanel::GenerateUVDisplayModeComboItem)
                                                                  .OnSelectionChanged(this, &SWetClothingPartEditorPanel::HandleUVDisplayModeSelectionChanged)
                                                                      [SNew(STextBlock)
                                                                           .Text(this, &SWetClothingPartEditorPanel::GetSelectedUVDisplayModeText)]]

                                                   + SHorizontalBox::Slot()
                                                         .AutoWidth()
                                                         .VAlign(VAlign_Center)
                                                         .Padding(10.0f, 0.0f, 0.0f, 0.0f)
                                                             [SNew(SWetPartAutoPartitionControls)
                                                                  .IsAutoPartitionEnabled(this, &SWetClothingPartEditorPanel::IsAutoPartitionEnabled)
                                                                  .OnAutoPartitionClicked(this, &SWetClothingPartEditorPanel::HandleAutoPartitionClicked)]]

                                        + SVerticalBox::Slot()
                                              .FillHeight(1.0f)
                                                  [SAssignNew(UVView, SWetClothingAssetUVView)
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
                   [FWetClothingEditorCommonWidgets::BuildPreviewSection(
                       SAssignNew(PreviewViewport, SWetClothingAssetViewport)
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
    RefreshUVChannels();
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
    SelectedMaterialSlotIndex = INDEX_NONE;

    if (const UWetClothingAsset* WetClothingAssetPtr = WetClothingAsset.Get())
    {
        if (const USkeletalMesh* TargetMesh = WetClothingAssetPtr->TargetMesh)
        {
            const TArray<FSkeletalMaterial>& Materials = TargetMesh->GetMaterials();

            for (int32 MaterialIndex = 0; MaterialIndex < Materials.Num(); ++MaterialIndex)
            {
                const FSkeletalMaterial& SkeletalMaterial = Materials[MaterialIndex];

                FMaterialSlotItemPtr Item = MakeShared<FWetClothingMaterialSlotItem>();
                Item->SlotIndex = MaterialIndex;
                Item->SlotName = SkeletalMaterial.MaterialSlotName;
                Item->Material = SkeletalMaterial.MaterialInterface;
                Item->bIsWettableSlot = FWetClothingEditorCommonWidgets::IsMaterialSlotWettable(WetClothingAssetPtr, MaterialIndex);
                MaterialSlotItems.Add(Item);
            }
        }
    }

    if (MaterialSlotItems.IsValidIndex(PreviousSelection))
    {
        SelectedMaterialSlotIndex = PreviousSelection;
    }

    if (MaterialSlotListView.IsValid())
    {
        MaterialSlotListView->RequestListRefresh();

        if (MaterialSlotItems.IsValidIndex(SelectedMaterialSlotIndex))
        {
            MaterialSlotListView->SetSelection(MaterialSlotItems[SelectedMaterialSlotIndex], ESelectInfo::Direct);
        }
        else
        {
            MaterialSlotListView->ClearSelection();
        }
    }
}

void SWetClothingPartEditorPanel::RefreshMaterialTextures()
{
    const int32 UVChannelIndex = GetSelectedUVChannelIndex();
    const bool  bHasSavedSelection = HasSavedTextureSelection(SelectedMaterialSlotIndex, UVChannelIndex);
    UTexture*   SavedTexture = FindSavedTextureSelection(SelectedMaterialSlotIndex, UVChannelIndex);
    TextureItems.Reset();
    TextureThumbnails.Reset();
    SelectedTextureItem.Reset();

    if (MaterialSlotItems.IsValidIndex(SelectedMaterialSlotIndex))
    {
        const FMaterialSlotItemPtr& MaterialSlotItem = MaterialSlotItems[SelectedMaterialSlotIndex];
        if (MaterialSlotItem.IsValid() && MaterialSlotItem->Material.IsValid())
        {
            FWetClothingMaterialTextureResolver::BuildTextureItems(MaterialSlotItem->Material.Get(), TextureItems);

            if (bHasSavedSelection)
            {
                for (const FTextureItemPtr& TextureItem : TextureItems)
                {
                    if (TextureItem.IsValid() && TextureItem->Texture.Get() == SavedTexture)
                    {
                        SelectedTextureItem = TextureItem;
                        break;
                    }
                }
            }
            else
            {
                for (const FTextureItemPtr& TextureItem : TextureItems)
                {
                    if (TextureItem.IsValid() && TextureItem->Texture.IsValid())
                    {
                        SelectedTextureItem = TextureItem;
                        break;
                    }
                }
            }
        }
    }

    if (!SelectedTextureItem.IsValid() && TextureItems.Num() > 0)
    {
        SelectedTextureItem = TextureItems[0];
    }

    if (!bHasSavedSelection && SelectedTextureItem.IsValid() && SelectedTextureItem->Texture.IsValid())
    {
        SaveTextureSelection(SelectedMaterialSlotIndex, UVChannelIndex, SelectedTextureItem->Texture.Get());
    }

    bShowMaterialTextureInUVView = SelectedTextureItem.IsValid() && SelectedTextureItem->Texture.IsValid();

    RefreshTextureToggleWidgets();
    RefreshUVView();
}

void SWetClothingPartEditorPanel::RefreshTextureToggleWidgets()
{
    TextureThumbnails.Reset();

    if (!TextureSelectionContainer.IsValid())
    {
        return;
    }

    if (!MaterialSlotItems.IsValidIndex(SelectedMaterialSlotIndex))
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
        SAssignNew(TextureComboBox, SComboBox<FTextureItemPtr>)
            .OptionsSource(&TextureItems)
            .InitiallySelectedItem(SelectedTextureItem)
            .OnGenerateWidget(this, &SWetClothingPartEditorPanel::GenerateTextureComboItem)
            .OnSelectionChanged(this, &SWetClothingPartEditorPanel::HandleTextureSelectionChanged)
            .MaxListHeight(360.0f)
            .ContentPadding(FMargin(6.0f, 4.0f))
                [SAssignNew(SelectedTextureComboContentBox, SBox)
                     [BuildTextureComboContent(SelectedTextureItem, 24.0f, true)]]);
}

void SWetClothingPartEditorPanel::RefreshUVChannels()
{
    const int32 PreviousUVChannelIndex = SelectedUVChannelItem.IsValid() ? *SelectedUVChannelItem : INDEX_NONE;

    UVChannelItems.Reset();
    SelectedUVChannelItem.Reset();

    if (const UWetClothingAsset* WetClothingAssetPtr = WetClothingAsset.Get())
    {
        const int32 NumUVChannels = FWetClothingAssetMeshAnalyzer::GetNumUVChannels(WetClothingAssetPtr->TargetMesh, 0);
        for (int32 UVChannelIndex = 0; UVChannelIndex < NumUVChannels; ++UVChannelIndex)
        {
            UVChannelItems.Add(MakeShared<int32>(UVChannelIndex));
        }

        if (UVChannelItems.IsValidIndex(PreviousUVChannelIndex))
        {
            SelectedUVChannelItem = UVChannelItems[PreviousUVChannelIndex];
        }
        else if (UVChannelItems.Num() > 0)
        {
            SelectedUVChannelItem = UVChannelItems[0];
        }
    }

    if (UVChannelComboBox.IsValid())
    {
        UVChannelComboBox->RefreshOptions();

        if (SelectedUVChannelItem.IsValid())
        {
            UVChannelComboBox->SetSelectedItem(SelectedUVChannelItem);
        }
    }

    RefreshWetPartList();
    RefreshUVIslandList();
}

void SWetClothingPartEditorPanel::RefreshUVIslandList()
{
    const int32       PreviousPrimaryUVIslandID = SelectedUVIslandID;
    const TSet<int32> PreviousSelectedUVIslandIDs = SelectedUVIslandIDs;

    UVIslandItems.Reset();
    ResetIslandSelection();
    UVStatusMessage = TEXT("Select a material slot to inspect its UV islands.");

    const UWetClothingAsset* WetClothingAssetPtr = WetClothingAsset.Get();
    if (WetClothingAssetPtr == nullptr || WetClothingAssetPtr->TargetMesh == nullptr)
    {
        UVStatusMessage = TEXT("Assign a TargetMesh to see its UV islands.");
    }
    else if (SelectedMaterialSlotIndex == INDEX_NONE)
    {
        UVStatusMessage = TEXT("Select a material slot to inspect its UV islands.");
    }
    else if (!SelectedUVChannelItem.IsValid())
    {
        UVStatusMessage = TEXT("No UV channels are available on LOD 0.");
    }
    else
    {
        TArray<FWetClothingAssetUVIsland> BuiltIslands;
        FString                           ErrorMessage;
        const bool                        bBuiltIslands = FWetClothingAssetMeshAnalyzer::BuildMaterialSlotUVIslands(WetClothingAssetPtr->TargetMesh, 0, *SelectedUVChannelItem, SelectedMaterialSlotIndex, BuiltIslands, &ErrorMessage);
        if (!bBuiltIslands)
        {
            UVStatusMessage = ErrorMessage;
        }
        else if (BuiltIslands.Num() == 0)
        {
            UVStatusMessage = TEXT("No UV islands were found for the selected slot in LOD 0.");
        }
        else
        {
            for (const FWetClothingAssetUVIsland& Island : BuiltIslands)
            {
                UVIslandItems.Add(MakeShared<FWetClothingAssetUVIsland>(Island));
            }
            UVStatusMessage = FString::Printf(TEXT("LOD 0 / UV Channel %d / Slot %d / %d islands"), *SelectedUVChannelItem, SelectedMaterialSlotIndex, UVIslandItems.Num());
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

    RefreshWetPartList();
    RefreshUVView();
    RefreshPreviewIslandHighlight();
}

void SWetClothingPartEditorPanel::RefreshUVView()
{
    if (!UVView.IsValid())
    {
        return;
    }

    UVView->SetBackgroundTexture(ResolveTextureAddressTexture());
    UVView->SetDrawBackgroundTexture(bShowMaterialTextureInUVView && ResolveSelectedMaterialTexture() != nullptr);
    UVView->SetIslands(UVIslandItems);
    UVView->SetIslandColors(BuildUVIslandColorMap());
    UVView->SetHiddenUVIslandIDs(BuildHiddenUVIslandIDSet());
    UVView->SetSelectedIslands(SelectedUVIslandIDs);
    UVView->SetSelectionTool(CurrentUVSelectionTool);
    UVView->SetDisplayMode(CurrentUVDisplayMode);

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

void SWetClothingPartEditorPanel::RefreshWetPartList()
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
        const int32 UVChannelIndex = GetSelectedUVChannelIndex();
        for (const FWetClothingWetPartEntry& Entry : WetClothingAssetPtr->PartData.EditableWetPartData.WetPartEntries)
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

    RefreshUVView();
}

void SWetClothingPartEditorPanel::RefreshPreviewWetPartOverlay()
{
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->SetSelectableIslands(UVIslandItems);
        PreviewViewport->SetWetPartIslandAssignments(BuildUVIslandWetPartIDMap(), BuildUVIslandColorMap());
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
    if (WetClothingAssetPtr == nullptr || SelectedMaterialSlotIndex == INDEX_NONE || !SelectedUVChannelItem.IsValid())
    {
        return;
    }
    const int32 UVChannelIndex = GetSelectedUVChannelIndex();
    for (FWetClothingWetPartEntry& Entry : WetClothingAssetPtr->PartData.EditableWetPartData.WetPartEntries)
    {
        if (Entry.MaterialSlotIndex == SelectedMaterialSlotIndex && Entry.UVChannelIndex == UVChannelIndex && Entry.WetPartID == 0)
        {
            WetClothingAssetPtr->Modify();
            Entry.DisplayName = GetDefaultWetPartName(0);
            Entry.Color = GetDefaultWetPartColor(0);
            Entry.bViewEnabled = true;
            WetClothingAssetPtr->MarkPackageDirty();
            if (DetailsView.IsValid())
            {
                DetailsView->ForceRefresh();
            }
            return;
        }
    }

    WetClothingAssetPtr->Modify();
    FWetClothingWetPartEntry NewEntry;
    NewEntry.MaterialSlotIndex = SelectedMaterialSlotIndex;
    NewEntry.UVChannelIndex = UVChannelIndex;
    NewEntry.WetPartID = 0;
    NewEntry.DisplayName = GetDefaultWetPartName(NewEntry.WetPartID);
    NewEntry.Color = GetDefaultWetPartColor(NewEntry.WetPartID);
    NewEntry.bViewEnabled = true;
    WetClothingAssetPtr->PartData.EditableWetPartData.WetPartEntries.Add(NewEntry);
    WetClothingAssetPtr->MarkPackageDirty();
    if (DetailsView.IsValid())
    {
        DetailsView->ForceRefresh();
    }
}

int32 SWetClothingPartEditorPanel::GetSelectedUVChannelIndex() const
{
    return SelectedUVChannelItem.IsValid() ? *SelectedUVChannelItem : 0;
}

int32 SWetClothingPartEditorPanel::FindNextWetPartForSelectedScope() const
{
    int32 MaxWetPartID = 0;
    if (const UWetClothingAsset* WetClothingAssetPtr = WetClothingAsset.Get())
    {
        const int32 UVChannelIndex = GetSelectedUVChannelIndex();
        for (const FWetClothingWetPartEntry& Entry : WetClothingAssetPtr->PartData.EditableWetPartData.WetPartEntries)
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

    const int32 UVChannelIndex = GetSelectedUVChannelIndex();
    for (FWetClothingWetPartEntry& Entry : WetClothingAssetPtr->PartData.EditableWetPartData.WetPartEntries)
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

const FWetClothingWetPartEntry* SWetClothingPartEditorPanel::FindWetPartEntryForUVIsland(int32 UVIslandID) const
{
    if (const UWetClothingAsset* WetClothingAssetPtr = WetClothingAsset.Get())
    {
        const int32 UVChannelIndex = GetSelectedUVChannelIndex();
        for (const FWetClothingWetPartEntry& Entry : WetClothingAssetPtr->PartData.EditableWetPartData.WetPartEntries)
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

TSharedRef<ITableRow> SWetClothingPartEditorPanel::GenerateMaterialSlotRow(FMaterialSlotItemPtr Item, const TSharedRef<STableViewBase>& OwnerTable)
{
    FWetClothingMaterialSlotRowArgs Args;
    Args.WetClothingAsset = WetClothingAsset.Get();
    Args.TargetMesh = WetClothingAsset.IsValid() ? WetClothingAsset->TargetMesh.Get() : nullptr;
    Args.SelectedMaterialSlotIndex = SelectedMaterialSlotIndex;
    Args.OverridePreviewTexture = SelectedTextureItem.IsValid() ? SelectedTextureItem->Texture.Get() : nullptr;
    Args.ThumbnailPool = MaterialThumbnailPool;
    Args.ThumbnailSink = &MaterialSlotThumbnails;
    Args.OnWettableSlotClicked = FOnWettableMaterialSlotClicked::CreateSP(this, &SWetClothingPartEditorPanel::HandleWettableMaterialSlotClicked);

    return FWetClothingEditorCommonWidgets::GenerateMaterialSlotRow(Item, OwnerTable, Args);
}

void SWetClothingPartEditorPanel::HandleMaterialSlotSelectionChanged(FMaterialSlotItemPtr Item, ESelectInfo::Type)
{
    SelectedMaterialSlotIndex = Item.IsValid() ? Item->SlotIndex : INDEX_NONE;
    SelectedWetPartID = INDEX_NONE;
    ResetIslandSelection();
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
    RefreshMaterialTextures();
    if (MaterialSlotListView.IsValid())
    {
        MaterialSlotListView->RequestListRefresh();
    }
    RefreshWetPartList();
    RefreshUVIslandList();
}

FReply SWetClothingPartEditorPanel::HandleWettableMaterialSlotClicked(int32 MaterialSlotIndex)
{
    UWetClothingAsset* WetClothingAssetPtr = WetClothingAsset.Get();
    if (WetClothingAssetPtr == nullptr || MaterialSlotIndex == INDEX_NONE)
    {
        return FReply::Handled();
    }

    const bool bNewWettable = !FWetClothingEditorCommonWidgets::IsMaterialSlotWettable(WetClothingAssetPtr, MaterialSlotIndex);
    FWetClothingEditorCommonWidgets::SetMaterialSlotWettable(WetClothingAssetPtr, MaterialSlotIndex, bNewWettable);

    RefreshMaterialSlotItems();
    if (DetailsView.IsValid())
    {
        DetailsView->ForceRefresh();
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

    FWetClothingEditorCommonWidgets::MarkMaterialSlotWettable(WetClothingAssetPtr, SelectedMaterialSlotIndex);
    if (MaterialSlotItems.IsValidIndex(SelectedMaterialSlotIndex))
    {
        MaterialSlotItems[SelectedMaterialSlotIndex]->bIsWettableSlot = true;
    }

    if (MaterialSlotListView.IsValid())
    {
        MaterialSlotListView->RequestListRefresh();
    }
}

TSharedRef<SWidget> SWetClothingPartEditorPanel::GenerateTextureComboItem(FTextureItemPtr Item)
{
    return BuildTextureComboContent(Item, 36.0f, false);
}

void SWetClothingPartEditorPanel::HandleTextureSelectionChanged(FTextureItemPtr Item, ESelectInfo::Type SelectInfo)
{
    SelectedTextureItem = Item;
    bShowMaterialTextureInUVView = SelectedTextureItem.IsValid() && SelectedTextureItem->Texture.IsValid();
    SaveSelectedTexture();
    MarkSelectedMaterialSlotWettable();

    if (SelectedTextureComboContentBox.IsValid())
    {
        SelectedTextureComboContentBox->SetContent(BuildTextureComboContent(SelectedTextureItem, 24.0f, true));
    }

    if (MaterialSlotListView.IsValid())
    {
        MaterialSlotListView->RequestListRefresh();
    }

    RefreshUVView();
}

TSharedRef<SWidget> SWetClothingPartEditorPanel::BuildTextureComboContent(FTextureItemPtr Item, float ThumbnailSize, bool bCompactLayout)
{
    TSharedRef<SWidget> ThumbnailWidget =
        SNew(SBorder)
            .Padding(0.0f)
            .BorderImage(FAppStyle::Get().GetBrush(TEXT("WhiteBrush")))
            .BorderBackgroundColor(FLinearColor(0.06f, 0.06f, 0.06f, 1.0f));

    if (Item.IsValid() && Item->Texture.IsValid() && MaterialThumbnailPool.IsValid())
    {
        const uint32                ThumbnailDimension = static_cast<uint32>(FMath::RoundToInt(ThumbnailSize));
        TSharedPtr<FAssetThumbnail> Thumbnail = MakeShared<FAssetThumbnail>(Item->Texture.Get(), ThumbnailDimension, ThumbnailDimension, MaterialThumbnailPool);
        TextureThumbnails.Add(Thumbnail);

        FAssetThumbnailConfig ThumbnailConfig;
        ThumbnailConfig.bAllowFadeIn = false;
        ThumbnailWidget = Thumbnail->MakeThumbnailWidget(ThumbnailConfig);
    }

    return SNew(SHorizontalBox) + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(ThumbnailSize).HeightOverride(ThumbnailSize)[ThumbnailWidget]] + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(bCompactLayout ? FMargin(8.0f, 0.0f, 18.0f, 0.0f) : FMargin(8.0f, 0.0f, 6.0f, 0.0f))[SNew(STextBlock).Text(Item.IsValid() ? FText::FromString(Item->Label) : LOCTEXT("SelectTextureComboItem", "Select Texture")).OverflowPolicy(ETextOverflowPolicy::Ellipsis)];
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

    FWetClothingMaterialSetupResult Result = FWetClothingMaterialSetup::DuplicateAndApplyToMaterialInterface(
        SourceMaterial,
        WetClothingAssetPtr != nullptr ? WetClothingAssetPtr->WrinkleData.WrinkleUVChannelIndex : INDEX_NONE);

    if (Result.bSucceeded && Result.ConfiguredMaterial != nullptr && SourceMaterial != nullptr)
    {
        if (WetClothingAssetPtr != nullptr)
        {
            if (const USkeletalMesh* TargetMesh = WetClothingAssetPtr->TargetMesh)
            {
                const TArray<FSkeletalMaterial>& Materials = TargetMesh->GetMaterials();
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
                        FWetClothingGeneratedWetMaterialOverride* ExistingOverride = WetClothingAssetPtr->PartData.GeneratedWetMaterialOverrides.FindByPredicate(
                            [MaterialIndex](const FWetClothingGeneratedWetMaterialOverride& MaterialOverride)
                            {
                                return MaterialOverride.MaterialSlotIndex == MaterialIndex;
                            });

                        if (ExistingOverride == nullptr)
                        {
                            ExistingOverride = &WetClothingAssetPtr->PartData.GeneratedWetMaterialOverrides.AddDefaulted_GetRef();
                            ExistingOverride->MaterialSlotIndex = MaterialIndex;
                        }

                        ExistingOverride->SourceMaterial = SourceMaterial;
                        ExistingOverride->WetMaterial = Result.ConfiguredMaterial;
                        FWetClothingEditorCommonWidgets::MarkMaterialSlotWettable(WetClothingAssetPtr, MaterialIndex);
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

                    Result.Message += FString::Printf(TEXT("\nStored '%s' as the runtime wet material override for %d material slot(s) on '%s': %s."),
                                                      *Result.ConfiguredMaterial->GetName(),
                                                      AssignedSlotIndices.Num(),
                                                      *TargetMesh->GetName(),
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

    const EAppMsgCategory MessageCategory = Result.bSucceeded ? EAppMsgCategory::Success : EAppMsgCategory::Error;
    FMessageDialog::Open(MessageCategory, EAppMsgType::Ok, FText::FromString(Result.Message));

    return FReply::Handled();
}

bool SWetClothingPartEditorPanel::IsApplyMaterialSetupEnabled() const
{
    if (SelectedMaterialSlotIndex == INDEX_NONE)
    {
        return false;
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

TSharedRef<SWidget> SWetClothingPartEditorPanel::GenerateUVChannelComboItem(FUVChannelItemPtr Item)
{
    const FString Label = Item.IsValid()
                              ? FString::Printf(TEXT("UV Channel %d"), *Item)
                              : TEXT("Invalid UV Channel");

    return SNew(STextBlock)
        .Text(FText::FromString(Label));
}

void SWetClothingPartEditorPanel::HandleUVChannelSelectionChanged(FUVChannelItemPtr Item, ESelectInfo::Type SelectInfo)
{
    SelectedUVChannelItem = Item;
    SelectedWetPartID = INDEX_NONE;
    ResetIslandSelection();
    RefreshMaterialTextures();
    RefreshWetPartList();
    RefreshUVIslandList();
}

TSharedRef<SWidget> SWetClothingPartEditorPanel::GenerateUVDisplayModeComboItem(FUVDisplayModeItemPtr Item)
{
    const FText Label = (!Item.IsValid() || *Item == EWetClothingAssetUVDisplayMode::Normal)
                            ? LOCTEXT("UVDisplayModeNormal", "Normal")
                            : LOCTEXT("UVDisplayModeOutline", "Outline");

    return SNew(STextBlock)
        .Text(Label);
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
    FText        RowText = LOCTEXT("InvalidUVIsland", "Invalid UV island");
    FLinearColor SwatchColor(0.06f, 0.06f, 0.06f, 1.0f);
    if (Item.IsValid())
    {
        if (const FWetClothingWetPartEntry* EffectiveEntry = FindEffectiveWetPartEntryForUVIsland(Item->UVIslandID))
        {
            SwatchColor = EffectiveEntry->WetPartID == 0 ? FLinearColor::White : EffectiveEntry->Color;
            SwatchColor.A = 1.0f;
            RowText = FText::Format(LOCTEXT("UVIslandAssignedRow", "Island {0} | {1} tris | ID {2}"), FText::AsNumber(Item->UVIslandID), FText::AsNumber(Item->TriangleCount), FText::AsNumber(EffectiveEntry->WetPartID));
        }
    }

    return SNew(STableRow<FUVIslandItemPtr>, OwnerTable)
        .Padding(4.0f)
            [SNew(SHorizontalBox) + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)[SNew(SBox).WidthOverride(18.0f).HeightOverride(18.0f)[SNew(SBorder).BorderImage(FAppStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(SwatchColor)]] + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[SNew(STextBlock).AutoWrapText(true).Text(RowText)]];
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

void SWetClothingPartEditorPanel::HandleUVIslandSelectionChangedFromUVView(const TArray<int32>& UVIslandIDs, EWetClothingAssetUVSelectionOp SelectionOp)
{
    ApplyIslandSelection(UVIslandIDs, SelectionOp == EWetClothingAssetUVSelectionOp::Add);
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
    RefreshUVView();
    RefreshPreviewIslandHighlight();
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

    RefreshUVView();
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

    WetClothingAssetPtr->Modify();
    const FString TrimmedName = InText.ToString().TrimStartAndEnd();
    Entry->DisplayName = TrimmedName.IsEmpty() ? GetDefaultWetPartName(Entry->WetPartID) : TrimmedName;
    WetClothingAssetPtr->MarkPackageDirty();
    MarkSelectedMaterialSlotWettable();

    if (DetailsView.IsValid())
    {
        DetailsView->ForceRefresh();
    }

    RefreshWetPartList();
    RefreshUVView();
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
    WetClothingAssetPtr->Modify();
    Entry->Color = NewColor;
    Item->Color = NewColor;
    WetClothingAssetPtr->MarkPackageDirty();
    MarkSelectedMaterialSlotWettable();

    if (DetailsView.IsValid())
    {
        DetailsView->ForceRefresh();
    }

    RefreshWetPartList();
    RefreshUVView();
    RefreshPreviewWetPartOverlay();
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

        if (DetailsView.IsValid())
        {
            DetailsView->ForceRefresh();
        }
    }

    RefreshWetPartList();
    RefreshUVView();
    RefreshPreviewWetPartOverlay();
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

    if (DetailsView.IsValid())
    {
        DetailsView->ForceRefresh();
    }

    RefreshWetPartList();
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
    if (WetClothingAssetPtr == nullptr || SelectedMaterialSlotIndex == INDEX_NONE || !SelectedUVChannelItem.IsValid())
    {
        return FReply::Handled();
    }

    const int32 NewWetPartID = FindNextWetPartForSelectedScope();
    WetClothingAssetPtr->Modify();

    FWetClothingWetPartEntry NewEntry;
    NewEntry.MaterialSlotIndex = SelectedMaterialSlotIndex;
    NewEntry.UVChannelIndex = GetSelectedUVChannelIndex();
    NewEntry.WetPartID = NewWetPartID;
    NewEntry.DisplayName = GetDefaultWetPartName(NewWetPartID);
    NewEntry.Color = GetDefaultWetPartColor(NewWetPartID);
    NewEntry.bViewEnabled = true;

    WetClothingAssetPtr->PartData.EditableWetPartData.WetPartEntries.Add(NewEntry);
    WetClothingAssetPtr->MarkPackageDirty();
    MarkSelectedMaterialSlotWettable();
    SelectedWetPartID = INDEX_NONE;
    SelectedAssignWetPartID = NewWetPartID;

    if (DetailsView.IsValid())
    {
        DetailsView->ForceRefresh();
    }

    RefreshWetPartList();
    RefreshUVView();
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
    const int32 UVChannelIndex = GetSelectedUVChannelIndex();
    WetClothingAssetPtr->Modify();
    for (int32 Index = WetClothingAssetPtr->PartData.EditableWetPartData.WetPartEntries.Num() - 1; Index >= 0; --Index)
    {
        const FWetClothingWetPartEntry& Entry = WetClothingAssetPtr->PartData.EditableWetPartData.WetPartEntries[Index];
        if (Entry.MaterialSlotIndex == SelectedMaterialSlotIndex && Entry.UVChannelIndex == UVChannelIndex && Entry.WetPartID == SelectedWetPartID)
        {
            WetClothingAssetPtr->PartData.EditableWetPartData.WetPartEntries.RemoveAt(Index);
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
    if (DetailsView.IsValid())
    {
        DetailsView->ForceRefresh();
    }
    EnsureDefaultWetPartForSelectedScope();
    RefreshWetPartList();
    RefreshUVIslandList();
    return FReply::Handled();
}

bool SWetClothingPartEditorPanel::IsWetPartRemoveEnabled() const
{
    return SelectedWetPartID != INDEX_NONE && SelectedWetPartID != 0;
}

bool SWetClothingPartEditorPanel::IsAutoPartitionEnabled() const
{
    return WetClothingAsset.IsValid() && SelectedMaterialSlotIndex != INDEX_NONE && SelectedUVChannelItem.IsValid() && UVIslandItems.Num() > 0;
}

bool SWetClothingPartEditorPanel::HasAutoPartitionDataToReplace() const
{
    if (const UWetClothingAsset* WetClothingAssetPtr = WetClothingAsset.Get())
    {
        const int32 UVChannelIndex = GetSelectedUVChannelIndex();
        for (const FWetClothingWetPartEntry& Entry : WetClothingAssetPtr->PartData.EditableWetPartData.WetPartEntries)
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
    TSharedPtr<SWetClothingAssetUVView>     BeforePreviewView;
    TSharedPtr<SWetClothingAssetUVView>     AfterPreviewView;
    const FSlateFontInfo                    AutoPartitionDialogTextFont = FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 13);
    const FSlateFontInfo                    AutoPartitionDialogHeadingFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 14);

    const TSharedRef<SWindow> PreviewWindow = SNew(SWindow)
                                                  .Title(LOCTEXT("AutoPartitionPreviewTitle", "Auto Partition Preview"))
                                                  .ClientSize(FVector2D(980.0f, 680.0f))
                                                  .SupportsMinimize(false)
                                                  .SupportsMaximize(false);

    TWeakPtr<SWindow>                             WeakPreviewWindow = PreviewWindow.ToSharedPtr();
    TSharedRef<TWeakPtr<SWetClothingAssetUVView>> WeakAfterPreviewView = MakeShared<TWeakPtr<SWetClothingAssetUVView>>();
    const auto                                    RefreshAutoPartitionPreview = [this, PreviewTextureData, PreviewClusters, WeakAfterPreviewView]()
    {
        FString PreviewErrorMessage;
        if (FWetPartAutoPartitioner::BuildClusters(UVIslandItems, *PreviewTextureData, AutoPartitionTolerancePercent, AutoPartitionColorMode, *PreviewClusters, &PreviewErrorMessage))
        {
            if (TSharedPtr<SWetClothingAssetUVView> PinnedAfterView = WeakAfterPreviewView->Pin())
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
                                                 [SAssignNew(BeforePreviewView, SWetClothingAssetUVView)]]

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
                                                 [SAssignNew(AfterPreviewView, SWetClothingAssetUVView)]]]

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
        BeforePreviewView->SetDisplayMode(EWetClothingAssetUVDisplayMode::Normal);
    }

    if (AfterPreviewView.IsValid())
    {
        AfterPreviewView->SetBackgroundTexture(PartitionTexture);
        AfterPreviewView->SetIslands(UVIslandItems);
        AfterPreviewView->SetIslandColors(BuildAutoPartitionPreviewColorMap(*PreviewClusters));
        AfterPreviewView->SetDisplayMode(EWetClothingAssetUVDisplayMode::Normal);
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

    const int32 UVChannelIndex = GetSelectedUVChannelIndex();
    WetClothingAssetPtr->Modify();
    MarkSelectedMaterialSlotWettable();

    for (int32 EntryIndex = WetClothingAssetPtr->PartData.EditableWetPartData.WetPartEntries.Num() - 1; EntryIndex >= 0; --EntryIndex)
    {
        const FWetClothingWetPartEntry& Entry = WetClothingAssetPtr->PartData.EditableWetPartData.WetPartEntries[EntryIndex];
        if (Entry.MaterialSlotIndex == SelectedMaterialSlotIndex && Entry.UVChannelIndex == UVChannelIndex && Entry.WetPartID != 0)
        {
            WetClothingAssetPtr->PartData.EditableWetPartData.WetPartEntries.RemoveAt(EntryIndex);
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
        WetClothingAssetPtr->PartData.EditableWetPartData.WetPartEntries.Add(NewEntry);
    }

    WetClothingAssetPtr->MarkPackageDirty();

    if (DetailsView.IsValid())
    {
        DetailsView->ForceRefresh();
    }

    SelectedWetPartID = Clusters.Num() > 0 ? 1 : 0;
    SelectedAssignWetPartID = SelectedWetPartID;

    if (MaterialSlotItems.IsValidIndex(SelectedMaterialSlotIndex) && MaterialSlotListView.IsValid())
    {
        MaterialSlotListView->SetSelection(MaterialSlotItems[SelectedMaterialSlotIndex], ESelectInfo::Direct);
    }

    RefreshWetPartList();
    RefreshUVIslandList();
}

FReply SWetClothingPartEditorPanel::HandleAssignSelectedUVIslandToWetPartClicked()
{
    UWetClothingAsset* WetClothingAssetPtr = WetClothingAsset.Get();
    if (WetClothingAssetPtr == nullptr || SelectedMaterialSlotIndex == INDEX_NONE || SelectedUVIslandIDs.Num() == 0 || SelectedAssignWetPartID == INDEX_NONE)
    {
        return FReply::Handled();
    }
    const int32 UVChannelIndex = GetSelectedUVChannelIndex();
    WetClothingAssetPtr->Modify();
    MarkSelectedMaterialSlotWettable();
    for (FWetClothingWetPartEntry& Entry : WetClothingAssetPtr->PartData.EditableWetPartData.WetPartEntries)
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
    if (DetailsView.IsValid())
    {
        DetailsView->ForceRefresh();
    }
    RefreshWetPartList();
    RefreshUVIslandList();
    return FReply::Handled();
}

FText SWetClothingPartEditorPanel::GetMaterialSlotCountText() const
{
    return FText::Format(
        LOCTEXT("MaterialSlotCount", "{0} Slots"),
        FText::AsNumber(MaterialSlotItems.Num()));
}

FText SWetClothingPartEditorPanel::GetSelectedMaterialSlotText() const
{
    if (!MaterialSlotItems.IsValidIndex(SelectedMaterialSlotIndex))
    {
        return LOCTEXT("NoMaterialSlotSelected", "Select a material slot to isolate its coverage on the preview mesh.");
    }

    const FMaterialSlotItemPtr& Item = MaterialSlotItems[SelectedMaterialSlotIndex];
    const FString               MaterialName = Item->Material.IsValid() ? Item->Material->GetName() : TEXT("None");

    return FText::Format(
        LOCTEXT("SelectedMaterialSlot", "Selected: [{0}] {1} ({2})"),
        FText::AsNumber(Item->SlotIndex),
        FText::FromName(Item->SlotName),
        FText::FromString(MaterialName));
}

FText SWetClothingPartEditorPanel::GetSelectedUVChannelText() const
{
    if (!SelectedUVChannelItem.IsValid())
    {
        return LOCTEXT("NoUVChannelSelected", "No UV Channel");
    }

    return FText::Format(
        LOCTEXT("SelectedUVChannel", "UV Channel {0}"),
        FText::AsNumber(*SelectedUVChannelItem));
}

FText SWetClothingPartEditorPanel::GetSelectedUVDisplayModeText() const
{
    if (!SelectedUVDisplayModeItem.IsValid() || *SelectedUVDisplayModeItem == EWetClothingAssetUVDisplayMode::Normal)
    {
        return LOCTEXT("SelectedUVDisplayModeNormal", "Normal");
    }

    return LOCTEXT("SelectedUVDisplayModeOutline", "Outline");
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
        FText::AsNumber(GetSelectedUVChannelIndex()));
}

FText SWetClothingPartEditorPanel::GetWetnessProfileMapBakeSlotsText() const
{
    TArray<int32> MaterialSlotIndices;
    CollectMaterialSlotsForWetnessProfileMap(ResolveSelectedMaterialTexture(), GetSelectedUVChannelIndex(), MaterialSlotIndices);

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

    const FWetClothingBakedWetnessProfileMap* BakedWetnessProfileMap = FindBakedWetnessProfileMap(SourceTexture, GetSelectedUVChannelIndex());
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
    const FWetClothingBakedWetnessProfileMap* BakedWetnessProfileMap = FindBakedWetnessProfileMap(ResolveSelectedMaterialTexture(), GetSelectedUVChannelIndex());
    const int32                                    Resolution = BakedWetnessProfileMap != nullptr ? BakedWetnessProfileMap->Resolution : 512;
    const int32                                    PaddingPixels = BakedWetnessProfileMap != nullptr ? BakedWetnessProfileMap->PaddingPixels : 4;

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

FReply SWetClothingPartEditorPanel::HandleUVSelectionToolButtonClicked(FUVSelectionToolItemPtr Item)
{
    if (Item.IsValid())
    {
        SetCurrentUVSelectionTool(Item->Tool);
    }

    return FReply::Handled();
}

void SWetClothingPartEditorPanel::SetCurrentUVSelectionTool(EWetClothingAssetUVSelectionTool InTool)
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

FSlateColor SWetClothingPartEditorPanel::GetUVSelectionToolButtonColor(FUVSelectionToolItemPtr Item) const
{
    const bool bIsSelected = Item.IsValid() && Item->Tool == CurrentUVSelectionTool;
    return FSlateColor(bIsSelected
                           ? FStyleColors::AccentBlue
                           : FStyleColors::Header);
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

TSharedRef<SWidget> SWetClothingPartEditorPanel::BuildBakeMapsMenu()
{
    return SNew(SVerticalBox)

        + SVerticalBox::Slot()
              .AutoHeight()
              .Padding(4.0f, 2.0f)
                  [SNew(SButton)
                       .Text(LOCTEXT("BakeAllMapsMenuItem", "Bake All Maps"))
                       .OnClicked(this, &SWetClothingPartEditorPanel::HandleBakeAllMapsClicked)]

        + SVerticalBox::Slot()
              .AutoHeight()
              .Padding(4.0f, 2.0f)
                  [SNew(SButton)
                       .Text(LOCTEXT("BakeWetnessProfileMapsMenuItem", "Bake Wetness Profile Maps"))
                       .OnClicked(this, &SWetClothingPartEditorPanel::HandleBakeAllWetnessProfileMapsClicked)]

        + SVerticalBox::Slot()
              .AutoHeight()
              .Padding(4.0f, 2.0f)
                  [SNew(SButton)
                       .Text(LOCTEXT("BakeWrinkleNormalMapMenuItem", "Bake Wrinkle Normal Map"))
                       .OnClicked(this, &SWetClothingPartEditorPanel::HandleBakeWrinkleNormalMapClicked)]

        + SVerticalBox::Slot()
              .AutoHeight()
              .Padding(4.0f, 2.0f)
                  [SNew(SButton)
                       .Text(LOCTEXT("BakeWrinkleMaskMenuItem", "Bake Wrinkle Mask"))
                       .OnClicked(this, &SWetClothingPartEditorPanel::HandleBakeWrinkleMaskClicked)];
}

FReply SWetClothingPartEditorPanel::HandleBakeAllMapsClicked()
{
    return HandleBakeAllWetnessProfileMapsClicked();
}

FReply SWetClothingPartEditorPanel::HandleBakeWrinkleNormalMapClicked()
{
    FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("BakeWrinkleNormalMapPending", "Wrinkle Normal Map baking is not implemented yet."));
    return FReply::Handled();
}

FReply SWetClothingPartEditorPanel::HandleBakeWrinkleMaskClicked()
{
    FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("BakeWrinkleMaskPending", "Wrinkle Mask baking is not implemented yet."));
    return FReply::Handled();
}

bool SWetClothingPartEditorPanel::IsWetnessProfileMapBakeSourceValid() const
{
    return WetClothingAsset.IsValid() && WetClothingAsset->TargetMesh != nullptr && ResolveSelectedMaterialTexture() != nullptr && SelectedUVChannelItem.IsValid();
}

bool SWetClothingPartEditorPanel::CanBakeAnyWetnessProfileMap() const
{
    TArray<UTexture*> SourceTextures;
    CollectWetnessProfileMapSourceTextures(GetSelectedUVChannelIndex(), SourceTextures);
    return WetClothingAsset.IsValid() && WetClothingAsset->TargetMesh != nullptr && SelectedUVChannelItem.IsValid() && SourceTextures.Num() > 0;
}

FReply SWetClothingPartEditorPanel::HandleBakeSelectedWetnessProfileMapClicked()
{
    UWetClothingAsset* WetClothingAssetPtr = WetClothingAsset.Get();
    UTexture*          SourceTexture = ResolveSelectedMaterialTexture();
    if (WetClothingAssetPtr == nullptr || SourceTexture == nullptr)
    {
        return FReply::Handled();
    }

    const int32 UVChannelIndex = GetSelectedUVChannelIndex();

    TArray<int32> MaterialSlotIndices;
    CollectMaterialSlotsForWetnessProfileMap(SourceTexture, UVChannelIndex, MaterialSlotIndices);
    if (MaterialSlotIndices.Num() == 0)
    {
        FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("NoWetnessProfileMapBakeSlots", "No material slots were found for the selected texture."));
        return FReply::Handled();
    }

    const FWetClothingBakedWetnessProfileMap* ExistingWetnessProfileMap = FindBakedWetnessProfileMap(SourceTexture, UVChannelIndex);
    FWetClothingWetnessProfileMapBakeSettings      Settings;
    if (ExistingWetnessProfileMap != nullptr)
    {
        Settings.Resolution = ExistingWetnessProfileMap->Resolution;
        Settings.PaddingPixels = ExistingWetnessProfileMap->PaddingPixels;
    }

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

    const int32 UVChannelIndex = GetSelectedUVChannelIndex();

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

        const FWetClothingBakedWetnessProfileMap* ExistingWetnessProfileMap = FindBakedWetnessProfileMap(SourceTexture, UVChannelIndex);
        FWetClothingWetnessProfileMapBakeSettings      Settings;
        if (ExistingWetnessProfileMap != nullptr)
        {
            Settings.Resolution = ExistingWetnessProfileMap->Resolution;
            Settings.PaddingPixels = ExistingWetnessProfileMap->PaddingPixels;
        }

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
    const UWetClothingAsset* WetClothingAssetPtr = WetClothingAsset.Get();
    if (WetClothingAssetPtr == nullptr || MaterialSlotIndex == INDEX_NONE || UVChannelIndex == INDEX_NONE)
    {
        return nullptr;
    }

    for (const FWetClothingSourceTextureSelection& Selection : WetClothingAssetPtr->PartData.EditableWetPartData.SourceTextureSelections)
    {
        if (Selection.MaterialSlotIndex == MaterialSlotIndex && Selection.UVChannelIndex == UVChannelIndex)
        {
            return Selection.Texture;
        }
    }

    return nullptr;
}

UTexture* SWetClothingPartEditorPanel::ResolveOrSaveTextureSelection(int32 MaterialSlotIndex, int32 UVChannelIndex)
{
    if (UTexture* SavedTexture = FindSavedTextureSelection(MaterialSlotIndex, UVChannelIndex))
    {
        return SavedTexture;
    }

    UWetClothingAsset* WetClothingAssetPtr = WetClothingAsset.Get();
    if (WetClothingAssetPtr == nullptr || WetClothingAssetPtr->TargetMesh == nullptr)
    {
        return nullptr;
    }

    const TArray<FSkeletalMaterial>& Materials = WetClothingAssetPtr->TargetMesh->GetMaterials();
    if (!Materials.IsValidIndex(MaterialSlotIndex) || Materials[MaterialSlotIndex].MaterialInterface == nullptr)
    {
        return nullptr;
    }

    TArray<FTextureItemPtr> ResolvedTextureItems;
    FWetClothingMaterialTextureResolver::BuildTextureItems(Materials[MaterialSlotIndex].MaterialInterface, ResolvedTextureItems);
    for (const FTextureItemPtr& TextureItem : ResolvedTextureItems)
    {
        if (TextureItem.IsValid() && TextureItem->Texture.IsValid())
        {
            UTexture* ResolvedTexture = TextureItem->Texture.Get();
            SaveTextureSelection(MaterialSlotIndex, UVChannelIndex, ResolvedTexture);
            return ResolvedTexture;
        }
    }

    return nullptr;
}

bool SWetClothingPartEditorPanel::HasSavedTextureSelection(int32 MaterialSlotIndex, int32 UVChannelIndex) const
{
    const UWetClothingAsset* WetClothingAssetPtr = WetClothingAsset.Get();
    if (WetClothingAssetPtr == nullptr || MaterialSlotIndex == INDEX_NONE || UVChannelIndex == INDEX_NONE)
    {
        return false;
    }

    return WetClothingAssetPtr->PartData.EditableWetPartData.SourceTextureSelections.ContainsByPredicate(
        [MaterialSlotIndex, UVChannelIndex](const FWetClothingSourceTextureSelection& Selection)
        {
            return Selection.MaterialSlotIndex == MaterialSlotIndex && Selection.UVChannelIndex == UVChannelIndex;
        });
}

void SWetClothingPartEditorPanel::SaveTextureSelection(int32 MaterialSlotIndex, int32 UVChannelIndex, UTexture* Texture)
{
    UWetClothingAsset* WetClothingAssetPtr = WetClothingAsset.Get();
    if (WetClothingAssetPtr == nullptr || MaterialSlotIndex == INDEX_NONE || UVChannelIndex == INDEX_NONE)
    {
        return;
    }

    WetClothingAssetPtr->Modify();

    for (int32 SelectionIndex = WetClothingAssetPtr->PartData.EditableWetPartData.SourceTextureSelections.Num() - 1; SelectionIndex >= 0; --SelectionIndex)
    {
        FWetClothingSourceTextureSelection& Selection = WetClothingAssetPtr->PartData.EditableWetPartData.SourceTextureSelections[SelectionIndex];
        if (Selection.MaterialSlotIndex == MaterialSlotIndex && Selection.UVChannelIndex == UVChannelIndex)
        {
            Selection.Texture = Texture;
            WetClothingAssetPtr->MarkPackageDirty();
            return;
        }
    }

    FWetClothingSourceTextureSelection NewSelection;
    NewSelection.MaterialSlotIndex = MaterialSlotIndex;
    NewSelection.UVChannelIndex = UVChannelIndex;
    NewSelection.Texture = Texture;
    WetClothingAssetPtr->PartData.EditableWetPartData.SourceTextureSelections.Add(NewSelection);
    WetClothingAssetPtr->MarkPackageDirty();
}

bool SWetClothingPartEditorPanel::HasPendingWetSetupTasks(FString* OutSummary) const
{
    const UWetClothingAsset* WetClothingAssetPtr = WetClothingAsset.Get();
    if (WetClothingAssetPtr == nullptr || WetClothingAssetPtr->TargetMesh == nullptr)
    {
        if (OutSummary != nullptr)
        {
            *OutSummary = TEXT("Wet setup has no pending work.");
        }
        return false;
    }

    TSet<int32>     WetMaterialSlots;
    TSet<FIntPoint> WetPartScopePairs;
    TSet<FString>   ExpectedTextureUvKeys;
    for (const FWetClothingWetPartEntry& Entry : WetClothingAssetPtr->PartData.EditableWetPartData.WetPartEntries)
    {
        if (SWetClothingPartEditorPanelLocal::IsWetPartEntryRelevantForWetSetup(Entry))
        {
            WetMaterialSlots.Add(Entry.MaterialSlotIndex);
            WetPartScopePairs.Add(FIntPoint(Entry.MaterialSlotIndex, Entry.UVChannelIndex));
        }
    }

    TArray<FString>                  PendingLines;
    const TArray<FSkeletalMaterial>& Materials = WetClothingAssetPtr->TargetMesh->GetMaterials();
    for (const int32 MaterialSlotIndex : WetMaterialSlots)
    {
        const FWetClothingGeneratedWetMaterialOverride* MaterialOverride = WetClothingAssetPtr->PartData.GeneratedWetMaterialOverrides.FindByPredicate(
            [MaterialSlotIndex](const FWetClothingGeneratedWetMaterialOverride& ExistingOverride)
            {
                return ExistingOverride.MaterialSlotIndex == MaterialSlotIndex;
            });

        if (!Materials.IsValidIndex(MaterialSlotIndex))
        {
            PendingLines.Add(FString::Printf(TEXT("Material slot %d is out of range."), MaterialSlotIndex));
            continue;
        }

        if (MaterialOverride == nullptr || MaterialOverride->WetMaterial == nullptr)
        {
            PendingLines.Add(FString::Printf(TEXT("Material setup needed for slot %d."), MaterialSlotIndex));
        }
        else if (MaterialOverride->SourceMaterial != Materials[MaterialSlotIndex].MaterialInterface)
        {
            PendingLines.Add(FString::Printf(TEXT("Material setup source changed for slot %d."), MaterialSlotIndex));
        }
        else if (!FWetClothingMaterialSetup::IsMaterialConfiguredForDwc(MaterialOverride->WetMaterial))
        {
            PendingLines.Add(FString::Printf(TEXT("Wet material override on slot %d is missing DWC material functions."), MaterialSlotIndex));
        }
    }

    for (const FIntPoint& WetPartScopePair : WetPartScopePairs)
    {
        UTexture* SourceTexture = FindSavedTextureSelection(WetPartScopePair.X, WetPartScopePair.Y);
        if (SourceTexture == nullptr)
        {
                PendingLines.Add(FString::Printf(TEXT("Wetness Profile Map source texture is not selected for slot %d UV Channel %d."), WetPartScopePair.X, WetPartScopePair.Y));
            continue;
        }
        ExpectedTextureUvKeys.Add(SWetClothingPartEditorPanelLocal::MakeTextureUvKey(SourceTexture, WetPartScopePair.Y));

        TArray<int32> MaterialSlotIndices;
        for (const FWetClothingSourceTextureSelection& Selection : WetClothingAssetPtr->PartData.EditableWetPartData.SourceTextureSelections)
        {
            if (Selection.Texture == SourceTexture && Selection.UVChannelIndex == WetPartScopePair.Y && WetMaterialSlots.Contains(Selection.MaterialSlotIndex))
            {
                MaterialSlotIndices.AddUnique(Selection.MaterialSlotIndex);
            }
        }
        MaterialSlotIndices.Sort();

        const FWetClothingBakedWetnessProfileMap* ExistingWetnessProfileMap = FindBakedWetnessProfileMap(SourceTexture, WetPartScopePair.Y);
        if (ExistingWetnessProfileMap == nullptr || ExistingWetnessProfileMap->WetnessProfileMap0 == nullptr)
        {
            PendingLines.Add(FString::Printf(TEXT("Wetness Profile Map bake needed for '%s' UV Channel %d."), *GetNameSafe(SourceTexture), WetPartScopePair.Y));
        }
        else if (ExistingWetnessProfileMap->MaterialSlotIndices != MaterialSlotIndices)
        {
            PendingLines.Add(FString::Printf(TEXT("Wetness Profile Map slot list is outdated for '%s' UV Channel %d."), *GetNameSafe(SourceTexture), WetPartScopePair.Y));
        }
        else
        {
            const FString CurrentBuildSignature = FWetClothingWetnessProfileMapBaker::MakeBuildSignature(
                WetClothingAssetPtr,
                SourceTexture,
                WetPartScopePair.Y,
                MaterialSlotIndices);
            if (ExistingWetnessProfileMap->BuildSignature != CurrentBuildSignature)
            {
                PendingLines.Add(FString::Printf(TEXT("Wetness Profile Map data is outdated for '%s' UV Channel %d."), *GetNameSafe(SourceTexture), WetPartScopePair.Y));
            }
        }
    }

    for (const FWetClothingBakedWetnessProfileMap& BakedWetnessProfileMap : WetClothingAssetPtr->PartData.BakedWetnessProfileMaps)
    {
        const FString TextureUvKey = SWetClothingPartEditorPanelLocal::MakeTextureUvKey(BakedWetnessProfileMap.SourceTexture.Get(), BakedWetnessProfileMap.UVChannelIndex);
        if (!TextureUvKey.IsEmpty() && !ExpectedTextureUvKeys.Contains(TextureUvKey))
        {
            PendingLines.Add(FString::Printf(
                TEXT("Stale Wetness Profile Map reference will be removed for '%s' UV Channel %d."),
                *GetNameSafe(BakedWetnessProfileMap.SourceTexture.Get()),
                BakedWetnessProfileMap.UVChannelIndex));
        }
    }

    if (OutSummary != nullptr)
    {
        *OutSummary = PendingLines.Num() == 0
                          ? TEXT("Wet setup is up to date.")
                          : FString::Printf(TEXT("Pending Wet Setup:\n- %s"), *FString::Join(PendingLines, TEXT("\n- ")));
    }

    return PendingLines.Num() > 0;
}

bool SWetClothingPartEditorPanel::BuildWetSetup(FString& OutSummary, bool* OutHadWarnings)
{
    if (OutHadWarnings != nullptr)
    {
        *OutHadWarnings = false;
    }

    UWetClothingAsset* WetClothingAssetPtr = WetClothingAsset.Get();
    if (WetClothingAssetPtr == nullptr || WetClothingAssetPtr->TargetMesh == nullptr)
    {
        OutSummary = TEXT("Assign a TargetMesh before building wet setup.");
        return false;
    }

    TSet<int32>     WetMaterialSlots;
    TSet<FIntPoint> WetPartScopePairs;
    for (const FWetClothingWetPartEntry& Entry : WetClothingAssetPtr->PartData.EditableWetPartData.WetPartEntries)
    {
        if (SWetClothingPartEditorPanelLocal::IsWetPartEntryRelevantForWetSetup(Entry))
        {
            WetMaterialSlots.Add(Entry.MaterialSlotIndex);
            WetPartScopePairs.Add(FIntPoint(Entry.MaterialSlotIndex, Entry.UVChannelIndex));
        }
    }

    if (WetMaterialSlots.Num() == 0)
    {
        OutSummary = TEXT("No WetPart material slots were found.");
        return false;
    }

    TArray<FString> CreatedOrUpdatedMaterials;
    TArray<FString> BakedWetnessProfileMaps;
    TArray<FString> RemovedStaleWetnessProfileMaps;
    TArray<FString> Skipped;
    TArray<FString> Warnings;

    const TArray<FSkeletalMaterial>& Materials = WetClothingAssetPtr->TargetMesh->GetMaterials();
    for (const int32 MaterialSlotIndex : WetMaterialSlots)
    {
        if (!Materials.IsValidIndex(MaterialSlotIndex))
        {
            Warnings.Add(FString::Printf(TEXT("Material slot %d is out of range."), MaterialSlotIndex));
            continue;
        }

        UMaterialInterface* SourceMaterial = Materials[MaterialSlotIndex].MaterialInterface;
        if (SourceMaterial == nullptr)
        {
            Warnings.Add(FString::Printf(TEXT("Material slot %d has no source material."), MaterialSlotIndex));
            continue;
        }

        FWetClothingGeneratedWetMaterialOverride* ExistingOverride = WetClothingAssetPtr->PartData.GeneratedWetMaterialOverrides.FindByPredicate(
            [MaterialSlotIndex](const FWetClothingGeneratedWetMaterialOverride& MaterialOverride)
            {
                return MaterialOverride.MaterialSlotIndex == MaterialSlotIndex;
            });

        if (ExistingOverride != nullptr &&
            ExistingOverride->SourceMaterial == SourceMaterial &&
            ExistingOverride->WetMaterial != nullptr &&
            FWetClothingMaterialSetup::IsMaterialConfiguredForDwc(ExistingOverride->WetMaterial))
        {
            FWetClothingMaterialSetupResult RefreshResult = FWetClothingMaterialSetup::DuplicateAndApplyToMaterialInterface(
                ExistingOverride->WetMaterial,
                WetClothingAssetPtr->WrinkleData.WrinkleUVChannelIndex);
            if (!RefreshResult.bSucceeded)
            {
                Warnings.Add(FString::Printf(TEXT("Slot %d existing wet material refresh failed: %s"), MaterialSlotIndex, *RefreshResult.Message));
            }
            else
            {
                Skipped.Add(FString::Printf(
                    TEXT("Slot %d already has '%s'. Refreshed wrinkle UV channel %d."),
                    MaterialSlotIndex,
                    *GetNameSafe(ExistingOverride->WetMaterial),
                    FMath::Max(WetClothingAssetPtr->WrinkleData.WrinkleUVChannelIndex, 0)));
            }
            continue;
        }

        FWetClothingMaterialSetupResult Result = FWetClothingMaterialSetup::DuplicateAndApplyToMaterialInterface(
            SourceMaterial,
            WetClothingAssetPtr->WrinkleData.WrinkleUVChannelIndex);
        if (!Result.bSucceeded || Result.ConfiguredMaterial == nullptr)
        {
            Warnings.Add(FString::Printf(TEXT("Slot %d material setup failed: %s"), MaterialSlotIndex, *Result.Message));
            continue;
        }

        WetClothingAssetPtr->Modify();
        if (ExistingOverride == nullptr)
        {
            ExistingOverride = &WetClothingAssetPtr->PartData.GeneratedWetMaterialOverrides.AddDefaulted_GetRef();
            ExistingOverride->MaterialSlotIndex = MaterialSlotIndex;
        }
        ExistingOverride->SourceMaterial = SourceMaterial;
        ExistingOverride->WetMaterial = Result.ConfiguredMaterial;
        WetClothingAssetPtr->MarkPackageDirty();

        CreatedOrUpdatedMaterials.Add(FString::Printf(TEXT("Slot %d -> %s"), MaterialSlotIndex, *GetNameSafe(Result.ConfiguredMaterial)));
    }

    TSet<FString> BakedTextureUvKeys;
    bool          bCanPruneStaleWetnessProfileMaps = true;
    for (const FIntPoint& WetPartScopePair : WetPartScopePairs)
    {
        UTexture* SourceTexture = ResolveOrSaveTextureSelection(WetPartScopePair.X, WetPartScopePair.Y);
        if (SourceTexture == nullptr)
        {
            bCanPruneStaleWetnessProfileMaps = false;
            Warnings.Add(FString::Printf(TEXT("Slot %d UV Channel %d has no selected source texture."), WetPartScopePair.X, WetPartScopePair.Y));
            continue;
        }

        const FString TextureUvKey = SWetClothingPartEditorPanelLocal::MakeTextureUvKey(SourceTexture, WetPartScopePair.Y);
        if (BakedTextureUvKeys.Contains(TextureUvKey))
        {
            continue;
        }
        BakedTextureUvKeys.Add(TextureUvKey);

        TArray<int32> MaterialSlotIndices;
        for (const FWetClothingSourceTextureSelection& Selection : WetClothingAssetPtr->PartData.EditableWetPartData.SourceTextureSelections)
        {
            if (Selection.Texture == SourceTexture && Selection.UVChannelIndex == WetPartScopePair.Y && WetMaterialSlots.Contains(Selection.MaterialSlotIndex))
            {
                MaterialSlotIndices.AddUnique(Selection.MaterialSlotIndex);
            }
        }
        MaterialSlotIndices.Sort();

        if (MaterialSlotIndices.Num() == 0)
        {
            Warnings.Add(FString::Printf(TEXT("No wet material slots use selected texture '%s' on UV Channel %d."), *GetNameSafe(SourceTexture), WetPartScopePair.Y));
            continue;
        }

        const FWetClothingBakedWetnessProfileMap* ExistingWetnessProfileMap = FindBakedWetnessProfileMap(SourceTexture, WetPartScopePair.Y);
        const FString                                  CurrentBuildSignature = FWetClothingWetnessProfileMapBaker::MakeBuildSignature(
            WetClothingAssetPtr,
            SourceTexture,
            WetPartScopePair.Y,
            MaterialSlotIndices);
        const bool                                     bNeedsBake = ExistingWetnessProfileMap == nullptr ||
                                                                    ExistingWetnessProfileMap->WetnessProfileMap0 == nullptr ||
                                                                    ExistingWetnessProfileMap->MaterialSlotIndices != MaterialSlotIndices ||
                                                                    ExistingWetnessProfileMap->BuildSignature != CurrentBuildSignature;
        if (!bNeedsBake)
        {
            Skipped.Add(FString::Printf(TEXT("Wetness Profile Map for %s UV Channel %d is up to date."), *GetNameSafe(SourceTexture), WetPartScopePair.Y));
            continue;
        }

        FWetClothingWetnessProfileMapBakeSettings Settings;
        if (ExistingWetnessProfileMap != nullptr)
        {
            Settings.Resolution = ExistingWetnessProfileMap->Resolution;
            Settings.PaddingPixels = ExistingWetnessProfileMap->PaddingPixels;
        }

        FWetClothingWetnessProfileMapBakeResult Result;
        FString                                 ErrorMessage;
        if (!FWetClothingWetnessProfileMapBaker::BakeWetnessProfileMap0(WetClothingAssetPtr, SourceTexture, WetPartScopePair.Y, MaterialSlotIndices, Settings, Result, ErrorMessage))
        {
            Warnings.Add(FString::Printf(TEXT("Wetness Profile Map bake failed for %s UV Channel %d: %s"), *GetNameSafe(SourceTexture), WetPartScopePair.Y, *ErrorMessage));
            continue;
        }

        BakedWetnessProfileMaps.Add(FString::Printf(TEXT("%s -> %s"), *GetNameSafe(SourceTexture), *GetNameSafe(Result.WetnessProfileMap0.Get())));
    }

    for (int32 MapIndex = bCanPruneStaleWetnessProfileMaps ? WetClothingAssetPtr->PartData.BakedWetnessProfileMaps.Num() - 1 : INDEX_NONE; MapIndex >= 0; --MapIndex)
    {
        const FWetClothingBakedWetnessProfileMap& BakedWetnessProfileMap = WetClothingAssetPtr->PartData.BakedWetnessProfileMaps[MapIndex];
        const FString TextureUvKey = SWetClothingPartEditorPanelLocal::MakeTextureUvKey(BakedWetnessProfileMap.SourceTexture.Get(), BakedWetnessProfileMap.UVChannelIndex);
        if (TextureUvKey.IsEmpty() || BakedTextureUvKeys.Contains(TextureUvKey))
        {
            continue;
        }

        RemovedStaleWetnessProfileMaps.Add(FString::Printf(
            TEXT("%s UV Channel %d"),
            *GetNameSafe(BakedWetnessProfileMap.SourceTexture.Get()),
            BakedWetnessProfileMap.UVChannelIndex));

        WetClothingAssetPtr->Modify();
        WetClothingAssetPtr->PartData.BakedWetnessProfileMaps.RemoveAt(MapIndex);
        WetClothingAssetPtr->MarkPackageDirty();
    }

    TArray<FString> Sections;
    if (CreatedOrUpdatedMaterials.Num() > 0)
    {
        Sections.Add(FString::Printf(TEXT("Wet materials:\n- %s"), *FString::Join(CreatedOrUpdatedMaterials, TEXT("\n- "))));
    }
    if (BakedWetnessProfileMaps.Num() > 0)
    {
        Sections.Add(FString::Printf(TEXT("Wetness Profile Maps:\n- %s"), *FString::Join(BakedWetnessProfileMaps, TEXT("\n- "))));
    }
    if (RemovedStaleWetnessProfileMaps.Num() > 0)
    {
        Sections.Add(FString::Printf(TEXT("Removed stale Wetness Profile Map references:\n- %s"), *FString::Join(RemovedStaleWetnessProfileMaps, TEXT("\n- "))));
    }
    if (Skipped.Num() > 0)
    {
        Sections.Add(FString::Printf(TEXT("Skipped:\n- %s"), *FString::Join(Skipped, TEXT("\n- "))));
    }
    if (Warnings.Num() > 0)
    {
        Sections.Add(FString::Printf(TEXT("Warnings:\n- %s"), *FString::Join(Warnings, TEXT("\n- "))));
    }

    if (Warnings.Num() > 0)
    {
        Sections.Insert(TEXT("Build completed with warnings. Successful outputs were kept."), 0);
    }

    OutSummary = Sections.Num() > 0 ? FString::Join(Sections, TEXT("\n\n")) : TEXT("Wet setup is already up to date.");

    if (OutHadWarnings != nullptr)
    {
        *OutHadWarnings = Warnings.Num() > 0;
    }

    if (DetailsView.IsValid())
    {
        DetailsView->ForceRefresh();
    }

    return true;
}

bool SWetClothingPartEditorPanel::SaveWetSetupAssets() const
{
    UWetClothingAsset* WetClothingAssetPtr = WetClothingAsset.Get();
    if (WetClothingAssetPtr == nullptr)
    {
        return false;
    }

    TArray<UPackage*> PackagesToSave;
    auto              AddPackageForObject = [&PackagesToSave](UObject* Object)
    {
        if (Object == nullptr)
        {
            return;
        }

        UPackage* Package = Object->GetOutermost();
        if (Package != nullptr)
        {
            PackagesToSave.AddUnique(Package);
        }
    };

    AddPackageForObject(WetClothingAssetPtr);

    for (const FWetClothingGeneratedWetMaterialOverride& MaterialOverride : WetClothingAssetPtr->PartData.GeneratedWetMaterialOverrides)
    {
        AddPackageForObject(MaterialOverride.WetMaterial.Get());
    }

    for (const FWetClothingBakedWetnessProfileMap& BakedWetnessProfileMap : WetClothingAssetPtr->PartData.BakedWetnessProfileMaps)
    {
        AddPackageForObject(BakedWetnessProfileMap.WetnessProfileMap0.Get());
    }

    if (PackagesToSave.Num() == 0)
    {
        return true;
    }

    return FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSave, false, false) == FEditorFileUtils::PR_Success;
}

const FWetClothingBakedWetnessProfileMap* SWetClothingPartEditorPanel::FindBakedWetnessProfileMap(UTexture* SourceTexture, int32 UVChannelIndex) const
{
    const UWetClothingAsset* WetClothingAssetPtr = WetClothingAsset.Get();
    if (WetClothingAssetPtr == nullptr || SourceTexture == nullptr || UVChannelIndex == INDEX_NONE)
    {
        return nullptr;
    }

    return WetClothingAssetPtr->PartData.BakedWetnessProfileMaps.FindByPredicate(
        [SourceTexture, UVChannelIndex](const FWetClothingBakedWetnessProfileMap& BakedWetnessProfileMap)
        {
            return BakedWetnessProfileMap.SourceTexture == SourceTexture && BakedWetnessProfileMap.UVChannelIndex == UVChannelIndex;
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

    for (const FWetClothingSourceTextureSelection& Selection : WetClothingAssetPtr->PartData.EditableWetPartData.SourceTextureSelections)
    {
        if (Selection.Texture == SourceTexture && Selection.UVChannelIndex == UVChannelIndex && Selection.MaterialSlotIndex != INDEX_NONE)
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

    for (const FWetClothingSourceTextureSelection& Selection : WetClothingAssetPtr->PartData.EditableWetPartData.SourceTextureSelections)
    {
        if (Selection.UVChannelIndex == UVChannelIndex && Selection.Texture != nullptr)
        {
            OutSourceTextures.AddUnique(Selection.Texture);
        }
    }
}

void SWetClothingPartEditorPanel::SaveSelectedTexture()
{
    SaveTextureSelection(SelectedMaterialSlotIndex, GetSelectedUVChannelIndex(), ResolveSelectedMaterialTexture());
}

#undef LOCTEXT_NAMESPACE
