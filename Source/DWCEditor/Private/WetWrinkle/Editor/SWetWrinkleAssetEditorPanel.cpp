#include "SWetWrinkleAssetEditorPanel.h"

#include "Core/DWCEditorUtils.h"
#include "DataAssets/WetClothingAsset.h"
#include "DataAssets/WetWrinkleAsset.h"
#include "Engine/SkeletalMesh.h"
#include "IDetailsView.h"
#include "Styling/CoreStyle.h"
#include "WetClothing/Texture/WetClothingMaterialTextureResolver.h"
#include "WetWrinkle/Viewport/WetWrinkleAssetViewport.h"
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

#define LOCTEXT_NAMESPACE "WetWrinkleAssetEditorPanel"

void SWetWrinkleAssetEditorPanel::Construct(const FArguments& InArgs)
{
    WetWrinkleAsset = InArgs._WetWrinkleAsset;
    DetailsView = InArgs._DetailsView;
    RefreshMaterialSlotOptions();

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
                                   .OnClicked(this, &SWetWrinkleAssetEditorPanel::HandleFocusClicked)]

                    + SHorizontalBox::Slot()
                          .AutoWidth()
                              [SNew(SButton)
                                   .Text(LOCTEXT("SaveButton", "Save"))
                                   .OnClicked(this, &SWetWrinkleAssetEditorPanel::HandleSaveClicked)]]

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
                                                  [SAssignNew(PreviewViewport, SWetWrinkleAssetViewport)
                                                       .WetWrinkleAsset(WetWrinkleAsset.Get())
                                                       .OnSurfaceHitChanged(FOnWetWrinkleSurfaceHitChanged::CreateSP(this, &SWetWrinkleAssetEditorPanel::HandleSurfaceHitChanged))
                                                       .OnPaintStrokeStarted(FOnWetWrinklePaintStrokeStarted::CreateSP(this, &SWetWrinkleAssetEditorPanel::HandlePaintStrokeStarted))
                                                       .OnPaintStampRequested(FOnWetWrinklePaintStampRequested::CreateSP(this, &SWetWrinkleAssetEditorPanel::HandlePaintStampRequested))
                                                       .OnPaintStrokeEnded(FOnWetWrinklePaintStrokeEnded::CreateSP(this, &SWetWrinkleAssetEditorPanel::HandlePaintStrokeEnded))]

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
                                                       .Text(LOCTEXT("UVChannelLabel", "UV Channel"))]

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                                                  [SNew(SSpinBox<int32>)
                                                       .MinValue(0)
                                                       .MaxValue(7)
                                                       .Value(BrushSettings.UVChannelIndex)
                                                       .OnValueChanged(this, &SWetWrinkleAssetEditorPanel::HandleUVChannelChanged)]

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
                                                       .OnGenerateWidget(this, &SWetWrinkleAssetEditorPanel::GenerateMaterialSlotComboRow)
                                                       .OnSelectionChanged(this, &SWetWrinkleAssetEditorPanel::HandleMaterialSlotComboChanged)
                                                           [SNew(STextBlock)
                                                                .Text(this, &SWetWrinkleAssetEditorPanel::GetSelectedMaterialSlotText)]]

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
                                                       .OnValueChanged(this, &SWetWrinkleAssetEditorPanel::HandleBrushRadiusChanged)]

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
                                                       .OnValueChanged(this, &SWetWrinkleAssetEditorPanel::HandleStrengthChanged)]

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
                                                       .OnValueChanged(this, &SWetWrinkleAssetEditorPanel::HandleFalloffChanged)]

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
                                                       .OnValueChanged(this, &SWetWrinkleAssetEditorPanel::HandleRotationChanged)]

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 0.0f, 0.0f, 12.0f)
                                                  [SNew(SCheckBox)
                                                       .IsChecked(this, &SWetWrinkleAssetEditorPanel::GetPreviewToggleState)
                                                       .OnCheckStateChanged(this, &SWetWrinkleAssetEditorPanel::HandlePreviewToggleChanged)
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
                                                                .Text(this, &SWetWrinkleAssetEditorPanel::GetHitInfoText)]]

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
                                                                  .IsEnabled(this, &SWetWrinkleAssetEditorPanel::IsClearStrokesEnabled)
                                                                  .OnClicked(this, &SWetWrinkleAssetEditorPanel::HandleClearStrokesClicked)]]

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                                                  [SNew(STextBlock)
                                                       .AutoWrapText(true)
                                                       .Text(this, &SWetWrinkleAssetEditorPanel::GetStrokeSummaryText)]

                                        + SVerticalBox::Slot()
                                              .FillHeight(1.0f)
                                                  [SAssignNew(StrokeListView, SListView<FStrokeListItemPtr>)
                                                       .ListItemsSource(&StrokeListItems)
                                                       .OnGenerateRow(this, &SWetWrinkleAssetEditorPanel::GenerateStrokeRow)
                                                       .OnSelectionChanged(this, &SWetWrinkleAssetEditorPanel::HandleStrokeSelectionChanged)]]]]];

    PushBrushSettingsToViewport();
    RefreshFromAsset();
}

