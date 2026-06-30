#include "SWetClothingAssetEditorPanel.h"

#include "AssetThumbnail.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Core/DynamicWetClothesEditorUtils.h"
#include "Core/DynamicWetClothesEditorStyle.h"
#include "WetClothing/AutoPartition/WetClothingAutoPartitioner.h"
#include "WetClothing/Material/WetClothingMaterialSetup.h"
#include "WetClothing/ProfileMap/WetClothingProfileMapBaker.h"
#include "WetClothing/Texture/WetClothingMaterialTextureResolver.h"
#include "WetClothing/Texture/WetClothingTextureReadback.h"
#include "WetClothing/Widgets/SWetClothingAssetUVView.h"
#include "WetClothing/Widgets/SWetClothingMaterialSlotPreview.h"
#include "WetClothingAsset.h"
#include "WetClothing/Analysis/WetClothingAssetMeshAnalyzer.h"
#include "WetClothing/Viewport/WetClothingAssetViewport.h"
#include "WetnessProfile.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "IDetailsView.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Misc/MessageDialog.h"
#include "Modules/ModuleManager.h"
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
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "WetClothingAssetEditorPanel"

namespace SWetClothingAssetEditorPanelLocal
{
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
} // namespace SWetClothingAssetEditorPanelLocal

void SWetClothingAssetEditorPanel::Construct(const FArguments& InArgs)
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
        ToolItem->IconBrushName = IconBrushName;
        UVSelectionToolItems.Add(ToolItem);
        return ToolItem;
    };

    FUVSelectionToolItemPtr SelectToolItem = AddSelectionToolItem(
        EWetClothingAssetUVSelectionTool::Select,
        LOCTEXT("UVSelectionToolSelect", "Select"),
        LOCTEXT("UVSelectionToolSelectTooltip", "Click a UV island to select it. Hold Shift to add to the current selection."),
        TEXT("DynamicWetClothesEditor.UVTool.Select"));
    AddSelectionToolItem(
        EWetClothingAssetUVSelectionTool::BoxSelect,
        LOCTEXT("UVSelectionToolBoxSelect", "Box Select"),
        LOCTEXT("UVSelectionToolBoxSelectTooltip", "Drag a box to select UV islands. Hold Shift to add to the current selection."),
        TEXT("DynamicWetClothesEditor.UVTool.BoxSelect"));
    AddSelectionToolItem(
        EWetClothingAssetUVSelectionTool::EllipseSelect,
        LOCTEXT("UVSelectionToolEllipseSelect", "Ellipse Select"),
        LOCTEXT("UVSelectionToolEllipseSelectTooltip", "Drag an ellipse to select UV islands. Hold Shift to add to the current selection."),
        TEXT("DynamicWetClothesEditor.UVTool.EllipseSelect"));
    AddSelectionToolItem(
        EWetClothingAssetUVSelectionTool::LassoSelect,
        LOCTEXT("UVSelectionToolLassoSelect", "Lasso Select"),
        LOCTEXT("UVSelectionToolLassoSelectTooltip", "Draw a freeform lasso to select UV islands. Hold Shift to add to the current selection."),
        TEXT("DynamicWetClothesEditor.UVTool.LassoSelect"));

    SelectedUVSelectionToolItem = SelectToolItem;
    CurrentUVSelectionTool = EWetClothingAssetUVSelectionTool::Select;
    UVDisplayModeItems.Reset();
    UVDisplayModeItems.Add(MakeShared<EWetClothingAssetUVDisplayMode>(EWetClothingAssetUVDisplayMode::Normal));
    UVDisplayModeItems.Add(MakeShared<EWetClothingAssetUVDisplayMode>(EWetClothingAssetUVDisplayMode::OutlineOnly));
    SelectedUVDisplayModeItem = UVDisplayModeItems[0];
    CurrentUVDisplayMode = EWetClothingAssetUVDisplayMode::Normal;

    auto BuildSelectionToolButton = [this](FUVSelectionToolItemPtr ToolItem)
    {
        return SNew(SButton)
            .ButtonColorAndOpacity(this, &SWetClothingAssetEditorPanel::GetUVSelectionToolButtonColor, ToolItem)
            .ContentPadding(FMargin(2.0f))
            .HAlign(HAlign_Center)
            .VAlign(VAlign_Center)
            .OnClicked(this, &SWetClothingAssetEditorPanel::HandleUVSelectionToolButtonClicked, ToolItem)
            .ToolTipText(ToolItem->Tooltip)
                [SNew(SBox)
                     .WidthOverride(18.0f)
                     .HeightOverride(18.0f)
                     .HAlign(HAlign_Center)
                     .VAlign(VAlign_Center)
                         [SNew(SImage)
                              .Image(this, &SWetClothingAssetEditorPanel::GetUVSelectionToolBrush, ToolItem)
                              .ColorAndOpacity(this, &SWetClothingAssetEditorPanel::GetUVSelectionToolIconColor, ToolItem)]];
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
        [SNew(SSplitter)

         // Column 1: Target Mesh / UV Channel / Material Slots / Wet Part Map.
         + SSplitter::Slot()
               .Value(0.25f)
                   [SNew(SBorder)
                        .Padding(10.0f)
                            [SNew(SVerticalBox)

                             + SVerticalBox::Slot()
                                   .AutoHeight()
                                   .Padding(0.0f, 10.0f, 0.0f, 4.0f)
                                       [SNew(SHorizontalBox)

                                        + SHorizontalBox::Slot()
                                              .FillWidth(1.0f)
                                              .VAlign(VAlign_Center)
                                                  [SNew(STextBlock)
                                                       .Text(LOCTEXT("ProfileDetailsLabel", "Wet Clothing Asset"))
                                                       .Font(PanelHeadingFont)]

                                        + SHorizontalBox::Slot()
                                              .AutoWidth()
                                                  [SNew(SButton)
                                                       .Text(LOCTEXT("SaveAssetButton", "Save"))
                                                       .OnClicked(this, &SWetClothingAssetEditorPanel::HandleSaveAssetClicked)]]

                             + SVerticalBox::Slot()
                                   .AutoHeight()
                                   .Padding(0.0f, 0.0f, 0.0f, 12.0f)
                                       [SNew(SSeparator)
                                            .Orientation(Orient_Horizontal)]

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
                                   .Padding(0.0f, 0.0f, 0.0f, 16.0f)
                                       [SAssignNew(UVChannelComboBox, SComboBox<FUVChannelItemPtr>)
                                            .OptionsSource(&UVChannelItems)
                                            .OnGenerateWidget(this, &SWetClothingAssetEditorPanel::GenerateUVChannelComboItem)
                                            .OnSelectionChanged(this, &SWetClothingAssetEditorPanel::HandleUVChannelSelectionChanged)
                                                [SNew(STextBlock)
                                                     .Text(this, &SWetClothingAssetEditorPanel::GetSelectedUVChannelText)]]

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
                                                                             .Text(this, &SWetClothingAssetEditorPanel::GetMaterialSlotCountText)]]

                                                   + SVerticalBox::Slot()
                                                         .AutoHeight()
                                                         .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                                                             [SNew(SSeparator)
                                                                  .Orientation(Orient_Horizontal)]

                                                   + SVerticalBox::Slot()
                                                         .AutoHeight()
                                                         .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                                                             [SNew(SHorizontalBox)

                                                             + SHorizontalBox::Slot()
                                                                   .AutoWidth()
                                                                   .VAlign(VAlign_Center)
                                                                        [SNew(SButton)
                                                                             .Text(LOCTEXT("AutoPartitionButton", "Auto-Partitioning"))
                                                                             .IsEnabled(this, &SWetClothingAssetEditorPanel::IsAutoPartitionEnabled)
                                                                             .OnClicked(this, &SWetClothingAssetEditorPanel::HandleAutoPartitionClicked)]

                                                              + SHorizontalBox::Slot()
                                                                    .AutoWidth()
                                                                    .VAlign(VAlign_Center)
                                                                    .Padding(6.0f, 0.0f, 0.0f, 0.0f)
                                                                        [SNew(SButton)
                                                                             .Text(LOCTEXT("ApplyMaterialSetupButton", "Apply Material Setup"))
                                                                             .ToolTipText(LOCTEXT("ApplyMaterialSetupTooltip", "Duplicate the selected material slot material, insert DWC material functions into the copy, then assign the copy to this material slot."))
                                                                             .IsEnabled(this, &SWetClothingAssetEditorPanel::IsApplyMaterialSetupEnabled)
                                                                             .OnClicked(this, &SWetClothingAssetEditorPanel::HandleApplyMaterialSetupClicked)]

                                                              + SHorizontalBox::Slot()
                                                                    .FillWidth(1.0f)
                                                                    .Padding(10.0f, 0.0f, 0.0f, 0.0f)
                                                                    .VAlign(VAlign_Center)
                                                                        [SNew(SHorizontalBox)

                                                                         + SHorizontalBox::Slot()
                                                                               .AutoWidth()
                                                                               .VAlign(VAlign_Center)
                                                                               .Padding(0.0f, 0.0f, 8.0f, 0.0f)
                                                                                   [SNew(STextBlock)
                                                                                        .Text(LOCTEXT("AutoPartitionToleranceLabel", "Color Tolerance"))]

                                                                         + SHorizontalBox::Slot()
                                                                               .FillWidth(1.0f)
                                                                                   [SNew(SSpinBox<float>)
                                                                                        .MinValue(0.0f)
                                                                                        .MaxValue(100.0f)
                                                                                        .MinSliderValue(0.0f)
                                                                                        .MaxSliderValue(100.0f)
                                                                                        .Delta(0.1f)
                                                                                        .Value(this, &SWetClothingAssetEditorPanel::GetAutoPartitionTolerance)
                                                                                        .OnValueChanged(this, &SWetClothingAssetEditorPanel::HandleAutoPartitionToleranceChanged)]]]

                                                   + SVerticalBox::Slot()
                                                         .FillHeight(1.0f)
                                                             [SAssignNew(MaterialSlotListView, SListView<FMaterialSlotItemPtr>)
                                                                  .ListItemsSource(&MaterialSlotItems)
                                                                  .OnGenerateRow(this, &SWetClothingAssetEditorPanel::GenerateMaterialSlotRow)
                                                                  .OnSelectionChanged(this, &SWetClothingAssetEditorPanel::HandleMaterialSlotSelectionChanged)
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
                                                                             .Text(this, &SWetClothingAssetEditorPanel::GetWetPartSectionText)
                                                                             .Font(SectionHeadingFont)]

                                                              + SHorizontalBox::Slot()
                                                                    .AutoWidth()
                                                                    .Padding(6.0f, 0.0f, 0.0f, 0.0f)
                                                                        [SNew(SButton)
                                                                             .Text(LOCTEXT("AddWetPartButton", "+ Add Part"))
                                                                             .OnClicked(this, &SWetClothingAssetEditorPanel::HandleAddWetPartClicked)]

                                                              + SHorizontalBox::Slot()
                                                                    .AutoWidth()
                                                                    .Padding(4.0f, 0.0f, 0.0f, 0.0f)
                                                                        [SNew(SButton)
                                                                             .Text(LOCTEXT("RemoveWetPartButton", "Remove"))
                                                                             .IsEnabled(this, &SWetClothingAssetEditorPanel::IsWetPartRemoveEnabled)
                                                                             .OnClicked(this, &SWetClothingAssetEditorPanel::HandleRemoveWetPartClicked)]]

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
                                                                  .Text(this, &SWetClothingAssetEditorPanel::GetSelectedWetPartText)]

                                                   + SVerticalBox::Slot()
                                                         .AutoHeight()
                                                         .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                                                             [SNew(STextBlock)
                                                                  .AutoWrapText(true)
                                                                  .Text(this, &SWetClothingAssetEditorPanel::GetWetnessProfileLibraryStatusText)
                                                                  .ColorAndOpacity(FSlateColor(FLinearColor(0.72f, 0.72f, 0.72f, 1.0f)))]

                                                   + SVerticalBox::Slot()
                                                         .FillHeight(1.0f)
                                                             [SAssignNew(WetPartListView, SListView<FWetPartEntryPtr>)
                                                                  .ListItemsSource(&CurrentWetPartItems)
                                                                  .OnGenerateRow(this, &SWetClothingAssetEditorPanel::GenerateWetPartRow)
                                                                  .OnSelectionChanged(this, &SWetClothingAssetEditorPanel::HandleWetPartSelectionChanged)
                                                                  .OnMouseButtonDoubleClick(this, &SWetClothingAssetEditorPanel::HandleWetPartItemDoubleClicked)
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
                                                       .Text(this, &SWetClothingAssetEditorPanel::GetUVStatusText)]

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 0.0f, 0.0f, 8.0f)
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
                                                                  .OnGenerateWidget(this, &SWetClothingAssetEditorPanel::GenerateUVDisplayModeComboItem)
                                                                  .OnSelectionChanged(this, &SWetClothingAssetEditorPanel::HandleUVDisplayModeSelectionChanged)
                                                                      [SNew(STextBlock)
                                                                           .Text(this, &SWetClothingAssetEditorPanel::GetSelectedUVDisplayModeText)]]]

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                                                  [BuildProfileMapBakePanel()]

                                        + SVerticalBox::Slot()
                                              .FillHeight(1.0f)
                                                  [SAssignNew(UVView, SWetClothingAssetUVView)
                                                       .OnIslandSelectionChanged(this, &SWetClothingAssetEditorPanel::HandleUVIslandSelectionChangedFromUVView)]]

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
                                                                           .OnGenerateWidget(this, &SWetClothingAssetEditorPanel::GenerateAssignWetPartComboItem)
                                                                           .OnSelectionChanged(this, &SWetClothingAssetEditorPanel::HandleAssignWetPartSelectionChanged)
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
                                                                                                        .BorderBackgroundColor(this, &SWetClothingAssetEditorPanel::GetSelectedAssignWetPartColor)]]

                                                                                + SHorizontalBox::Slot()
                                                                                      .FillWidth(1.0f)
                                                                                      .VAlign(VAlign_Center)
                                                                                          [SNew(STextBlock)
                                                                                               .Text(this, &SWetClothingAssetEditorPanel::GetSelectedAssignWetPartText)]]]]

                                                   + SHorizontalBox::Slot()
                                                         .AutoWidth()
                                                         .Padding(8.0f, 0.0f, 0.0f, 0.0f)
                                                         .VAlign(VAlign_Center)
                                                             [SNew(SButton)
                                                                  .ContentPadding(FMargin(10.0f, 4.0f))
                                                                  .HAlign(HAlign_Center)
                                                                  .VAlign(VAlign_Center)
                                                                  .OnClicked(this, &SWetClothingAssetEditorPanel::HandleAssignSelectedIslandToWetPartClicked)
                                                                      [SNew(STextBlock)
                                                                           .Text(this, &SWetClothingAssetEditorPanel::GetAssignIslandToWetPartText)
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
                                                       .Text(this, &SWetClothingAssetEditorPanel::GetUVIslandCountText)]

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                                                  [SNew(STextBlock)
                                                       .AutoWrapText(true)
                                                       .Text(this, &SWetClothingAssetEditorPanel::GetSelectedUVIslandText)]

                                        + SVerticalBox::Slot()
                                              .FillHeight(1.0f)
                                                  [SAssignNew(UVIslandListView, SListView<FUVIslandItemPtr>)
                                                       .ListItemsSource(&UVIslandItems)
                                                       .OnGenerateRow(this, &SWetClothingAssetEditorPanel::GenerateUVIslandRow)
                                                       .OnSelectionChanged(this, &SWetClothingAssetEditorPanel::HandleUVIslandSelectionChanged)
                                                       .SelectionMode(ESelectionMode::Multi)]]]]

         // Column 3: 3D Viewport.
         + SSplitter::Slot()
               .Value(0.375f)
                   [SNew(SBorder)
                        .Padding(8.0f)
                            [SNew(SVerticalBox)

                             + SVerticalBox::Slot()
                                   .AutoHeight()
                                   .Padding(0.0f, 10.0f, 0.0f, 4.0f)
                                       [SNew(SHorizontalBox)

                                        + SHorizontalBox::Slot()
                                              .FillWidth(1.0f)
                                              .VAlign(VAlign_Center)
                                                  [SNew(STextBlock)
                                                       .Text(LOCTEXT("Viewport3DLabel", "3D Viewport"))
                                                       .Font(SectionHeadingFont)]

                                        + SHorizontalBox::Slot()
                                              .AutoWidth()
                                              .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                                              .VAlign(VAlign_Center)
                                                  [SNew(STextBlock)
                                                       .Text(LOCTEXT("SelectionLineThicknessLabel", "Selection Line"))]

                                        + SHorizontalBox::Slot()
                                              .AutoWidth()
                                              .Padding(0.0f, 0.0f, 10.0f, 0.0f)
                                              .VAlign(VAlign_Center)
                                                  [SNew(SBox)
                                                       .WidthOverride(88.0f)
                                                           [SNew(SSpinBox<float>)
                                                                .MinValue(0.25f)
                                                                .MaxValue(4.0f)
                                                                .MinSliderValue(0.25f)
                                                                .MaxSliderValue(4.0f)
                                                                .Delta(0.05f)
                                                                .Value(this, &SWetClothingAssetEditorPanel::GetSelectionLineThicknessScale)
                                                                .OnValueChanged(this, &SWetClothingAssetEditorPanel::HandleSelectionLineThicknessChanged)]]

                                        + SHorizontalBox::Slot()
                                              .AutoWidth()
                                                  [SNew(SButton)
                                                       .Text(LOCTEXT("FocusMeshButton", "Focus Mesh"))
                                                       .OnClicked(this, &SWetClothingAssetEditorPanel::HandleFocusPreviewClicked)]]

                             + SVerticalBox::Slot()
                                   .AutoHeight()
                                   .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                                       [SNew(SSeparator)
                                            .Orientation(Orient_Horizontal)]

                             + SVerticalBox::Slot()
                                   .FillHeight(1.0f)
                                       [SAssignNew(PreviewViewport, SWetClothingAssetViewport)
                                            .WetClothingAsset(WetClothingAsset.Get())
                                            .OnIslandPicked(this, &SWetClothingAssetEditorPanel::HandleUVIslandPickedFromPreview)]]]];

    RefreshFromAsset();
}

