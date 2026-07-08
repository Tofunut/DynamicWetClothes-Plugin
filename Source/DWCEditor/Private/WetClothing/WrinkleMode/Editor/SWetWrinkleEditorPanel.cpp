#include "SWetWrinkleEditorPanel.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Core/DWCEditorUtils.h"
#include "DataAssets/WetClothingAsset.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "AssetThumbnail.h"
#include "Core/DWCEditorStyle.h"
#include "Brushes/SlateImageBrush.h"
#include "Materials/MaterialInterface.h"
#include "Modules/ModuleManager.h"
#include "IDetailsView.h"
#include "Misc/MessageDialog.h"
#include "PropertyCustomizationHelpers.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "WetClothing/Common/Analysis/WetClothingAssetMeshAnalyzer.h"
#include "WetClothing/Common/Texture/WetClothingMaterialTextureResolver.h"
#include "WetClothing/Common/Widgets/WetClothingEditorCommonWidgets.h"
#include "WetClothing/PartMode/Partition/WetPartEditingService.h"
#include "WetClothing/WrinkleMode/Bake/WetWrinkleNormalMapBaker.h"
#include "WetClothing/WrinkleMode/UV/WetWrinkleUVChannelGenerator.h"
#include "WetClothing/WrinkleMode/Viewport/WetWrinkleViewport.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "WetClothingAssetEditorPanel"

namespace
{
    constexpr const TCHAR* WetWrinklePreset0Path = TEXT("/DynamicWetClothes/Presets/WrinkleTextures/Wet_Wrinkle_Normal0.Wet_Wrinkle_Normal0");
    constexpr const TCHAR* WetWrinklePresetFolderPath = TEXT("/DynamicWetClothes/Presets/WrinkleTextures");
    constexpr float WetWrinkleDefaultSizeCm = 8.0f;
    constexpr float WetWrinkleDefaultSizeUV = 0.0677f;
    constexpr float WetWrinkleUVPerCm = WetWrinkleDefaultSizeUV / WetWrinkleDefaultSizeCm;

    FText FormatWetWrinkleBrushSizeCm(float SizeCm)
    {
        FNumberFormattingOptions Options;
        Options.MinimumFractionalDigits = 0;
        Options.MaximumFractionalDigits = 1;
        return FText::AsNumber(SizeCm, &Options);
    }
}

void SWetWrinkleEditorPanel::Construct(const FArguments& InArgs)
{
    WetClothingAsset = InArgs._WetClothingAsset;
    DetailsView = InArgs._DetailsView;
    MaterialThumbnailPool = MakeShared<FAssetThumbnailPool>(32);
    PatchTextureThumbnailPool = MakeShared<FAssetThumbnailPool>(32);

    if (UWetClothingAsset* Asset = WetClothingAsset.Get())
    {
        BrushSettings.UVChannelIndex = BrushSettings.MaterialSlotIndex != INDEX_NONE ? Asset->WrinkleData.WrinkleUVChannelIndex : INDEX_NONE;
    }
    RefreshMaterialSlotOptions();
    RefreshUVChannelOptions();
    RefreshBrushPresetOptions();
    BrushSettings.BrushHeightTexture = ResolveDefaultBrushHeightTexture();
    SizeCm = WetWrinkleDefaultSizeCm;
    SizeUV = WetWrinkleDefaultSizeUV;
    BrushSettings.BrushRadiusUV = SizeUV;

    const FSlateFontInfo PanelHeadingFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 16);

    ChildSlot
        [SNew(SVerticalBox)

         + SVerticalBox::Slot()
               .AutoHeight()
               .Padding(10.0f, 10.0f, 10.0f, 8.0f)
                   [SNew(STextBlock)
                        .Text(LOCTEXT("EditorHeading", "Wet Wrinkle"))
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

                    + SSplitter::Slot()
                          .Value(0.28f)
                              [SNew(SBorder)
                                   .Padding(10.0f)
                                       [SNew(SVerticalBox)

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                                                  [SNew(STextBlock)
                                                       .Text(LOCTEXT("UVChannelLabel", "Wrinkle UV Channel"))]

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                                                  [SNew(STextBlock)
                                                       .AutoWrapText(true)
                                                       .Text(this, &SWetWrinkleEditorPanel::GetWrinkleUVChannelText)]

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 0.0f, 0.0f, 4.0f)
                                                  [SNew(STextBlock)
                                                       .Text(LOCTEXT("MeshUVChannelsLabel", "Mesh UV Channels"))]

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                                                  [SNew(SHorizontalBox)

                                                   + SHorizontalBox::Slot()
                                                         .FillWidth(1.0f)
                                                         .VAlign(VAlign_Center)
                                                             [SAssignNew(MeshUVChannelComboBox, SComboBox<TSharedPtr<int32>>)
                                                                  .OptionsSource(&MeshUVChannelOptions)
                                                                  .OnGenerateWidget(this, &SWetWrinkleEditorPanel::GenerateMeshUVChannelComboRow)
                                                                  .OnSelectionChanged(this, &SWetWrinkleEditorPanel::HandleMeshUVChannelComboChanged)
                                                                      [SNew(STextBlock)
                                                                           .Text(this, &SWetWrinkleEditorPanel::GetSelectedMeshUVChannelText)]]

                                                   + SHorizontalBox::Slot()
                                                         .AutoWidth()
                                                         .VAlign(VAlign_Center)
                                                         .Padding(6.0f, 0.0f, 0.0f, 0.0f)
                                                             [SNew(SButton)
                                                                  .Text(LOCTEXT("DeleteMeshUVChannelButton", "Delete"))
                                                                  .ToolTipText(LOCTEXT("DeleteMeshUVChannelTooltip", "Delete the selected DWC-added UV channel. Original/imported mesh UV channels are protected."))
                                                                  .IsEnabled(this, &SWetWrinkleEditorPanel::IsDeleteMeshUVChannelEnabled)
                                                                  .OnClicked(this, &SWetWrinkleEditorPanel::HandleDeleteMeshUVChannelClicked)]]

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 0.0f, 0.0f, 14.0f)
                                                  [SNew(SButton)
                                                       .ToolTipText(LOCTEXT("AutoGenerateWrinkleTextureTooltip", "Automatically generate wrinkle texture maps from the current wrinkle patch data."))
                                                       .ContentPadding(FMargin(8.0f, 5.0f))
                                                       .OnClicked(this, &SWetWrinkleEditorPanel::HandleAutoGenerateClicked)
                                                           [SNew(SHorizontalBox)

                                                            + SHorizontalBox::Slot()
                                                                  .AutoWidth()
                                                                  .VAlign(VAlign_Center)
                                                                  .Padding(0.0f, 0.0f, 8.0f, 0.0f)
                                                                      [SNew(SImage)
                                                                           .DesiredSizeOverride(FVector2D(28.0f, 28.0f))
                                                                           .Image(FDWCEditorStyle::GetBrush(TEXT("DWCEditor.MagicWandTool.Large")))]

                                                            + SHorizontalBox::Slot()
                                                                  .FillWidth(1.0f)
                                                                  .VAlign(VAlign_Center)
                                                                      [SNew(STextBlock)
                                                                           .Text(LOCTEXT("AutoGenerateWrinkleButton", "Generate Wrinkle Textures"))]]]

                                        + SVerticalBox::Slot()
                                              .FillHeight(1.0f)
                                                  [SNew(SSplitter)
                                                       .Orientation(Orient_Vertical)

                                                   + SSplitter::Slot()
                                                         .Value(0.52f)
                                                             [SNew(SVerticalBox)

                                                              + SVerticalBox::Slot()
                                                                    .AutoHeight()
                                                                    .Padding(0.0f, FWetClothingEditorCommonWidgets::MaterialSlotListHeaderTopPadding, 0.0f, 4.0f)
                                                                        [FWetClothingEditorCommonWidgets::BuildSectionHeader(
                                                                            LOCTEXT("MaterialSlotsLabel", "Material Slots"),
                                                                            TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &SWetWrinkleEditorPanel::GetMaterialSlotCountText)))]

                                                              + SVerticalBox::Slot()
                                                                    .AutoHeight()
                                                                    .Padding(0.0f, 0.0f, 0.0f, FWetClothingEditorCommonWidgets::MaterialSlotListSeparatorBottomPadding)
                                                                        [SNew(SSeparator)
                                                                             .Orientation(Orient_Horizontal)]

                                                              + SVerticalBox::Slot()
                                                                    .FillHeight(1.0f)
                                                                        [SAssignNew(MaterialSlotListView, SListView<FMaterialSlotItemPtr>)
                                                                             .ListItemsSource(&MaterialSlotItems)
                                                                             .OnGenerateRow(this, &SWetWrinkleEditorPanel::GenerateMaterialSlotRow)
                                                                             .OnSelectionChanged(this, &SWetWrinkleEditorPanel::HandleMaterialSlotSelectionChanged)
                                                                             .SelectionMode(ESelectionMode::Single)]]

                                                   + SSplitter::Slot()
                                                         .Value(0.48f)
                                                             [SNew(SVerticalBox)

                                                              + SVerticalBox::Slot()
                                                                    .AutoHeight()
                                                                    .Padding(0.0f, 8.0f, 0.0f, 4.0f)
                                                                        [FWetClothingEditorCommonWidgets::BuildSectionHeader(
                                                                            TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &SWetWrinkleEditorPanel::GetPartMapSectionText)))]

                                                              + SVerticalBox::Slot()
                                                                    .AutoHeight()
                                                                    .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                                                                        [SNew(SSeparator)
                                                                             .Orientation(Orient_Horizontal)]

                                                              + SVerticalBox::Slot()
                                                                    .FillHeight(1.0f)
                                                                        [SAssignNew(PartMapListView, SListView<FWetPartEntryPtr>)
                                                                             .ListItemsSource(&PartMapItems)
                                                                             .OnGenerateRow(this, &SWetWrinkleEditorPanel::GeneratePartMapRow)
                                                                             .SelectionMode(ESelectionMode::None)]]]]]

                    + SSplitter::Slot()
                          .Value(0.47f)
                              [FWetClothingEditorCommonWidgets::BuildPreviewSection(
                                  SNew(SSplitter)
                                      .Orientation(Orient_Vertical)

                                      + SSplitter::Slot()
                                            .Value(0.68f)
                                                [SAssignNew(PreviewViewport, SWetWrinkleViewport)
                                                     .WetClothingAsset(WetClothingAsset.Get())
                                                     .OnSurfaceHitChanged(FOnWetWrinkleSurfaceHitChanged::CreateSP(this, &SWetWrinkleEditorPanel::HandleSurfaceHitChanged))
                                                     .OnPaintStrokeStarted(FOnWetWrinklePaintStrokeStarted::CreateSP(this, &SWetWrinkleEditorPanel::HandlePaintStrokeStarted))
                                                     .OnPaintStampRequested(FOnWetWrinklePaintStampRequested::CreateSP(this, &SWetWrinkleEditorPanel::HandlePaintStampRequested))
                                                     .OnPaintStrokeEnded(FOnWetWrinklePaintStrokeEnded::CreateSP(this, &SWetWrinkleEditorPanel::HandlePaintStrokeEnded))]

                                      + SSplitter::Slot()
                                            .Value(0.32f)
                                                [BuildWrinkleUVViewSection()],
                                  FOnWetClothingPreviewFocusClicked::CreateSP(this, &SWetWrinkleEditorPanel::HandleFocusClicked))]

                    + SSplitter::Slot()
                          .Value(0.25f)
                              [SNew(SSplitter)
                                   .Orientation(Orient_Vertical)

                               + SSplitter::Slot()
                                     .Value(0.58f)
                                         [BuildPatchBrushSection()]

                               + SSplitter::Slot()
                                     .Value(0.42f)
                                         [BuildPatchListSection()]]]];
    PushBrushSettingsToViewport();
    RefreshFromAsset();
}