void SWetWrinkleAssetEditorPanel::RefreshFromAsset()
{
    RefreshMaterialSlotOptions();
    RefreshStrokeList();

    if (PreviewViewport.IsValid())
    {
        PreviewViewport->RefreshPreviewMesh();
        PushBrushSettingsToViewport();
        RefreshStrokeOverlay();
    }

    RefreshTexturePreview();
}

FReply SWetWrinkleAssetEditorPanel::HandleSaveClicked()
{
    DWCEditorUtils::SaveAsset(WetWrinkleAsset.Get());
    return FReply::Handled();
}

FReply SWetWrinkleAssetEditorPanel::HandleFocusClicked()
{
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->FocusOnPreviewMesh();
    }
    return FReply::Handled();
}

void SWetWrinkleAssetEditorPanel::HandleSurfaceHitChanged(const FWetWrinkleSurfaceHit& SurfaceHit)
{
    CurrentHit = SurfaceHit;
    RefreshTexturePreview();
}

void SWetWrinkleAssetEditorPanel::HandlePaintStrokeStarted(const FWetWrinkleSurfaceHit& SurfaceHit)
{
    UWetWrinkleAsset* Asset = WetWrinkleAsset.Get();
    if (Asset == nullptr || !SurfaceHit.bHit)
    {
        return;
    }

    ActivePaintTransaction = MakeUnique<FScopedTransaction>(LOCTEXT("PaintWetWrinkleStrokeTransaction", "Paint Wet Wrinkle Stroke"));
    Asset->Modify();

    FWetWrinkleStroke NewStroke;
    NewStroke.StrokeGuid = FGuid::NewGuid();
    NewStroke.Name = MakeDefaultStrokeName();
    NewStroke.bEnabled = true;
    NewStroke.Stamps.Add(MakeStampFromHit(SurfaceHit));
    Asset->Strokes.Add(NewStroke);

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

void SWetWrinkleAssetEditorPanel::HandlePaintStampRequested(const FWetWrinkleSurfaceHit& SurfaceHit)
{
    UWetWrinkleAsset* Asset = WetWrinkleAsset.Get();
    FWetWrinkleStroke* ActiveStroke = FindMutableStroke(ActiveStrokeGuid);
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
    ActiveStroke->Stamps.Add(MakeStampFromHit(SurfaceHit));
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

void SWetWrinkleAssetEditorPanel::HandlePaintStrokeEnded()
{
    ActiveStrokeGuid.Invalidate();
    bHasLastStamp = false;
    bAllowImmediateNextStrokeStamp = false;
    ActivePaintTransaction.Reset();
}

void SWetWrinkleAssetEditorPanel::PushBrushSettingsToViewport()
{
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->SetBrushSettings(BrushSettings);
    }

    RefreshTexturePreview();
}

void SWetWrinkleAssetEditorPanel::RefreshStrokeList()
{
    StrokeListItems.Reset();

    if (const UWetWrinkleAsset* Asset = WetWrinkleAsset.Get())
    {
        for (const FWetWrinkleStroke& Stroke : Asset->Strokes)
        {
            FStrokeListItemPtr Item = MakeShared<FWetWrinkleStrokeListItem>();
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

void SWetWrinkleAssetEditorPanel::RefreshStrokeOverlay()
{
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->SetSelectedStrokeGuid(SelectedStrokeGuid);
        PreviewViewport->RefreshStoredStampOverlay();
    }
}

void SWetWrinkleAssetEditorPanel::RefreshMaterialSlotOptions()
{
    MaterialSlotOptions.Reset();
    MaterialSlotOptions.Add(MakeShared<int32>(INDEX_NONE));

    const UWetWrinkleAsset* Asset = WetWrinkleAsset.Get();
    const USkeletalMesh* TargetMesh = nullptr;
    if (Asset != nullptr)
    {
        TargetMesh = Asset->TargetMesh != nullptr ? Asset->TargetMesh.Get() : nullptr;
        if (TargetMesh == nullptr && Asset->SourceWetClothingAsset != nullptr)
        {
            TargetMesh = Asset->SourceWetClothingAsset->TargetMesh;
        }
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

void SWetWrinkleAssetEditorPanel::RefreshTexturePreview()
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
        WetWrinkleAsset.Get(),
        PreviewTexture,
        PreviewMaterialSlotIndex,
        PreviewUVChannelIndex,
        BrushSettings,
        CurrentHit);
}

FText SWetWrinkleAssetEditorPanel::GetHitInfoText() const
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

FText SWetWrinkleAssetEditorPanel::GetStrokeSummaryText() const
{
    const UWetWrinkleAsset* Asset = WetWrinkleAsset.Get();
    const int32 StrokeCount = Asset != nullptr ? Asset->Strokes.Num() : 0;
    int32 StampCount = 0;
    if (Asset != nullptr)
    {
        for (const FWetWrinkleStroke& Stroke : Asset->Strokes)
        {
            StampCount += Stroke.Stamps.Num();
        }
    }

    return FText::Format(LOCTEXT("StrokeSummary", "{0} stroke(s), {1} stamp(s)."), FText::AsNumber(StrokeCount), FText::AsNumber(StampCount));
}

TSharedRef<SWidget> SWetWrinkleAssetEditorPanel::GenerateMaterialSlotComboRow(TSharedPtr<int32> Item) const
{
    const int32 MaterialSlotIndex = Item.IsValid() ? *Item : INDEX_NONE;
    return SNew(STextBlock)
        .Text(GetMaterialSlotDisplayText(MaterialSlotIndex));
}

FText SWetWrinkleAssetEditorPanel::GetSelectedMaterialSlotText() const
{
    return GetMaterialSlotDisplayText(BrushSettings.MaterialSlotIndex);
}

void SWetWrinkleAssetEditorPanel::HandleMaterialSlotComboChanged(TSharedPtr<int32> Item, ESelectInfo::Type SelectInfo)
{
    BrushSettings.MaterialSlotIndex = Item.IsValid() && *Item >= 0 ? *Item : INDEX_NONE;
    CurrentHit = FWetWrinkleSurfaceHit();
    PushBrushSettingsToViewport();
    RefreshStrokeOverlay();
    RefreshTexturePreview();
}

TSharedRef<ITableRow> SWetWrinkleAssetEditorPanel::GenerateStrokeRow(FStrokeListItemPtr Item, const TSharedRef<STableViewBase>& OwnerTable)
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
                                                  const FWetWrinkleStroke* Stroke = Item.IsValid() ? FindStroke(Item->StrokeGuid) : nullptr;
                                                  return Stroke != nullptr && Stroke->bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
                                              })
                            .OnCheckStateChanged(this, &SWetWrinkleAssetEditorPanel::HandleStrokeEnabledChanged, Item)]

             + SHorizontalBox::Slot()
                   .FillWidth(1.0f)
                   .VAlign(VAlign_Center)
                       [SNew(SInlineEditableTextBlock)
                            .Text_Lambda([this, Item]()
                                         {
                                             const FWetWrinkleStroke* Stroke = Item.IsValid() ? FindStroke(Item->StrokeGuid) : nullptr;
                                             return Stroke != nullptr ? FText::FromString(Stroke->Name) : LOCTEXT("MissingStrokeName", "<missing>");
                                         })
                            .OnTextCommitted(this, &SWetWrinkleAssetEditorPanel::HandleStrokeNameCommitted, Item)]

             + SHorizontalBox::Slot()
                   .AutoWidth()
                   .VAlign(VAlign_Center)
                   .Padding(6.0f, 0.0f, 6.0f, 0.0f)
                       [SNew(STextBlock)
                            .Text_Lambda([this, Item]()
                                         {
                                             const FWetWrinkleStroke* Stroke = Item.IsValid() ? FindStroke(Item->StrokeGuid) : nullptr;
                                             return FText::AsNumber(Stroke != nullptr ? Stroke->Stamps.Num() : 0);
                                         })]

             + SHorizontalBox::Slot()
                   .AutoWidth()
                   .VAlign(VAlign_Center)
                       [SNew(SButton)
                            .Text(LOCTEXT("DeleteStrokeButton", "Delete"))
                            .OnClicked(this, &SWetWrinkleAssetEditorPanel::HandleDeleteStrokeClicked, Item)]];
}

