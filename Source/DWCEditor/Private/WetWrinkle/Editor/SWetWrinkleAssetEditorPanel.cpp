#include "SWetWrinkleAssetEditorPanel.h"

#include "Core/DWCEditorUtils.h"
#include "DataAssets/WetWrinkleAsset.h"
#include "IDetailsView.h"
#include "Styling/CoreStyle.h"
#include "WetWrinkle/Viewport/WetWrinkleAssetViewport.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "WetWrinkleAssetEditorPanel"

void SWetWrinkleAssetEditorPanel::Construct(const FArguments& InArgs)
{
    WetWrinkleAsset = InArgs._WetWrinkleAsset;
    DetailsView = InArgs._DetailsView;

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
                                              .FillHeight(1.0f)
                                                  [SAssignNew(PreviewViewport, SWetWrinkleAssetViewport)
                                                       .WetWrinkleAsset(WetWrinkleAsset.Get())
                                                       .OnSurfaceHitChanged(FOnWetWrinkleSurfaceHitChanged::CreateSP(this, &SWetWrinkleAssetEditorPanel::HandleSurfaceHitChanged))]]]

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
                                                  [SNew(SSpinBox<int32>)
                                                       .MinValue(-1)
                                                       .MaxValue(128)
                                                       .Value(BrushSettings.MaterialSlotIndex)
                                                       .OnValueChanged(this, &SWetWrinkleAssetEditorPanel::HandleMaterialSlotChanged)]

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
                                                  [SNew(STextBlock)
                                                       .AutoWrapText(true)
                                                       .Text(this, &SWetWrinkleAssetEditorPanel::GetHitInfoText)]

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 6.0f, 0.0f, 8.0f)
                                                  [SNew(STextBlock)
                                                       .Text(LOCTEXT("StrokeHeading", "Strokes"))
                                                       .Font(SectionHeadingFont)]

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                                  [SNew(STextBlock)
                                                       .AutoWrapText(true)
                                                       .Text(this, &SWetWrinkleAssetEditorPanel::GetStrokeSummaryText)]]]]];

    PushBrushSettingsToViewport();
    RefreshFromAsset();
}

void SWetWrinkleAssetEditorPanel::RefreshFromAsset()
{
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->RefreshPreviewMesh();
        PushBrushSettingsToViewport();
    }
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
}

void SWetWrinkleAssetEditorPanel::PushBrushSettingsToViewport()
{
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->SetBrushSettings(BrushSettings);
    }
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
    return FText::Format(LOCTEXT("StrokeSummary", "{0} stroke(s). Stroke editing starts in the next implementation step."), FText::AsNumber(StrokeCount));
}

void SWetWrinkleAssetEditorPanel::HandleUVChannelChanged(int32 NewValue)
{
    BrushSettings.UVChannelIndex = FMath::Max(0, NewValue);
    PushBrushSettingsToViewport();
}

void SWetWrinkleAssetEditorPanel::HandleMaterialSlotChanged(int32 NewValue)
{
    BrushSettings.MaterialSlotIndex = NewValue < 0 ? INDEX_NONE : NewValue;
    PushBrushSettingsToViewport();
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

#undef LOCTEXT_NAMESPACE
