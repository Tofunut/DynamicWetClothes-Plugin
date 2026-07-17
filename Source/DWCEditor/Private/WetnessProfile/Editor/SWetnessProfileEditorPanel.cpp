#include "SWetnessProfileEditorPanel.h"

#include "Core/DWCEditorUtils.h"
#include "IDetailsView.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "DataAssets/WetnessProfile.h"
#include "WetnessProfile/Viewport/WetnessProfileViewport.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "WetnessProfileEditorPanel"

void SWetnessProfileEditorPanel::Construct(const FArguments& InArgs)
{
    WetnessProfile = InArgs._WetnessProfile;
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
                                   .Text(LOCTEXT("EditorHeading", "Wetness Profile"))
                                   .Font(PanelHeadingFont)]

                    + SHorizontalBox::Slot()
                          .AutoWidth()
                              [SNew(SButton)
                                   .Text(LOCTEXT("SaveButton", "Save"))
                                   .OnClicked(this, &SWetnessProfileEditorPanel::HandleSaveClicked)]]

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
                          .Value(0.45f)
                              [SNew(SBorder)
                                   .Padding(10.0f)
                                       [SNew(SVerticalBox)

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                                                  [SNew(STextBlock)
                                                       .Text(LOCTEXT("ParametersHeading", "Parameters"))
                                                       .Font(SectionHeadingFont)]

                                        + SVerticalBox::Slot()
                                              .FillHeight(1.0f)
                                                  [DetailsView.IsValid()
                                                       ? StaticCastSharedRef<SWidget>(DetailsView.ToSharedRef())
                                                       : StaticCastSharedRef<SWidget>(
                                                             SNew(STextBlock)
                                                                 .Text(LOCTEXT("MissingDetails", "Details view is unavailable.")))]]]

                    + SSplitter::Slot()
                          .Value(0.55f)
                              [SNew(SBorder)
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
                                                                  .Text(LOCTEXT("PreviewHeading", "Preview"))
                                                                  .Font(SectionHeadingFont)]

                                                   + SHorizontalBox::Slot()
                                                         .AutoWidth()
                                                         .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                                                         .VAlign(VAlign_Center)
                                                             [SNew(STextBlock)
                                                                  .Text(LOCTEXT("SimulationModeLabel", "Simulation"))]

                                                   + SHorizontalBox::Slot()
                                                         .AutoWidth()
                                                         .VAlign(VAlign_Center)
                                                             [BuildSimulationModeButton(
                                                                 EDWCSimulationMode::VertexCPU,
                                                                 LOCTEXT("CPUSimulationMode", "CPU"))]

                                                   + SHorizontalBox::Slot()
                                                         .AutoWidth()
                                                         .Padding(4.0f, 0.0f, 0.0f, 0.0f)
                                                         .VAlign(VAlign_Center)
                                                             [BuildSimulationModeButton(
                                                                 EDWCSimulationMode::WetnessMapGPU,
                                                                 LOCTEXT("GPUSimulationMode", "GPU"))]]

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                                                  [SNew(SHorizontalBox)

                                                   + SHorizontalBox::Slot()
                                                         .AutoWidth()
                                                         .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                                                         .VAlign(VAlign_Center)
                                                             [SNew(STextBlock)
                                                                  .Text(LOCTEXT("RainRadiusLabel", "Rain Radius"))]

                                                   + SHorizontalBox::Slot()
                                                         .AutoWidth()
                                                         .Padding(0.0f, 0.0f, 14.0f, 0.0f)
                                                         .VAlign(VAlign_Center)
                                                             [SNew(SBox)
                                                                  .WidthOverride(82.0f)
                                                                      [SNew(SSpinBox<float>)
                                                                           .MinValue(35.0f)
                                                                           .MaxValue(180.0f)
                                                                           .MinSliderValue(35.0f)
                                                                           .MaxSliderValue(180.0f)
                                                                           .Delta(1.0f)
                                                                           .Value(this, &SWetnessProfileEditorPanel::GetPreviewRainRadius)
                                                                           .OnValueChanged(this, &SWetnessProfileEditorPanel::HandlePreviewRainRadiusChanged)]]

                                                   + SHorizontalBox::Slot()
                                                         .AutoWidth()
                                                         .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                                                         .VAlign(VAlign_Center)
                                                             [SNew(STextBlock)
                                                                  .Text(LOCTEXT("RainAmountLabel", "Rain Amount"))]

                                                   + SHorizontalBox::Slot()
                                                         .AutoWidth()
                                                         .Padding(0.0f, 0.0f, 14.0f, 0.0f)
                                                         .VAlign(VAlign_Center)
                                                             [SNew(SBox)
                                                                  .WidthOverride(82.0f)
                                                                      [SNew(SSpinBox<float>)
                                                                           .MinValue(0.0f)
                                                                           .MaxValue(300.0f)
                                                                           .MinSliderValue(0.0f)
                                                                           .MaxSliderValue(300.0f)
                                                                           .Delta(5.0f)
                                                                           .Value(this, &SWetnessProfileEditorPanel::GetPreviewRainAmountPercent)
                                                                           .OnValueChanged(this, &SWetnessProfileEditorPanel::HandlePreviewRainAmountPercentChanged)]]

                                                   + SHorizontalBox::Slot()
                                                         .AutoWidth()
                                                         .VAlign(VAlign_Center)
                                                             [SNew(SCheckBox)
                                                                  .Style(FAppStyle::Get(), "ToggleButtonCheckbox")
                                                                  .Padding(FMargin(10.0f, 4.0f))
                                                                  .IsChecked(this, &SWetnessProfileEditorPanel::IsPreviewWetnessDebugColorChecked)
                                                                  .OnCheckStateChanged(this, &SWetnessProfileEditorPanel::HandlePreviewWetnessDebugColorChanged)
                                                                      [SNew(STextBlock)
                                                                           .Text(LOCTEXT("WetnessDebugColorMode", "Wetness Debug"))]]]

                                        + SVerticalBox::Slot()
                                              .FillHeight(1.0f)
                                                  [SAssignNew(PreviewViewport, SWetnessProfileViewport)
                                                       .WetnessProfile(WetnessProfile.Get())]]]]];

    RefreshFromProfile();
}

