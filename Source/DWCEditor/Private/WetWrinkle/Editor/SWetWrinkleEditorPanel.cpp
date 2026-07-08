#include "SWetWrinkleEditorPanel.h"

#include "AssetRegistry/AssetData.h"
#include "Core/DWCEditorUtils.h"
#include "DataAssets/WetClothingAsset.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "IDetailsView.h"
#include "PropertyCustomizationHelpers.h"
#include "Styling/CoreStyle.h"
#include "WetClothing/Texture/WetClothingMaterialTextureResolver.h"
#include "WetWrinkle/Viewport/WetWrinkleViewport.h"
#include "WetWrinkle/Widgets/SWetWrinkleTexturePreview.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "WetClothingAssetEditorPanel"

namespace
{
    constexpr const TCHAR* WetWrinklePreset0Path = TEXT("/DynamicWetClothes/Presets/WrinkleTextures/Wet_Wrinkle_Normal0.Wet_Wrinkle_Normal0");
}

void SWetWrinkleEditorPanel::Construct(const FArguments& InArgs)
{
    WetClothingAsset = InArgs._WetClothingAsset;
    DetailsView = InArgs._DetailsView;
    if (UWetClothingAsset* Asset = WetClothingAsset.Get())
    {
        BrushSettings.UVChannelIndex = FMath::Max(0, Asset->WrinkleData.WrinkleUVChannelIndex);
    }
    RefreshMaterialSlotOptions();
    RefreshBrushPresetOptions();
    BrushSettings.BrushHeightTexture = ResolveDefaultBrushHeightTexture();

    const FSlateFontInfo PanelHeadingFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 16);
    const FSlateFontInfo SectionHeadingFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 13);

    ChildSlot
        [SNew(SVerticalBox)

         + SVerticalBox::Slot()
               .AutoHeight()
               .Padding(10.0f, 10.0f, 10.0f, 8.0f)
                   [SNew(SHorizontalBox)

                    + SHorizontalBox::Slot()
                          .FillWidth(1.0f)
                          .VAlign(VAlign_Center)
                              [SNew(STextBlock)
                                   .Text(LOCTEXT("EditorHeading", "Wet Wrinkle"))
                                   .Font(PanelHeadingFont)]

                    + SHorizontalBox::Slot()
                          .AutoWidth()
                          .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                              [SNew(SButton)
                                   .Text(LOCTEXT("FocusButton", "Focus"))
                                   .OnClicked(this, &SWetWrinkleEditorPanel::HandleFocusClicked)]

                    + SHorizontalBox::Slot()
                          .AutoWidth()
                              [SNew(SButton)
                                   .Text(LOCTEXT("SaveButton", "Save"))
                                   .OnClicked(this, &SWetWrinkleEditorPanel::HandleSaveClicked)]]

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
                          .Value(0.25f)
                              [SNew(SBorder)
                                   .Padding(10.0f)
                                       [SNew(SVerticalBox)

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                                                  [SNew(STextBlock)
                                                       .Text(LOCTEXT("AssetHeading", "Asset"))
                                                       .Font(SectionHeadingFont)]

                                        + SVerticalBox::Slot()
                                              .FillHeight(1.0f)
                                                  [DetailsView.IsValid()
                                                       ? StaticCastSharedRef<SWidget>(DetailsView.ToSharedRef())
                                                       : StaticCastSharedRef<SWidget>(SNew(STextBlock).Text(LOCTEXT("MissingDetails", "Details view is unavailable.")))]]]

                    + SSplitter::Slot()
                          .Value(0.5f)
                              [SNew(SBorder)
                                   .Padding(10.0f)
                                       [SNew(SVerticalBox)

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                                                  [SNew(STextBlock)
                                                       .Text(LOCTEXT("PreviewHeading", "Preview"))
                                                       .Font(SectionHeadingFont)]

                                        + SVerticalBox::Slot()
                                              .FillHeight(0.68f)
                                                  [SAssignNew(PreviewViewport, SWetWrinkleViewport)
                                                       .WetClothingAsset(WetClothingAsset.Get())
                                                       .OnSurfaceHitChanged(FOnWetWrinkleSurfaceHitChanged::CreateSP(this, &SWetWrinkleEditorPanel::HandleSurfaceHitChanged))
                                                       .OnPaintStrokeStarted(FOnWetWrinklePaintStrokeStarted::CreateSP(this, &SWetWrinkleEditorPanel::HandlePaintStrokeStarted))
                                                       .OnPaintStampRequested(FOnWetWrinklePaintStampRequested::CreateSP(this, &SWetWrinkleEditorPanel::HandlePaintStampRequested))
                                                       .OnPaintStrokeEnded(FOnWetWrinklePaintStrokeEnded::CreateSP(this, &SWetWrinkleEditorPanel::HandlePaintStrokeEnded))]

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 10.0f, 0.0f, 6.0f)
                                                  [SNew(STextBlock)
                                                       .Text(LOCTEXT("TexturePreviewHeading", "Texture Preview"))
                                                       .Font(SectionHeadingFont)]

                                        + SVerticalBox::Slot()
                                              .FillHeight(0.32f)
                                                  [SNew(SBox)
                                                       .MinDesiredHeight(180.0f)
                                                           [SAssignNew(TexturePreview, SWetWrinkleTexturePreview)
                                                            ]]]]

                    + SSplitter::Slot()
                          .Value(0.25f)
                              [SNew(SBorder)
                                   .Padding(10.0f)
                                       [SNew(SVerticalBox)

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                                                  [SNew(STextBlock)
                                                       .Text(LOCTEXT("BrushHeading", "Brush"))
                                                       .Font(SectionHeadingFont)]

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                                                  [SNew(STextBlock)
                                                       .Text(LOCTEXT("UVChannelLabel", "Wrinkle UV Channel"))]

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                                                  [SNew(SSpinBox<int32>)
                                                       .MinValue(0)
                                                       .MaxValue(7)
                                                       .Value(BrushSettings.UVChannelIndex)
                                                       .OnValueChanged(this, &SWetWrinkleEditorPanel::HandleUVChannelChanged)]

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                                                  [SNew(STextBlock)
                                                       .Text(LOCTEXT("MaterialSlotLabel", "Material Slot Filter"))]

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                                                  [SAssignNew(MaterialSlotComboBox, SComboBox<TSharedPtr<int32>>)
                                                       .OptionsSource(&MaterialSlotOptions)
                                                       .InitiallySelectedItem(FindMaterialSlotOption(BrushSettings.MaterialSlotIndex))
                                                       .OnGenerateWidget(this, &SWetWrinkleEditorPanel::GenerateMaterialSlotComboRow)
                                                       .OnSelectionChanged(this, &SWetWrinkleEditorPanel::HandleMaterialSlotComboChanged)
                                                           [SNew(STextBlock)
                                                                .Text(this, &SWetWrinkleEditorPanel::GetSelectedMaterialSlotText)]]

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                                                  [SNew(STextBlock)
                                                       .Text(LOCTEXT("BrushPresetLabel", "Wrinkle Preset"))]

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                                                  [SAssignNew(BrushPresetComboBox, SComboBox<TSharedPtr<FWetWrinkleBrushPresetOption>>)
                                                       .OptionsSource(&BrushPresetOptions)
                                                       .InitiallySelectedItem(FindBrushPresetOption(BrushSettings.BrushHeightTexture.Get()))
                                                       .OnGenerateWidget(this, &SWetWrinkleEditorPanel::GenerateBrushPresetComboRow)
                                                       .OnSelectionChanged(this, &SWetWrinkleEditorPanel::HandleBrushPresetChanged)
                                                           [SNew(STextBlock)
                                                                .Text(this, &SWetWrinkleEditorPanel::GetSelectedBrushPresetText)]]

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                                                  [SNew(STextBlock)
                                                       .Text(LOCTEXT("BrushHeightTextureLabel", "Brush Normal Texture"))]

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                                                  [SNew(SObjectPropertyEntryBox)
                                                       .AllowedClass(UTexture2D::StaticClass())
                                                       .ObjectPath(this, &SWetWrinkleEditorPanel::GetBrushHeightTextureObjectPath)
                                                       .OnObjectChanged(this, &SWetWrinkleEditorPanel::HandleBrushHeightTextureChanged)]

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                                                  [SNew(STextBlock)
                                                       .Text(LOCTEXT("RadiusLabel", "Radius UV"))]

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                                                  [SNew(SSpinBox<float>)
                                                       .MinValue(0.001f)
                                                       .MaxValue(0.5f)
                                                       .Value(BrushSettings.BrushRadiusUV)
                                                       .OnValueChanged(this, &SWetWrinkleEditorPanel::HandleBrushRadiusChanged)]

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
                                                       .MaxValue(1.0f)
                                                       .Value(BrushSettings.Strength)
                                                       .OnValueChanged(this, &SWetWrinkleEditorPanel::HandleStrengthChanged)]

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                                                  [SNew(STextBlock)
                                                       .Text(LOCTEXT("FalloffLabel", "Falloff"))]

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                                                  [SNew(SSpinBox<float>)
                                                       .MinValue(0.0f)
                                                       .MaxValue(1.0f)
                                                       .Value(BrushSettings.Falloff)
                                                       .OnValueChanged(this, &SWetWrinkleEditorPanel::HandleFalloffChanged)]

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                                                  [SNew(STextBlock)
                                                       .Text(LOCTEXT("RotationLabel", "Rotation"))]

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                                                  [SNew(SSpinBox<float>)
                                                       .MinValue(-3.14159f)
                                                       .MaxValue(3.14159f)
                                                       .Value(BrushSettings.RotationRadians)
                                                       .OnValueChanged(this, &SWetWrinkleEditorPanel::HandleRotationChanged)]

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 0.0f, 0.0f, 12.0f)
                                                  [SNew(SCheckBox)
                                                       .IsChecked(this, &SWetWrinkleEditorPanel::GetPreviewToggleState)
                                                       .OnCheckStateChanged(this, &SWetWrinkleEditorPanel::HandlePreviewToggleChanged)
                                                           [SNew(STextBlock)
                                                                .Text(LOCTEXT("PreviewToggle", "Show Preview Cursor"))]]

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 6.0f, 0.0f, 8.0f)
                                                  [SNew(STextBlock)
                                                       .Text(LOCTEXT("HitHeading", "Surface Hit"))
                                                       .Font(SectionHeadingFont)]

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 0.0f, 0.0f, 12.0f)
                                                  [SNew(SBox)
                                                       .HeightOverride(84.0f)
                                                           [SNew(STextBlock)
                                                                .AutoWrapText(true)
                                                                .Text(this, &SWetWrinkleEditorPanel::GetHitInfoText)]]

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 6.0f, 0.0f, 8.0f)
                                                  [SNew(SHorizontalBox)

                                                   + SHorizontalBox::Slot()
                                                         .FillWidth(1.0f)
                                                         .VAlign(VAlign_Center)
                                                             [SNew(STextBlock)
                                                                  .Text(LOCTEXT("StrokeHeading", "Strokes"))
                                                                  .Font(SectionHeadingFont)]

                                                   + SHorizontalBox::Slot()
                                                         .AutoWidth()
                                                             [SNew(SButton)
                                                                  .Text(LOCTEXT("ClearStrokesButton", "Clear"))
                                                                  .IsEnabled(this, &SWetWrinkleEditorPanel::IsClearStrokesEnabled)
                                                                  .OnClicked(this, &SWetWrinkleEditorPanel::HandleClearStrokesClicked)]]

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                                                  [SNew(STextBlock)
                                                       .AutoWrapText(true)
                                                       .Text(this, &SWetWrinkleEditorPanel::GetStrokeSummaryText)]

                                        + SVerticalBox::Slot()
                                              .FillHeight(1.0f)
                                                  [SAssignNew(StrokeListView, SListView<FStrokeListItemPtr>)
                                                       .ListItemsSource(&StrokeListItems)
                                                       .OnGenerateRow(this, &SWetWrinkleEditorPanel::GenerateStrokeRow)
                                                       .OnSelectionChanged(this, &SWetWrinkleEditorPanel::HandleStrokeSelectionChanged)]]]]];

    PushBrushSettingsToViewport();
    RefreshFromAsset();
}