TSharedRef<SWidget> SWetWrinkleEditorPanel::BuildPatchBrushSection()
{
    const FSlateFontInfo SectionHeadingFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 13);

    return SNew(SBorder)
        .Padding(10.0f)
            [SNew(SVerticalBox)

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                       [SNew(STextBlock)
                            .Text(LOCTEXT("PatchBrushHeading", "Patch Brush"))
                            .Font(SectionHeadingFont)]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                       [SNew(STextBlock)
                            .Text(LOCTEXT("PatchNormalTextureLabel", "Normal Texture"))]

             + SVerticalBox::Slot()
                   .FillHeight(1.0f)
                   .Padding(0.0f, 0.0f, 0.0f, 12.0f)
                       [SAssignNew(PatchTextureListView, SListView<FPatchTextureItemPtr>)
                            .ListItemsSource(&BrushPresetOptions)
                            .OnGenerateRow(this, &SWetWrinkleEditorPanel::GeneratePatchTextureRow)
                            .OnSelectionChanged(this, &SWetWrinkleEditorPanel::HandlePatchTextureSelectionChanged)]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                       [SNew(STextBlock)
                            .Text(LOCTEXT("SizeLabel", "Size (cm)"))]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                       [SNew(SHorizontalBox)

                        + SHorizontalBox::Slot()
                              .AutoWidth()
                                  [SNew(SBox)
                                       .WidthOverride(64.0f)
                                           [SNew(SSpinBox<float>)
                                                .MinValue(0.1f)
                                                .MaxValue(100.0f)
                                                .Value(this, &SWetWrinkleEditorPanel::GetBrushSizeCm)
                                                .OnValueChanged(this, &SWetWrinkleEditorPanel::HandleBrushRadiusChanged)]]

                        + SHorizontalBox::Slot()
                              .FillWidth(1.0f)
                              .Padding(4.0f, 0.0f, 0.0f, 0.0f)
                                  [SAssignNew(BrushSizeComboButton, SComboButton)
                                       .HasDownArrow(true)
                                       .ContentPadding(FMargin(8.0f, 2.0f))
                                       .ButtonContent()
                                           [SNew(STextBlock)
                                                .Text(this, &SWetWrinkleEditorPanel::GetBrushSizeDisplayText)]
                                       .MenuContent()
                                           [BuildBrushSizeMenu()]]]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                       [SNew(STextBlock)
                            .Text(LOCTEXT("StrengthLabel", "Strength"))]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                       [SNew(SSpinBox<float>)
                            .MinValue(0.0f)
                            .MaxValue(4.0f)
                            .MinSliderValue(0.0f)
                            .MaxSliderValue(4.0f)
                            .Value(BrushSettings.Strength)
                            .OnValueChanged(this, &SWetWrinkleEditorPanel::HandleStrengthChanged)]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                       [SNew(STextBlock)
                            .Text(LOCTEXT("EdgeSoftnessLabel", "Edge Softness (%)"))]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                       [SNew(SSpinBox<float>)
                            .MinValue(0.0f)
                            .MaxValue(100.0f)
                            .Value(BrushSettings.Falloff * 100.0f)
                            .OnValueChanged(this, &SWetWrinkleEditorPanel::HandleFalloffChanged)]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                       [SNew(STextBlock)
                            .Text(LOCTEXT("RotationLabel", "Rotation (°)"))]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                       [SNew(SSpinBox<float>)
                            .MinValue(-180.0f)
                            .MaxValue(180.0f)
                            .Value(FMath::RadiansToDegrees(BrushSettings.RotationRadians))
                            .OnValueChanged(this, &SWetWrinkleEditorPanel::HandleRotationChanged)]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 0.0f)
                       [SNew(SCheckBox)
                            .IsChecked(this, &SWetWrinkleEditorPanel::GetPreviewToggleState)
                            .OnCheckStateChanged(this, &SWetWrinkleEditorPanel::HandlePreviewToggleChanged)
                                [SNew(STextBlock)
                                     .Text(LOCTEXT("PreviewToggle", "Show Preview Cursor"))]]];
}

TSharedRef<SWidget> SWetWrinkleEditorPanel::BuildPatchListSection()
{
    const FSlateFontInfo SectionHeadingFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 13);

    return SNew(SBorder)
        .Padding(10.0f)
            [SNew(SVerticalBox)

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                       [SNew(SHorizontalBox)

                        + SHorizontalBox::Slot()
                              .FillWidth(1.0f)
                              .VAlign(VAlign_Center)
                                  [SNew(STextBlock)
                                       .Text(LOCTEXT("PatchListHeading", "Patch List"))
                                       .Font(SectionHeadingFont)]

                        + SHorizontalBox::Slot()
                              .AutoWidth()
                                  [SNew(SButton)
                                       .Text(LOCTEXT("ClearPatchListButton", "Clear"))
                                       .IsEnabled(this, &SWetWrinkleEditorPanel::IsClearStrokesEnabled)
                                       .OnClicked(this, &SWetWrinkleEditorPanel::HandleClearStrokesClicked)]]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                       [SNew(STextBlock)
                            .AutoWrapText(true)
                            .Text(this, &SWetWrinkleEditorPanel::GetPatchListSummaryText)]

             + SVerticalBox::Slot()
                   .FillHeight(1.0f)
                       [SAssignNew(StrokeListView, SListView<FStrokeListItemPtr>)
                            .ListItemsSource(&StrokeListItems)
                            .OnGenerateRow(this, &SWetWrinkleEditorPanel::GenerateStrokeRow)
                            .OnSelectionChanged(this, &SWetWrinkleEditorPanel::HandleStrokeSelectionChanged)]];
}

void SWetWrinkleEditorPanel::RefreshFromAsset()
{
    if (UWetClothingAsset* Asset = WetClothingAsset.Get())
    {
        BrushSettings.UVChannelIndex = BrushSettings.MaterialSlotIndex != INDEX_NONE ? Asset->WrinkleData.WrinkleUVChannelIndex : INDEX_NONE;
    }
    RefreshMaterialSlotOptions();
    RefreshUVChannelOptions();
    RefreshBrushPresetOptions();
    RefreshPartMapItems();
    RefreshStrokeList();
    InvalidateWrinkleUVViewCache();

    if (PreviewViewport.IsValid())
    {
        PreviewViewport->RefreshPreviewMesh();
        PushBrushSettingsToViewport();
        RefreshStrokeOverlay();
    }

    RefreshWrinkleUVView();
}

FReply SWetWrinkleEditorPanel::HandleSaveClicked()
{
    DWCEditorUtils::SaveAsset(WetClothingAsset.Get());
    return FReply::Handled();
}

TSharedRef<SWidget> SWetWrinkleEditorPanel::BuildBakeMapsMenu()
{
    return SNew(SVerticalBox)

        + SVerticalBox::Slot()
              .AutoHeight()
              .Padding(4.0f, 2.0f)
                  [SNew(SButton)
                       .Text(LOCTEXT("BakeAllMapsMenuItem", "Bake All Maps"))
                       .OnClicked(this, &SWetWrinkleEditorPanel::HandleBakeAllMapsClicked)]

        + SVerticalBox::Slot()
              .AutoHeight()
              .Padding(4.0f, 2.0f)
                  [SNew(SButton)
                       .Text(LOCTEXT("BakeWetnessProfileMapsMenuItem", "Bake Wetness Profile Maps"))
                       .OnClicked(this, &SWetWrinkleEditorPanel::HandleBakeWetnessProfileMapsClicked)]

        + SVerticalBox::Slot()
              .AutoHeight()
              .Padding(4.0f, 2.0f)
                  [SNew(SButton)
                       .Text(LOCTEXT("BakeWrinkleNormalMapMenuItem", "Bake Wrinkle Normal Map"))
                       .OnClicked(this, &SWetWrinkleEditorPanel::HandleBakeWrinkleNormalMapClicked)]

        + SVerticalBox::Slot()
              .AutoHeight()
              .Padding(4.0f, 2.0f)
                  [SNew(SButton)
                       .Text(LOCTEXT("BakeWrinkleMaskMenuItem", "Bake Wrinkle Mask"))
                       .OnClicked(this, &SWetWrinkleEditorPanel::HandleBakeWrinkleMaskClicked)];
}

FReply SWetWrinkleEditorPanel::HandleBakeAllMapsClicked()
{
    return BakeWrinkleMapsForSelectedSlot(true, true);
}

FReply SWetWrinkleEditorPanel::HandleBakeWetnessProfileMapsClicked()
{
    FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("BakeWetnessProfileMapsFromWrinkle", "Wetness Profile Map baking is available from Part mode."));
    return FReply::Handled();
}

FReply SWetWrinkleEditorPanel::HandleBakeWrinkleNormalMapClicked()
{
    return BakeWrinkleMapsForSelectedSlot(true, false);
}

FReply SWetWrinkleEditorPanel::HandleBakeWrinkleMaskClicked()
{
    // The mask is generated from the same patch rasterization pass as the normal map.
    return BakeWrinkleMapsForSelectedSlot(true, true);
}

FReply SWetWrinkleEditorPanel::BakeWrinkleMapsForSelectedSlot(bool bBakeNormalMap, bool bBakeMask)
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr)
    {
        FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("BakeWrinkleNoAsset", "Wet Clothing Asset is unavailable."));
        return FReply::Handled();
    }

    if (BrushSettings.MaterialSlotIndex == INDEX_NONE)
    {
        FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("BakeWrinkleNoSlot", "Select a material slot before baking wrinkle maps."));
        return FReply::Handled();
    }

    FWetWrinkleNormalMapBakeSettings Settings;
    Settings.Resolution = Asset->WrinkleData.BakeSettings.DefaultResolution;
    Settings.PaddingPixels = Asset->WrinkleData.BakeSettings.PaddingPixels;
    Settings.bIncludeDisabledPatchStrokes = Asset->WrinkleData.BakeSettings.bIncludeDisabledPatchStrokes;
    Settings.bBakeNormalMap = bBakeNormalMap && Asset->WrinkleData.BakeSettings.bBakeNormalMap;
    Settings.bBakeMask = bBakeMask && Asset->WrinkleData.BakeSettings.bBakeMask;

    FWetWrinkleNormalMapBakeResult Result;
    FString ErrorMessage;
    if (!FWetWrinkleNormalMapBaker::BakeMaterialSlot(Asset, BrushSettings.MaterialSlotIndex, Settings, Result, ErrorMessage))
    {
        FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(ErrorMessage));
        return FReply::Handled();
    }

    MarkAssetEdited();
    if (DetailsView.IsValid())
    {
        DetailsView->ForceRefresh();
    }

    FMessageDialog::Open(
        EAppMsgType::Ok,
        FText::Format(
            LOCTEXT("BakeWrinkleSlotSuccess", "Baked {0} wrinkle map set(s) from {1} patch(es)."),
            FText::AsNumber(Result.BakedMapCount),
            FText::AsNumber(Result.BakedStampCount)));
    return FReply::Handled();
}

FReply SWetWrinkleEditorPanel::HandleFocusClicked()
{
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->FocusOnPreviewMesh();
    }
    return FReply::Handled();
}

void SWetWrinkleEditorPanel::HandleSurfaceHitChanged(const FWetWrinkleSurfaceHit& SurfaceHit)
{
    CurrentHit = SurfaceHit;
    RefreshWrinkleUVView();
}

