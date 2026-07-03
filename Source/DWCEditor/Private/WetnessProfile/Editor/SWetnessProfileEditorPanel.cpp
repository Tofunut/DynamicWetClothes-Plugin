#include "SWetnessProfileEditorPanel.h"

#include "Core/DWCEditorUtils.h"
#include "IDetailsView.h"
#include "Styling/CoreStyle.h"
#include "DataAssets/WetnessProfile.h"
#include "WetnessProfile/Viewport/WetnessProfileViewport.h"
#include "Widgets/Input/SButton.h"
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

#undef LOCTEXT_NAMESPACE