void SWetClothingAssetEditorPanel::RefreshFromAsset()
{
    RefreshAvailableWetnessProfiles();
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

void SWetClothingAssetEditorPanel::RefreshMaterialSlotItems()
{
    const int32 PreviousSelection = SelectedMaterialSlotIndex;

    MaterialSlotItems.Reset();
    MaterialSlotThumbnails.Reset();
    SelectedMaterialSlotIndex = INDEX_NONE;

    if (const UWetClothingAsset* Profile = WetClothingAsset.Get())
    {
        if (const USkeletalMesh* TargetMesh = Profile->TargetMesh)
        {
            const TArray<FSkeletalMaterial>& Materials = TargetMesh->GetMaterials();

            for (int32 MaterialIndex = 0; MaterialIndex < Materials.Num(); ++MaterialIndex)
            {
                const FSkeletalMaterial& SkeletalMaterial = Materials[MaterialIndex];

                FMaterialSlotItemPtr Item = MakeShared<FWetClothingMaterialSlotItem>();
                Item->SlotIndex = MaterialIndex;
                Item->SlotName = SkeletalMaterial.MaterialSlotName;
                Item->Material = SkeletalMaterial.MaterialInterface;
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

void SWetClothingAssetEditorPanel::RefreshMaterialTextures()
{
    const bool bPreviousShowMaterialTextureInUVView = bShowMaterialTextureInUVView;
    UTexture*  PreviousSelectedTexture = SelectedTextureItem.IsValid() ? SelectedTextureItem->Texture.Get() : nullptr;
    TextureItems.Reset();
    TextureThumbnails.Reset();
    SelectedTextureItem.Reset();

    if (MaterialSlotItems.IsValidIndex(SelectedMaterialSlotIndex))
    {
        const FMaterialSlotItemPtr& MaterialSlotItem = MaterialSlotItems[SelectedMaterialSlotIndex];
        if (MaterialSlotItem.IsValid() && MaterialSlotItem->Material.IsValid())
        {
            FWetClothingMaterialTextureResolver::BuildTextureItems(MaterialSlotItem->Material.Get(), TextureItems);

            for (const FTextureItemPtr& TextureItem : TextureItems)
            {
                if (TextureItem.IsValid() && TextureItem->Texture.Get() == PreviousSelectedTexture)
                {
                    SelectedTextureItem = TextureItem;
                    break;
                }
            }
        }
    }

    if (!SelectedTextureItem.IsValid())
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

    if (!SelectedTextureItem.IsValid() && TextureItems.Num() > 0)
    {
        SelectedTextureItem = TextureItems[0];
    }

    bShowMaterialTextureInUVView = SelectedTextureItem.IsValid() && SelectedTextureItem->Texture.IsValid()
                                       ? bPreviousShowMaterialTextureInUVView
                                       : false;

    RefreshTextureToggleWidgets();
    RefreshUVView();
}

void SWetClothingAssetEditorPanel::RefreshTextureToggleWidgets()
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
            .OnGenerateWidget(this, &SWetClothingAssetEditorPanel::GenerateTextureComboItem)
            .OnSelectionChanged(this, &SWetClothingAssetEditorPanel::HandleTextureSelectionChanged)
            .MaxListHeight(360.0f)
            .ContentPadding(FMargin(6.0f, 4.0f))
                [SAssignNew(SelectedTextureComboContentBox, SBox)
                     [BuildTextureComboContent(SelectedTextureItem, 24.0f, true)]]);
}

void SWetClothingAssetEditorPanel::RefreshUVChannels()
{
    const int32 PreviousUVChannelIndex = SelectedUVChannelItem.IsValid() ? *SelectedUVChannelItem : INDEX_NONE;

    UVChannelItems.Reset();
    SelectedUVChannelItem.Reset();

    if (const UWetClothingAsset* Profile = WetClothingAsset.Get())
    {
        const int32 NumUVChannels = FWetClothingAssetMeshAnalyzer::GetNumUVChannels(Profile->TargetMesh, 0);
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

void SWetClothingAssetEditorPanel::RefreshUVIslandList()
{
    const int32       PreviousPrimaryIslandID = SelectedUVIslandID;
    const TSet<int32> PreviousSelectedIslandIDs = SelectedUVIslandIDs;

    UVIslandItems.Reset();
    ResetIslandSelection();
    UVStatusMessage = TEXT("Select a material slot to inspect its UV islands.");

    const UWetClothingAsset* Profile = WetClothingAsset.Get();
    if (Profile == nullptr || Profile->TargetMesh == nullptr)
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
        FString                             ErrorMessage;
        const bool                          bBuiltIslands = FWetClothingAssetMeshAnalyzer::BuildMaterialSlotUVIslands(Profile->TargetMesh, 0, *SelectedUVChannelItem, SelectedMaterialSlotIndex, BuiltIslands, &ErrorMessage);
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
        if (IslandItem.IsValid() && PreviousSelectedIslandIDs.Contains(IslandItem->IslandID))
        {
            SelectedUVIslandIDs.Add(IslandItem->IslandID);
        }
    }
    if (SelectedUVIslandIDs.Contains(PreviousPrimaryIslandID))
    {
        SelectedUVIslandID = PreviousPrimaryIslandID;
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

void SWetClothingAssetEditorPanel::RefreshUVView()
{
    if (!UVView.IsValid())
    {
        return;
    }

    UVView->SetBackgroundTexture(bShowMaterialTextureInUVView ? ResolveSelectedMaterialTexture() : nullptr);
    UVView->SetIslands(UVIslandItems);
    UVView->SetIslandColors(BuildIslandColorMap());
    UVView->SetSelectedIslands(SelectedUVIslandIDs);
    UVView->SetSelectionTool(CurrentUVSelectionTool);
    UVView->SetDisplayMode(CurrentUVDisplayMode);

    RefreshPreviewWetPartOverlay();
}

void SWetClothingAssetEditorPanel::RefreshPreviewIslandHighlight()
{
    if (!PreviewViewport.IsValid())
    {
        return;
    }

    PreviewViewport->SetSelectableIslands(UVIslandItems);
    PreviewViewport->SetHighlightedIslandIDs(SelectedUVIslandIDs);
}

void SWetClothingAssetEditorPanel::RefreshWetPartList()
{
    const int32 PreviousSelectedWetPart = SelectedWetPartID;
    const int32 PreviousAssignWetPartID = SelectedAssignWetPartID;

    CurrentWetPartItems.Reset();
    SelectedWetPartID = INDEX_NONE;
    SelectedAssignWetPartID = INDEX_NONE;
    WetPartInlineRenameWidgets.Reset();

    EnsureDefaultWetPartForSelectedScope();

    if (const UWetClothingAsset* Profile = WetClothingAsset.Get())
    {
        const int32 UVChannelIndex = GetSelectedUVChannelIndex();
        for (const FWetClothingAssetWetPartEntry& Entry : Profile->WetPartEntries)
        {
            if (Entry.MaterialSlotIndex == SelectedMaterialSlotIndex && Entry.UVChannelIndex == UVChannelIndex)
            {
                CurrentWetPartItems.Add(MakeShared<FWetClothingAssetWetPartEntry>(Entry));
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

void SWetClothingAssetEditorPanel::RefreshPreviewWetPartOverlay()
{
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->SetSelectableIslands(UVIslandItems);
        PreviewViewport->SetWetPartIslandAssignments(BuildIslandWetPartIDMap(), BuildIslandColorMap());
    }
}

void SWetClothingAssetEditorPanel::RefreshWetPartWidgets()
{
    WetPartInlineRenameWidgets.Reset();

    if (WetPartListView.IsValid())
    {
        WetPartListView->RequestListRefresh();
    }
}

void SWetClothingAssetEditorPanel::RefreshAvailableWetnessProfiles()
{
    AvailableWetnessProfileItems.Reset();

    FARFilter Filter;
    Filter.ClassPaths.Add(UWetnessProfile::StaticClass()->GetClassPathName());
    Filter.bRecursivePaths = true;

    const TArray<FString> SearchPaths = GetProfileSearchPaths();
    for (const FString& SearchPath : SearchPaths)
    {
        Filter.PackagePaths.Add(*SearchPath);
    }

    TArray<FAssetData>    AssetDataList;
    FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
    AssetRegistryModule.Get().ScanPathsSynchronous(SearchPaths, false);
    AssetRegistryModule.Get().GetAssets(Filter, AssetDataList);

    AssetDataList.Sort([](const FAssetData& A, const FAssetData& B)
                       {
		const FString APath = A.PackagePath.ToString();
		const FString BPath = B.PackagePath.ToString();
		const bool bADefault =
			APath.StartsWith(DynamicWetClothesEditorUtils::DefaultWetnessProfileLibraryPath) ||
			APath.StartsWith(DynamicWetClothesEditorUtils::PluginWetnessProfileLibraryPath);
		const bool bBDefault =
			BPath.StartsWith(DynamicWetClothesEditorUtils::DefaultWetnessProfileLibraryPath) ||
			BPath.StartsWith(DynamicWetClothesEditorUtils::PluginWetnessProfileLibraryPath);
		if (bADefault != bBDefault)
		{
			return bADefault;
		}

		return A.AssetName.ToString() < B.AssetName.ToString(); });

    for (const FAssetData& AssetData : AssetDataList)
    {
        FWetnessProfileAssetItemPtr Item = MakeShared<FWetnessProfileAssetItem>();
        Item->AssetData = AssetData;
        Item->DisplayName = AssetData.AssetName.ToString();
        Item->ContentPath = AssetData.PackagePath.ToString();
        AvailableWetnessProfileItems.Add(Item);
    }
}

void SWetClothingAssetEditorPanel::EnsureDefaultWetPartForSelectedScope()
{
    UWetClothingAsset* Profile = WetClothingAsset.Get();
    if (Profile == nullptr || SelectedMaterialSlotIndex == INDEX_NONE || !SelectedUVChannelItem.IsValid())
    {
        return;
    }
    const int32 UVChannelIndex = GetSelectedUVChannelIndex();
    for (FWetClothingAssetWetPartEntry& Entry : Profile->WetPartEntries)
    {
        if (Entry.MaterialSlotIndex == SelectedMaterialSlotIndex && Entry.UVChannelIndex == UVChannelIndex && Entry.WetPartID == 0)
        {
            Profile->Modify();
            Entry.Name = GetDefaultWetPartName(0);
            Entry.Color = GetDefaultWetPartColor(0);
            Entry.bViewEnabled = true;
            Profile->MarkPackageDirty();
            if (DetailsView.IsValid())
            {
                DetailsView->ForceRefresh();
            }
            return;
        }
    }

    Profile->Modify();
    FWetClothingAssetWetPartEntry NewEntry;
    NewEntry.MaterialSlotIndex = SelectedMaterialSlotIndex;
    NewEntry.UVChannelIndex = UVChannelIndex;
    NewEntry.WetPartID = 0;
    NewEntry.Name = GetDefaultWetPartName(NewEntry.WetPartID);
    NewEntry.Color = GetDefaultWetPartColor(NewEntry.WetPartID);
    NewEntry.bViewEnabled = true;
    Profile->WetPartEntries.Add(NewEntry);
    Profile->MarkPackageDirty();
    if (DetailsView.IsValid())
    {
        DetailsView->ForceRefresh();
    }
}

int32 SWetClothingAssetEditorPanel::GetSelectedUVChannelIndex() const
{
    return SelectedUVChannelItem.IsValid() ? *SelectedUVChannelItem : 0;
}

int32 SWetClothingAssetEditorPanel::FindNextWetPartForSelectedScope() const
{
    int32 MaxWetPartID = 0;
    if (const UWetClothingAsset* Profile = WetClothingAsset.Get())
    {
        const int32 UVChannelIndex = GetSelectedUVChannelIndex();
        for (const FWetClothingAssetWetPartEntry& Entry : Profile->WetPartEntries)
        {
            if (Entry.MaterialSlotIndex == SelectedMaterialSlotIndex && Entry.UVChannelIndex == UVChannelIndex)
            {
                MaxWetPartID = FMath::Max(MaxWetPartID, Entry.WetPartID);
            }
        }
    }

    return MaxWetPartID + 1;
}

FWetClothingAssetWetPartEntry* SWetClothingAssetEditorPanel::FindMutableWetPartEntry(int32 WetPartID) const
{
    UWetClothingAsset* Profile = WetClothingAsset.Get();
    if (Profile == nullptr)
    {
        return nullptr;
    }

    const int32 UVChannelIndex = GetSelectedUVChannelIndex();
    for (FWetClothingAssetWetPartEntry& Entry : Profile->WetPartEntries)
    {
        if (Entry.MaterialSlotIndex == SelectedMaterialSlotIndex && Entry.UVChannelIndex == UVChannelIndex && Entry.WetPartID == WetPartID)
        {
            return &Entry;
        }
    }

    return nullptr;
}

const FWetClothingAssetWetPartEntry* SWetClothingAssetEditorPanel::FindWetPartEntry(int32 WetPartID) const
{
    return FindMutableWetPartEntry(WetPartID);
}

SWetClothingAssetEditorPanel::FWetPartEntryPtr SWetClothingAssetEditorPanel::FindWetPartItemByID(int32 WetPartID) const
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

const FWetClothingAssetWetPartEntry* SWetClothingAssetEditorPanel::FindWetPartEntryForIsland(int32 IslandID) const
{
    if (const UWetClothingAsset* Profile = WetClothingAsset.Get())
    {
        const int32 UVChannelIndex = GetSelectedUVChannelIndex();
        for (const FWetClothingAssetWetPartEntry& Entry : Profile->WetPartEntries)
        {
            if (Entry.MaterialSlotIndex == SelectedMaterialSlotIndex && Entry.UVChannelIndex == UVChannelIndex && Entry.AssignedIslandIDs.Contains(IslandID))
            {
                return &Entry;
            }
        }
    }
    return nullptr;
}

const FWetClothingAssetWetPartEntry* SWetClothingAssetEditorPanel::FindEffectiveWetPartEntryForIsland(int32 IslandID) const
{
    if (const FWetClothingAssetWetPartEntry* AssignedEntry = FindWetPartEntryForIsland(IslandID))
    {
        return AssignedEntry;
    }
    return FindWetPartEntry(0);
}

TSet<int32> SWetClothingAssetEditorPanel::GetIslandIDsForWetPart(int32 WetPartID) const
{
    TSet<int32> Result;

    for (const FUVIslandItemPtr& IslandItem : UVIslandItems)
    {
        if (IslandItem.IsValid() && GetEffectiveWetPartForIsland(IslandItem->IslandID) == WetPartID)
        {
            Result.Add(IslandItem->IslandID);
        }
    }

    return Result;
}

int32 SWetClothingAssetEditorPanel::GetEffectiveWetPartForIsland(int32 IslandID) const
{
    if (const FWetClothingAssetWetPartEntry* EffectiveEntry = FindEffectiveWetPartEntryForIsland(IslandID))
    {
        return EffectiveEntry->WetPartID;
    }
    return 0;
}

FLinearColor SWetClothingAssetEditorPanel::GetDefaultWetPartColor(int32 WetPartID) const
{
    if (WetPartID == 0)
    {
        return FLinearColor(0.62f, 0.62f, 0.62f, 1.0f);
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

FString SWetClothingAssetEditorPanel::GetDefaultWetPartName(int32 WetPartID) const
{
    return WetPartID == 0 ? TEXT("Part Default") : FString::Printf(TEXT("Part %d"), WetPartID);
}

FString SWetClothingAssetEditorPanel::GetWetPartDisplayName(const FWetClothingAssetWetPartEntry& Entry) const
{
    const FString TrimmedName = Entry.Name.TrimStartAndEnd();
    if (!TrimmedName.IsEmpty())
    {
        return TrimmedName;
    }

    return GetDefaultWetPartName(Entry.WetPartID);
}

FString SWetClothingAssetEditorPanel::GetAssignedProfileLabel(const FWetClothingAssetWetPartEntry& Entry) const
{
    const FString TrimmedLabel = Entry.ProfileAssignment.SourceProfileName.TrimStartAndEnd();
    return TrimmedLabel.IsEmpty() ? TEXT("Select Profile") : TrimmedLabel;
}

TArray<FString> SWetClothingAssetEditorPanel::GetProfileSearchPaths() const
{
    if (const UWetClothingAsset* Profile = WetClothingAsset.Get())
    {
#if WITH_EDITORONLY_DATA
        return DynamicWetClothesEditorUtils::BuildUniqueProfileSearchPaths(Profile->AdditionalProfileSearchPaths);
#endif
    }

    const TArray<FString> EmptyPaths;
    return DynamicWetClothesEditorUtils::BuildUniqueProfileSearchPaths(EmptyPaths);
}

TMap<int32, int32> SWetClothingAssetEditorPanel::BuildIslandWetPartIDMap() const
{
    TMap<int32, int32> Result;
    for (const FUVIslandItemPtr& IslandItem : UVIslandItems)
    {
        if (!IslandItem.IsValid())
        {
            continue;
        }

        const int32 EffectiveWetPartID = GetEffectiveWetPartForIsland(IslandItem->IslandID);
        if (EffectiveWetPartID == 0)
        {
            continue;
        }

        if (FindWetPartEntry(EffectiveWetPartID) != nullptr)
        {
            Result.Add(IslandItem->IslandID, EffectiveWetPartID);
        }
    }
    return Result;
}

TMap<int32, FLinearColor> SWetClothingAssetEditorPanel::BuildIslandColorMap() const
{
    TMap<int32, FLinearColor> Result;
    const FLinearColor        HiddenColor(0.45f, 0.45f, 0.45f, 1.0f);

    for (const FUVIslandItemPtr& IslandItem : UVIslandItems)
    {
        if (!IslandItem.IsValid())
        {
            continue;
        }

        if (const FWetClothingAssetWetPartEntry* Entry = FindEffectiveWetPartEntryForIsland(IslandItem->IslandID))
        {
            if (Entry->WetPartID == 0)
            {
                continue;
            }

            FLinearColor Color = Entry->bViewEnabled ? Entry->Color : HiddenColor;
            Color.A = 1.0f;
            Result.Add(IslandItem->IslandID, Color);
        }
    }

    return Result;
}

TSharedRef<ITableRow> SWetClothingAssetEditorPanel::GenerateMaterialSlotRow(FMaterialSlotItemPtr Item, const TSharedRef<STableViewBase>& OwnerTable)
{
    UMaterialInterface* MaterialObject = Item.IsValid() ? Item->Material.Get() : nullptr;
    const FText         SlotTitle = Item.IsValid()
                                        ? FText::Format(
                                      LOCTEXT("MaterialSlotThumbnailTitle", "[{0}] {1}"),
                                      FText::AsNumber(Item->SlotIndex),
                                      FText::FromName(Item->SlotName))
                                        : LOCTEXT("InvalidMaterialSlotTitle", "Invalid Material Slot");

    TSharedRef<SWidget> ThumbnailWidget =
        SNew(SBorder)
            .BorderImage(FAppStyle::Get().GetBrush(TEXT("WhiteBrush")))
            .BorderBackgroundColor(FLinearColor(0.06f, 0.06f, 0.06f, 1.0f));

    TArray<FWetClothingAssetUVTriangle> SlotPreviewTriangles;
    UTexture*                             SlotPreviewTexture = FWetClothingMaterialTextureResolver::ResolveBestMaterialTexture(MaterialObject);
    if (Item.IsValid() && Item->SlotIndex == SelectedMaterialSlotIndex && SelectedTextureItem.IsValid() && SelectedTextureItem->Texture.IsValid())
    {
        SlotPreviewTexture = SelectedTextureItem->Texture.Get();
    }
    if (const UWetClothingAsset* Profile = WetClothingAsset.Get())
    {
        SlotPreviewTriangles = SWetClothingAssetEditorPanelLocal::BuildMaterialSlotPreviewTriangles(Profile->TargetMesh, Item.IsValid() ? Item->SlotIndex : INDEX_NONE);
    }

    TSharedRef<SWidget> SlotPreviewWidget =
        SNew(SBorder)
            .Padding(0.0f)
            .BorderImage(FAppStyle::Get().GetBrush(TEXT("Brushes.Panel")))
                [SNew(SWetClothingMaterialSlotPreview)
                     .Triangles(MoveTemp(SlotPreviewTriangles))
                     .PreviewTexture(SlotPreviewTexture)];

    if (MaterialObject != nullptr && MaterialThumbnailPool.IsValid())
    {
        TSharedPtr<FAssetThumbnail> Thumbnail = MakeShared<FAssetThumbnail>(MaterialObject, 48, 48, MaterialThumbnailPool);
        MaterialSlotThumbnails.Add(Thumbnail);

        FAssetThumbnailConfig ThumbnailConfig;
        ThumbnailConfig.bAllowFadeIn = false;
        ThumbnailWidget = Thumbnail->MakeThumbnailWidget(ThumbnailConfig);
    }

    return SNew(STableRow<FMaterialSlotItemPtr>, OwnerTable)
        .Padding(4.0f)
            [SNew(SHorizontalBox)

             + SHorizontalBox::Slot()
                   .AutoWidth()
                   .VAlign(VAlign_Center)
                   .Padding(0.0f, 0.0f, 8.0f, 0.0f)
                       [SNew(SBox)
                            .WidthOverride(52.0f)
                            .HeightOverride(52.0f)
                                [ThumbnailWidget]]

             + SHorizontalBox::Slot()
                   .AutoWidth()
                   .VAlign(VAlign_Center)
                   .Padding(0.0f, 0.0f, 8.0f, 0.0f)
                       [SNew(SBox)
                            .WidthOverride(52.0f)
                            .HeightOverride(52.0f)
                                [SlotPreviewWidget]]

             + SHorizontalBox::Slot()
                   .FillWidth(1.0f)
                   .VAlign(VAlign_Center)
                       [SNew(SVerticalBox)

                        + SVerticalBox::Slot()
                              .AutoHeight()
                                  [SNew(STextBlock)
                                       .Text(SlotTitle)]]];
}

void SWetClothingAssetEditorPanel::HandleMaterialSlotSelectionChanged(FMaterialSlotItemPtr Item, ESelectInfo::Type SelectInfo)
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

TSharedRef<SWidget> SWetClothingAssetEditorPanel::GenerateTextureComboItem(FTextureItemPtr Item)
{
    return BuildTextureComboContent(Item, 36.0f, false);
}

void SWetClothingAssetEditorPanel::HandleTextureSelectionChanged(FTextureItemPtr Item, ESelectInfo::Type SelectInfo)
{
    SelectedTextureItem = Item;
    bShowMaterialTextureInUVView = SelectedTextureItem.IsValid() && SelectedTextureItem->Texture.IsValid();

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

TSharedRef<SWidget> SWetClothingAssetEditorPanel::BuildTextureComboContent(FTextureItemPtr Item, float ThumbnailSize, bool bCompactLayout)
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

    return SNew(SHorizontalBox) + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(ThumbnailSize).HeightOverride(ThumbnailSize)[ThumbnailWidget]] + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(bCompactLayout ? FMargin(8.0f, 0.0f, 18.0f, 0.0f) : FMargin(8.0f, 0.0f, 6.0f, 0.0f))[SNew(STextBlock).Text(Item.IsValid() ? FText::FromString(Item->Label) : LOCTEXT("InvalidTextureComboItem", "Invalid Texture")).OverflowPolicy(ETextOverflowPolicy::Ellipsis)];
}

TSharedRef<SWidget> SWetClothingAssetEditorPanel::BuildProfileMapBakePanel()
{
    return SNew(SBorder)
        .Padding(8.0f)
        .BorderImage(FAppStyle::Get().GetBrush(TEXT("ToolPanel.GroupBorder")))
            [SNew(SVerticalBox)

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                       [SNew(SHorizontalBox)

                        + SHorizontalBox::Slot()
                              .FillWidth(1.0f)
                              .VAlign(VAlign_Center)
                                  [SNew(STextBlock)
                                       .Text(LOCTEXT("ProfileMapBakeLabel", "ProfileMap Bake"))
                                       .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 11))]

                        + SHorizontalBox::Slot()
                              .AutoWidth()
                              .Padding(6.0f, 0.0f, 0.0f, 0.0f)
                                  [SNew(SButton)
                                       .IsEnabled(this, &SWetClothingAssetEditorPanel::IsProfileMapBakeSourceValid)
                                       .ToolTipText(LOCTEXT("BakeSelectedProfileMapTooltip", "Generate the ProfileMap texture for the selected source texture."))
                                       .OnClicked(this, &SWetClothingAssetEditorPanel::HandleBakeSelectedProfileMapClicked)
                                       .Text(LOCTEXT("BakeSelectedProfileMapButton", "Bake Selected"))]

                        + SHorizontalBox::Slot()
                              .AutoWidth()
                              .Padding(4.0f, 0.0f, 0.0f, 0.0f)
                                  [SNew(SButton)
                                       .IsEnabled(this, &SWetClothingAssetEditorPanel::CanBakeAnyProfileMap)
                                       .ToolTipText(LOCTEXT("BakeAllProfileMapsTooltip", "Generate ProfileMap textures for all source textures on the selected UV channel."))
                                       .OnClicked(this, &SWetClothingAssetEditorPanel::HandleBakeAllProfileMapsClicked)
                                       .Text(LOCTEXT("BakeAllProfileMapsButton", "Bake All"))]]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 3.0f)
                       [SNew(STextBlock)
                            .AutoWrapText(true)
                            .Text(this, &SWetClothingAssetEditorPanel::GetProfileMapBakeSourceText)]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 3.0f)
                       [SNew(STextBlock)
                            .AutoWrapText(true)
                            .Text(this, &SWetClothingAssetEditorPanel::GetProfileMapBakeSlotsText)
                            .ColorAndOpacity(FSlateColor(FLinearColor(0.72f, 0.72f, 0.72f, 1.0f)))]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 3.0f)
                       [SNew(STextBlock)
                            .AutoWrapText(true)
                            .Text(this, &SWetClothingAssetEditorPanel::GetProfileMapBakeSettingsText)
                            .ColorAndOpacity(FSlateColor(FLinearColor(0.72f, 0.72f, 0.72f, 1.0f)))]

             + SVerticalBox::Slot()
                   .AutoHeight()
                       [SNew(STextBlock)
                            .AutoWrapText(true)
                            .Text(this, &SWetClothingAssetEditorPanel::GetProfileMapBakeStatusText)
                            .ColorAndOpacity(FSlateColor(FStyleColors::ForegroundHover))]];
}

FReply SWetClothingAssetEditorPanel::HandleApplyMaterialSetupClicked()
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

    FWetClothingMaterialSetupResult Result = FWetClothingMaterialSetup::DuplicateAndApplyToMaterialInterface(SelectedMaterial);

    if (Result.bSucceeded && !Result.bAlreadyConfigured && Result.ConfiguredMaterial != nullptr)
    {
        if (UWetClothingAsset* Profile = WetClothingAsset.Get())
        {
            if (USkeletalMesh* TargetMesh = Profile->TargetMesh)
            {
                TArray<FSkeletalMaterial> Materials = TargetMesh->GetMaterials();
                if (Materials.IsValidIndex(SelectedMaterialSlotIndex))
                {
                    TargetMesh->Modify();
                    Materials[SelectedMaterialSlotIndex].MaterialInterface = Result.ConfiguredMaterial;
                    TargetMesh->SetMaterials(Materials);
                    TargetMesh->PostEditChange();
                    TargetMesh->MarkPackageDirty();

                    Result.Message += FString::Printf(TEXT("\nAssigned '%s' to material slot %d on '%s'."),
                        *Result.ConfiguredMaterial->GetName(),
                        SelectedMaterialSlotIndex,
                        *TargetMesh->GetName());

                    RefreshMaterialSlotItems();
                    RefreshMaterialTextures();
                    if (PreviewViewport.IsValid())
                    {
                        PreviewViewport->RefreshPreviewMesh();
                        PreviewViewport->SetHighlightedMaterialSlot(SelectedMaterialSlotIndex);
                    }
                }
            }
        }
    }

    const EAppMsgCategory           MessageCategory = Result.bSucceeded ? EAppMsgCategory::Success : EAppMsgCategory::Error;
    FMessageDialog::Open(MessageCategory, EAppMsgType::Ok, FText::FromString(Result.Message));

    return FReply::Handled();
}

bool SWetClothingAssetEditorPanel::IsApplyMaterialSetupEnabled() const
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

TSharedRef<SWidget> SWetClothingAssetEditorPanel::GenerateUVChannelComboItem(FUVChannelItemPtr Item)
{
    const FString Label = Item.IsValid()
                              ? FString::Printf(TEXT("UV Channel %d"), *Item)
                              : TEXT("Invalid UV Channel");

    return SNew(STextBlock)
        .Text(FText::FromString(Label));
}

void SWetClothingAssetEditorPanel::HandleUVChannelSelectionChanged(FUVChannelItemPtr Item, ESelectInfo::Type SelectInfo)
{
    SelectedUVChannelItem = Item;
    SelectedWetPartID = INDEX_NONE;
    ResetIslandSelection();
    RefreshMaterialTextures();
    RefreshWetPartList();
    RefreshUVIslandList();
}

TSharedRef<SWidget> SWetClothingAssetEditorPanel::GenerateUVDisplayModeComboItem(FUVDisplayModeItemPtr Item)
{
    const FText Label = (!Item.IsValid() || *Item == EWetClothingAssetUVDisplayMode::Normal)
                            ? LOCTEXT("UVDisplayModeNormal", "Normal")
                            : LOCTEXT("UVDisplayModeOutline", "Outline");

    return SNew(STextBlock)
        .Text(Label);
}

void SWetClothingAssetEditorPanel::HandleUVDisplayModeSelectionChanged(FUVDisplayModeItemPtr Item, ESelectInfo::Type SelectInfo)
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

TSharedRef<ITableRow> SWetClothingAssetEditorPanel::GenerateUVIslandRow(FUVIslandItemPtr Item, const TSharedRef<STableViewBase>& OwnerTable)
{
    FText        RowText = LOCTEXT("InvalidUVIsland", "Invalid UV island");
    FLinearColor SwatchColor(0.06f, 0.06f, 0.06f, 1.0f);
    if (Item.IsValid())
    {
        if (const FWetClothingAssetWetPartEntry* EffectiveEntry = FindEffectiveWetPartEntryForIsland(Item->IslandID))
        {
            SwatchColor = (EffectiveEntry->WetPartID == 0 || EffectiveEntry->bViewEnabled)
                              ? EffectiveEntry->Color
                              : FLinearColor(0.45f, 0.45f, 0.45f, 1.0f);
            SwatchColor.A = 1.0f;
            RowText = FText::Format(LOCTEXT("UVIslandAssignedRow", "Island {0}  |  {1} tris  |  ID {2}"), FText::AsNumber(Item->IslandID), FText::AsNumber(Item->TriangleCount), FText::AsNumber(EffectiveEntry->WetPartID));
        }
    }

    return SNew(STableRow<FUVIslandItemPtr>, OwnerTable)
        .Padding(4.0f)
            [SNew(SHorizontalBox) + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)[SNew(SBox).WidthOverride(18.0f).HeightOverride(18.0f)[SNew(SBorder).BorderImage(FAppStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(SwatchColor)]] + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[SNew(STextBlock).AutoWrapText(true).Text(RowText)]];
}

void SWetClothingAssetEditorPanel::HandleUVIslandSelectionChanged(FUVIslandItemPtr Item, ESelectInfo::Type SelectInfo)
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
            NewSelectedIDs.Add(SelectedItem->IslandID);
        }
    }
    const int32 NewPrimaryID = Item.IsValid() ? Item->IslandID : (NewSelectedIDs.Num() > 0 ? *NewSelectedIDs.CreateConstIterator() : INDEX_NONE);
    SetSelectedIslandIDs(NewSelectedIDs, NewPrimaryID, false);
}

void SWetClothingAssetEditorPanel::HandleUVIslandSelectionChangedFromUVView(const TArray<int32>& IslandIDs, EWetClothingAssetUVSelectionOp SelectionOp)
{
    ApplyIslandSelection(IslandIDs, SelectionOp == EWetClothingAssetUVSelectionOp::Add);
}

void SWetClothingAssetEditorPanel::HandleUVIslandPickedFromPreview(int32 IslandID, bool bAppendSelection)
{
    if (IslandID == INDEX_NONE)
    {
        if (!bAppendSelection)
        {
            SetSelectedIslandIDs(TSet<int32>(), INDEX_NONE);
        }
        return;
    }
    ApplyIslandSelection({ IslandID }, bAppendSelection);
}

void SWetClothingAssetEditorPanel::ApplyIslandSelection(const TArray<int32>& HitIslandIDs, bool bAppendSelection)
{
    TSet<int32> NewSelection = bAppendSelection ? SelectedUVIslandIDs : TSet<int32>();
    for (int32 IslandID : HitIslandIDs)
    {
        if (IslandID != INDEX_NONE)
        {
            NewSelection.Add(IslandID);
        }
    }
    const int32 NewPrimaryID = HitIslandIDs.Num() > 0 ? HitIslandIDs.Last() : INDEX_NONE;
    SetSelectedIslandIDs(NewSelection, NewPrimaryID);
}

void SWetClothingAssetEditorPanel::SetSelectedIslandIDs(const TSet<int32>& InSelectedIslandIDs, int32 InPrimarySelectedIslandID, bool bSyncListSelection)
{
    SelectedUVIslandIDs = InSelectedIslandIDs;
    SelectedUVIslandID = SelectedUVIslandIDs.Contains(InPrimarySelectedIslandID) ? InPrimarySelectedIslandID : (SelectedUVIslandIDs.Num() > 0 ? *SelectedUVIslandIDs.CreateConstIterator() : INDEX_NONE);

    bool bKeepSelectedWetPart = SelectedWetPartID == INDEX_NONE;
    if (!bKeepSelectedWetPart)
    {
        const TSet<int32> WetPartIslandIDs = GetIslandIDsForWetPart(SelectedWetPartID);
        bKeepSelectedWetPart = WetPartIslandIDs.Num() == SelectedUVIslandIDs.Num();
        if (bKeepSelectedWetPart)
        {
            for (int32 IslandID : WetPartIslandIDs)
            {
                if (!SelectedUVIslandIDs.Contains(IslandID))
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

void SWetClothingAssetEditorPanel::SyncUVIslandListSelectionToState()
{
    if (!UVIslandListView.IsValid())
    {
        return;
    }
    bSyncingUVIslandListSelection = true;
    UVIslandListView->ClearSelection();
    for (const FUVIslandItemPtr& IslandItem : UVIslandItems)
    {
        if (IslandItem.IsValid() && SelectedUVIslandIDs.Contains(IslandItem->IslandID))
        {
            UVIslandListView->SetItemSelection(IslandItem, true, ESelectInfo::Direct);
            if (IslandItem->IslandID == SelectedUVIslandID)
            {
                UVIslandListView->RequestScrollIntoView(IslandItem);
            }
        }
    }
    bSyncingUVIslandListSelection = false;
}

void SWetClothingAssetEditorPanel::ResetIslandSelection()
{
    SelectedUVIslandID = INDEX_NONE;
    SelectedUVIslandIDs.Reset();
    if (UVIslandListView.IsValid())
    {
        UVIslandListView->ClearSelection();
    }
}

TSharedRef<ITableRow> SWetClothingAssetEditorPanel::GenerateWetPartRow(FWetPartEntryPtr Item, const TSharedRef<STableViewBase>& OwnerTable)
{
    const FLinearColor                   Color = Item.IsValid() ? ((Item->WetPartID == 0 || Item->bViewEnabled) ? Item->Color : FLinearColor(0.45f, 0.45f, 0.45f, 1.0f)) : FLinearColor::White;
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
                                                        .OnClicked(this, &SWetClothingAssetEditorPanel::HandleToggleWetPartViewClicked, Item)
                                                        .IsEnabled_Lambda([Item]()
                                                                          { return Item.IsValid() && Item->WetPartID != 0; })
                                                            [SNew(SImage)
                                                                 .Image(this, &SWetClothingAssetEditorPanel::GetWetPartVisibilityBrush, Item)]]

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
                                                                 .OnClicked(this, &SWetClothingAssetEditorPanel::HandleWetPartColorClicked, Item)
                                                                     [SNew(SColorBlock)
                                                                          .Color(Color)
                                                                          .Size(FVector2D(30.0f, 30.0f))
                                                                          .ShowBackgroundForAlpha(false)]]]

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
                                                                              .OnTextCommitted(this, &SWetClothingAssetEditorPanel::HandleWetPartNameCommitted, Item)]

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
                                                                         [SNew(SComboButton)
                                                                              .ContentPadding(FMargin(8.0f, 3.0f))
                                                                              .OnGetMenuContent(this, &SWetClothingAssetEditorPanel::BuildWetnessProfilePickerMenu, Item)
                                                                              .ButtonContent()
                                                                                  [SNew(STextBlock)
                                                                                       .Text(this, &SWetClothingAssetEditorPanel::GetWetnessProfileButtonText, Item)]]]]];

    if (Item.IsValid() && InlineTextBlock.IsValid())
    {
        WetPartInlineRenameWidgets.Add(Item->WetPartID, InlineTextBlock);
    }

    return Row;
}

