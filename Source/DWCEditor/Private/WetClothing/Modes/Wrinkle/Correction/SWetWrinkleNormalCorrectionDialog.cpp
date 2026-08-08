// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "SWetWrinkleNormalCorrectionDialog.h"

#include "Core/DWCEditorUtils.h"
#include "DataAssets/WetClothingAsset.h"
#include "DataAssets/WetWrinkleNormalTextureBuilder.h"
#include "Engine/Texture2D.h"
#include "Misc/MessageDialog.h"
#include "Styling/AppStyle.h"
#include "WetClothing/Modes/Wrinkle/Correction/WetWrinkleNormalCorrectionService.h"
#include "WetClothing/Modes/Wrinkle/Editor/WetWrinkleEditorSettings.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "WetWrinkleNormalCorrectionDialog"

void SWetWrinkleNormalCorrectionDialog::Construct(const FArguments& InArgs)
{
    ParentWindow = InArgs._ParentWindow;
    SourceTexture = InArgs._SourceTexture;
    WetClothingAsset = InArgs._WetClothingAsset;
    OnCorrectedTextureCreated = InArgs._OnCorrectedTextureCreated;

    const UWetWrinkleEditorSettings* UserSettings = GetDefault<UWetWrinkleEditorSettings>();
    bUseCorrection = UserSettings->bLastUseCorrection;
    bHideOriginal = UserSettings->bLastHideOriginal;
    CorrectionSettings = UserSettings->LastCorrectionSettings;
    if (const UWetClothingAsset* Asset = WetClothingAsset.Get())
    {
        CoverageSettings = Asset->Authored.WrinkleData.CoverageExtractionSettings;
    }

    UTexture2D* Source = SourceTexture.Get();
    if (Source != nullptr)
    {
        SourceNormalBrush.SetResourceObject(Source);
        SourceNormalBrush.SetImageSize(FVector2D(Source->GetSizeX(), Source->GetSizeY()));
    }

    ChildSlot
        [SNew(SVerticalBox)

         + SVerticalBox::Slot()
               .FillHeight(1.0f)
               .Padding(10.0f)
                   [SNew(SSplitter)

                    + SSplitter::Slot()
                          .Value(0.30f)
                              [SNew(SBorder)
                                   .Padding(10.0f)
                                       [SNew(SScrollBox)

                                        + SScrollBox::Slot()
                                              [SNew(SVerticalBox)

                                               + SVerticalBox::Slot()
                                                     .AutoHeight()
                                                     .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                                                         [SNew(STextBlock)
                                                              .Text(LOCTEXT("CorrectionSettings", "Normal Correction"))
                                                              .Font(FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle"))]

                                               + SVerticalBox::Slot()
                                                     .AutoHeight()
                                                     .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                                                         [SNew(SCheckBox)
                                                              .IsChecked_Lambda([this]()
                                                                                { return bUseCorrection ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                                                              .OnCheckStateChanged_Lambda([this](ECheckBoxState State)
                                                                                          {
                                                          bUseCorrection = State == ECheckBoxState::Checked;
                                                          RebuildPreview(); })
                                                                  [SNew(STextBlock).Text(LOCTEXT("UseCorrection", "Use Correction"))]]

                                               + SVerticalBox::Slot()
                                                     .AutoHeight()
                                                     .Padding(0.0f, 0.0f, 0.0f, 4.0f)
                                                         [SNew(STextBlock).Text(LOCTEXT("BorderPercent", "Border Percent"))]

                                               + SVerticalBox::Slot()
                                                     .AutoHeight()
                                                     .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                                                         [SNew(SSpinBox<float>)
                                                              .MinValue(0.0f)
                                                              .MaxValue(50.0f)
                                                              .Value_Lambda([this]()
                                                                            { return CorrectionSettings.BorderPercent; })
                                                              .OnValueChanged_Lambda([this](float Value)
                                                                                     { CorrectionSettings.BorderPercent = Value; })
                                                              .OnValueCommitted_Lambda([this](float, ETextCommit::Type)
                                                                                       { RebuildPreview(); })]

                                               + SVerticalBox::Slot()
                                                     .AutoHeight()
                                                     .Padding(0.0f, 0.0f, 0.0f, 4.0f)
                                                         [SNew(STextBlock).Text(LOCTEXT("FlatThreshold", "Flat Threshold"))]

                                               + SVerticalBox::Slot()
                                                     .AutoHeight()
                                                     .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                                                         [SNew(SSpinBox<float>)
                                                              .MinValue(0.0f)
                                                              .MaxValue(1.0f)
                                                              .MinSliderValue(0.0f)
                                                              .MaxSliderValue(0.2f)
                                                              .Value_Lambda([this]()
                                                                            { return CorrectionSettings.FlatThreshold; })
                                                              .OnValueChanged_Lambda([this](float Value)
                                                                                     { CorrectionSettings.FlatThreshold = Value; })
                                                              .OnValueCommitted_Lambda([this](float, ETextCommit::Type)
                                                                                       { RebuildPreview(); })]

                                               + SVerticalBox::Slot()
                                                     .AutoHeight()
                                                     .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                                                         [SNew(SCheckBox)
                                                              .IsChecked_Lambda([this]()
                                                                                { return CorrectionSettings.bFlipGreen ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                                                              .OnCheckStateChanged_Lambda([this](ECheckBoxState State)
                                                                                          {
                                                          CorrectionSettings.bFlipGreen = State == ECheckBoxState::Checked;
                                                          RebuildPreview(); })
                                                                  [SNew(STextBlock).Text(LOCTEXT("FlipGreen", "Flip Green"))]]

                                               + SVerticalBox::Slot()
                                                     .AutoHeight()
                                                     .Padding(0.0f, 0.0f, 0.0f, 4.0f)
                                                         [SNew(STextBlock).Text(LOCTEXT("DeviationAmplify", "Deviation Preview Amplify"))]

                                               + SVerticalBox::Slot()
                                                     .AutoHeight()
                                                     .Padding(0.0f, 0.0f, 0.0f, 12.0f)
                                                         [SNew(SSpinBox<float>)
                                                              .MinValue(0.0f)
                                                              .MaxValue(64.0f)
                                                              .Value_Lambda([this]()
                                                                            { return CorrectionSettings.DeviationPreviewAmplify; })
                                                              .OnValueChanged_Lambda([this](float Value)
                                                                                     { CorrectionSettings.DeviationPreviewAmplify = Value; })
                                                              .OnValueCommitted_Lambda([this](float, ETextCommit::Type)
                                                                                       { RebuildPreview(); })]

                                               + SVerticalBox::Slot()
                                                     .AutoHeight()
                                                     .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                                                         [SNew(STextBlock)
                                                              .Text(LOCTEXT("CoveragePreviewSettings", "Convex Separation Preview"))
                                                              .Font(FAppStyle::GetFontStyle("DetailsView.CategoryFontStyle"))]

                                               + SVerticalBox::Slot()
                                                     .AutoHeight()
                                                     .Padding(0.0f, 0.0f, 0.0f, 4.0f)
                                                         [SNew(STextBlock).Text(LOCTEXT("BlurRadius", "Input Blur Radius"))]

                                               + SVerticalBox::Slot()
                                                     .AutoHeight()
                                                     .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                                                         [SNew(SSpinBox<int32>)
                                                              .MinValue(0)
                                                              .MaxValue(8)
                                                              .Value_Lambda([this]()
                                                                            { return CoverageSettings.InputBlurRadiusPixels; })
                                                              .OnValueChanged_Lambda([this](int32 Value)
                                                                                     { CoverageSettings.InputBlurRadiusPixels = Value; })
                                                              .OnValueCommitted_Lambda([this](int32, ETextCommit::Type)
                                                                                       { RebuildPreview(); })]

                                               + SVerticalBox::Slot()
                                                     .AutoHeight()
                                                     .Padding(0.0f, 0.0f, 0.0f, 4.0f)
                                                         [SNew(STextBlock).Text(LOCTEXT("ConvexityThreshold", "Convexity Threshold"))]

                                               + SVerticalBox::Slot()
                                                     .AutoHeight()
                                                     .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                                                         [SNew(SSpinBox<float>)
                                                              .MinValue(0.0f)
                                                              .MaxValue(1.0f)
                                                              .Value_Lambda([this]()
                                                                            { return CoverageSettings.ConvexityThreshold; })
                                                              .OnValueChanged_Lambda([this](float Value)
                                                                                     { CoverageSettings.ConvexityThreshold = Value; })
                                                              .OnValueCommitted_Lambda([this](float, ETextCommit::Type)
                                                                                       { RebuildPreview(); })]

                                               + SVerticalBox::Slot()
                                                     .AutoHeight()
                                                     .Padding(0.0f, 0.0f, 0.0f, 4.0f)
                                                         [SNew(STextBlock).Text(LOCTEXT("MinimumComponent", "Minimum Component Pixels"))]

                                               + SVerticalBox::Slot()
                                                     .AutoHeight()
                                                     .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                                                         [SNew(SSpinBox<int32>)
                                                              .MinValue(1)
                                                              .MaxValue(1024)
                                                              .Value_Lambda([this]()
                                                                            { return CoverageSettings.MinimumComponentPixels; })
                                                              .OnValueChanged_Lambda([this](int32 Value)
                                                                                     { CoverageSettings.MinimumComponentPixels = Value; })
                                                              .OnValueCommitted_Lambda([this](int32, ETextCommit::Type)
                                                                                       { RebuildPreview(); })]

                                               + SVerticalBox::Slot()
                                                     .AutoHeight()
                                                     .Padding(0.0f, 0.0f, 0.0f, 12.0f)
                                                         [SNew(SCheckBox)
                                                              .IsChecked_Lambda([this]()
                                                                                { return CoverageSettings.bInvertConvexity ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                                                              .OnCheckStateChanged_Lambda([this](ECheckBoxState State)
                                                                                          {
                                                          CoverageSettings.bInvertConvexity = State == ECheckBoxState::Checked;
                                                          RebuildPreview(); })
                                                                  [SNew(STextBlock).Text(LOCTEXT("InvertConvexity", "Invert Convexity"))]]

                                               + SVerticalBox::Slot()
                                                     .AutoHeight()
                                                         [SNew(SCheckBox)
                                                              .IsChecked_Lambda([this]()
                                                                                { return bHideOriginal ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                                                              .OnCheckStateChanged_Lambda([this](ECheckBoxState State)
                                                                                          { bHideOriginal = State == ECheckBoxState::Checked; })
                                                                  [SNew(STextBlock).Text(LOCTEXT("HideOriginal", "Hide Original After Creation"))]]]]]

                    + SSplitter::Slot()
                          .Value(0.70f)
                              [SNew(SBorder)
                                   .Padding(8.0f)
                                       [SNew(SUniformGridPanel)
                                            .SlotPadding(FMargin(5.0f))

                                        + SUniformGridPanel::Slot(0, 0)
                                              [BuildPreviewCell(LOCTEXT("SourceNormal", "Source Normal"), &SourceNormalBrush)]

                                        + SUniformGridPanel::Slot(1, 0)
                                              [BuildPreviewCell(LOCTEXT("SourceDeviation", "Source Deviation Preview"), &SourceDeviationBrush)]

                                        + SUniformGridPanel::Slot(0, 1)
                                              [BuildPreviewCell(LOCTEXT("CorrectedNormal", "Corrected Normal"), &CorrectedNormalBrush)]

                                        + SUniformGridPanel::Slot(1, 1)
                                              [BuildPreviewCell(LOCTEXT("CorrectedDeviation", "Corrected Deviation Preview"), &CorrectedDeviationBrush)]

                                        + SUniformGridPanel::Slot(0, 2)
                                              [BuildPreviewCell(LOCTEXT("ConvexSeparation", "Convex Separation Preview"), &ConvexSeparationBrush)]]]]

         + SVerticalBox::Slot()
               .AutoHeight()
               .Padding(10.0f, 0.0f, 10.0f, 6.0f)
                   [SNew(STextBlock)
                        .Text_Lambda([this]()
                                     { return StatusText; })
                        .ColorAndOpacity_Lambda([this]()
                                                { return StatusColor; })]

         + SVerticalBox::Slot()
               .AutoHeight()
               .Padding(10.0f, 0.0f, 10.0f, 10.0f)
                   [SNew(SHorizontalBox)

                    + SHorizontalBox::Slot()
                          .AutoWidth()
                              [SNew(SButton)
                                   .Text(LOCTEXT("RefreshPreview", "Refresh Preview"))
                                   .OnClicked(this, &SWetWrinkleNormalCorrectionDialog::HandleRefreshPreviewClicked)]

                    + SHorizontalBox::Slot()
                          .FillWidth(1.0f)

                    + SHorizontalBox::Slot()
                          .AutoWidth()
                          .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                              [SNew(SButton)
                                   .IsEnabled_Lambda([this]()
                                                     { return bPreviewValid; })
                                   .Text(LOCTEXT("CreateCorrectedNormal", "Create Corrected Normal"))
                                   .OnClicked(this, &SWetWrinkleNormalCorrectionDialog::HandleCreateClicked)]

                    + SHorizontalBox::Slot()
                          .AutoWidth()
                              [SNew(SButton)
                                   .Text(LOCTEXT("Cancel", "Cancel"))
                                   .OnClicked(this, &SWetWrinkleNormalCorrectionDialog::HandleCancelClicked)]]];

    RebuildPreview();
}

void SWetWrinkleNormalCorrectionDialog::RebuildPreview()
{
    bPreviewValid = false;
    LastBuildOutput = FWetWrinkleNormalBuildOutput();
    SourceDeviationTexture.Reset();
    CorrectedNormalPreviewTexture.Reset();
    CorrectedDeviationTexture.Reset();
    ConvexSeparationTexture.Reset();
    SourceDeviationBrush.SetResourceObject(nullptr);
    CorrectedNormalBrush.SetResourceObject(nullptr);
    CorrectedDeviationBrush.SetResourceObject(nullptr);
    ConvexSeparationBrush.SetResourceObject(nullptr);

    UTexture2D* Source = SourceTexture.Get();
    if (Source == nullptr)
    {
        StatusText = LOCTEXT("SourceMissing", "The source normal texture is unavailable.");
        StatusColor = FSlateColor(FLinearColor(1.0f, 0.35f, 0.30f));
        return;
    }

    FString Error;
    if (!FWetWrinkleNormalTextureBuilder::BuildTextureBuffers(
            Source,
            bUseCorrection,
            CorrectionSettings,
            CoverageSettings,
            LastBuildOutput,
            Error,
            768))
    {
        StatusText = FText::FromString(Error);
        StatusColor = FSlateColor(FLinearColor(1.0f, 0.35f, 0.30f));
        return;
    }

    CreateTransientPreviewTexture(SourceDeviationTexture, LastBuildOutput.DeviationPreview, false);
    CreateTransientPreviewTexture(CorrectedNormalPreviewTexture, LastBuildOutput.CorrectedNormal, true);
    CreateTransientPreviewTexture(CorrectedDeviationTexture, LastBuildOutput.CorrectedDeviationPreview, false);
    CreateTransientPreviewTexture(ConvexSeparationTexture, LastBuildOutput.ConvexSeparationPreview, false);

    SourceDeviationBrush.SetResourceObject(SourceDeviationTexture.Get());
    CorrectedNormalBrush.SetResourceObject(CorrectedNormalPreviewTexture.Get());
    CorrectedDeviationBrush.SetResourceObject(CorrectedDeviationTexture.Get());
    ConvexSeparationBrush.SetResourceObject(ConvexSeparationTexture.Get());
    const FVector2D PreviewSize(LastBuildOutput.CorrectedNormal.Size.X, LastBuildOutput.CorrectedNormal.Size.Y);
    SourceDeviationBrush.SetImageSize(PreviewSize);
    CorrectedNormalBrush.SetImageSize(PreviewSize);
    CorrectedDeviationBrush.SetImageSize(PreviewSize);
    ConvexSeparationBrush.SetImageSize(PreviewSize);

    bPreviewValid = true;
    StatusText = FText::Format(
        LOCTEXT("PreviewReady", "Preview ready. Output: {0}"),
        FText::FromString(FWetWrinkleNormalCorrectionService::MakeCorrectedTextureObjectPath(*Source)));
    StatusColor = FSlateColor(FLinearColor(0.35f, 0.9f, 0.45f));
}

bool SWetWrinkleNormalCorrectionDialog::CreateTransientPreviewTexture(
    TStrongObjectPtr<UTexture2D>&        OutTexture,
    const FWetWrinkleTexturePixelBuffer& PixelBuffer,
    const bool                           bNormalMap) const
{
    OutTexture.Reset();
    if (!PixelBuffer.IsValid())
    {
        return false;
    }

    UTexture2D* Texture = UTexture2D::CreateTransient(PixelBuffer.Size.X, PixelBuffer.Size.Y, PF_B8G8R8A8);
    if (Texture == nullptr || Texture->GetPlatformData() == nullptr || !Texture->GetPlatformData()->Mips.IsValidIndex(0))
    {
        return false;
    }

    Texture->SRGB = false;
    Texture->CompressionSettings = bNormalMap ? TC_Normalmap : TC_Grayscale;
    Texture->MipGenSettings = TMGS_NoMipmaps;
    Texture->Filter = TF_Bilinear;
    Texture->AddressX = TA_Clamp;
    Texture->AddressY = TA_Clamp;
    Texture->NeverStream = true;

    FTexture2DMipMap& Mip = Texture->GetPlatformData()->Mips[0];
    void*             MipData = Mip.BulkData.Lock(LOCK_READ_WRITE);
    FMemory::Memcpy(MipData, PixelBuffer.Pixels.GetData(), PixelBuffer.Pixels.Num() * sizeof(FColor));
    Mip.BulkData.Unlock();
    Texture->UpdateResource();
    OutTexture.Reset(Texture);
    return true;
}

TSharedRef<SWidget> SWetWrinkleNormalCorrectionDialog::BuildPreviewCell(const FText& Label, const FSlateBrush* Brush) const
{
    return SNew(SVerticalBox) + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)[SNew(STextBlock).Text(Label)] + SVerticalBox::Slot().FillHeight(1.0f)[SNew(SBorder).BorderImage(FAppStyle::GetBrush("WhiteBrush")).BorderBackgroundColor(FLinearColor::Black).Padding(1.0f)[SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::Both)[SNew(SImage).Image(Brush)]]];
}

FReply SWetWrinkleNormalCorrectionDialog::HandleCreateClicked()
{
    UTexture2D* Source = SourceTexture.Get();
    if (Source == nullptr || !bPreviewValid)
    {
        return FReply::Handled();
    }

    if (FWetWrinkleNormalCorrectionService::FindExistingCorrectedTexture(*Source) != nullptr)
    {
        const EAppReturnType::Type Choice = FMessageDialog::Open(
            EAppMsgType::YesNo,
            LOCTEXT("ConfirmOverwrite", "A corrected normal with the target name already exists. Update it?"));
        if (Choice != EAppReturnType::Yes)
        {
            return FReply::Handled();
        }
    }

    UTexture2D*                  CorrectedTexture = nullptr;
    FString                      Error;
    FWetWrinkleNormalBuildOutput FullResolutionOutput;
    if (!FWetWrinkleNormalTextureBuilder::BuildTextureBuffers(
            Source,
            bUseCorrection,
            CorrectionSettings,
            CoverageSettings,
            FullResolutionOutput,
            Error))
    {
        StatusText = FText::FromString(Error);
        StatusColor = FSlateColor(FLinearColor(1.0f, 0.35f, 0.30f));
        return FReply::Handled();
    }

    if (!FWetWrinkleNormalCorrectionService::CreateOrUpdateCorrectedTexture(
            *Source,
            FullResolutionOutput.CorrectedNormal,
            CorrectedTexture,
            Error))
    {
        StatusText = FText::FromString(Error);
        StatusColor = FSlateColor(FLinearColor(1.0f, 0.35f, 0.30f));
        return FReply::Handled();
    }

    if (UWetClothingAsset* Asset = WetClothingAsset.Get())
    {
        Asset->Modify();
        Asset->Authored.WrinkleData.CoverageExtractionSettings = CoverageSettings;
        Asset->MarkPackageDirty();
    }

    SaveLastSettings();
    DWCEditorUtils::SaveAsset(CorrectedTexture);
    OnCorrectedTextureCreated.ExecuteIfBound(CorrectedTexture, bHideOriginal);
    CloseWindow();
    return FReply::Handled();
}

FReply SWetWrinkleNormalCorrectionDialog::HandleRefreshPreviewClicked()
{
    RebuildPreview();
    return FReply::Handled();
}

FReply SWetWrinkleNormalCorrectionDialog::HandleCancelClicked()
{
    SaveLastSettings();
    CloseWindow();
    return FReply::Handled();
}

void SWetWrinkleNormalCorrectionDialog::SaveLastSettings() const
{
    UWetWrinkleEditorSettings* UserSettings = GetMutableDefault<UWetWrinkleEditorSettings>();
    UserSettings->bLastUseCorrection = bUseCorrection;
    UserSettings->bLastHideOriginal = bHideOriginal;
    UserSettings->LastCorrectionSettings = CorrectionSettings;
    UserSettings->SaveConfig();
}

void SWetWrinkleNormalCorrectionDialog::CloseWindow() const
{
    if (const TSharedPtr<SWindow> Window = ParentWindow.Pin())
    {
        Window->RequestDestroyWindow();
    }
}

#undef LOCTEXT_NAMESPACE