void SWetWrinkleEditorPanel::HandlePaintStrokeStarted(const FWetWrinkleSurfaceHit& SurfaceHit)
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || !SurfaceHit.bHit)
    {
        return;
    }

    ActivePaintTransaction = MakeUnique<FScopedTransaction>(LOCTEXT("PaintWetWrinkleStrokeTransaction", "Paint Wet Wrinkle Patch List"));
    Asset->Modify();

    FWetWrinklePatchStroke NewStroke;
    NewStroke.StrokeGuid = FGuid::NewGuid();
    NewStroke.DisplayName = MakeDefaultStrokeName();
    NewStroke.bEnabled = true;
    NewStroke.PatchPlacements.Add(MakeStampFromHit(SurfaceHit));
    Asset->WrinkleData.EditablePatchStrokes.Add(NewStroke);

    ActiveStrokeGuid = NewStroke.StrokeGuid;
    SelectedStrokeGuid = NewStroke.StrokeGuid;
    LastStampUV = SurfaceHit.UV;
    LastStampMaterialSlotIndex = SurfaceHit.MaterialSlotIndex;
    LastStampUVChannelIndex = SurfaceHit.UVChannelIndex;
    bHasLastStamp = true;
    bAllowImmediateNextStrokeStamp = true;

    MarkAssetEdited();
    RefreshStrokeList();
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->SetSelectedStrokeGuid(SelectedStrokeGuid);
        PreviewViewport->AppendAccumulatedPreviewStamp(NewStroke.PatchPlacements.Last());
    }
    RefreshWrinkleUVView();
}

void SWetWrinkleEditorPanel::HandlePaintStampRequested(const FWetWrinkleSurfaceHit& SurfaceHit)
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    FWetWrinklePatchStroke* ActiveStroke = FindMutableStroke(ActiveStrokeGuid);
    if (Asset == nullptr || ActiveStroke == nullptr || !SurfaceHit.bHit)
    {
        return;
    }

    const bool bAllowStamp = bAllowImmediateNextStrokeStamp || ShouldAddStampForHit(SurfaceHit);
    if (!bAllowStamp)
    {
        return;
    }

    Asset->Modify();
    ActiveStroke->PatchPlacements.Add(MakeStampFromHit(SurfaceHit));
    bAllowImmediateNextStrokeStamp = false;

    LastStampUV = SurfaceHit.UV;
    LastStampMaterialSlotIndex = SurfaceHit.MaterialSlotIndex;
    LastStampUVChannelIndex = SurfaceHit.UVChannelIndex;
    bHasLastStamp = true;

    MarkAssetEdited();
    if (StrokeListView.IsValid())
    {
        StrokeListView->RequestListRefresh();
    }
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->AppendAccumulatedPreviewStamp(ActiveStroke->PatchPlacements.Last());
    }
    RefreshWrinkleUVView();
}

void SWetWrinkleEditorPanel::HandlePaintStrokeEnded()
{
    ActiveStrokeGuid.Invalidate();
    bHasLastStamp = false;
    bAllowImmediateNextStrokeStamp = false;
    ActivePaintTransaction.Reset();
}

void SWetWrinkleEditorPanel::PushBrushSettingsToViewport()
{
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->SetBrushSettings(BrushSettings);
    }

}

void SWetWrinkleEditorPanel::RefreshStrokeList()
{
    StrokeListItems.Reset();

    if (const UWetClothingAsset* Asset = WetClothingAsset.Get())
    {
        for (const FWetWrinklePatchStroke& Stroke : Asset->WrinkleData.EditablePatchStrokes)
        {
            FStrokeListItemPtr Item = MakeShared<FWetWrinklePatchStrokeListItem>();
            Item->StrokeGuid = Stroke.StrokeGuid;
            StrokeListItems.Add(Item);
        }
    }

    if (StrokeListView.IsValid())
    {
        StrokeListView->RequestListRefresh();
        for (const FStrokeListItemPtr& Item : StrokeListItems)
        {
            if (Item.IsValid() && Item->StrokeGuid == SelectedStrokeGuid)
            {
                StrokeListView->SetSelection(Item);
                break;
            }
        }
    }
}

void SWetWrinkleEditorPanel::RefreshStrokeOverlay(bool bRebuildAccumulatedPreview)
{
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->SetSelectedStrokeGuid(SelectedStrokeGuid);
        PreviewViewport->RefreshStoredStampOverlay(bRebuildAccumulatedPreview);
    }
    RefreshWrinkleUVView();
}

void SWetWrinkleEditorPanel::RefreshMaterialSlotOptions()
{
    MaterialSlotOptions.Reset();
    MaterialSlotItems.Reset();
    MaterialSlotThumbnails.Reset();

    MaterialSlotOptions.Add(MakeShared<int32>(INDEX_NONE));

    FMaterialSlotItemPtr AllSlotsItem = MakeShared<FWetClothingMaterialSlotItem>();
    AllSlotsItem->SlotIndex = INDEX_NONE;
    AllSlotsItem->SlotName = TEXT("All Slots");
    MaterialSlotItems.Add(AllSlotsItem);

    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const USkeletalMesh* TargetMesh = nullptr;
    if (Asset != nullptr)
    {
        TargetMesh = Asset->TargetMesh != nullptr ? Asset->TargetMesh.Get() : nullptr;
    }

    if (TargetMesh != nullptr)
    {
        const int32 MaterialCount = TargetMesh->GetMaterials().Num();
        for (int32 MaterialSlotIndex = 0; MaterialSlotIndex < MaterialCount; ++MaterialSlotIndex)
        {
            MaterialSlotOptions.Add(MakeShared<int32>(MaterialSlotIndex));

            const FSkeletalMaterial& SkeletalMaterial = TargetMesh->GetMaterials()[MaterialSlotIndex];
            FMaterialSlotItemPtr Item = MakeShared<FWetClothingMaterialSlotItem>();
            Item->SlotIndex = MaterialSlotIndex;
            Item->SlotName = SkeletalMaterial.MaterialSlotName;
            Item->Material = SkeletalMaterial.MaterialInterface;
            Item->bIsWettableSlot = FWetClothingEditorCommonWidgets::IsMaterialSlotWettable(Asset, MaterialSlotIndex);
            MaterialSlotItems.Add(Item);
        }
    }

    if (MaterialSlotComboBox.IsValid())
    {
        MaterialSlotComboBox->RefreshOptions();
        MaterialSlotComboBox->SetSelectedItem(FindMaterialSlotOption(BrushSettings.MaterialSlotIndex));
    }

    if (MaterialSlotListView.IsValid())
    {
        MaterialSlotListView->RequestListRefresh();
        MaterialSlotListView->SetSelection(FindMaterialSlotItem(BrushSettings.MaterialSlotIndex), ESelectInfo::Direct);
    }
}

void SWetWrinkleEditorPanel::RefreshPartMapItems()
{
    PartMapItems.Reset();

    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset != nullptr && BrushSettings.MaterialSlotIndex != INDEX_NONE)
    {
        for (const FWetClothingWetPartEntry& Entry : Asset->PartData.EditableWetPartData.WetPartEntries)
        {
            if (Entry.MaterialSlotIndex == BrushSettings.MaterialSlotIndex)
            {
                PartMapItems.Add(MakeShared<FWetClothingWetPartEntry>(Entry));
            }
        }

        PartMapItems.Sort([](const FWetPartEntryPtr& A, const FWetPartEntryPtr& B)
        {
            if (!A.IsValid() || !B.IsValid())
            {
                return A.IsValid();
            }

            if (A->UVChannelIndex != B->UVChannelIndex)
            {
                return A->UVChannelIndex < B->UVChannelIndex;
            }

            return A->WetPartID < B->WetPartID;
        });
    }

    if (PartMapListView.IsValid())
    {
        PartMapListView->RequestListRefresh();
    }
}


void SWetWrinkleEditorPanel::EnsureWrinkleUVChannelForModeEntry()
{
    // Wrinkle UVs are intentionally generated lazily per material slot.
    // Opening wrinkle mode should not unwrap the entire target skeletal mesh.
}

bool SWetWrinkleEditorPanel::HasUsableWrinkleUVChannel() const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const USkeletalMesh* TargetMesh = Asset != nullptr ? Asset->TargetMesh.Get() : nullptr;
    if (Asset == nullptr || TargetMesh == nullptr)
    {
        return false;
    }

    const int32 UVChannelIndex = Asset->WrinkleData.WrinkleUVChannelIndex;
    if (UVChannelIndex == INDEX_NONE)
    {
        return false;
    }

    const int32 NumUVChannels = FWetClothingAssetMeshAnalyzer::GetNumUVChannels(TargetMesh, 0);
    return UVChannelIndex >= 0 && UVChannelIndex < NumUVChannels;
}

bool SWetWrinkleEditorPanel::HasGeneratedWrinkleUVForMaterialSlot(int32 MaterialSlotIndex) const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || MaterialSlotIndex == INDEX_NONE || !HasUsableWrinkleUVChannel())
    {
        return false;
    }

    const int32 WrinkleUVChannelIndex = Asset->WrinkleData.WrinkleUVChannelIndex;
    const FWetWrinkleGeneratedUVSlot* GeneratedSlot = Asset->WrinkleData.GeneratedWrinkleUVSlots.FindByPredicate(
        [MaterialSlotIndex, WrinkleUVChannelIndex](const FWetWrinkleGeneratedUVSlot& Candidate)
        {
            return Candidate.MaterialSlotIndex == MaterialSlotIndex && Candidate.UVChannelIndex == WrinkleUVChannelIndex;
        });

    return GeneratedSlot != nullptr;
}

bool SWetWrinkleEditorPanel::EnsureWrinkleUVChannelForMaterialSlot(int32 MaterialSlotIndex, bool bShowFailureDialog)
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr)
    {
        return false;
    }

    USkeletalMesh* TargetMesh = Asset->TargetMesh.Get();
    if (TargetMesh == nullptr)
    {
        if (bShowFailureDialog)
        {
            FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("GenerateWrinkleUVNoTargetMesh", "Assign a Target Mesh before generating wrinkle UVs."));
        }
        return false;
    }

    if (MaterialSlotIndex == INDEX_NONE)
    {
        BrushSettings.UVChannelIndex = INDEX_NONE;
        InvalidateWrinkleUVViewCache();
        return false;
    }

    if (!FWetClothingEditorCommonWidgets::IsMaterialSlotWettable(Asset, MaterialSlotIndex))
    {
        BrushSettings.UVChannelIndex = INDEX_NONE;
        InvalidateWrinkleUVViewCache();
        if (bShowFailureDialog)
        {
            FMessageDialog::Open(
                EAppMsgType::Ok,
                LOCTEXT("GenerateWrinkleUVSlotNotWettable", "This material slot is not marked wettable. Enable the wettable toggle for the slot before editing wrinkles."));
        }
        return false;
    }

    if (HasGeneratedWrinkleUVForMaterialSlot(MaterialSlotIndex))
    {
        BrushSettings.UVChannelIndex = Asset->WrinkleData.WrinkleUVChannelIndex;
        SelectedMeshUVChannelIndex = Asset->WrinkleData.WrinkleUVChannelIndex;
        return true;
    }

    FWetWrinkleUVChannelGenerationSettings Settings;
    Settings.LODIndex = 0;
    Settings.Resolution = FMath::Max(16, Asset->WrinkleData.BakeSettings.DefaultResolution);
    Settings.PaddingPixels = FMath::Max(4, Asset->WrinkleData.BakeSettings.PaddingPixels);
    Settings.SourceUVChannelIndex = 0;
    Settings.PreferredUVChannelIndex = Asset->WrinkleData.WrinkleUVChannelIndex != INDEX_NONE
                                           ? Asset->WrinkleData.WrinkleUVChannelIndex
                                           : FWetClothingAssetMeshAnalyzer::GetNumUVChannels(TargetMesh, 0);
    Settings.TargetMaterialSlotIndex = MaterialSlotIndex;
    Settings.bAllowOverwriteExistingGeneratedChannel = true;

    const FScopedTransaction Transaction(LOCTEXT("LazyGenerateWrinkleUVForSlotTransaction", "Generate Wrinkle UV For Material Slot"));
    FWetWrinkleUVChannelGenerationResult Result = FWetWrinkleUVChannelGenerator::GenerateForAsset(Asset, Settings);
    if (!Result.bSucceeded)
    {
        BrushSettings.UVChannelIndex = INDEX_NONE;
        InvalidateWrinkleUVViewCache();
        if (bShowFailureDialog)
        {
            FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Result.Message));
        }
        return false;
    }

    BrushSettings.UVChannelIndex = Result.UVChannelIndex;
    SelectedMeshUVChannelIndex = Result.UVChannelIndex;
    RefreshUVChannelOptions();
    CurrentHit = FWetWrinkleSurfaceHit();
    bHasLastStamp = false;
    LastStampUVChannelIndex = INDEX_NONE;
    InvalidateWrinkleUVViewCache();
    MarkAssetEdited();
    return true;
}