void SWetClothingAssetEditorPanel::HandleWetPartSelectionChanged(FWetPartEntryPtr Item, ESelectInfo::Type SelectInfo)
{
    SelectedWetPartID = Item.IsValid() ? Item->WetPartID : INDEX_NONE;

    if (WetPartListView.IsValid() && Item.IsValid() && !WetPartListView->IsItemSelected(Item))
    {
        WetPartListView->SetSelection(Item);
    }

    if (Item.IsValid())
    {
        const TSet<int32> IslandsForWetPart = GetIslandIDsForWetPart(Item->WetPartID);
        const int32       PrimaryIslandID = IslandsForWetPart.Num() > 0 ? *IslandsForWetPart.CreateConstIterator() : INDEX_NONE;
        SetSelectedIslandIDs(IslandsForWetPart, PrimaryIslandID);
        return;
    }

    RefreshUVView();
}

void SWetClothingAssetEditorPanel::HandleWetPartItemDoubleClicked(FWetPartEntryPtr Item)
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

void SWetClothingAssetEditorPanel::HandleWetPartNameCommitted(const FText& InText, ETextCommit::Type CommitType, FWetPartEntryPtr Item)
{
    if (!Item.IsValid())
    {
        return;
    }

    UWetClothingAsset*             Profile = WetClothingAsset.Get();
    FWetClothingAssetWetPartEntry* Entry = FindMutableWetPartEntry(Item->WetPartID);
    if (Profile == nullptr || Entry == nullptr)
    {
        return;
    }

    Profile->Modify();
    const FString TrimmedName = InText.ToString().TrimStartAndEnd();
    Entry->Name = TrimmedName.IsEmpty() ? GetDefaultWetPartName(Entry->WetPartID) : TrimmedName;
    Profile->MarkPackageDirty();

    if (DetailsView.IsValid())
    {
        DetailsView->ForceRefresh();
    }

    RefreshWetPartList();
    RefreshUVView();
}