void SWetnessProfileEditorPanel::RefreshFromProfile()
{
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->RefreshPreviewScene();
    }
}

FReply SWetnessProfileEditorPanel::HandleSaveClicked()
{
    DWCEditorUtils::SaveAsset(WetnessProfile.Get());
    return FReply::Handled();
}

TSharedRef<SWidget> SWetnessProfileEditorPanel::BuildSimulationModeButton(EDWCSimulationMode Mode, const FText& Label)
{
    return SNew(SCheckBox)
        .Style(FAppStyle::Get(), "ToggleButtonCheckbox")
        .Padding(FMargin(10.0f, 4.0f))
        .IsChecked(this, &SWetnessProfileEditorPanel::IsSimulationModeChecked, Mode)
        .OnCheckStateChanged(this, &SWetnessProfileEditorPanel::HandleSimulationModeChanged, Mode)
            [SNew(STextBlock)
                 .Text(Label)];
}

ECheckBoxState SWetnessProfileEditorPanel::IsSimulationModeChecked(EDWCSimulationMode Mode) const
{
    return PreviewViewport.IsValid() && PreviewViewport->GetPreviewSimulationMode() == Mode
               ? ECheckBoxState::Checked
               : ECheckBoxState::Unchecked;
}

void SWetnessProfileEditorPanel::HandleSimulationModeChanged(ECheckBoxState NewState, EDWCSimulationMode Mode)
{
    if (NewState == ECheckBoxState::Checked && PreviewViewport.IsValid())
    {
        PreviewViewport->SetPreviewSimulationMode(Mode);
    }
}

float SWetnessProfileEditorPanel::GetPreviewRainRadius() const
{
    return PreviewViewport.IsValid() ? PreviewViewport->GetPreviewRainRadius() : 92.0f;
}

void SWetnessProfileEditorPanel::HandlePreviewRainRadiusChanged(float InRadius)
{
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->SetPreviewRainRadius(InRadius);
    }
}

float SWetnessProfileEditorPanel::GetPreviewRainAmountPercent() const
{
    return PreviewViewport.IsValid() ? PreviewViewport->GetPreviewRainAmountScale() * 100.0f : 100.0f;
}

void SWetnessProfileEditorPanel::HandlePreviewRainAmountPercentChanged(float InAmountPercent)
{
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->SetPreviewRainAmountScale(InAmountPercent * 0.01f);
    }
}

ECheckBoxState SWetnessProfileEditorPanel::IsPreviewWetnessDebugColorChecked() const
{
    return PreviewViewport.IsValid() && PreviewViewport->IsPreviewWetnessDebugColorEnabled()
               ? ECheckBoxState::Checked
               : ECheckBoxState::Unchecked;
}

void SWetnessProfileEditorPanel::HandlePreviewWetnessDebugColorChanged(ECheckBoxState NewState)
{
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->SetPreviewWetnessDebugColorEnabled(NewState == ECheckBoxState::Checked);
    }
}

#undef LOCTEXT_NAMESPACE