void SWetWrinkleEditorPanel::InvalidateWrinkleUVViewCache()
{
    CachedWrinkleUVViewChannelIndex = INDEX_NONE;
    CachedWrinkleUVViewMaterialSlotIndex = INDEX_NONE;
}

void SWetWrinkleEditorPanel::RefreshUVChannelOptions()
{
    MeshUVChannelOptions.Reset();

    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const USkeletalMesh* TargetMesh = Asset != nullptr ? Asset->TargetMesh.Get() : nullptr;
    const int32 NumUVChannels = FWetClothingAssetMeshAnalyzer::GetNumUVChannels(TargetMesh, 0);

    for (int32 UVChannelIndex = 0; UVChannelIndex < NumUVChannels; ++UVChannelIndex)
    {
        MeshUVChannelOptions.Add(MakeShared<int32>(UVChannelIndex));
    }

    if (SelectedMeshUVChannelIndex == INDEX_NONE || SelectedMeshUVChannelIndex >= NumUVChannels)
    {
        SelectedMeshUVChannelIndex = Asset != nullptr ? Asset->WrinkleData.WrinkleUVChannelIndex : INDEX_NONE;
        if (SelectedMeshUVChannelIndex < 0 || SelectedMeshUVChannelIndex >= NumUVChannels)
        {
            SelectedMeshUVChannelIndex = NumUVChannels > 0 ? 0 : INDEX_NONE;
        }
    }

    if (MeshUVChannelComboBox.IsValid())
    {
        MeshUVChannelComboBox->RefreshOptions();
        TSharedPtr<int32> SelectedItem;
        for (const TSharedPtr<int32>& Item : MeshUVChannelOptions)
        {
            if (Item.IsValid() && *Item == SelectedMeshUVChannelIndex)
            {
                SelectedItem = Item;
                break;
            }
        }
        MeshUVChannelComboBox->SetSelectedItem(SelectedItem);
    }
}

int32 SWetWrinkleEditorPanel::GetWrinkleUVViewChannelIndex() const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const USkeletalMesh* TargetMesh = Asset != nullptr ? Asset->TargetMesh.Get() : nullptr;
    const int32 NumUVChannels = FWetClothingAssetMeshAnalyzer::GetNumUVChannels(TargetMesh, 0);

    if (SelectedMeshUVChannelIndex >= 0 && SelectedMeshUVChannelIndex < NumUVChannels)
    {
        return SelectedMeshUVChannelIndex;
    }

    const int32 WrinkleUVChannelIndex = Asset != nullptr ? Asset->WrinkleData.WrinkleUVChannelIndex : INDEX_NONE;
    return WrinkleUVChannelIndex >= 0 && WrinkleUVChannelIndex < NumUVChannels ? WrinkleUVChannelIndex : INDEX_NONE;
}

int32 SWetWrinkleEditorPanel::GetProtectedBaseUVChannelCount() const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const USkeletalMesh* TargetMesh = Asset != nullptr ? Asset->TargetMesh.Get() : nullptr;
    const int32 NumUVChannels = FWetClothingAssetMeshAnalyzer::GetNumUVChannels(TargetMesh, 0);
    if (Asset == nullptr || TargetMesh == nullptr)
    {
        return NumUVChannels;
    }

#if WITH_EDITORONLY_DATA
    if (Asset->WrinkleData.OriginalUVChannelCount != INDEX_NONE)
    {
        return FMath::Clamp(Asset->WrinkleData.OriginalUVChannelCount, 0, NumUVChannels);
    }
#endif

    int32 FirstKnownGeneratedChannel = TNumericLimits<int32>::Max();
    for (const FWetWrinkleGeneratedUVSlot& GeneratedSlot : Asset->WrinkleData.GeneratedWrinkleUVSlots)
    {
        if (GeneratedSlot.UVChannelIndex >= 0)
        {
            FirstKnownGeneratedChannel = FMath::Min(FirstKnownGeneratedChannel, GeneratedSlot.UVChannelIndex);
        }
    }

#if WITH_EDITORONLY_DATA
    if (Asset->WrinkleData.bHasGeneratedWrinkleUV && Asset->WrinkleData.WrinkleUVChannelIndex >= 0)
    {
        FirstKnownGeneratedChannel = FMath::Min(FirstKnownGeneratedChannel, Asset->WrinkleData.WrinkleUVChannelIndex);
    }
#endif

    return FirstKnownGeneratedChannel != TNumericLimits<int32>::Max()
               ? FMath::Clamp(FirstKnownGeneratedChannel, 0, NumUVChannels)
               : NumUVChannels;
}

bool SWetWrinkleEditorPanel::IsUVChannelDeleteAllowed(int32 UVChannelIndex) const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const USkeletalMesh* TargetMesh = Asset != nullptr ? Asset->TargetMesh.Get() : nullptr;
    const int32 NumUVChannels = FWetClothingAssetMeshAnalyzer::GetNumUVChannels(TargetMesh, 0);
    return UVChannelIndex >= 0 &&
           UVChannelIndex < NumUVChannels &&
           UVChannelIndex >= GetProtectedBaseUVChannelCount();
}

TSharedRef<SWidget> SWetWrinkleEditorPanel::BuildWrinkleUVViewSection()
{
    return SNew(SBorder)
        .Padding(8.0f)
        .BorderImage(FAppStyle::Get().GetBrush(TEXT("ToolPanel.GroupBorder")))
            [SNew(SVerticalBox)

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 4.0f)
                       [FWetClothingEditorCommonWidgets::BuildSectionHeader(
                           LOCTEXT("WrinkleUVViewLabel", "Wrinkle UV View"),
                           TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &SWetWrinkleEditorPanel::GetSelectedMeshUVChannelText)))]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                       [SNew(SSeparator)
                            .Orientation(Orient_Horizontal)]

             + SVerticalBox::Slot()
                   .FillHeight(1.0f)
                       [SAssignNew(WrinkleUVView, SWetClothingAssetUVView)]];
}

void SWetWrinkleEditorPanel::RefreshWrinkleUVView()
{
    if (!WrinkleUVView.IsValid())
    {
        return;
    }

    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const USkeletalMesh* TargetMesh = Asset != nullptr ? Asset->TargetMesh.Get() : nullptr;
    const int32 MaterialSlotIndex = BrushSettings.MaterialSlotIndex;
    const int32 UVChannelIndex = GetWrinkleUVViewChannelIndex();
    const int32 NumUVChannels = FWetClothingAssetMeshAnalyzer::GetNumUVChannels(TargetMesh, 0);
    const bool bNeedsIslandRebuild = CachedWrinkleUVViewChannelIndex != UVChannelIndex ||
                                     CachedWrinkleUVViewMaterialSlotIndex != MaterialSlotIndex ||
                                     WrinkleUVIslandItems.Num() == 0;

    if (bNeedsIslandRebuild)
    {
        WrinkleUVIslandItems.Reset();
        CachedWrinkleUVViewChannelIndex = UVChannelIndex;
        CachedWrinkleUVViewMaterialSlotIndex = MaterialSlotIndex;

        if (TargetMesh != nullptr &&
            MaterialSlotIndex != INDEX_NONE &&
            UVChannelIndex >= 0 &&
            UVChannelIndex < NumUVChannels)
        {
            TArray<FWetClothingAssetUVIsland> BuiltIslands;
            if (FWetClothingAssetMeshAnalyzer::BuildMaterialSlotUVIslands(TargetMesh, 0, UVChannelIndex, MaterialSlotIndex, BuiltIslands, nullptr))
            {
                int32 NextUVIslandID = 0;
                for (FWetClothingAssetUVIsland& Island : BuiltIslands)
                {
                    Island.UVIslandID = NextUVIslandID++;
                    for (FWetClothingAssetUVTriangle& Triangle : Island.UVTriangles)
                    {
                        Triangle.UVIslandID = Island.UVIslandID;
                    }
                    WrinkleUVIslandItems.Add(MakeShared<FWetClothingAssetUVIsland>(Island));
                }
            }
        }

        WrinkleUVView->SetBackgroundTexture(nullptr);
        WrinkleUVView->SetDrawBackgroundTexture(false);
        WrinkleUVView->SetIslands(WrinkleUVIslandItems);
        WrinkleUVView->SetIslandColors(TMap<int32, FLinearColor>());
        WrinkleUVView->SetHiddenUVIslandIDs(TSet<int32>());
        WrinkleUVView->SetSelectedIslands(TSet<int32>());
        WrinkleUVView->SetDisplayMode(EWetClothingAssetUVDisplayMode::OutlineOnly);
    }

    TArray<FWetClothingAssetUVViewCircleMarker> CircleMarkers;
    if (Asset != nullptr && MaterialSlotIndex != INDEX_NONE && UVChannelIndex != INDEX_NONE)
    {
        const FLinearColor PatchFillColor(0.35f, 0.82f, 1.0f, 0.38f);
        const FLinearColor PatchOutlineColor(0.35f, 0.82f, 1.0f, 0.95f);
        for (const FWetWrinklePatchStroke& Stroke : Asset->WrinkleData.EditablePatchStrokes)
        {
            if (!Stroke.bEnabled)
            {
                continue;
            }

            for (const FWetWrinklePatchPlacement& Patch : Stroke.PatchPlacements)
            {
                if (Patch.MaterialSlotIndex != MaterialSlotIndex || Patch.UVChannelIndex != UVChannelIndex)
                {
                    continue;
                }

                FWetClothingAssetUVViewCircleMarker Marker;
                Marker.CenterUV = Patch.PositionUV;
                Marker.RadiusUV = FMath::Max(Patch.BrushRadiusUV * static_cast<float>(FMath::Max(Patch.Scale.X, Patch.Scale.Y)), 0.001f);
                Marker.FillColor = PatchFillColor;
                Marker.OutlineColor = PatchOutlineColor;
                Marker.OutlineThickness = 1.0f;
                CircleMarkers.Add(Marker);
            }
        }
    }

    if (BrushSettings.bShowPreview &&
        CurrentHit.bHit &&
        CurrentHit.MaterialSlotIndex == MaterialSlotIndex &&
        CurrentHit.UVChannelIndex == UVChannelIndex)
    {
        FWetClothingAssetUVViewCircleMarker Marker;
        Marker.CenterUV = CurrentHit.UV;
        Marker.RadiusUV = FMath::Max(BrushSettings.BrushRadiusUV, 0.001f);
        Marker.FillColor = FLinearColor(1.0f, 0.55f, 0.08f, 0.45f);
        Marker.OutlineColor = FLinearColor(1.0f, 0.55f, 0.08f, 1.0f);
        Marker.OutlineThickness = 1.4f;
        CircleMarkers.Add(Marker);
    }

    WrinkleUVView->SetCircleMarkers(CircleMarkers);
}