FReply SWetClothingAssetEditorPanel::HandleWetPartColorClicked(FWetPartEntryPtr Item)
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
    PickerArgs.OnColorCommitted = FOnLinearColorValueChanged::CreateSP(this, &SWetClothingAssetEditorPanel::HandleWetPartColorCommitted, Item);
    OpenColorPicker(PickerArgs);

    return FReply::Handled();
}

void SWetClothingAssetEditorPanel::HandleWetPartColorCommitted(FLinearColor NewColor, FWetPartEntryPtr Item)
{
    if (!Item.IsValid() || Item->WetPartID == 0)
    {
        return;
    }

    UWetClothingAsset*             Profile = WetClothingAsset.Get();
    FWetClothingAssetWetPartEntry* Entry = FindMutableWetPartEntry(Item->WetPartID);
    if (Profile == nullptr || Entry == nullptr)
    {
        return;
    }

    NewColor.A = 1.0f;
    Profile->Modify();
    Entry->Color = NewColor;
    Item->Color = NewColor;
    Profile->MarkPackageDirty();

    if (DetailsView.IsValid())
    {
        DetailsView->ForceRefresh();
    }

    RefreshWetPartList();
    RefreshUVView();
    RefreshPreviewWetPartOverlay();
}

FReply SWetClothingAssetEditorPanel::HandleToggleWetPartViewClicked(FWetPartEntryPtr Item)
{
    if (!Item.IsValid() || Item->WetPartID == 0)
    {
        return FReply::Handled();
    }

    UWetClothingAsset*             Profile = WetClothingAsset.Get();
    FWetClothingAssetWetPartEntry* Entry = FindMutableWetPartEntry(Item->WetPartID);
    if (Profile != nullptr && Entry != nullptr)
    {
        Profile->Modify();
        Entry->bViewEnabled = !Entry->bViewEnabled;
        Profile->MarkPackageDirty();

        if (DetailsView.IsValid())
        {
            DetailsView->ForceRefresh();
        }
    }

    RefreshWetPartList();
    RefreshUVView();
    return FReply::Handled();
}