void SWetWrinkleAssetEditorPanel::HandleStrokeSelectionChanged(FStrokeListItemPtr Item, ESelectInfo::Type SelectInfo)
{
    SelectedStrokeGuid = Item.IsValid() ? Item->StrokeGuid : FGuid();
    RefreshStrokeOverlay();
}

FReply SWetWrinkleAssetEditorPanel::HandleClearStrokesClicked()
{
    UWetWrinkleAsset* Asset = WetWrinkleAsset.Get();
    if (Asset == nullptr || Asset->Strokes.Num() == 0)
    {
        return FReply::Handled();
    }

    const FScopedTransaction Transaction(LOCTEXT("ClearWetWrinkleStrokesTransaction", "Clear Wet Wrinkle Strokes"));
    Asset->Modify();
    Asset->Strokes.Reset();
    ActiveStrokeGuid.Invalidate();
    SelectedStrokeGuid.Invalidate();
    bHasLastStamp = false;
    MarkAssetEdited();
    RefreshStrokeList();
    RefreshStrokeOverlay();
    return FReply::Handled();
}

bool SWetWrinkleAssetEditorPanel::IsClearStrokesEnabled() const
{
    const UWetWrinkleAsset* Asset = WetWrinkleAsset.Get();
    return Asset != nullptr && Asset->Strokes.Num() > 0;
}