void SWetWrinkleEditorPanel::RefreshFromAsset()
{
    if (UWetClothingAsset* Asset = WetClothingAsset.Get())
    {
        BrushSettings.UVChannelIndex = FMath::Max(0, Asset->WrinkleData.WrinkleUVChannelIndex);
    }
    RefreshMaterialSlotOptions();
    RefreshBrushPresetOptions();
    RefreshStrokeList();

    if (PreviewViewport.IsValid())
    {
        PreviewViewport->RefreshPreviewMesh();
        PushBrushSettingsToViewport();
        RefreshStrokeOverlay();
    }

    RefreshTexturePreview();
}

FReply SWetWrinkleEditorPanel::HandleSaveClicked()
{
    DWCEditorUtils::SaveAsset(WetClothingAsset.Get());
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
    RefreshTexturePreview();
}

void SWetWrinkleEditorPanel::HandlePaintStrokeStarted(const FWetWrinkleSurfaceHit& SurfaceHit)
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || !SurfaceHit.bHit)
    {
        return;
    }

    ActivePaintTransaction = MakeUnique<FScopedTransaction>(LOCTEXT("PaintWetWrinkleStrokeTransaction", "Paint Wet Wrinkle Stroke"));
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
    RefreshStrokeOverlay();
    RefreshTexturePreview();
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
    RefreshStrokeOverlay();
    RefreshTexturePreview();
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

    RefreshTexturePreview();
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