const FSlateBrush* SWetClothingAssetEditorPanel::GetWetPartVisibilityBrush(FWetPartEntryPtr Item) const
{
    const bool bVisible = Item.IsValid() ? Item->bViewEnabled : false;
    return FAppStyle::Get().GetBrush(bVisible ? TEXT("Icons.Visible") : TEXT("Icons.Hidden"));
}

TSharedRef<SWidget> SWetClothingAssetEditorPanel::BuildWetnessProfilePickerMenu(FWetPartEntryPtr Item)
{
    TSharedRef<SVerticalBox> MenuContent = SNew(SVerticalBox);

    MenuContent->AddSlot()
        .AutoHeight()
        .Padding(0.0f, 0.0f, 0.0f, 6.0f)
            [SNew(STextBlock)
                 .Text(LOCTEXT("ProfileMenuHeader", "Choose a Wetness Profile"))
                 .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 10))];

    MenuContent->AddSlot()
        .AutoHeight()
        .Padding(0.0f, 0.0f, 0.0f, 8.0f)
            [SNew(STextBlock)
                 .Text(this, &SWetClothingAssetEditorPanel::GetWetnessProfileLibraryStatusText)
                 .AutoWrapText(true)];

    MenuContent->AddSlot()
        .AutoHeight()
        .Padding(0.0f, 0.0f, 0.0f, 4.0f)
            [SNew(SButton)
                 .Text(LOCTEXT("ClearProfileAssignment", "Clear Profile"))
                 .OnClicked_Lambda([this, Item]()
                                   {
			HandleWetnessProfilePicked(Item, nullptr);
			FSlateApplication::Get().DismissAllMenus();
			return FReply::Handled(); })];

    TSharedRef<SVerticalBox> ProfileButtons = SNew(SVerticalBox);
    for (const FWetnessProfileAssetItemPtr& ProfileItem : AvailableWetnessProfileItems)
    {
        ProfileButtons->AddSlot()
            .AutoHeight()
            .Padding(0.0f, 0.0f, 0.0f, 4.0f)
                [SNew(SButton)
                     .HAlign(HAlign_Left)
                     .ContentPadding(FMargin(8.0f, 4.0f))
                     .OnClicked_Lambda([this, Item, ProfileItem]()
                                       {
				HandleWetnessProfilePicked(Item, ProfileItem);
				FSlateApplication::Get().DismissAllMenus();
				return FReply::Handled(); })
                         [SNew(SVerticalBox)

                          + SVerticalBox::Slot()
                                .AutoHeight()
                                    [SNew(STextBlock)
                                         .Text(FText::FromString(ProfileItem.IsValid() ? ProfileItem->DisplayName : TEXT("Invalid Profile")))]

                          + SVerticalBox::Slot()
                                .AutoHeight()
                                    [SNew(STextBlock)
                                         .Text(FText::FromString(ProfileItem.IsValid() ? ProfileItem->ContentPath : TEXT("")))
                                         .ColorAndOpacity(FSlateColor(FLinearColor(0.72f, 0.72f, 0.72f, 1.0f)))]]];
    }

    MenuContent->AddSlot()
        .FillHeight(1.0f)
        .Padding(0.0f, 0.0f, 0.0f, 8.0f)
            [SNew(SBox)
                 .MaxDesiredHeight(280.0f)
                 .MinDesiredWidth(280.0f)
                     [SNew(SScrollBox) + SScrollBox::Slot()
                                             [ProfileButtons]]];

    MenuContent->AddSlot()
        .AutoHeight()
            [SNew(SButton)
                 .Text(LOCTEXT("AddProfileFolderButton", "Add Content Folder"))
                 .OnClicked_Lambda([this]()
                                   {
			HandleAddProfileSearchPathClicked();
			FSlateApplication::Get().DismissAllMenus();
			return FReply::Handled(); })];

    return SNew(SBorder)
        .Padding(8.0f)
            [MenuContent];
}