void SWetWrinkleEditorPanel::RefreshBrushPresetOptions()
{
    BrushPresetOptions.Reset();
    PatchTextureThumbnails.Reset();

    auto AddPreset = [this](const FText& DisplayName, const FSoftObjectPath& TexturePath)
    {
        if (!TexturePath.IsValid() || Cast<UTexture2D>(TexturePath.TryLoad()) == nullptr)
        {
            return;
        }

        TSharedPtr<FWetWrinkleBrushPresetOption> Option = MakeShared<FWetWrinkleBrushPresetOption>();
        Option->DisplayName = DisplayName;
        Option->TexturePath = TexturePath;
        BrushPresetOptions.Add(Option);
    };

    bool bFoundFromRegistry = false;
    FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    TArray<FAssetData> TextureAssets;
    AssetRegistryModule.Get().GetAssetsByPath(FName(WetWrinklePresetFolderPath), TextureAssets, false);
    TextureAssets.Sort([](const FAssetData& A, const FAssetData& B)
    {
        return A.AssetName.ToString() < B.AssetName.ToString();
    });

    for (const FAssetData& TextureAsset : TextureAssets)
    {
        const int32 PreviousCount = BrushPresetOptions.Num();
        AddPreset(FText::FromName(TextureAsset.AssetName), TextureAsset.ToSoftObjectPath());
        bFoundFromRegistry |= BrushPresetOptions.Num() > PreviousCount;
    }

    if (!bFoundFromRegistry)
    {
        AddPreset(LOCTEXT("WetWrinklePreset0", "Wet_Wrinkle_Normal0"), FSoftObjectPath(WetWrinklePreset0Path));
    }

    if (BrushPresetComboBox.IsValid())
    {
        BrushPresetComboBox->RefreshOptions();
        BrushPresetComboBox->SetSelectedItem(FindBrushPresetOption(BrushSettings.BrushHeightTexture.Get()));
    }

    if (PatchTextureListView.IsValid())
    {
        PatchTextureListView->RequestListRefresh();
        PatchTextureListView->SetSelection(FindBrushPresetOption(BrushSettings.BrushHeightTexture.Get()), ESelectInfo::Direct);
    }
}

FText SWetWrinkleEditorPanel::GetSelectedMeshUVChannelText() const
{
    if (SelectedMeshUVChannelIndex == INDEX_NONE)
    {
        return LOCTEXT("NoMeshUVChannelSelected", "No UV Channel");
    }

    return GetMeshUVChannelDisplayText(SelectedMeshUVChannelIndex);
}

FText SWetWrinkleEditorPanel::GetMeshUVChannelDisplayText(int32 UVChannelIndex) const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const USkeletalMesh* TargetMesh = Asset != nullptr ? Asset->TargetMesh.Get() : nullptr;
    const int32 NumUVChannels = FWetClothingAssetMeshAnalyzer::GetNumUVChannels(TargetMesh, 0);
    if (UVChannelIndex < 0 || UVChannelIndex >= NumUVChannels)
    {
        return LOCTEXT("InvalidMeshUVChannel", "Invalid UV Channel");
    }

    const bool bProtectedOriginal = UVChannelIndex < GetProtectedBaseUVChannelCount();
    bool bKnownDWCGenerated = false;
    if (Asset != nullptr)
    {
        bKnownDWCGenerated = Asset->WrinkleData.GeneratedWrinkleUVSlots.ContainsByPredicate(
            [UVChannelIndex](const FWetWrinkleGeneratedUVSlot& Candidate)
            {
                return Candidate.UVChannelIndex == UVChannelIndex;
            });
#if WITH_EDITORONLY_DATA
        bKnownDWCGenerated = bKnownDWCGenerated ||
                             (Asset->WrinkleData.bHasGeneratedWrinkleUV && Asset->WrinkleData.WrinkleUVChannelIndex == UVChannelIndex);
#endif
    }

    if (bProtectedOriginal)
    {
        return FText::Format(LOCTEXT("OriginalMeshUVChannelLabel", "UV {0} (Original / Protected)"), FText::AsNumber(UVChannelIndex));
    }

    if (bKnownDWCGenerated)
    {
        return FText::Format(LOCTEXT("GeneratedMeshUVChannelLabel", "UV {0} (DWC Generated)"), FText::AsNumber(UVChannelIndex));
    }

    return FText::Format(LOCTEXT("ExtraMeshUVChannelLabel", "UV {0} (Extra / Deletable)"), FText::AsNumber(UVChannelIndex));
}

TSharedRef<SWidget> SWetWrinkleEditorPanel::GenerateMeshUVChannelComboRow(TSharedPtr<int32> Item) const
{
    const int32 UVChannelIndex = Item.IsValid() ? *Item : INDEX_NONE;
    return SNew(STextBlock)
        .Text(GetMeshUVChannelDisplayText(UVChannelIndex));
}

void SWetWrinkleEditorPanel::HandleMeshUVChannelComboChanged(TSharedPtr<int32> Item, ESelectInfo::Type SelectInfo)
{
    if (!Item.IsValid())
    {
        return;
    }

    SelectedMeshUVChannelIndex = *Item;
    InvalidateWrinkleUVViewCache();
    RefreshWrinkleUVView();
}

bool SWetWrinkleEditorPanel::IsDeleteMeshUVChannelEnabled() const
{
    return IsUVChannelDeleteAllowed(SelectedMeshUVChannelIndex);
}

FReply SWetWrinkleEditorPanel::HandleDeleteMeshUVChannelClicked()
{
    if (!IsUVChannelDeleteAllowed(SelectedMeshUVChannelIndex))
    {
        FMessageDialog::Open(
            EAppMsgType::Ok,
            LOCTEXT("DeleteMeshUVChannelProtected", "This UV channel belongs to the original mesh data or is not valid, so it cannot be deleted from the wrinkle editor."));
        return FReply::Handled();
    }

    const EAppReturnType::Type ConfirmResult = FMessageDialog::Open(
        EAppMsgType::YesNo,
        FText::Format(
            LOCTEXT(
                "DeleteMeshUVChannelConfirm",
                "Delete UV Channel {0}?\n\nThis will remove wrinkle patches and baked wrinkle map records that use this channel. Higher UV channel indices will shift down by one."),
            FText::AsNumber(SelectedMeshUVChannelIndex)));
    if (ConfirmResult != EAppReturnType::Yes)
    {
        return FReply::Handled();
    }

    const FScopedTransaction Transaction(LOCTEXT("DeleteMeshUVChannelTransaction", "Delete DWC Generated UV Channel"));
    const int32 DeletedChannelIndex = SelectedMeshUVChannelIndex;
    FWetWrinkleUVChannelGenerationResult Result = FWetWrinkleUVChannelGenerator::DeleteUVChannelForAsset(WetClothingAsset.Get(), 0, DeletedChannelIndex);
    if (!Result.bSucceeded)
    {
        FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Result.Message));
        return FReply::Handled();
    }

    if (BrushSettings.UVChannelIndex == DeletedChannelIndex)
    {
        BrushSettings.UVChannelIndex = INDEX_NONE;
    }
    else if (BrushSettings.UVChannelIndex > DeletedChannelIndex)
    {
        --BrushSettings.UVChannelIndex;
    }

    CurrentHit = FWetWrinkleSurfaceHit();
    bHasLastStamp = false;
    LastStampUVChannelIndex = INDEX_NONE;
    SelectedMeshUVChannelIndex = BrushSettings.UVChannelIndex != INDEX_NONE ? BrushSettings.UVChannelIndex : FMath::Max(0, DeletedChannelIndex - 1);

    RefreshUVChannelOptions();
    InvalidateWrinkleUVViewCache();
    RefreshStrokeList();
    RefreshStrokeOverlay();
    PushBrushSettingsToViewport();
    RefreshWrinkleUVView();
    MarkAssetEdited();

    FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Result.Message));
    return FReply::Handled();
}

FText SWetWrinkleEditorPanel::GetWrinkleUVChannelText() const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const USkeletalMesh* TargetMesh = Asset != nullptr ? Asset->TargetMesh.Get() : nullptr;
    const int32 MaterialSlotIndex = BrushSettings.MaterialSlotIndex;
    const int32 UVChannelIndex = Asset != nullptr ? Asset->WrinkleData.WrinkleUVChannelIndex : INDEX_NONE;

    if (TargetMesh == nullptr)
    {
        return LOCTEXT("WrinkleUVNoTargetMeshText", "Not generated. Assign a Target Mesh first.");
    }

    if (MaterialSlotIndex == INDEX_NONE)
    {
        return LOCTEXT(
            "WrinkleUVNoSelectedSlotText",
            "Select a wettable material slot. Wrinkle UVs are generated lazily per selected slot, not for the whole mesh.");
    }

    if (!FWetClothingEditorCommonWidgets::IsMaterialSlotWettable(Asset, MaterialSlotIndex))
    {
        return FText::Format(
            LOCTEXT("WrinkleUVSlotNotWettableText", "Slot {0} is not wettable. Enable wettable for this slot to generate wrinkle UVs."),
            FText::AsNumber(MaterialSlotIndex));
    }

    if (UVChannelIndex == INDEX_NONE)
    {
        return FText::Format(
            LOCTEXT("WrinkleUVChannelNotGeneratedText", "Slot {0}: not generated yet. It will be generated when this slot is selected."),
            FText::AsNumber(MaterialSlotIndex));
    }

    const int32 NumUVChannels = FWetClothingAssetMeshAnalyzer::GetNumUVChannels(TargetMesh, 0);
    if (UVChannelIndex < 0 || NumUVChannels <= UVChannelIndex)
    {
        return FText::Format(
            LOCTEXT("WrinkleUVChannelMissingText", "Slot {0}: UV Channel {1} is recorded as wrinkle UV, but missing on the target mesh."),
            FText::AsNumber(MaterialSlotIndex),
            FText::AsNumber(UVChannelIndex));
    }

    if (!HasGeneratedWrinkleUVForMaterialSlot(MaterialSlotIndex))
    {
        return FText::Format(
            LOCTEXT("WrinkleUVSlotPendingText", "Slot {0}: pending lazy generation in wrinkle UV Channel {1}."),
            FText::AsNumber(MaterialSlotIndex),
            FText::AsNumber(UVChannelIndex));
    }

    return FText::Format(
        LOCTEXT(
            "WrinkleUVChannelGeneratedText",
            "Slot {0}: UV Channel {1}. Only this material slot is shown/generated in the Wrinkle UV View."),
        FText::AsNumber(MaterialSlotIndex),
        FText::AsNumber(UVChannelIndex));
}

FReply SWetWrinkleEditorPanel::HandleGenerateWrinkleUVChannelClicked()
{
    const int32 MaterialSlotIndex = BrushSettings.MaterialSlotIndex;
    if (MaterialSlotIndex == INDEX_NONE)
    {
        FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("GenerateWrinkleUVNoSelectedSlot", "Select a material slot before generating wrinkle UVs."));
        return FReply::Handled();
    }

    const bool bGenerated = EnsureWrinkleUVChannelForMaterialSlot(MaterialSlotIndex, true);
    if (bGenerated)
    {
        MarkAssetEdited();
        RefreshUVChannelOptions();
        RefreshPartMapItems();
        RefreshStrokeOverlay();
        PushBrushSettingsToViewport();
        RefreshWrinkleUVView();
    }

    return FReply::Handled();
}

FReply SWetWrinkleEditorPanel::HandleAutoGenerateClicked()
{
    FMessageDialog::Open(
        EAppMsgType::Ok,
        LOCTEXT(
            "WrinkleAutoGenerateNotImplementedMessage",
            "Automatic wrinkle texture generation is not implemented yet. This button will generate wrinkle texture maps from the current patch data."));

    return FReply::Handled();
}

FText SWetWrinkleEditorPanel::GetHitInfoText() const
{
    if (!CurrentHit.bHit)
    {
        return LOCTEXT("NoSurfaceHit", "No mesh surface under cursor.");
    }

    return FText::FromString(FString::Printf(
        TEXT("Slot: %d\nTriangle: %d\nUV%d: %.4f, %.4f\nPosition: %.1f, %.1f, %.1f"),
        CurrentHit.MaterialSlotIndex,
        CurrentHit.TriangleID,
        CurrentHit.UVChannelIndex,
        CurrentHit.UV.X,
        CurrentHit.UV.Y,
        CurrentHit.WorldPosition.X,
        CurrentHit.WorldPosition.Y,
        CurrentHit.WorldPosition.Z));
}