void SWetWrinkleEditorPanel::RefreshStrokeOverlay()
{
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->SetSelectedStrokeGuid(SelectedStrokeGuid);
        PreviewViewport->RefreshStoredStampOverlay();
    }
}

void SWetWrinkleEditorPanel::RefreshMaterialSlotOptions()
{
    MaterialSlotOptions.Reset();
    MaterialSlotOptions.Add(MakeShared<int32>(INDEX_NONE));

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
        }
    }

    if (MaterialSlotComboBox.IsValid())
    {
        MaterialSlotComboBox->RefreshOptions();
        MaterialSlotComboBox->SetSelectedItem(FindMaterialSlotOption(BrushSettings.MaterialSlotIndex));
    }
}

void SWetWrinkleEditorPanel::RefreshBrushPresetOptions()
{
    BrushPresetOptions.Reset();

    auto AddPreset = [this](const FText& DisplayName, const TCHAR* TexturePath)
    {
        if (TexturePath == nullptr || LoadObject<UTexture2D>(nullptr, TexturePath) == nullptr)
        {
            return;
        }

        TSharedPtr<FWetWrinkleBrushPresetOption> Option = MakeShared<FWetWrinkleBrushPresetOption>();
        Option->DisplayName = DisplayName;
        Option->TexturePath = FSoftObjectPath(TexturePath);
        BrushPresetOptions.Add(Option);
    };

    AddPreset(LOCTEXT("WetWrinklePreset0", "Preset 0"), WetWrinklePreset0Path);

    if (BrushPresetComboBox.IsValid())
    {
        BrushPresetComboBox->RefreshOptions();
        BrushPresetComboBox->SetSelectedItem(FindBrushPresetOption(BrushSettings.BrushHeightTexture.Get()));
    }
}