void SWetClothingAssetEditorPanel::HandleWetnessProfilePicked(FWetPartEntryPtr Item, FWetnessProfileAssetItemPtr ProfileItem)
{
    if (!Item.IsValid())
    {
        return;
    }

    UWetClothingAsset*             Profile = WetClothingAsset.Get();
    FWetClothingAssetWetPartEntry* Entry = FindMutableWetPartEntry(Item->WetPartID);
    if (Profile == nullptr || Entry == nullptr)
    {
        return;
    }

    Profile->Modify();

    if (ProfileItem.IsValid())
    {
        if (const UWetnessProfile* SourceProfile = Cast<UWetnessProfile>(ProfileItem->AssetData.GetAsset()))
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

    Profile->MarkPackageDirty();

    if (DetailsView.IsValid())
    {
        DetailsView->ForceRefresh();
    }

    RefreshWetPartList();
}

FReply SWetClothingAssetEditorPanel::HandleAddProfileSearchPathClicked()
{
    UWetClothingAsset* Profile = WetClothingAsset.Get();
    if (Profile == nullptr)
    {
        return FReply::Handled();
    }

    FString NewContentPath;
    if (!DynamicWetClothesEditorUtils::PromptForContentFolder(NewContentPath))
    {
        return FReply::Handled();
    }

#if WITH_EDITORONLY_DATA
    if (!Profile->AdditionalProfileSearchPaths.Contains(NewContentPath))
    {
        Profile->Modify();
        Profile->AdditionalProfileSearchPaths.Add(NewContentPath);
        Profile->MarkPackageDirty();
    }
#endif

    RefreshAvailableWetnessProfiles();

    if (DetailsView.IsValid())
    {
        DetailsView->ForceRefresh();
    }

    return FReply::Handled();
}

TSharedRef<SWidget> SWetClothingAssetEditorPanel::GenerateAssignWetPartComboItem(FWetPartEntryPtr Item)
{
    const FLinearColor Color = Item.IsValid()
                                   ? ((Item->WetPartID == 0 || Item->bViewEnabled) ? Item->Color : FLinearColor(0.45f, 0.45f, 0.45f, 1.0f))
                                   : FLinearColor::White;

    return SNew(SHorizontalBox) + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 6.0f, 0.0f)[SNew(SBox).WidthOverride(14.0f).HeightOverride(14.0f)[SNew(SBorder).BorderImage(FAppStyle::Get().GetBrush(TEXT("WhiteBrush"))).BorderBackgroundColor(Color)]]

           + SHorizontalBox::Slot()
                 .FillWidth(1.0f)
                 .VAlign(VAlign_Center)
                     [SNew(STextBlock)
                          .Text(Item.IsValid()
                                    ? FText::Format(
                                          LOCTEXT("AssignWetPartOption", "{0}  |  ID {1}"),
                                          FText::FromString(GetWetPartDisplayName(*Item)),
                                          FText::AsNumber(Item->WetPartID))
                                    : LOCTEXT("AssignWetPartInvalid", "Invalid Part"))];
}

void SWetClothingAssetEditorPanel::HandleAssignWetPartSelectionChanged(FWetPartEntryPtr Item, ESelectInfo::Type SelectInfo)
{
    SelectedAssignWetPartID = Item.IsValid() ? Item->WetPartID : INDEX_NONE;
}

FReply SWetClothingAssetEditorPanel::HandleAddWetPartClicked()
{
    UWetClothingAsset* Profile = WetClothingAsset.Get();
    if (Profile == nullptr || SelectedMaterialSlotIndex == INDEX_NONE || !SelectedUVChannelItem.IsValid())
    {
        return FReply::Handled();
    }

    const int32 NewWetPartID = FindNextWetPartForSelectedScope();
    Profile->Modify();

    FWetClothingAssetWetPartEntry NewEntry;
    NewEntry.MaterialSlotIndex = SelectedMaterialSlotIndex;
    NewEntry.UVChannelIndex = GetSelectedUVChannelIndex();
    NewEntry.WetPartID = NewWetPartID;
    NewEntry.Name = GetDefaultWetPartName(NewWetPartID);
    NewEntry.Color = GetDefaultWetPartColor(NewWetPartID);
    NewEntry.bViewEnabled = true;

    Profile->WetPartEntries.Add(NewEntry);
    Profile->MarkPackageDirty();
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

FReply SWetClothingAssetEditorPanel::HandleRemoveWetPartClicked()
{
    UWetClothingAsset* Profile = WetClothingAsset.Get();
    if (Profile == nullptr || SelectedMaterialSlotIndex == INDEX_NONE || SelectedWetPartID == INDEX_NONE || SelectedWetPartID == 0)
    {
        return FReply::Handled();
    }

    const int32 RemovedWetPartID = SelectedWetPartID;
    const int32 UVChannelIndex = GetSelectedUVChannelIndex();
    Profile->Modify();
    for (int32 Index = Profile->WetPartEntries.Num() - 1; Index >= 0; --Index)
    {
        const FWetClothingAssetWetPartEntry& Entry = Profile->WetPartEntries[Index];
        if (Entry.MaterialSlotIndex == SelectedMaterialSlotIndex && Entry.UVChannelIndex == UVChannelIndex && Entry.WetPartID == SelectedWetPartID)
        {
            Profile->WetPartEntries.RemoveAt(Index);
            break;
        }
    }
    SelectedWetPartID = INDEX_NONE;
    if (SelectedAssignWetPartID == RemovedWetPartID)
    {
        SelectedAssignWetPartID = INDEX_NONE;
    }
    Profile->MarkPackageDirty();
    if (DetailsView.IsValid())
    {
        DetailsView->ForceRefresh();
    }
    EnsureDefaultWetPartForSelectedScope();
    RefreshWetPartList();
    RefreshUVIslandList();
    return FReply::Handled();
}

bool SWetClothingAssetEditorPanel::IsWetPartRemoveEnabled() const
{
    return SelectedWetPartID != INDEX_NONE && SelectedWetPartID != 0;
}

bool SWetClothingAssetEditorPanel::IsAutoPartitionEnabled() const
{
    return WetClothingAsset.IsValid() && SelectedMaterialSlotIndex != INDEX_NONE && SelectedUVChannelItem.IsValid() && UVIslandItems.Num() > 0;
}

bool SWetClothingAssetEditorPanel::HasAutoPartitionDataToReplace() const
{
    if (const UWetClothingAsset* Profile = WetClothingAsset.Get())
    {
        const int32 UVChannelIndex = GetSelectedUVChannelIndex();
        for (const FWetClothingAssetWetPartEntry& Entry : Profile->WetPartEntries)
        {
            if (Entry.MaterialSlotIndex == SelectedMaterialSlotIndex && Entry.UVChannelIndex == UVChannelIndex && Entry.WetPartID != 0)
            {
                return true;
            }
        }
    }

    return false;
}

FReply SWetClothingAssetEditorPanel::HandleAutoPartitionClicked()
{
    UWetClothingAsset* Profile = WetClothingAsset.Get();
    if (Profile == nullptr || !IsAutoPartitionEnabled())
    {
        return FReply::Handled();
    }

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

    UTexture2D*                 PartitionTexture = Cast<UTexture2D>(ResolveSelectedMaterialTexture());
    FWetClothingTextureReadback TextureData;
    FString                     TextureErrorMessage;
    if (!FWetClothingTextureReadbackUtils::TryReadTextureSourceData(PartitionTexture, TextureData, TextureErrorMessage))
    {
        FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(TextureErrorMessage));
        return FReply::Handled();
    }

    TArray<FWetClothingAutoPartitionCluster> Clusters;
    FString                                  AutoPartitionErrorMessage;
    if (!FWetClothingAutoPartitioner::BuildClusters(UVIslandItems, TextureData, AutoPartitionTolerancePercent, Clusters, &AutoPartitionErrorMessage))
    {
        FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(AutoPartitionErrorMessage));
        return FReply::Handled();
    }

    const int32 UVChannelIndex = GetSelectedUVChannelIndex();
    Profile->Modify();

    for (int32 EntryIndex = Profile->WetPartEntries.Num() - 1; EntryIndex >= 0; --EntryIndex)
    {
        const FWetClothingAssetWetPartEntry& Entry = Profile->WetPartEntries[EntryIndex];
        if (Entry.MaterialSlotIndex == SelectedMaterialSlotIndex && Entry.UVChannelIndex == UVChannelIndex && Entry.WetPartID != 0)
        {
            Profile->WetPartEntries.RemoveAt(EntryIndex);
        }
    }

    EnsureDefaultWetPartForSelectedScope();
    if (FWetClothingAssetWetPartEntry* DefaultEntry = FindMutableWetPartEntry(0))
    {
        DefaultEntry->AssignedIslandIDs.Reset();
        DefaultEntry->Name = GetDefaultWetPartName(0);
        DefaultEntry->Color = GetDefaultWetPartColor(0);
        DefaultEntry->bViewEnabled = true;
    }

    for (int32 ClusterIndex = 0; ClusterIndex < Clusters.Num(); ++ClusterIndex)
    {
        const int32 NewWetPartID = ClusterIndex + 1;

        FWetClothingAssetWetPartEntry NewEntry;
        NewEntry.MaterialSlotIndex = SelectedMaterialSlotIndex;
        NewEntry.UVChannelIndex = UVChannelIndex;
        NewEntry.WetPartID = NewWetPartID;
        NewEntry.Name = GetDefaultWetPartName(NewWetPartID);
        NewEntry.Color = GetDefaultWetPartColor(NewWetPartID);
        NewEntry.bViewEnabled = true;
        NewEntry.AssignedIslandIDs = Clusters[ClusterIndex].IslandIDs;
        Profile->WetPartEntries.Add(NewEntry);
    }

    Profile->MarkPackageDirty();

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
    return FReply::Handled();
}