FText SWetWrinkleEditorPanel::GetPatchListSummaryText() const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const int32 StrokeCount = Asset != nullptr ? Asset->WrinkleData.EditablePatchStrokes.Num() : 0;
    int32 StampCount = 0;
    if (Asset != nullptr)
    {
        for (const FWetWrinklePatchStroke& Stroke : Asset->WrinkleData.EditablePatchStrokes)
        {
            StampCount += Stroke.PatchPlacements.Num();
        }
    }

    return FText::Format(LOCTEXT("PatchListSummary", "{0} patch list(s), {1} patch(es)."), FText::AsNumber(StrokeCount), FText::AsNumber(StampCount));
}

FText SWetWrinkleEditorPanel::GetMaterialSlotCountText() const
{
    int32 SlotCount = 0;
    for (const FMaterialSlotItemPtr& Item : MaterialSlotItems)
    {
        if (Item.IsValid() && Item->SlotIndex != INDEX_NONE)
        {
            ++SlotCount;
        }
    }

    return FText::Format(LOCTEXT("MaterialSlotCount", "{0} Slots"), FText::AsNumber(SlotCount));
}

FText SWetWrinkleEditorPanel::GetPartMapSectionText() const
{
    if (BrushSettings.MaterialSlotIndex == INDEX_NONE)
    {
        return LOCTEXT("PartMapSectionNoSlot", "Part Map: No slot");
    }

    return FText::Format(
        LOCTEXT("PartMapSection", "Part Map / Slot {0} / {1}"),
        FText::AsNumber(BrushSettings.MaterialSlotIndex),
        FText::Format(LOCTEXT("PartMapCount", "{0} Parts"), FText::AsNumber(PartMapItems.Num())));
}

TSharedRef<SWidget> SWetWrinkleEditorPanel::GenerateMaterialSlotComboRow(TSharedPtr<int32> Item) const
{
    const int32 MaterialSlotIndex = Item.IsValid() ? *Item : INDEX_NONE;
    return SNew(STextBlock)
        .Text(GetMaterialSlotDisplayText(MaterialSlotIndex));
}

FText SWetWrinkleEditorPanel::GetSelectedMaterialSlotText() const
{
    return GetMaterialSlotDisplayText(BrushSettings.MaterialSlotIndex);
}

void SWetWrinkleEditorPanel::HandleMaterialSlotComboChanged(TSharedPtr<int32> Item, ESelectInfo::Type SelectInfo)
{
    BrushSettings.MaterialSlotIndex = Item.IsValid() && *Item >= 0 ? *Item : INDEX_NONE;
    CurrentHit = FWetWrinkleSurfaceHit();
    EnsureWrinkleUVChannelForMaterialSlot(BrushSettings.MaterialSlotIndex, false);
    RefreshUVChannelOptions();
    PushBrushSettingsToViewport();
    RefreshStrokeOverlay();
    RefreshPartMapItems();
    RefreshWrinkleUVView();
}


TSharedRef<ITableRow> SWetWrinkleEditorPanel::GenerateMaterialSlotRow(FMaterialSlotItemPtr Item, const TSharedRef<STableViewBase>& OwnerTable)
{
    FWetClothingMaterialSlotRowArgs Args;
    Args.WetClothingAsset = WetClothingAsset.Get();
    Args.TargetMesh = WetClothingAsset.IsValid() ? WetClothingAsset->TargetMesh.Get() : nullptr;
    Args.SelectedMaterialSlotIndex = BrushSettings.MaterialSlotIndex;
    Args.ThumbnailPool = MaterialThumbnailPool;
    Args.ThumbnailSink = &MaterialSlotThumbnails;
    Args.OnWettableSlotClicked = FOnWettableMaterialSlotClicked::CreateSP(this, &SWetWrinkleEditorPanel::HandleWettableMaterialSlotClicked);

    return FWetClothingEditorCommonWidgets::GenerateMaterialSlotRow(Item, OwnerTable, Args);
}

void SWetWrinkleEditorPanel::HandleMaterialSlotSelectionChanged(FMaterialSlotItemPtr Item, ESelectInfo::Type SelectInfo)
{
    BrushSettings.MaterialSlotIndex = Item.IsValid() ? Item->SlotIndex : INDEX_NONE;
    CurrentHit = FWetWrinkleSurfaceHit();
    EnsureWrinkleUVChannelForMaterialSlot(BrushSettings.MaterialSlotIndex, false);
    RefreshUVChannelOptions();
    if (MaterialSlotComboBox.IsValid())
    {
        MaterialSlotComboBox->SetSelectedItem(FindMaterialSlotOption(BrushSettings.MaterialSlotIndex));
    }
    PushBrushSettingsToViewport();
    RefreshStrokeOverlay();
    RefreshPartMapItems();
    RefreshWrinkleUVView();
}

FReply SWetWrinkleEditorPanel::HandleWettableMaterialSlotClicked(int32 MaterialSlotIndex)
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || MaterialSlotIndex == INDEX_NONE)
    {
        return FReply::Handled();
    }

    const bool bNewWettable = !FWetClothingEditorCommonWidgets::IsMaterialSlotWettable(Asset, MaterialSlotIndex);
    FWetClothingEditorCommonWidgets::SetMaterialSlotWettable(Asset, MaterialSlotIndex, bNewWettable);

    if (bNewWettable)
    {
        BrushSettings.MaterialSlotIndex = MaterialSlotIndex;
        CurrentHit = FWetWrinkleSurfaceHit();
        EnsureWrinkleUVChannelForMaterialSlot(MaterialSlotIndex, true);
        PushBrushSettingsToViewport();
        RefreshUVChannelOptions();
        RefreshPartMapItems();
        RefreshStrokeOverlay();
        RefreshWrinkleUVView();
    }
    else if (BrushSettings.MaterialSlotIndex == MaterialSlotIndex)
    {
        BrushSettings.UVChannelIndex = INDEX_NONE;
        CurrentHit = FWetWrinkleSurfaceHit();
        InvalidateWrinkleUVViewCache();
        PushBrushSettingsToViewport();
        RefreshStrokeOverlay();
        RefreshWrinkleUVView();
    }

    RefreshMaterialSlotOptions();
    return FReply::Handled();
}

TSharedRef<ITableRow> SWetWrinkleEditorPanel::GeneratePartMapRow(FWetPartEntryPtr Item, const TSharedRef<STableViewBase>& OwnerTable)
{
    return FWetClothingEditorCommonWidgets::GeneratePartMapRow(Item, OwnerTable);
}

TSharedRef<SWidget> SWetWrinkleEditorPanel::GenerateBrushPresetComboRow(TSharedPtr<FWetWrinkleBrushPresetOption> Item) const
{
    return SNew(STextBlock)
        .Text(Item.IsValid() ? Item->DisplayName : LOCTEXT("MissingWrinklePreset", "<missing>"));
}


TSharedRef<ITableRow> SWetWrinkleEditorPanel::GeneratePatchTextureRow(FPatchTextureItemPtr Item, const TSharedRef<STableViewBase>& OwnerTable)
{
    UTexture2D* Texture = Item.IsValid() ? Cast<UTexture2D>(Item->TexturePath.TryLoad()) : nullptr;
    TSharedRef<SWidget> ThumbnailWidget =
        SNew(SBorder)
        .BorderImage(FAppStyle::Get().GetBrush(TEXT("WhiteBrush")))
        .BorderBackgroundColor(FLinearColor(0.06f, 0.06f, 0.06f, 1.0f));

    if (Texture != nullptr && PatchTextureThumbnailPool.IsValid())
    {
        TSharedPtr<FAssetThumbnail> Thumbnail = MakeShared<FAssetThumbnail>(Texture, 52, 52, PatchTextureThumbnailPool);
        PatchTextureThumbnails.Add(Thumbnail);

        FAssetThumbnailConfig ThumbnailConfig;
        ThumbnailConfig.bAllowFadeIn = false;
        ThumbnailWidget = Thumbnail->MakeThumbnailWidget(ThumbnailConfig);
    }

    return SNew(STableRow<FPatchTextureItemPtr>, OwnerTable)
        .Padding(4.0f)
        [SNew(SHorizontalBox)

         + SHorizontalBox::Slot()
               .AutoWidth()
               .VAlign(VAlign_Center)
               .Padding(0.0f, 0.0f, 8.0f, 0.0f)
                   [SNew(SBox)
                        .WidthOverride(56.0f)
                        .HeightOverride(56.0f)
                            [ThumbnailWidget]]

         + SHorizontalBox::Slot()
               .FillWidth(1.0f)
               .VAlign(VAlign_Center)
                   [SNew(STextBlock)
                        .Text(Item.IsValid() ? Item->DisplayName : LOCTEXT("MissingPatchTexture", "<missing>"))]];
}

FText SWetWrinkleEditorPanel::GetSelectedBrushPresetText() const
{
    if (TSharedPtr<FWetWrinkleBrushPresetOption> Option = FindBrushPresetOption(BrushSettings.BrushHeightTexture.Get()))
    {
        return Option->DisplayName;
    }

    UTexture2D* BrushHeightTexture = BrushSettings.BrushHeightTexture.Get();
    return BrushHeightTexture != nullptr
               ? FText::FromString(FString::Printf(TEXT("Custom - %s"), *BrushHeightTexture->GetName()))
               : LOCTEXT("NoWrinklePresetSelected", "None");
}

void SWetWrinkleEditorPanel::HandleBrushPresetChanged(TSharedPtr<FWetWrinkleBrushPresetOption> Item, ESelectInfo::Type SelectInfo)
{
    if (!Item.IsValid())
    {
        return;
    }

    BrushSettings.BrushHeightTexture = Cast<UTexture2D>(Item->TexturePath.TryLoad());
    PushBrushSettingsToViewport();
    RefreshStrokeOverlay();
}


void SWetWrinkleEditorPanel::HandlePatchTextureSelectionChanged(FPatchTextureItemPtr Item, ESelectInfo::Type SelectInfo)
{
    if (!Item.IsValid())
    {
        return;
    }

    BrushSettings.BrushHeightTexture = Cast<UTexture2D>(Item->TexturePath.TryLoad());
    if (BrushPresetComboBox.IsValid())
    {
        BrushPresetComboBox->SetSelectedItem(Item);
    }

    PushBrushSettingsToViewport();
    RefreshStrokeOverlay();
}

FString SWetWrinkleEditorPanel::GetBrushHeightTextureObjectPath() const
{
    UTexture2D* BrushHeightTexture = BrushSettings.BrushHeightTexture.Get();
    return BrushHeightTexture != nullptr ? BrushHeightTexture->GetPathName() : FString();
}

void SWetWrinkleEditorPanel::HandleBrushHeightTextureChanged(const FAssetData& AssetData)
{
    BrushSettings.BrushHeightTexture = Cast<UTexture2D>(AssetData.GetAsset());
    if (BrushPresetComboBox.IsValid())
    {
        BrushPresetComboBox->SetSelectedItem(FindBrushPresetOption(BrushSettings.BrushHeightTexture.Get()));
    }

    PushBrushSettingsToViewport();
    RefreshStrokeOverlay();
}