void SWetWrinkleEditorPanel::RefreshTexturePreview()
{
    if (!TexturePreview.IsValid())
    {
        return;
    }

    const int32 PreviewMaterialSlotIndex = CurrentHit.bHit ? CurrentHit.MaterialSlotIndex : BrushSettings.MaterialSlotIndex;
    const int32 PreviewUVChannelIndex = CurrentHit.bHit ? CurrentHit.UVChannelIndex : BrushSettings.UVChannelIndex;
    UTexture* PreviewTexture = PreviewMaterialSlotIndex != INDEX_NONE
                                   ? ResolveSourceTextureForStamp(PreviewMaterialSlotIndex, PreviewUVChannelIndex)
                                   : nullptr;

    TexturePreview->SetPreviewContext(
        WetClothingAsset.Get(),
        PreviewTexture,
        PreviewMaterialSlotIndex,
        PreviewUVChannelIndex,
        BrushSettings,
        CurrentHit);
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

FText SWetWrinkleEditorPanel::GetStrokeSummaryText() const
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

    return FText::Format(LOCTEXT("StrokeSummary", "{0} stroke(s), {1} stamp(s)."), FText::AsNumber(StrokeCount), FText::AsNumber(StampCount));
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
    PushBrushSettingsToViewport();
    RefreshStrokeOverlay();
    RefreshTexturePreview();
}