FReply SWetClothingAssetEditorPanel::HandleAssignSelectedIslandToWetPartClicked()
{
    UWetClothingAsset* Profile = WetClothingAsset.Get();
    if (Profile == nullptr || SelectedMaterialSlotIndex == INDEX_NONE || SelectedUVIslandIDs.Num() == 0 || SelectedAssignWetPartID == INDEX_NONE)
    {
        return FReply::Handled();
    }
    const int32 UVChannelIndex = GetSelectedUVChannelIndex();
    Profile->Modify();
    for (FWetClothingAssetWetPartEntry& Entry : Profile->WetPartEntries)
    {
        if (Entry.MaterialSlotIndex == SelectedMaterialSlotIndex && Entry.UVChannelIndex == UVChannelIndex)
        {
            for (int32 IslandID : SelectedUVIslandIDs)
            {
                Entry.AssignedIslandIDs.Remove(IslandID);
            }
        }
    }
    if (FWetClothingAssetWetPartEntry* SelectedEntry = FindMutableWetPartEntry(SelectedAssignWetPartID))
    {
        for (int32 IslandID : SelectedUVIslandIDs)
        {
            if (SelectedAssignWetPartID != 0)
            {
                SelectedEntry->AssignedIslandIDs.AddUnique(IslandID);
            }
        }
    }
    Profile->MarkPackageDirty();
    if (DetailsView.IsValid())
    {
        DetailsView->ForceRefresh();
    }
    RefreshWetPartList();
    RefreshUVIslandList();
    return FReply::Handled();
}

FText SWetClothingAssetEditorPanel::GetMaterialSlotCountText() const
{
    return FText::Format(
        LOCTEXT("MaterialSlotCount", "{0} Slots"),
        FText::AsNumber(MaterialSlotItems.Num()));
}

FText SWetClothingAssetEditorPanel::GetSelectedMaterialSlotText() const
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

FText SWetClothingAssetEditorPanel::GetSelectedUVChannelText() const
{
    if (!SelectedUVChannelItem.IsValid())
    {
        return LOCTEXT("NoUVChannelSelected", "No UV Channel");
    }

    return FText::Format(
        LOCTEXT("SelectedUVChannel", "UV Channel {0}"),
        FText::AsNumber(*SelectedUVChannelItem));
}

FText SWetClothingAssetEditorPanel::GetSelectedUVDisplayModeText() const
{
    if (!SelectedUVDisplayModeItem.IsValid() || *SelectedUVDisplayModeItem == EWetClothingAssetUVDisplayMode::Normal)
    {
        return LOCTEXT("SelectedUVDisplayModeNormal", "Normal");
    }

    return LOCTEXT("SelectedUVDisplayModeOutline", "Outline");
}

FText SWetClothingAssetEditorPanel::GetSelectedTextureText() const
{
    if (!SelectedTextureItem.IsValid())
    {
        return LOCTEXT("NoTextureSelected", "No Texture");
    }

    return FText::FromString(SelectedTextureItem->Label);
}

FText SWetClothingAssetEditorPanel::GetProfileMapBakeSourceText() const
{
    UTexture* SourceTexture = ResolveSelectedMaterialTexture();
    if (SourceTexture == nullptr)
    {
        return LOCTEXT("ProfileMapBakeNoSource", "Source Texture: None");
    }

    return FText::Format(
        LOCTEXT("ProfileMapBakeSource", "Source Texture: {0} / UV Channel {1}"),
        FText::FromString(SourceTexture->GetName()),
        FText::AsNumber(GetSelectedUVChannelIndex()));
}

FText SWetClothingAssetEditorPanel::GetProfileMapBakeSlotsText() const
{
    TArray<int32> MaterialSlotIndices;
    CollectMaterialSlotsForProfileMap(ResolveSelectedMaterialTexture(), GetSelectedUVChannelIndex(), MaterialSlotIndices);

    if (MaterialSlotIndices.Num() == 0)
    {
        return LOCTEXT("ProfileMapBakeNoSlots", "Material Slots: None");
    }

    TArray<FString> SlotLabels;
    SlotLabels.Reserve(MaterialSlotIndices.Num());
    for (const int32 MaterialSlotIndex : MaterialSlotIndices)
    {
        SlotLabels.Add(FString::Printf(TEXT("%d"), MaterialSlotIndex));
    }

    return FText::Format(
        LOCTEXT("ProfileMapBakeSlots", "Material Slots: {0}"),
        FText::FromString(FString::Join(SlotLabels, TEXT(", "))));
}

FText SWetClothingAssetEditorPanel::GetProfileMapBakeStatusText() const
{
    UTexture* SourceTexture = ResolveSelectedMaterialTexture();
    if (SourceTexture == nullptr)
    {
        return LOCTEXT("ProfileMapBakeStatusNoSource", "Select a material texture to prepare a texture-level ProfileMap.");
    }

    const FWetClothingAssetBakedProfileMap* BakedProfileMap = FindBakedProfileMap(SourceTexture, GetSelectedUVChannelIndex());
    if (BakedProfileMap == nullptr)
    {
        return LOCTEXT("ProfileMapBakeStatusNotBaked", "Status: Not baked yet. Phase 2 will generate ProfileMap0 for this texture.");
    }

    if (BakedProfileMap->ProfileMap0 == nullptr)
    {
        return LOCTEXT("ProfileMapBakeStatusMissingTexture", "Status: Bake entry exists, but ProfileMap0 is missing.");
    }

    return FText::Format(
        LOCTEXT("ProfileMapBakeStatusReady", "Status: {0}"),
        FText::FromString(BakedProfileMap->ProfileMap0->GetName()));
}

FText SWetClothingAssetEditorPanel::GetProfileMapBakeSettingsText() const
{
    const FWetClothingAssetBakedProfileMap* BakedProfileMap = FindBakedProfileMap(ResolveSelectedMaterialTexture(), GetSelectedUVChannelIndex());
    const int32                             Resolution = BakedProfileMap != nullptr ? BakedProfileMap->Resolution : 512;
    const int32                             PaddingPixels = BakedProfileMap != nullptr ? BakedProfileMap->PaddingPixels : 4;

    return FText::Format(
        LOCTEXT("ProfileMapBakeSettings", "Settings: {0} px / Padding {1} px"),
        FText::AsNumber(Resolution),
        FText::AsNumber(PaddingPixels));
}

FText SWetClothingAssetEditorPanel::GetUVIslandCountText() const
{
    return FText::Format(
        LOCTEXT("UVIslandCount", "Island Count: {0}"),
        FText::AsNumber(UVIslandItems.Num()));
}

FText SWetClothingAssetEditorPanel::GetSelectedUVIslandText() const
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
        if (IslandItem.IsValid() && IslandItem->IslandID == SelectedUVIslandID)
        {
            return FText::Format(LOCTEXT("SelectedUVIsland", "Selected Island: {0}  |  Bounds Min({1}, {2}) Max({3}, {4})"), FText::AsNumber(IslandItem->IslandID), FText::AsNumber(IslandItem->UVBounds.Min.X), FText::AsNumber(IslandItem->UVBounds.Min.Y), FText::AsNumber(IslandItem->UVBounds.Max.X), FText::AsNumber(IslandItem->UVBounds.Max.Y));
        }
    }
    return LOCTEXT("NoUVIslandSelectedFallback", "Select UV islands from the list, UV view, or 3D preview.");
}

FText SWetClothingAssetEditorPanel::GetUVStatusText() const
{
    return FText::FromString(UVStatusMessage);
}

FText SWetClothingAssetEditorPanel::GetWetPartSectionText() const
{
    if (SelectedMaterialSlotIndex == INDEX_NONE)
    {
        return LOCTEXT("WetPartSectionNoSlot", "Part Map: No slot");
    }

    return FText::Format(
        LOCTEXT("WetPartSection", "Part Map / Slot {0}"),
        FText::AsNumber(SelectedMaterialSlotIndex));
}

FText SWetClothingAssetEditorPanel::GetAssignIslandToWetPartText() const
{
    return LOCTEXT("AssignSelectedIslands", "Assign");
}

FText SWetClothingAssetEditorPanel::GetSelectedAssignWetPartText() const
{
    if (const FWetPartEntryPtr Item = FindWetPartItemByID(SelectedAssignWetPartID))
    {
        return FText::Format(
            LOCTEXT("SelectedAssignWetPart", "{0}  |  ID {1}"),
            FText::FromString(GetWetPartDisplayName(*Item)),
            FText::AsNumber(Item->WetPartID));
    }

    return LOCTEXT("SelectedAssignWetPartNone", "Select Part");
}

FSlateColor SWetClothingAssetEditorPanel::GetSelectedAssignWetPartColor() const
{
    if (const FWetPartEntryPtr Item = FindWetPartItemByID(SelectedAssignWetPartID))
    {
        return FSlateColor((Item->WetPartID == 0 || Item->bViewEnabled) ? Item->Color : FLinearColor(0.45f, 0.45f, 0.45f, 1.0f));
    }

    return FSlateColor(FLinearColor(0.08f, 0.08f, 0.08f, 1.0f));
}

FText SWetClothingAssetEditorPanel::GetSelectedWetPartText() const
{
    if (SelectedWetPartID == INDEX_NONE)
    {
        return LOCTEXT("SelectedWetPartNone", "Selected Part: None. Pick a row to select all islands assigned to that ID.");
    }

    if (const FWetClothingAssetWetPartEntry* Entry = FindWetPartEntry(SelectedWetPartID))
    {
        return FText::Format(
            LOCTEXT("SelectedWetPart", "Selected Part: {0}  |  Double-click name to rename"),
            FText::FromString(GetWetPartDisplayName(*Entry)));
    }

    return LOCTEXT("SelectedWetPartInvalid", "Selected Part: Invalid");
}

FText SWetClothingAssetEditorPanel::GetWetnessProfileLibraryStatusText() const
{
    const TArray<FString> SearchPaths = GetProfileSearchPaths();
    const FString         PathsLabel = FString::Join(SearchPaths, TEXT(", "));

    return FText::Format(
        LOCTEXT("WetnessProfileLibraryStatus", "Profile folders: {0}  |  Candidates: {1}"),
        FText::FromString(PathsLabel),
        FText::AsNumber(AvailableWetnessProfileItems.Num()));
}

FText SWetClothingAssetEditorPanel::GetBlendModeText(FWetPartEntryPtr Item) const
{
    if (!Item.IsValid())
    {
        return LOCTEXT("InvalidBlendMode", "Standard");
    }

    const UEnum* BlendModeEnum = StaticEnum<EWetClothingPartBlendMode>();
    return BlendModeEnum != nullptr
               ? BlendModeEnum->GetDisplayNameTextByValue(static_cast<int64>(Item->ProfileAssignment.BlendMode))
               : LOCTEXT("BlendModeFallback", "Standard");
}