TSharedRef<ITableRow> SWetWrinkleEditorPanel::GenerateStrokeRow(FStrokeListItemPtr Item, const TSharedRef<STableViewBase>& OwnerTable)
{
    return SNew(STableRow<FStrokeListItemPtr>, OwnerTable)
        .Padding(2.0f)
            [SNew(SHorizontalBox)

             + SHorizontalBox::Slot()
                   .AutoWidth()
                   .VAlign(VAlign_Center)
                   .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                       [SNew(SCheckBox)
                            .IsChecked_Lambda([this, Item]()
                                              {
                                                  const FWetWrinklePatchStroke* Stroke = Item.IsValid() ? FindStroke(Item->StrokeGuid) : nullptr;
                                                  return Stroke != nullptr && Stroke->bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
                                              })
                            .OnCheckStateChanged(this, &SWetWrinkleEditorPanel::HandleStrokeEnabledChanged, Item)]

             + SHorizontalBox::Slot()
                   .FillWidth(1.0f)
                   .VAlign(VAlign_Center)
                       [SNew(SInlineEditableTextBlock)
                            .Text_Lambda([this, Item]()
                                         {
                                             const FWetWrinklePatchStroke* Stroke = Item.IsValid() ? FindStroke(Item->StrokeGuid) : nullptr;
                                             return Stroke != nullptr ? FText::FromString(Stroke->DisplayName) : LOCTEXT("MissingPatchListName", "<missing>");
                                         })
                            .OnTextCommitted(this, &SWetWrinkleEditorPanel::HandleStrokeNameCommitted, Item)]

             + SHorizontalBox::Slot()
                   .AutoWidth()
                   .VAlign(VAlign_Center)
                   .Padding(6.0f, 0.0f, 6.0f, 0.0f)
                       [SNew(STextBlock)
                            .Text_Lambda([this, Item]()
                                         {
                                             const FWetWrinklePatchStroke* Stroke = Item.IsValid() ? FindStroke(Item->StrokeGuid) : nullptr;
                                             return FText::AsNumber(Stroke != nullptr ? Stroke->PatchPlacements.Num() : 0);
                                         })]

             + SHorizontalBox::Slot()
                   .AutoWidth()
                   .VAlign(VAlign_Center)
                       [SNew(SButton)
                            .Text(LOCTEXT("DeleteStrokeButton", "Delete"))
                            .OnClicked(this, &SWetWrinkleEditorPanel::HandleDeleteStrokeClicked, Item)]];
}

void SWetWrinkleEditorPanel::HandleStrokeSelectionChanged(FStrokeListItemPtr Item, ESelectInfo::Type SelectInfo)
{
    SelectedStrokeGuid = Item.IsValid() ? Item->StrokeGuid : FGuid();
    RefreshStrokeOverlay();
}

FReply SWetWrinkleEditorPanel::HandleClearStrokesClicked()
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || Asset->WrinkleData.EditablePatchStrokes.Num() == 0)
    {
        return FReply::Handled();
    }

    const FScopedTransaction Transaction(LOCTEXT("ClearWetWrinkleStrokesTransaction", "Clear Wet Wrinkle Patch Lists"));
    Asset->Modify();
    Asset->WrinkleData.EditablePatchStrokes.Reset();
    ActiveStrokeGuid.Invalidate();
    SelectedStrokeGuid.Invalidate();
    bHasLastStamp = false;
    MarkAssetEdited();
    RefreshStrokeList();
    RefreshStrokeOverlay();
    return FReply::Handled();
}

bool SWetWrinkleEditorPanel::IsClearStrokesEnabled() const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    return Asset != nullptr && Asset->WrinkleData.EditablePatchStrokes.Num() > 0;
}

void SWetWrinkleEditorPanel::HandleStrokeEnabledChanged(ECheckBoxState NewState, FStrokeListItemPtr Item)
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    FWetWrinklePatchStroke* Stroke = Item.IsValid() ? FindMutableStroke(Item->StrokeGuid) : nullptr;
    if (Asset == nullptr || Stroke == nullptr)
    {
        return;
    }

    const bool bNewEnabled = NewState == ECheckBoxState::Checked;
    if (Stroke->bEnabled == bNewEnabled)
    {
        return;
    }

    const FScopedTransaction Transaction(LOCTEXT("ToggleWetWrinkleStrokeTransaction", "Toggle Wet Wrinkle Patch List"));
    Asset->Modify();
    Stroke->bEnabled = bNewEnabled;
    MarkAssetEdited();
    RefreshStrokeOverlay();
}

void SWetWrinkleEditorPanel::HandleStrokeNameCommitted(const FText& InText, ETextCommit::Type CommitType, FStrokeListItemPtr Item)
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    FWetWrinklePatchStroke* Stroke = Item.IsValid() ? FindMutableStroke(Item->StrokeGuid) : nullptr;
    if (Asset == nullptr || Stroke == nullptr)
    {
        return;
    }

    const FString NewName = InText.ToString().TrimStartAndEnd();
    if (NewName.IsEmpty() || Stroke->DisplayName == NewName)
    {
        return;
    }

    const FScopedTransaction Transaction(LOCTEXT("RenameWetWrinkleStrokeTransaction", "Rename Wet Wrinkle Patch List"));
    Asset->Modify();
    Stroke->DisplayName = NewName;
    MarkAssetEdited();
    RefreshStrokeList();
}

FReply SWetWrinkleEditorPanel::HandleDeleteStrokeClicked(FStrokeListItemPtr Item)
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || !Item.IsValid())
    {
        return FReply::Handled();
    }

    const int32 StrokeIndex = Asset->WrinkleData.EditablePatchStrokes.IndexOfByPredicate(
        [Item](const FWetWrinklePatchStroke& Stroke)
        {
            return Stroke.StrokeGuid == Item->StrokeGuid;
        });
    if (StrokeIndex == INDEX_NONE)
    {
        return FReply::Handled();
    }

    const FScopedTransaction Transaction(LOCTEXT("DeleteWetWrinkleStrokeTransaction", "Delete Wet Wrinkle Patch List"));
    Asset->Modify();
    const FGuid DeletedGuid = Asset->WrinkleData.EditablePatchStrokes[StrokeIndex].StrokeGuid;
    Asset->WrinkleData.EditablePatchStrokes.RemoveAt(StrokeIndex);
    if (ActiveStrokeGuid == DeletedGuid)
    {
        ActiveStrokeGuid.Invalidate();
        bHasLastStamp = false;
        ActivePaintTransaction.Reset();
    }
    if (SelectedStrokeGuid == DeletedGuid)
    {
        SelectedStrokeGuid.Invalidate();
    }
    MarkAssetEdited();
    RefreshStrokeList();
    RefreshStrokeOverlay();
    return FReply::Handled();
}

void SWetWrinkleEditorPanel::HandleUVChannelChanged(int32 NewValue)
{
    const int32 NewUVChannelIndex = NewValue;
    BrushSettings.UVChannelIndex = NewUVChannelIndex;
    if (UWetClothingAsset* Asset = WetClothingAsset.Get())
    {
        Asset->Modify();
        Asset->WrinkleData.WrinkleUVChannelIndex = NewUVChannelIndex;
        MarkAssetEdited();
    }
    CurrentHit = FWetWrinkleSurfaceHit();
    InvalidateWrinkleUVViewCache();
    PushBrushSettingsToViewport();
    RefreshPartMapItems();
    RefreshWrinkleUVView();
}

void SWetWrinkleEditorPanel::HandleMaterialSlotChanged(int32 NewValue)
{
    BrushSettings.MaterialSlotIndex = NewValue < 0 ? INDEX_NONE : NewValue;
    CurrentHit = FWetWrinkleSurfaceHit();
    EnsureWrinkleUVChannelForMaterialSlot(BrushSettings.MaterialSlotIndex, true);
    if (MaterialSlotComboBox.IsValid())
    {
        MaterialSlotComboBox->SetSelectedItem(FindMaterialSlotOption(BrushSettings.MaterialSlotIndex));
    }
    PushBrushSettingsToViewport();
    RefreshStrokeOverlay();
    RefreshPartMapItems();
    RefreshWrinkleUVView();
}

float SWetWrinkleEditorPanel::GetBrushSizeCm() const
{
    return SizeCm;
}

FText SWetWrinkleEditorPanel::GetBrushSizeDisplayText() const
{
    return FormatWetWrinkleBrushSizeCm(SizeCm);
}

TSharedRef<SWidget> SWetWrinkleEditorPanel::BuildBrushSizeMenu()
{
    static constexpr float BrushSizePresetsCm[] = {
        0.7f, 1.0f, 1.5f, 2.0f, 2.5f, 3.0f, 4.0f, 5.0f,
        6.0f, 7.0f, 8.0f, 10.0f, 12.0f, 15.0f, 17.0f, 20.0f,
        25.0f, 30.0f, 40.0f, 50.0f, 60.0f, 70.0f, 80.0f, 100.0f
    };

    constexpr int32 ColumnCount = 8;
    TSharedRef<SUniformGridPanel> Grid = SNew(SUniformGridPanel)
        .SlotPadding(FMargin(2.0f, 2.0f));

    for (int32 PresetIndex = 0; PresetIndex < UE_ARRAY_COUNT(BrushSizePresetsCm); ++PresetIndex)
    {
        const float PresetSizeCm = BrushSizePresetsCm[PresetIndex];
        const int32 Column = PresetIndex % ColumnCount;
        const int32 Row = PresetIndex / ColumnCount;
        const int32 DotFontSize = FMath::RoundToInt(8.0f + FMath::Sqrt(PresetSizeCm / 100.0f) * 18.0f);

        Grid->AddSlot(Column, Row)
            [SNew(SButton)
                 .ContentPadding(FMargin(3.0f, 2.0f))
                 .OnClicked(this, &SWetWrinkleEditorPanel::HandleBrushSizePresetClicked, PresetSizeCm)
                     [SNew(SVerticalBox)

                      + SVerticalBox::Slot()
                            .AutoHeight()
                            .HAlign(HAlign_Center)
                                [SNew(SBox)
                                     .HeightOverride(24.0f)
                                     .VAlign(VAlign_Center)
                                         [SNew(STextBlock)
                                              .Text(FText::FromString(TEXT("\u25CF")))
                                              .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), DotFontSize))
                                              .ColorAndOpacity(FSlateColor(FLinearColor::White))]]

                      + SVerticalBox::Slot()
                            .AutoHeight()
                            .HAlign(HAlign_Center)
                                [SNew(STextBlock)
                                     .Text(FormatWetWrinkleBrushSizeCm(PresetSizeCm))]]];
    }

    return SNew(SBorder)
        .Padding(4.0f)
        .BorderImage(FAppStyle::Get().GetBrush(TEXT("Menu.Background")))
            [Grid];
}

void SWetWrinkleEditorPanel::HandleBrushRadiusChanged(float NewValue)
{
    SizeCm = FMath::Clamp(NewValue, 0.1f, 100.0f);
    SizeUV = FMath::Clamp(SizeCm * WetWrinkleUVPerCm, 0.001f, 0.5f);
    BrushSettings.BrushRadiusUV = SizeUV;
    PushBrushSettingsToViewport();
    RefreshWrinkleUVView();
}

FReply SWetWrinkleEditorPanel::HandleBrushSizePresetClicked(float NewValue)
{
    HandleBrushRadiusChanged(NewValue);
    if (BrushSizeComboButton.IsValid())
    {
        BrushSizeComboButton->SetIsOpen(false);
    }

    return FReply::Handled();
}

void SWetWrinkleEditorPanel::HandleStrengthChanged(float NewValue)
{
    BrushSettings.Strength = FMath::Clamp(NewValue, 0.0f, 4.0f);
    PushBrushSettingsToViewport();
}

void SWetWrinkleEditorPanel::HandleFalloffChanged(float NewValue)
{
    BrushSettings.Falloff = FMath::Clamp(NewValue / 100.0f, 0.0f, 1.0f);
    PushBrushSettingsToViewport();
}

void SWetWrinkleEditorPanel::HandleRotationChanged(float NewValue)
{
    BrushSettings.RotationRadians = FMath::DegreesToRadians(NewValue);
    PushBrushSettingsToViewport();
}