TSharedRef<SWidget> SWetWrinkleEditorPanel::GenerateBrushPresetComboRow(TSharedPtr<FWetWrinkleBrushPresetOption> Item) const
{
    return SNew(STextBlock)
        .Text(Item.IsValid() ? Item->DisplayName : LOCTEXT("MissingWrinklePreset", "<missing>"));
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
                                             return Stroke != nullptr ? FText::FromString(Stroke->DisplayName) : LOCTEXT("MissingStrokeName", "<missing>");
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

    const FScopedTransaction Transaction(LOCTEXT("ClearWetWrinkleStrokesTransaction", "Clear Wet Wrinkle Strokes"));
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

    const FScopedTransaction Transaction(LOCTEXT("ToggleWetWrinkleStrokeTransaction", "Toggle Wet Wrinkle Stroke"));
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

    const FScopedTransaction Transaction(LOCTEXT("RenameWetWrinkleStrokeTransaction", "Rename Wet Wrinkle Stroke"));
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

    const FScopedTransaction Transaction(LOCTEXT("DeleteWetWrinkleStrokeTransaction", "Delete Wet Wrinkle Stroke"));
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
    const int32 NewUVChannelIndex = FMath::Max(0, NewValue);
    BrushSettings.UVChannelIndex = NewUVChannelIndex;
    if (UWetClothingAsset* Asset = WetClothingAsset.Get())
    {
        Asset->Modify();
        Asset->WrinkleData.WrinkleUVChannelIndex = NewUVChannelIndex;
        MarkAssetEdited();
    }
    CurrentHit = FWetWrinkleSurfaceHit();
    PushBrushSettingsToViewport();
}

void SWetWrinkleEditorPanel::HandleMaterialSlotChanged(int32 NewValue)
{
    BrushSettings.MaterialSlotIndex = NewValue < 0 ? INDEX_NONE : NewValue;
    CurrentHit = FWetWrinkleSurfaceHit();
    if (MaterialSlotComboBox.IsValid())
    {
        MaterialSlotComboBox->SetSelectedItem(FindMaterialSlotOption(BrushSettings.MaterialSlotIndex));
    }
    PushBrushSettingsToViewport();
    RefreshStrokeOverlay();
    RefreshTexturePreview();
}

void SWetWrinkleEditorPanel::HandleBrushRadiusChanged(float NewValue)
{
    BrushSettings.BrushRadiusUV = FMath::Clamp(NewValue, 0.001f, 0.5f);
    PushBrushSettingsToViewport();
}

void SWetWrinkleEditorPanel::HandleStrengthChanged(float NewValue)
{
    BrushSettings.Strength = FMath::Clamp(NewValue, 0.0f, 1.0f);
    PushBrushSettingsToViewport();
}

void SWetWrinkleEditorPanel::HandleFalloffChanged(float NewValue)
{
    BrushSettings.Falloff = FMath::Clamp(NewValue, 0.0f, 1.0f);
    PushBrushSettingsToViewport();
}

void SWetWrinkleEditorPanel::HandleRotationChanged(float NewValue)
{
    BrushSettings.RotationRadians = NewValue;
    PushBrushSettingsToViewport();
}

void SWetWrinkleEditorPanel::HandlePreviewToggleChanged(ECheckBoxState NewState)
{
    BrushSettings.bShowPreview = NewState == ECheckBoxState::Checked;
    PushBrushSettingsToViewport();
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
        RefreshTexturePreview();
        return;
    }

    PreviewViewport->ClearExternalBrushPreview();
}

void SWetWrinkleEditorPanel::HandleTextureUVHoverEnded()
{
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->ClearExternalBrushPreview();
    }
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
    return FString::Printf(TEXT("Stroke %03d"), StrokeNumber);
}

void SWetWrinkleEditorPanel::MarkAssetEdited()
{
    if (UWetClothingAsset* Asset = WetClothingAsset.Get())
    {
        Asset->MarkPackageDirty();
    }
}

#undef LOCTEXT_NAMESPACE