void SWetWrinkleAssetEditorPanel::HandleStrokeEnabledChanged(ECheckBoxState NewState, FStrokeListItemPtr Item)
{
    UWetWrinkleAsset* Asset = WetWrinkleAsset.Get();
    FWetWrinkleStroke* Stroke = Item.IsValid() ? FindMutableStroke(Item->StrokeGuid) : nullptr;
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

void SWetWrinkleAssetEditorPanel::HandleStrokeNameCommitted(const FText& InText, ETextCommit::Type CommitType, FStrokeListItemPtr Item)
{
    UWetWrinkleAsset* Asset = WetWrinkleAsset.Get();
    FWetWrinkleStroke* Stroke = Item.IsValid() ? FindMutableStroke(Item->StrokeGuid) : nullptr;
    if (Asset == nullptr || Stroke == nullptr)
    {
        return;
    }

    const FString NewName = InText.ToString().TrimStartAndEnd();
    if (NewName.IsEmpty() || Stroke->Name == NewName)
    {
        return;
    }

    const FScopedTransaction Transaction(LOCTEXT("RenameWetWrinkleStrokeTransaction", "Rename Wet Wrinkle Stroke"));
    Asset->Modify();
    Stroke->Name = NewName;
    MarkAssetEdited();
    RefreshStrokeList();
}

FReply SWetWrinkleAssetEditorPanel::HandleDeleteStrokeClicked(FStrokeListItemPtr Item)
{
    UWetWrinkleAsset* Asset = WetWrinkleAsset.Get();
    if (Asset == nullptr || !Item.IsValid())
    {
        return FReply::Handled();
    }

    const int32 StrokeIndex = Asset->Strokes.IndexOfByPredicate(
        [Item](const FWetWrinkleStroke& Stroke)
        {
            return Stroke.StrokeGuid == Item->StrokeGuid;
        });
    if (StrokeIndex == INDEX_NONE)
    {
        return FReply::Handled();
    }

    const FScopedTransaction Transaction(LOCTEXT("DeleteWetWrinkleStrokeTransaction", "Delete Wet Wrinkle Stroke"));
    Asset->Modify();
    const FGuid DeletedGuid = Asset->Strokes[StrokeIndex].StrokeGuid;
    Asset->Strokes.RemoveAt(StrokeIndex);
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

void SWetWrinkleAssetEditorPanel::HandleUVChannelChanged(int32 NewValue)
{
    BrushSettings.UVChannelIndex = FMath::Max(0, NewValue);
    CurrentHit = FWetWrinkleSurfaceHit();
    PushBrushSettingsToViewport();
}

void SWetWrinkleAssetEditorPanel::HandleMaterialSlotChanged(int32 NewValue)
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

void SWetWrinkleAssetEditorPanel::HandleBrushRadiusChanged(float NewValue)
{
    BrushSettings.BrushRadiusUV = FMath::Clamp(NewValue, 0.001f, 0.5f);
    PushBrushSettingsToViewport();
}

void SWetWrinkleAssetEditorPanel::HandleStrengthChanged(float NewValue)
{
    BrushSettings.Strength = FMath::Clamp(NewValue, 0.0f, 1.0f);
    PushBrushSettingsToViewport();
}

void SWetWrinkleAssetEditorPanel::HandleFalloffChanged(float NewValue)
{
    BrushSettings.Falloff = FMath::Clamp(NewValue, 0.0f, 1.0f);
    PushBrushSettingsToViewport();
}

void SWetWrinkleAssetEditorPanel::HandleRotationChanged(float NewValue)
{
    BrushSettings.RotationRadians = NewValue;
    PushBrushSettingsToViewport();
}

void SWetWrinkleAssetEditorPanel::HandlePreviewToggleChanged(ECheckBoxState NewState)
{
    BrushSettings.bShowPreview = NewState == ECheckBoxState::Checked;
    PushBrushSettingsToViewport();
}

ECheckBoxState SWetWrinkleAssetEditorPanel::GetPreviewToggleState() const
{
    return BrushSettings.bShowPreview ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

FWetWrinkleStroke* SWetWrinkleAssetEditorPanel::FindMutableStroke(const FGuid& StrokeGuid) const
{
    UWetWrinkleAsset* Asset = WetWrinkleAsset.Get();
    if (Asset == nullptr || !StrokeGuid.IsValid())
    {
        return nullptr;
    }

    return Asset->Strokes.FindByPredicate(
        [StrokeGuid](const FWetWrinkleStroke& Stroke)
        {
            return Stroke.StrokeGuid == StrokeGuid;
        });
}

const FWetWrinkleStroke* SWetWrinkleAssetEditorPanel::FindStroke(const FGuid& StrokeGuid) const
{
    const UWetWrinkleAsset* Asset = WetWrinkleAsset.Get();
    if (Asset == nullptr || !StrokeGuid.IsValid())
    {
        return nullptr;
    }

    return Asset->Strokes.FindByPredicate(
        [StrokeGuid](const FWetWrinkleStroke& Stroke)
        {
            return Stroke.StrokeGuid == StrokeGuid;
        });
}

FWetWrinkleStamp SWetWrinkleAssetEditorPanel::MakeStampFromHit(const FWetWrinkleSurfaceHit& SurfaceHit) const
{
    FWetWrinkleStamp Stamp;
    Stamp.MaterialSlotIndex = SurfaceHit.MaterialSlotIndex;
    Stamp.UVChannelIndex = SurfaceHit.UVChannelIndex;
    Stamp.SourceTexture = ResolveSourceTextureForStamp(SurfaceHit.MaterialSlotIndex, SurfaceHit.UVChannelIndex);
    Stamp.PositionUV = SurfaceHit.UV;
    Stamp.BrushRadiusUV = BrushSettings.BrushRadiusUV;
    Stamp.RotationRadians = BrushSettings.RotationRadians;
    Stamp.Scale = FVector2D(1.0f, 1.0f);
    Stamp.Strength = BrushSettings.Strength;
    Stamp.Falloff = BrushSettings.Falloff;
    Stamp.BrushNormalTexture = nullptr;
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

UTexture* SWetWrinkleAssetEditorPanel::ResolveSourceTextureForStamp(int32 MaterialSlotIndex, int32 UVChannelIndex) const
{
    const UWetWrinkleAsset* Asset = WetWrinkleAsset.Get();
    const UWetClothingAsset* SourceWetClothingAsset = Asset != nullptr ? Asset->SourceWetClothingAsset.Get() : nullptr;
    if (Asset == nullptr)
    {
        return nullptr;
    }

    if (SourceWetClothingAsset != nullptr)
    {
        for (const FWetClothingAssetTextureSelection& TextureSelection : SourceWetClothingAsset->TextureSelections)
        {
            if (TextureSelection.MaterialSlotIndex == MaterialSlotIndex &&
                TextureSelection.UVChannelIndex == UVChannelIndex &&
                TextureSelection.Texture != nullptr)
            {
                return TextureSelection.Texture;
            }
        }
    }

    const USkeletalMesh* TargetMesh = SourceWetClothingAsset != nullptr && SourceWetClothingAsset->TargetMesh != nullptr
                                          ? SourceWetClothingAsset->TargetMesh.Get()
                                          : nullptr;
    if (TargetMesh == nullptr && Asset->TargetMesh != nullptr)
    {
        TargetMesh = Asset->TargetMesh.Get();
    }

    if (TargetMesh != nullptr && TargetMesh->GetMaterials().IsValidIndex(MaterialSlotIndex))
    {
        return FWetClothingMaterialTextureResolver::ResolveBestMaterialTexture(TargetMesh->GetMaterials()[MaterialSlotIndex].MaterialInterface);
    }

    return nullptr;
}

FText SWetWrinkleAssetEditorPanel::GetMaterialSlotDisplayText(int32 MaterialSlotIndex) const
{
    if (MaterialSlotIndex == INDEX_NONE)
    {
        return LOCTEXT("AllMaterialSlots", "All Slots");
    }

    const UWetWrinkleAsset* Asset = WetWrinkleAsset.Get();
    const USkeletalMesh* TargetMesh = nullptr;
    if (Asset != nullptr)
    {
        TargetMesh = Asset->TargetMesh != nullptr ? Asset->TargetMesh.Get() : nullptr;
        if (TargetMesh == nullptr && Asset->SourceWetClothingAsset != nullptr)
        {
            TargetMesh = Asset->SourceWetClothingAsset->TargetMesh;
        }
    }

    if (TargetMesh != nullptr && TargetMesh->GetMaterials().IsValidIndex(MaterialSlotIndex))
    {
        const FName SlotName = TargetMesh->GetMaterials()[MaterialSlotIndex].MaterialSlotName;
        return FText::FromString(FString::Printf(TEXT("%d - %s"), MaterialSlotIndex, *SlotName.ToString()));
    }

    return FText::FromString(FString::Printf(TEXT("%d"), MaterialSlotIndex));
}

TSharedPtr<int32> SWetWrinkleAssetEditorPanel::FindMaterialSlotOption(int32 MaterialSlotIndex) const
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

void SWetWrinkleAssetEditorPanel::HandleTextureUVHovered(const FVector2D& UV)
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

void SWetWrinkleAssetEditorPanel::HandleTextureUVHoverEnded()
{
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->ClearExternalBrushPreview();
    }
}

void SWetWrinkleAssetEditorPanel::HandleTexturePaintStrokeStarted(const FVector2D& UV)
{
    FWetWrinkleSurfaceHit SurfaceHit;
    if (TryBuildTextureSurfaceHit(UV, SurfaceHit))
    {
        HandleSurfaceHitChanged(SurfaceHit);
        HandlePaintStrokeStarted(SurfaceHit);
    }
}

void SWetWrinkleAssetEditorPanel::HandleTexturePaintStampRequested(const FVector2D& UV)
{
    FWetWrinkleSurfaceHit SurfaceHit;
    if (TryBuildTextureSurfaceHit(UV, SurfaceHit))
    {
        HandleSurfaceHitChanged(SurfaceHit);
        HandlePaintStampRequested(SurfaceHit);
    }
}

void SWetWrinkleAssetEditorPanel::HandleTexturePaintStrokeEnded()
{
    HandlePaintStrokeEnded();
}

bool SWetWrinkleAssetEditorPanel::TryBuildTextureSurfaceHit(const FVector2D& UV, FWetWrinkleSurfaceHit& OutSurfaceHit) const
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

bool SWetWrinkleAssetEditorPanel::ShouldAddStampForHit(const FWetWrinkleSurfaceHit& SurfaceHit) const
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

FString SWetWrinkleAssetEditorPanel::MakeDefaultStrokeName() const
{
    const UWetWrinkleAsset* Asset = WetWrinkleAsset.Get();
    const int32 StrokeNumber = Asset != nullptr ? Asset->Strokes.Num() + 1 : 1;
    return FString::Printf(TEXT("Stroke %03d"), StrokeNumber);
}

void SWetWrinkleAssetEditorPanel::MarkAssetEdited()
{
    if (UWetWrinkleAsset* Asset = WetWrinkleAsset.Get())
    {
        Asset->MarkPackageDirty();
    }
}

#undef LOCTEXT_NAMESPACE
