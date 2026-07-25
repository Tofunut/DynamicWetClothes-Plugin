#include "SWetnessProfileEditorPanel.h"

#include "Core/DWCEditorUtils.h"
#include "DataAssets/WetnessProfile.h"
#include "IDetailsView.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "WetnessProfile/Editor/WetnessProfileEditorPolicy.h"
#include "WetnessProfile/Viewport/SWetnessProfileViewport.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBorder.h"
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
                                                  [SNew(STextBlock)
                                                       .Text(LOCTEXT("PreviewHeading", "Preview"))
                                                       .Font(SectionHeadingFont)]

                                        + SVerticalBox::Slot()
                                              .FillHeight(1.0f)
                                                  [SAssignNew(PreviewViewport, SWetnessProfileViewport)
                                                       .WetnessProfile(WetnessProfile.Get())]

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 8.0f, 0.0f, 0.0f)
                                                  [BuildPreviewSettingsSection()]]]]];

    RefreshFromProfile();
}

void SWetnessProfileEditorPanel::RefreshFromProfile()
{
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->RefreshFromProfile();
    }
}

FReply SWetnessProfileEditorPanel::HandleSaveClicked()
{
    TArray<FString> ClampedValues;
    if (FWetnessProfileEditorPolicy::SanitizeProfile(WetnessProfile.Get(), &ClampedValues))
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("Wetness Profile values were clamped before save:\n- %s"),
            *FString::Join(ClampedValues, TEXT("\n- ")));
        if (DetailsView.IsValid())
        {
            DetailsView->ForceRefresh();
        }
        RefreshFromProfile();
    }

    DWCEditorUtils::SaveAsset(WetnessProfile.Get());
    return FReply::Handled();
}

TSharedRef<SWidget> SWetnessProfileEditorPanel::BuildPreviewSettingsSection()
{
    return SNew(SBorder)
        .Padding(10.0f)
        .BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
            [SNew(SVerticalBox)

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                       [SNew(STextBlock)
                            .Text(LOCTEXT("PreviewWaterAmountHeading", "Preview Water Amount"))
                            .Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.BoldFont")))]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 3.0f)
                       [SNew(SHorizontalBox)

                        + SHorizontalBox::Slot()
                              .FillWidth(1.0f)
                                  [SNew(STextBlock)
                                       .Text(LOCTEXT("PreviewAbsorbedWetnessLabel", "Absorbed Wetness"))]

                        + SHorizontalBox::Slot()
                              .AutoWidth()
                                  [SNew(STextBlock)
                                       .Text(this, &SWetnessProfileEditorPanel::GetPreviewAbsorbedWaterPercentText)]]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                       [SNew(SSlider)
                            .MinValue(0.0f)
                            .MaxValue(100.0f)
                            .Value(this, &SWetnessProfileEditorPanel::GetPreviewAbsorbedWaterPercent)
                            .OnValueChanged(this, &SWetnessProfileEditorPanel::HandlePreviewAbsorbedWaterPercentChanged)]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 3.0f)
                       [SNew(SHorizontalBox)

                        + SHorizontalBox::Slot()
                              .FillWidth(1.0f)
                                  [SNew(STextBlock)
                                       .Text(LOCTEXT("PreviewSurfaceWaterLabel", "Surface Water"))]

                        + SHorizontalBox::Slot()
                              .AutoWidth()
                                  [SNew(STextBlock)
                                       .Text(this, &SWetnessProfileEditorPanel::GetPreviewSurfaceWaterPercentText)]]

             + SVerticalBox::Slot()
                   .AutoHeight()
                       [SNew(SSlider)
                            .MinValue(0.0f)
                            .MaxValue(100.0f)
                            .Value(this, &SWetnessProfileEditorPanel::GetPreviewSurfaceWaterPercent)
                            .OnValueChanged(this, &SWetnessProfileEditorPanel::HandlePreviewSurfaceWaterPercentChanged)]];
}

float SWetnessProfileEditorPanel::GetPreviewAbsorbedWaterPercent() const
{
    return PreviewViewport.IsValid()
               ? PreviewViewport->GetPreviewAbsorbedWater() * 100.0f
               : 50.0f;
}

void SWetnessProfileEditorPanel::HandlePreviewAbsorbedWaterPercentChanged(float InPercent)
{
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->SetPreviewAbsorbedWater(InPercent * 0.01f);
    }
}

FText SWetnessProfileEditorPanel::GetPreviewAbsorbedWaterPercentText() const
{
    return FText::Format(
        LOCTEXT("PreviewPercentFormat", "{0}%"),
        FText::AsNumber(FMath::RoundToInt(GetPreviewAbsorbedWaterPercent())));
}

float SWetnessProfileEditorPanel::GetPreviewSurfaceWaterPercent() const
{
    return PreviewViewport.IsValid()
               ? PreviewViewport->GetPreviewSurfaceWater() * 100.0f
               : 50.0f;
}

void SWetnessProfileEditorPanel::HandlePreviewSurfaceWaterPercentChanged(float InPercent)
{
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->SetPreviewSurfaceWater(InPercent * 0.01f);
    }
}

FText SWetnessProfileEditorPanel::GetPreviewSurfaceWaterPercentText() const
{
    return FText::Format(
        LOCTEXT("PreviewPercentFormat", "{0}%"),
        FText::AsNumber(FMath::RoundToInt(GetPreviewSurfaceWaterPercent())));
}

#undef LOCTEXT_NAMESPACE