FText SWetClothingAssetEditorPanel::GetWetnessProfileButtonText(FWetPartEntryPtr Item) const
{
    return Item.IsValid()
               ? FText::FromString(GetAssignedProfileLabel(*Item))
               : LOCTEXT("NoProfileSelected", "Select Profile");
}

FReply SWetClothingAssetEditorPanel::HandleUVSelectionToolButtonClicked(FUVSelectionToolItemPtr Item)
{
    if (Item.IsValid())
    {
        SetCurrentUVSelectionTool(Item->Tool);
    }

    return FReply::Handled();
}

void SWetClothingAssetEditorPanel::SetCurrentUVSelectionTool(EWetClothingAssetUVSelectionTool InTool)
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

const FSlateBrush* SWetClothingAssetEditorPanel::GetUVSelectionToolBrush(FUVSelectionToolItemPtr Item) const
{
    if (Item.IsValid() && Item->IconBrushName != NAME_None)
    {
        return FDynamicWetClothesEditorStyle::GetBrush(Item->IconBrushName);
    }

    return FAppStyle::GetBrush(TEXT("ClassIcon.Default"));
}

FSlateColor SWetClothingAssetEditorPanel::GetUVSelectionToolIconColor(FUVSelectionToolItemPtr Item) const
{
    const bool bIsSelected = Item.IsValid() && Item->Tool == CurrentUVSelectionTool;
    return FSlateColor(bIsSelected
                           ? FLinearColor::White
                           : FStyleColors::Foreground);
}

FSlateColor SWetClothingAssetEditorPanel::GetUVSelectionToolButtonColor(FUVSelectionToolItemPtr Item) const
{
    const bool bIsSelected = Item.IsValid() && Item->Tool == CurrentUVSelectionTool;
    return FSlateColor(bIsSelected
                           ? FStyleColors::AccentBlue
                           : FStyleColors::Header);
}

float SWetClothingAssetEditorPanel::GetAutoPartitionTolerance() const
{
    return AutoPartitionTolerancePercent;
}

void SWetClothingAssetEditorPanel::HandleAutoPartitionToleranceChanged(float InValue)
{
    AutoPartitionTolerancePercent = FMath::Clamp(InValue, 0.0f, 100.0f);
}

float SWetClothingAssetEditorPanel::GetSelectionLineThicknessScale() const
{
    return PreviewViewport.IsValid() ? PreviewViewport->GetSelectionOverlayThicknessScale() : 1.0f;
}

void SWetClothingAssetEditorPanel::HandleSelectionLineThicknessChanged(float InValue)
{
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->SetSelectionOverlayThicknessScale(InValue);
    }
}

FReply SWetClothingAssetEditorPanel::HandleFocusPreviewClicked()
{
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->FocusOnPreviewMesh();
    }

    return FReply::Handled();
}

FReply SWetClothingAssetEditorPanel::HandleSaveAssetClicked()
{
    DynamicWetClothesEditorUtils::SaveAsset(WetClothingAsset.Get());
    return FReply::Handled();
}

bool SWetClothingAssetEditorPanel::IsProfileMapBakeSourceValid() const
{
    return WetClothingAsset.IsValid() && WetClothingAsset->TargetMesh != nullptr && ResolveSelectedMaterialTexture() != nullptr && SelectedUVChannelItem.IsValid();
}

bool SWetClothingAssetEditorPanel::CanBakeAnyProfileMap() const
{
    TArray<UTexture*> SourceTextures;
    CollectProfileMapSourceTextures(GetSelectedUVChannelIndex(), SourceTextures);
    return WetClothingAsset.IsValid() && WetClothingAsset->TargetMesh != nullptr && SelectedUVChannelItem.IsValid() && SourceTextures.Num() > 0;
}

FReply SWetClothingAssetEditorPanel::HandleBakeSelectedProfileMapClicked()
{
    UWetClothingAsset* Profile = WetClothingAsset.Get();
    UTexture*          SourceTexture = ResolveSelectedMaterialTexture();
    if (Profile == nullptr || SourceTexture == nullptr)
    {
        return FReply::Handled();
    }

    const int32 UVChannelIndex = GetSelectedUVChannelIndex();

    TArray<int32> MaterialSlotIndices;
    CollectMaterialSlotsForProfileMap(SourceTexture, UVChannelIndex, MaterialSlotIndices);
    if (MaterialSlotIndices.Num() == 0)
    {
        FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("NoProfileMapBakeSlots", "No material slots were found for the selected texture."));
        return FReply::Handled();
    }

    const FWetClothingAssetBakedProfileMap* ExistingProfileMap = FindBakedProfileMap(SourceTexture, UVChannelIndex);
    FWetClothingProfileMapBakeSettings      Settings;
    if (ExistingProfileMap != nullptr)
    {
        Settings.Resolution = ExistingProfileMap->Resolution;
        Settings.PaddingPixels = ExistingProfileMap->PaddingPixels;
    }

    FWetClothingProfileMapBakeResult Result;
    FString                          ErrorMessage;
    if (!FWetClothingProfileMapBaker::BakeProfileMap0(
            Profile,
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
        TEXT("DynamicWetClothes: Baked ProfileMap0 '%s' for texture '%s' (%d painted pixels)."),
        *GetNameSafe(Result.ProfileMap0.Get()),
        *GetNameSafe(SourceTexture),
        Result.PaintedPixelCount);

    if (DetailsView.IsValid())
    {
        DetailsView->ForceRefresh();
    }

    return FReply::Handled();
}

FReply SWetClothingAssetEditorPanel::HandleBakeAllProfileMapsClicked()
{
    UWetClothingAsset* Profile = WetClothingAsset.Get();
    if (Profile == nullptr)
    {
        return FReply::Handled();
    }

    const int32 UVChannelIndex = GetSelectedUVChannelIndex();

    TArray<UTexture*> SourceTextures;
    CollectProfileMapSourceTextures(UVChannelIndex, SourceTextures);
    if (SourceTextures.Num() == 0)
    {
        FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("NoProfileMapBakeSources", "No source textures were found for the selected UV channel."));
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
        CollectMaterialSlotsForProfileMap(SourceTexture, UVChannelIndex, MaterialSlotIndices);
        if (MaterialSlotIndices.Num() == 0)
        {
            continue;
        }

        const FWetClothingAssetBakedProfileMap* ExistingProfileMap = FindBakedProfileMap(SourceTexture, UVChannelIndex);
        FWetClothingProfileMapBakeSettings      Settings;
        if (ExistingProfileMap != nullptr)
        {
            Settings.Resolution = ExistingProfileMap->Resolution;
            Settings.PaddingPixels = ExistingProfileMap->PaddingPixels;
        }

        FWetClothingProfileMapBakeResult Result;
        FString                          ErrorMessage;
        if (!FWetClothingProfileMapBaker::BakeProfileMap0(
                Profile,
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

    UE_LOG(LogTemp, Display, TEXT("DynamicWetClothes: Baked %d ProfileMap texture(s)."), BakedCount);

    if (DetailsView.IsValid())
    {
        DetailsView->ForceRefresh();
    }

    return FReply::Handled();
}

UTexture* SWetClothingAssetEditorPanel::ResolveSelectedMaterialTexture() const
{
    return SelectedTextureItem.IsValid() ? SelectedTextureItem->Texture.Get() : nullptr;
}

const FWetClothingAssetBakedProfileMap* SWetClothingAssetEditorPanel::FindBakedProfileMap(UTexture* SourceTexture, int32 UVChannelIndex) const
{
    const UWetClothingAsset* Profile = WetClothingAsset.Get();
    if (Profile == nullptr || SourceTexture == nullptr || UVChannelIndex == INDEX_NONE)
    {
        return nullptr;
    }

    return Profile->BakedProfileMaps.FindByPredicate(
        [SourceTexture, UVChannelIndex](const FWetClothingAssetBakedProfileMap& BakedProfileMap)
        {
            return BakedProfileMap.SourceTexture == SourceTexture && BakedProfileMap.UVChannelIndex == UVChannelIndex;
        });
}

void SWetClothingAssetEditorPanel::CollectMaterialSlotsForProfileMap(UTexture* SourceTexture, int32 UVChannelIndex, TArray<int32>& OutMaterialSlotIndices) const
{
    OutMaterialSlotIndices.Reset();

    const UWetClothingAsset* Profile = WetClothingAsset.Get();
    if (Profile == nullptr || SourceTexture == nullptr || UVChannelIndex == INDEX_NONE)
    {
        return;
    }

    for (const FMaterialSlotItemPtr& MaterialSlotItem : MaterialSlotItems)
    {
        if (!MaterialSlotItem.IsValid() || MaterialSlotItem->SlotIndex == INDEX_NONE || !MaterialSlotItem->Material.IsValid())
        {
            continue;
        }

        TArray<FTextureItemPtr> SlotTextureItems;
        FWetClothingMaterialTextureResolver::BuildTextureItems(MaterialSlotItem->Material.Get(), SlotTextureItems);

        const bool bUsesSourceTexture = SlotTextureItems.ContainsByPredicate(
            [SourceTexture](const FTextureItemPtr& TextureItem)
            {
                return TextureItem.IsValid() && TextureItem->Texture.Get() == SourceTexture;
            });

        if (bUsesSourceTexture)
        {
            OutMaterialSlotIndices.AddUnique(MaterialSlotItem->SlotIndex);
        }
    }

    for (const FWetClothingAssetTextureSelection& Selection : Profile->TextureSelections)
    {
        if (Selection.Texture == SourceTexture && Selection.UVChannelIndex == UVChannelIndex && Selection.MaterialSlotIndex != INDEX_NONE)
        {
            OutMaterialSlotIndices.AddUnique(Selection.MaterialSlotIndex);
        }
    }

    if (SelectedMaterialSlotIndex != INDEX_NONE && ResolveSelectedMaterialTexture() == SourceTexture && GetSelectedUVChannelIndex() == UVChannelIndex)
    {
        OutMaterialSlotIndices.AddUnique(SelectedMaterialSlotIndex);
    }

    OutMaterialSlotIndices.Sort();
}

void SWetClothingAssetEditorPanel::CollectProfileMapSourceTextures(int32 UVChannelIndex, TArray<UTexture*>& OutSourceTextures) const
{
    OutSourceTextures.Reset();

    if (UVChannelIndex == INDEX_NONE)
    {
        return;
    }

    for (const FMaterialSlotItemPtr& MaterialSlotItem : MaterialSlotItems)
    {
        if (!MaterialSlotItem.IsValid() || !MaterialSlotItem->Material.IsValid())
        {
            continue;
        }

        TArray<FTextureItemPtr> SlotTextureItems;
        FWetClothingMaterialTextureResolver::BuildTextureItems(MaterialSlotItem->Material.Get(), SlotTextureItems);
        for (const FTextureItemPtr& TextureItem : SlotTextureItems)
        {
            if (TextureItem.IsValid() && TextureItem->Texture.IsValid())
            {
                OutSourceTextures.AddUnique(TextureItem->Texture.Get());
            }
        }
    }
}

void SWetClothingAssetEditorPanel::SaveSelectedTexture()
{
    UWetClothingAsset* Profile = WetClothingAsset.Get();
    if (Profile == nullptr || SelectedMaterialSlotIndex == INDEX_NONE)
    {
        return;
    }

    const int32 UVChannelIndex = GetSelectedUVChannelIndex();
    UTexture*   Texture = ResolveSelectedMaterialTexture();

    Profile->Modify();

    for (FWetClothingAssetTextureSelection& Selection : Profile->TextureSelections)
    {
        if (Selection.MaterialSlotIndex == SelectedMaterialSlotIndex && Selection.UVChannelIndex == UVChannelIndex)
        {
            Selection.Texture = Texture;
            Profile->MarkPackageDirty();
            return;
        }
    }

    FWetClothingAssetTextureSelection NewSelection;
    NewSelection.MaterialSlotIndex = SelectedMaterialSlotIndex;
    NewSelection.UVChannelIndex = UVChannelIndex;
    NewSelection.Texture = Texture;
    Profile->TextureSelections.Add(NewSelection);
    Profile->MarkPackageDirty();
}

#undef LOCTEXT_NAMESPACE