void SWetWrinkleEditorPanel::HandlePreviewToggleChanged(ECheckBoxState NewState)
{
    BrushSettings.bShowPreview = NewState == ECheckBoxState::Checked;
    PushBrushSettingsToViewport();
    RefreshWrinkleUVView();
}

ECheckBoxState SWetWrinkleEditorPanel::GetPreviewToggleState() const
{
    return BrushSettings.bShowPreview ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

FWetWrinklePatchStroke* SWetWrinkleEditorPanel::FindMutableStroke(const FGuid& StrokeGuid) const
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || !StrokeGuid.IsValid())
    {
        return nullptr;
    }

    return Asset->WrinkleData.EditablePatchStrokes.FindByPredicate(
        [StrokeGuid](const FWetWrinklePatchStroke& Stroke)
        {
            return Stroke.StrokeGuid == StrokeGuid;
        });
}

const FWetWrinklePatchStroke* SWetWrinkleEditorPanel::FindStroke(const FGuid& StrokeGuid) const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || !StrokeGuid.IsValid())
    {
        return nullptr;
    }

    return Asset->WrinkleData.EditablePatchStrokes.FindByPredicate(
        [StrokeGuid](const FWetWrinklePatchStroke& Stroke)
        {
            return Stroke.StrokeGuid == StrokeGuid;
        });
}

FWetWrinklePatchPlacement SWetWrinkleEditorPanel::MakeStampFromHit(const FWetWrinkleSurfaceHit& SurfaceHit) const
{
    FWetWrinklePatchPlacement Stamp;
    Stamp.MaterialSlotIndex = SurfaceHit.MaterialSlotIndex;
    Stamp.UVChannelIndex = SurfaceHit.UVChannelIndex;
    Stamp.SourceTexture = ResolveSourceTextureForStamp(SurfaceHit.MaterialSlotIndex, SurfaceHit.UVChannelIndex);
    Stamp.PositionUV = SurfaceHit.UV;
    Stamp.BrushRadiusUV = BrushSettings.BrushRadiusUV;
    Stamp.RotationRadians = BrushSettings.RotationRadians;
    Stamp.Scale = FVector2D(1.0f, 1.0f);
    Stamp.Strength = BrushSettings.Strength;
    Stamp.Falloff = BrushSettings.Falloff;
    Stamp.NormalPatchTexture = BrushSettings.BrushHeightTexture.Get();
    Stamp.AffectedWetPartID = INDEX_NONE;
#if WITH_EDITORONLY_DATA
    Stamp.bHasEditorSurface = true;
    Stamp.EditorSurfaceLocalPosition = SurfaceHit.LocalPosition;
    Stamp.EditorSurfaceLocalNormal = SurfaceHit.LocalNormal;
    Stamp.EditorSurfaceLocalTangent = SurfaceHit.LocalTangent;
    Stamp.EditorSurfaceLocalBitangent = SurfaceHit.LocalBitangent;
#endif
    return Stamp;
}

UTexture* SWetWrinkleEditorPanel::ResolveSourceTextureForStamp(int32 MaterialSlotIndex, int32 UVChannelIndex) const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr)
    {
        return nullptr;
    }

    for (const FWetClothingSourceTextureSelection& TextureSelection : Asset->PartData.EditableWetPartData.SourceTextureSelections)
        {
            if (TextureSelection.MaterialSlotIndex == MaterialSlotIndex &&
                TextureSelection.UVChannelIndex == UVChannelIndex &&
                TextureSelection.Texture != nullptr)
            {
                return TextureSelection.Texture;
            }
        }

    const USkeletalMesh* TargetMesh = Asset != nullptr ? Asset->TargetMesh.Get() : nullptr;

    if (TargetMesh != nullptr && TargetMesh->GetMaterials().IsValidIndex(MaterialSlotIndex))
    {
        return FWetClothingMaterialTextureResolver::ResolveBestMaterialTexture(TargetMesh->GetMaterials()[MaterialSlotIndex].MaterialInterface);
    }

    return nullptr;
}

UTexture2D* SWetWrinkleEditorPanel::ResolveDefaultBrushHeightTexture() const
{
    return LoadObject<UTexture2D>(nullptr, WetWrinklePreset0Path);
}

FText SWetWrinkleEditorPanel::GetMaterialSlotDisplayText(int32 MaterialSlotIndex) const
{
    if (MaterialSlotIndex == INDEX_NONE)
    {
        return LOCTEXT("AllMaterialSlots", "All Slots");
    }

    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const USkeletalMesh* TargetMesh = nullptr;
    if (Asset != nullptr)
    {
        TargetMesh = Asset->TargetMesh != nullptr ? Asset->TargetMesh.Get() : nullptr;
    }

    if (TargetMesh != nullptr && TargetMesh->GetMaterials().IsValidIndex(MaterialSlotIndex))
    {
        const FName SlotName = TargetMesh->GetMaterials()[MaterialSlotIndex].MaterialSlotName;
        return FText::FromString(FString::Printf(TEXT("%d - %s"), MaterialSlotIndex, *SlotName.ToString()));
    }

    return FText::FromString(FString::Printf(TEXT("%d"), MaterialSlotIndex));
}

TSharedPtr<int32> SWetWrinkleEditorPanel::FindMaterialSlotOption(int32 MaterialSlotIndex) const
{
    for (const TSharedPtr<int32>& Option : MaterialSlotOptions)
    {
        if (Option.IsValid() && *Option == MaterialSlotIndex)
        {
            return Option;
        }
    }

    return MaterialSlotOptions.Num() > 0 ? MaterialSlotOptions[0] : nullptr;
}


SWetWrinkleEditorPanel::FMaterialSlotItemPtr SWetWrinkleEditorPanel::FindMaterialSlotItem(int32 MaterialSlotIndex) const
{
    for (const FMaterialSlotItemPtr& Item : MaterialSlotItems)
    {
        if (Item.IsValid() && Item->SlotIndex == MaterialSlotIndex)
        {
            return Item;
        }
    }

    return MaterialSlotItems.Num() > 0 ? MaterialSlotItems[0] : nullptr;
}

TSharedPtr<FWetWrinkleBrushPresetOption> SWetWrinkleEditorPanel::FindBrushPresetOption(UTexture2D* Texture) const
{
    if (Texture == nullptr)
    {
        return nullptr;
    }

    const FSoftObjectPath TexturePath(Texture);
    for (const TSharedPtr<FWetWrinkleBrushPresetOption>& Option : BrushPresetOptions)
    {
        if (Option.IsValid() && Option->TexturePath == TexturePath)
        {
            return Option;
        }
    }

    return nullptr;
}

void SWetWrinkleEditorPanel::HandleTextureUVHovered(const FVector2D& UV)
{
    if (!PreviewViewport.IsValid())
    {
        return;
    }

    const int32 PreviewMaterialSlotIndex = CurrentHit.bHit ? CurrentHit.MaterialSlotIndex : BrushSettings.MaterialSlotIndex;
    const int32 PreviewUVChannelIndex = CurrentHit.bHit ? CurrentHit.UVChannelIndex : BrushSettings.UVChannelIndex;
    if (PreviewMaterialSlotIndex == INDEX_NONE)
    {
        return;
    }

    FVector2D TiledUV = UV;
    if (CurrentHit.bHit &&
        CurrentHit.MaterialSlotIndex == PreviewMaterialSlotIndex &&
        CurrentHit.UVChannelIndex == PreviewUVChannelIndex)
    {
        TiledUV.X += FMath::FloorToFloat(CurrentHit.UV.X);
        TiledUV.Y += FMath::FloorToFloat(CurrentHit.UV.Y);
    }

    FWetWrinkleSurfaceHit SurfaceHit;
    if (PreviewViewport->TryBuildSurfaceHitAtUV(PreviewMaterialSlotIndex, PreviewUVChannelIndex, TiledUV, SurfaceHit))
    {
        CurrentHit = SurfaceHit;
        PreviewViewport->PreviewBrushAtUV(PreviewMaterialSlotIndex, PreviewUVChannelIndex, TiledUV);
        RefreshWrinkleUVView();
        return;
    }

    PreviewViewport->ClearExternalBrushPreview();
    CurrentHit = FWetWrinkleSurfaceHit();
    RefreshWrinkleUVView();
}

void SWetWrinkleEditorPanel::HandleTextureUVHoverEnded()
{
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->ClearExternalBrushPreview();
    }
    CurrentHit = FWetWrinkleSurfaceHit();
    RefreshWrinkleUVView();
}

void SWetWrinkleEditorPanel::HandleTexturePaintStrokeStarted(const FVector2D& UV)
{
    FWetWrinkleSurfaceHit SurfaceHit;
    if (TryBuildTextureSurfaceHit(UV, SurfaceHit))
    {
        HandleSurfaceHitChanged(SurfaceHit);
        HandlePaintStrokeStarted(SurfaceHit);
    }
}

void SWetWrinkleEditorPanel::HandleTexturePaintStampRequested(const FVector2D& UV)
{
    FWetWrinkleSurfaceHit SurfaceHit;
    if (TryBuildTextureSurfaceHit(UV, SurfaceHit))
    {
        HandleSurfaceHitChanged(SurfaceHit);
        HandlePaintStampRequested(SurfaceHit);
    }
}

void SWetWrinkleEditorPanel::HandleTexturePaintStrokeEnded()
{
    HandlePaintStrokeEnded();
}

bool SWetWrinkleEditorPanel::TryBuildTextureSurfaceHit(const FVector2D& UV, FWetWrinkleSurfaceHit& OutSurfaceHit) const
{
    if (!PreviewViewport.IsValid())
    {
        return false;
    }

    const int32 PreviewMaterialSlotIndex = CurrentHit.bHit ? CurrentHit.MaterialSlotIndex : BrushSettings.MaterialSlotIndex;
    const int32 PreviewUVChannelIndex = CurrentHit.bHit ? CurrentHit.UVChannelIndex : BrushSettings.UVChannelIndex;
    if (PreviewMaterialSlotIndex == INDEX_NONE)
    {
        return false;
    }

    FVector2D TiledUV = UV;
    if (CurrentHit.bHit &&
        CurrentHit.MaterialSlotIndex == PreviewMaterialSlotIndex &&
        CurrentHit.UVChannelIndex == PreviewUVChannelIndex)
    {
        TiledUV.X += FMath::FloorToFloat(CurrentHit.UV.X);
        TiledUV.Y += FMath::FloorToFloat(CurrentHit.UV.Y);
    }

    return PreviewViewport->TryBuildSurfaceHitAtUV(PreviewMaterialSlotIndex, PreviewUVChannelIndex, TiledUV, OutSurfaceHit);
}

bool SWetWrinkleEditorPanel::ShouldAddStampForHit(const FWetWrinkleSurfaceHit& SurfaceHit) const
{
    if (!SurfaceHit.bHit)
    {
        return false;
    }

    if (!bHasLastStamp)
    {
        return true;
    }

    if (SurfaceHit.MaterialSlotIndex != LastStampMaterialSlotIndex ||
        SurfaceHit.UVChannelIndex != LastStampUVChannelIndex)
    {
        return true;
    }

    const double SpacingUV = FMath::Max(BrushSettings.BrushRadiusUV * 0.5f, 0.0005f);
    return FVector2D::Distance(SurfaceHit.UV, LastStampUV) >= SpacingUV;
}

FString SWetWrinkleEditorPanel::MakeDefaultStrokeName() const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const int32 StrokeNumber = Asset != nullptr ? Asset->WrinkleData.EditablePatchStrokes.Num() + 1 : 1;
    return FString::Printf(TEXT("Patch %03d"), StrokeNumber);
}

void SWetWrinkleEditorPanel::MarkAssetEdited()
{
    if (UWetClothingAsset* Asset = WetClothingAsset.Get())
    {
        Asset->MarkPackageDirty();
    }
}

#undef LOCTEXT_NAMESPACE
