// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "WetClothing/Modes/Wrinkle/Generate/SWetWrinkleTextureGeneratorDialog.h"

#include "DataAssets/WetClothingAsset.h"
#include "Engine/Texture2D.h"
#include "Misc/MessageDialog.h"
#include "Styling/CoreStyle.h"
#include "Types/WidgetActiveTimerDelegate.h"
#include "WetClothing/Modes/Wrinkle/Authoring/WetWrinkleBrushConstants.h"
#include "WetClothing/Modes/Wrinkle/Generate/WetWrinkleTextureGenerator.h"
#include "WetClothing/Modes/Wrinkle/Viewport/WetWrinkleViewport.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SWindow.h"

#define LOCTEXT_NAMESPACE "SWetWrinkleTextureGeneratorDialog"

namespace
{
    constexpr int32 GeneratedPreviewMaxResolution = 1024;
    constexpr float GeneratedPreviewRebuildDelaySeconds = 0.1f;
}

class SWetWrinkleZoomableImage final : public SCompoundWidget
    {
      public:
        SLATE_BEGIN_ARGS(SWetWrinkleZoomableImage) {}
        SLATE_ARGUMENT(const FSlateBrush*, Image)
        SLATE_END_ARGS()

        void Construct(const FArguments& InArgs)
        {
            ImageBrush = InArgs._Image;

            ChildSlot
                [SNew(SBorder)
                     .Padding(0.0f)
                     .Clipping(EWidgetClipping::ClipToBounds)
                         [SAssignNew(ImageScaleBox, SScaleBox)
                              .Stretch(EStretch::ScaleToFit)
                              .StretchDirection(EStretchDirection::Both)
                              .RenderTransform(this, &SWetWrinkleZoomableImage::GetImageRenderTransform)
                              .RenderTransformPivot(FVector2D(0.5f, 0.5f))
                                  [SAssignNew(ImageWidget, SImage)
                                       .Image(ImageBrush)]]];
        }

        virtual FReply OnMouseWheel(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
        {
            const float WheelDelta = MouseEvent.GetWheelDelta();
            if (FMath::IsNearlyZero(WheelDelta))
            {
                return FReply::Handled();
            }

            Zoom = FMath::Clamp(Zoom * FMath::Pow(1.15f, WheelDelta), 0.5f, 8.0f);
            return FReply::Handled();
        }

        void SetImage(const FSlateBrush* InImageBrush)
        {
            ImageBrush = InImageBrush;
            if (ImageWidget.IsValid())
            {
                ImageWidget->SetImage(ImageBrush);
            }
            if (ImageScaleBox.IsValid())
            {
                ImageScaleBox->Invalidate(EInvalidateWidgetReason::Paint);
            }
        }

      private:
        TOptional<FSlateRenderTransform> GetImageRenderTransform() const
        {
            return FSlateRenderTransform(Zoom);
        }

        const FSlateBrush* ImageBrush = nullptr;
        TSharedPtr<SImage> ImageWidget;
        TSharedPtr<SScaleBox> ImageScaleBox;
        float Zoom = 1.0f;
};

void SWetWrinkleTextureGeneratorDialog::Construct(const FArguments& InArgs)
{
    ParentWindow = InArgs._ParentWindow;
    WetClothingAsset = InArgs._WetClothingAsset;
    MaterialSlotIndex = InArgs._MaterialSlotIndex;
    UVChannelIndex = InArgs._UVChannelIndex;
    Resolution = FMath::Clamp(InArgs._Resolution, 16, GeneratedPreviewMaxResolution);
    BaseNormalOptions = InArgs._BaseNormalOptions;
    if (BaseNormalOptions.Num() > 0)
    {
        SelectedBaseNormalOption = BaseNormalOptions[0];
    }

    GeneratedNormalBrush.DrawAs = ESlateBrushDrawType::Image;
    GeneratedNormalBrush.Tiling = ESlateBrushTileType::NoTile;
    GeneratedNormalBrush.ImageSize = FVector2D(1024.0f, 1024.0f);

    const FSlateFontInfo SectionHeadingFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 12);

    ChildSlot
        [SNew(SVerticalBox)

         + SVerticalBox::Slot()
               .FillHeight(1.0f)
                   [SNew(SSplitter)
                        .Orientation(Orient_Horizontal)

                    + SSplitter::Slot()
                          .Value(0.48f)
                              [SNew(SBorder)
                                   .Padding(10.0f)
                                       [SNew(SVerticalBox)

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                                                  [SNew(STextBlock)
                                                       .Text(LOCTEXT("GeneratedNormalTexturePreviewLabel", "Generated Normal Texture"))
                                                       .Font(SectionHeadingFont)]

                                        + SVerticalBox::Slot()
                                              .FillHeight(1.0f)
                                                  [SNew(SBorder)
                                                       .Padding(6.0f)
                                                       .HAlign(HAlign_Center)
                                                       .VAlign(VAlign_Center)
                                                           [SAssignNew(GeneratedNormalImage, SWetWrinkleZoomableImage)
                                                                .Image(&GeneratedNormalBrush)]]

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 8.0f, 0.0f, 0.0f)
                                                  [SNew(STextBlock)
                                                       .AutoWrapText(true)
                                                       .Text(this, &SWetWrinkleTextureGeneratorDialog::GetGenerationStatusText)]]]

                    + SSplitter::Slot()
                          .Value(0.52f)
                              [SNew(SBorder)
                                   .Padding(10.0f)
                                       [SNew(SVerticalBox)

                                        + SVerticalBox::Slot()
                                              .AutoHeight()
                                              .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                                                  [SNew(STextBlock)
                                                       .Text(LOCTEXT("GeneratedNormal3DPreviewLabel", "Preview"))
                                                       .Font(SectionHeadingFont)]

                                         + SVerticalBox::Slot()
                                               .FillHeight(1.0f)
                                                   [SAssignNew(PreviewViewport, SWetWrinkleViewport)
                                                        .WetClothingAsset(WetClothingAsset.Get())]]]]

         + SVerticalBox::Slot()
               .AutoHeight()
               .Padding(10.0f, 8.0f, 10.0f, 8.0f)
                   [SNew(SBorder)
                        .Padding(10.0f)
                            [SNew(SVerticalBox)

                             + SVerticalBox::Slot()
                                   .AutoHeight()
                                   .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                                       [SNew(STextBlock)
                                            .Text(LOCTEXT("GeneratedWrinkleTextureParamsLabel", "Generation Settings"))
                                            .Font(SectionHeadingFont)]

                             + SVerticalBox::Slot()
                                   .AutoHeight()
                                   .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                                       [SNew(SHorizontalBox)

                                        + SHorizontalBox::Slot()
                                              .AutoWidth()
                                              .VAlign(VAlign_Center)
                                              .Padding(0.0f, 0.0f, 8.0f, 0.0f)
                                                  [SNew(STextBlock)
                                                       .Text(LOCTEXT("BaseNormalTextureLabel", "Base Normal"))]

                                        + SHorizontalBox::Slot()
                                              .FillWidth(1.0f)
                                              .VAlign(VAlign_Center)
                                                  [SAssignNew(BaseNormalComboBox, SComboBox<TSharedPtr<FWetWrinkleBrushPresetOption>>)
                                                       .OptionsSource(&BaseNormalOptions)
                                                       .OnGenerateWidget(this, &SWetWrinkleTextureGeneratorDialog::GenerateBaseNormalComboRow)
                                                       .OnSelectionChanged(this, &SWetWrinkleTextureGeneratorDialog::HandleBaseNormalChanged)
                                                           [SNew(STextBlock)
                                                                .Text(this, &SWetWrinkleTextureGeneratorDialog::GetSelectedBaseNormalText)]]]

                             + SVerticalBox::Slot()
                                   .AutoHeight()
                                   .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                                       [BuildFloatControl(
                                           LOCTEXT("GeneratedWrinkleIntensityLabel", "Intensity"),
                                           TAttribute<float>::Create(TAttribute<float>::FGetter::CreateSP(this, &SWetWrinkleTextureGeneratorDialog::GetIntensity)),
                                           [this](const float Value) { HandleIntensityChanged(Value); },
                                           0.0f,
                                           4.0f)]

                             + SVerticalBox::Slot()
                                   .AutoHeight()
                                   .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                                       [BuildFloatControl(
                                           LOCTEXT("GeneratedWrinkleScaleLabel", "Scale"),
                                           TAttribute<float>::Create(TAttribute<float>::FGetter::CreateSP(this, &SWetWrinkleTextureGeneratorDialog::GetPatternScale)),
                                           [this](const float Value) { HandlePatternScaleChanged(Value); },
                                           0.25f,
                                           4.0f)]

                             + SVerticalBox::Slot()
                                   .AutoHeight()
                                   .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                                       [BuildFloatControl(
                                           LOCTEXT("GeneratedWrinkleOffsetXLabel", "Offset X"),
                                           TAttribute<float>::Create(TAttribute<float>::FGetter::CreateSP(this, &SWetWrinkleTextureGeneratorDialog::GetPatternOffsetX)),
                                           [this](const float Value) { HandlePatternOffsetXChanged(Value); },
                                           -0.5f,
                                           0.5f)]

                             + SVerticalBox::Slot()
                                   .AutoHeight()
                                   .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                                       [BuildFloatControl(
                                           LOCTEXT("GeneratedWrinkleOffsetYLabel", "Offset Y"),
                                           TAttribute<float>::Create(TAttribute<float>::FGetter::CreateSP(this, &SWetWrinkleTextureGeneratorDialog::GetPatternOffsetY)),
                                           [this](const float Value) { HandlePatternOffsetYChanged(Value); },
                                           -0.5f,
                                           0.5f)]

                             + SVerticalBox::Slot()
                                   .AutoHeight()
                                   .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                                       [BuildFloatControl(
                                           LOCTEXT("GeneratedWrinkleDirectionLabel", "Direction"),
                                           TAttribute<float>::Create(TAttribute<float>::FGetter::CreateSP(this, &SWetWrinkleTextureGeneratorDialog::GetDirectionDegrees)),
                                           [this](const float Value) { HandleDirectionDegreesChanged(Value); },
                                           -180.0f,
                                           180.0f)]

                             + SVerticalBox::Slot()
                                   .AutoHeight()
                                            [BuildFloatControl(
                                                LOCTEXT("GeneratedWrinkleNoiseLabel", "Wave Warp"),
                                                TAttribute<float>::Create(TAttribute<float>::FGetter::CreateSP(this, &SWetWrinkleTextureGeneratorDialog::GetNoise)),
                                                [this](const float Value) { HandleNoiseChanged(Value); },
                                                0.0f,
                                                1.0f)]]]

         + SVerticalBox::Slot()
               .AutoHeight()
               .Padding(10.0f, 0.0f, 10.0f, 10.0f)
               .HAlign(HAlign_Right)
                   [SNew(SUniformGridPanel)
                        .SlotPadding(FMargin(6.0f, 0.0f))

                    + SUniformGridPanel::Slot(0, 0)
                          [SNew(SButton)
                               .Text(LOCTEXT("CancelGeneratedWrinkleTexture", "Cancel"))
                               .OnClicked(this, &SWetWrinkleTextureGeneratorDialog::HandleCancelClicked)]

                    + SUniformGridPanel::Slot(1, 0)
                          [SNew(SButton)
                               .Text(LOCTEXT("ApplyGeneratedWrinkleTexture", "Apply"))
                               .OnClicked(this, &SWetWrinkleTextureGeneratorDialog::HandleApplyClicked)]]];

    if (BaseNormalComboBox.IsValid() && SelectedBaseNormalOption.IsValid())
    {
        BaseNormalComboBox->SetSelectedItem(SelectedBaseNormalOption);
    }

    ConfigurePreviewViewport();
    RebuildPreviewTexture();
}

void SWetWrinkleTextureGeneratorDialog::AddReferencedObjects(FReferenceCollector& Collector)
{
    Collector.AddReferencedObject(GeneratedNormalTexture);
    Collector.AddReferencedObject(DisplayPreviewTexture);
    Collector.AddReferencedObject(WetClothingAsset);
}

FString SWetWrinkleTextureGeneratorDialog::GetReferencerName() const
{
    return TEXT("SWetWrinkleTextureGeneratorDialog");
}

bool SWetWrinkleTextureGeneratorDialog::WasApplied() const
{
    return bApplied;
}

UTexture2D* SWetWrinkleTextureGeneratorDialog::GetGeneratedNormalTexture() const
{
    return bGeneratedPreviewValid ? GeneratedNormalTexture.Get() : nullptr;
}

TSharedRef<SWidget> SWetWrinkleTextureGeneratorDialog::BuildFloatControl(
    const FText& Label,
    TAttribute<float> ValueAttribute,
    TFunction<void(float)> OnValueChanged,
    const float MinValue,
    const float MaxValue)
{
    return SNew(SHorizontalBox)

        + SHorizontalBox::Slot()
              .AutoWidth()
              .VAlign(VAlign_Center)
              .Padding(0.0f, 0.0f, 8.0f, 0.0f)
                  [SNew(SBox)
                       .WidthOverride(92.0f)
                           [SNew(STextBlock)
                                .Text(Label)]]

        + SHorizontalBox::Slot()
              .FillWidth(1.0f)
              .VAlign(VAlign_Center)
                  [SNew(SSpinBox<float>)
                       .MinValue(MinValue)
                       .MaxValue(MaxValue)
                       .MinSliderValue(MinValue)
                       .MaxSliderValue(MaxValue)
                       .Value(ValueAttribute)
                       .OnValueChanged_Lambda([OnValueChanged = MoveTemp(OnValueChanged)](const float Value) mutable
                                              { OnValueChanged(Value); })];
}

TSharedRef<SWidget> SWetWrinkleTextureGeneratorDialog::GenerateBaseNormalComboRow(
    TSharedPtr<FWetWrinkleBrushPresetOption> Item) const
{
    return SNew(STextBlock)
        .Text(Item.IsValid() ? Item->DisplayName : LOCTEXT("MissingGeneratedBaseNormal", "<missing>"));
}

FText SWetWrinkleTextureGeneratorDialog::GetSelectedBaseNormalText() const
{
    return SelectedBaseNormalOption.IsValid()
               ? SelectedBaseNormalOption->DisplayName
               : LOCTEXT("NoGeneratedBaseNormalSelected", "None");
}

float SWetWrinkleTextureGeneratorDialog::GetIntensity() const
{
    return Intensity;
}

float SWetWrinkleTextureGeneratorDialog::GetDirectionDegrees() const
{
    return DirectionDegrees;
}

float SWetWrinkleTextureGeneratorDialog::GetPatternScale() const
{
    return PatternScale;
}

float SWetWrinkleTextureGeneratorDialog::GetPatternOffsetX() const
{
    return static_cast<float>(PatternOffset.X);
}

float SWetWrinkleTextureGeneratorDialog::GetPatternOffsetY() const
{
    return static_cast<float>(PatternOffset.Y);
}

float SWetWrinkleTextureGeneratorDialog::GetNoise() const
{
    return Noise;
}

FText SWetWrinkleTextureGeneratorDialog::GetGenerationStatusText() const
{
    if (bGeneratedPreviewValid && GeneratedNormalTexture != nullptr)
    {
        return FText::FromString(FString::Printf(TEXT("Generated %dx%d preview normal."), GeneratedTextureWidth, GeneratedTextureHeight));
    }

    return LastGenerationError.IsEmpty()
               ? LOCTEXT("GeneratedWrinkleTextureWaiting", "Waiting for a valid generated preview.")
               : FText::FromString(LastGenerationError);
}

void SWetWrinkleTextureGeneratorDialog::HandleBaseNormalChanged(
    TSharedPtr<FWetWrinkleBrushPresetOption> Item,
    ESelectInfo::Type SelectInfo)
{
    if (!Item.IsValid())
    {
        return;
    }

    SelectedBaseNormalOption = MoveTemp(Item);
    RequestPreviewTextureRebuild();
}

void SWetWrinkleTextureGeneratorDialog::HandleIntensityChanged(const float NewValue)
{
    Intensity = FMath::Clamp(NewValue, 0.0f, 4.0f);
    RequestPreviewTextureRebuild();
}

void SWetWrinkleTextureGeneratorDialog::HandleDirectionDegreesChanged(const float NewValue)
{
    DirectionDegrees = FMath::Clamp(NewValue, -180.0f, 180.0f);
    RequestPreviewTextureRebuild();
}

void SWetWrinkleTextureGeneratorDialog::HandlePatternScaleChanged(const float NewValue)
{
    PatternScale = FMath::Clamp(NewValue, 0.25f, 4.0f);
    RequestPreviewTextureRebuild();
}

void SWetWrinkleTextureGeneratorDialog::HandlePatternOffsetXChanged(const float NewValue)
{
    PatternOffset.X = FMath::Clamp(NewValue, -0.5f, 0.5f);
    RequestPreviewTextureRebuild();
}

void SWetWrinkleTextureGeneratorDialog::HandlePatternOffsetYChanged(const float NewValue)
{
    PatternOffset.Y = FMath::Clamp(NewValue, -0.5f, 0.5f);
    RequestPreviewTextureRebuild();
}

void SWetWrinkleTextureGeneratorDialog::HandleNoiseChanged(const float NewValue)
{
    Noise = FMath::Clamp(NewValue, 0.0f, 1.0f);
    RequestPreviewTextureRebuild();
}

UTexture2D* SWetWrinkleTextureGeneratorDialog::ResolveSelectedBaseNormalTexture() const
{
    return SelectedBaseNormalOption.IsValid()
               ? Cast<UTexture2D>(SelectedBaseNormalOption->TexturePath.TryLoad())
               : nullptr;
}

void SWetWrinkleTextureGeneratorDialog::ConfigurePreviewViewport()
{
    if (!PreviewViewport.IsValid())
    {
        return;
    }

    PreviewViewport->RefreshPreviewMesh();

    FWetWrinkleBrushSettings PreviewBrushSettings;
    PreviewBrushSettings.MaterialSlotIndex = MaterialSlotIndex;
    PreviewBrushSettings.UVChannelIndex = UVChannelIndex;
    PreviewBrushSettings.PreviewWetness = 1.0f;
    PreviewBrushSettings.bShowPreview = false;
    PreviewViewport->SynchronizeBrushSettings(PreviewBrushSettings);
    PreviewViewport->FocusOnPreviewMesh(true);
}

void SWetWrinkleTextureGeneratorDialog::RequestPreviewTextureRebuild()
{
    bPendingPreviewRebuild = true;
    if (bPreviewRebuildTimerActive)
    {
        return;
    }

    bPreviewRebuildTimerActive = true;
    RegisterActiveTimer(
        GeneratedPreviewRebuildDelaySeconds,
        FWidgetActiveTimerDelegate::CreateSP(this, &SWetWrinkleTextureGeneratorDialog::HandlePreviewRebuildTimer));
}

EActiveTimerReturnType SWetWrinkleTextureGeneratorDialog::HandlePreviewRebuildTimer(const double CurrentTime, const float DeltaTime)
{
    bPreviewRebuildTimerActive = false;
    if (bPendingPreviewRebuild)
    {
        RebuildPreviewTexture();
    }
    return EActiveTimerReturnType::Stop;
}

void SWetWrinkleTextureGeneratorDialog::RebuildPreviewTexture()
{
    bPendingPreviewRebuild = false;

    FString ErrorMessage;
    FWetWrinkleTextureGenerationResult GenerationResult;
    FWetWrinkleTextureGenerationSettings GenerationSettings;
    GenerationSettings.BaseNormalTexture = ResolveSelectedBaseNormalTexture();
    GenerationSettings.LODIndex = 0;
    GenerationSettings.UVChannelIndex = UVChannelIndex;
    GenerationSettings.Resolution = Resolution;
    GenerationSettings.Intensity = Intensity;
    GenerationSettings.PatternScale = PatternScale;
    GenerationSettings.PatternOffset = PatternOffset;
    GenerationSettings.DirectionRadians = FMath::DegreesToRadians(DirectionDegrees);
    GenerationSettings.Noise = Noise;

    if (FWetWrinkleTextureGenerator::GeneratePreviewMaterialSlotTexture(
            WetClothingAsset.Get(),
            MaterialSlotIndex,
            GenerationSettings,
            GenerationResult,
            ErrorMessage))
    {
        GeneratedNormalTexture = GenerationResult.GeneratedNormalMap;
        DisplayPreviewTexture = GenerationResult.PreviewDisplayMap != nullptr
                                    ? GenerationResult.PreviewDisplayMap
                                    : GenerationResult.GeneratedNormalMap;
        GeneratedTextureWidth = GenerationResult.Width;
        GeneratedTextureHeight = GenerationResult.Height;
        LastGenerationError.Reset();
        bGeneratedPreviewValid = GeneratedNormalTexture != nullptr;
    }
    else
    {
        GeneratedNormalTexture = nullptr;
        DisplayPreviewTexture = nullptr;
        GeneratedTextureWidth = 0;
        GeneratedTextureHeight = 0;
        LastGenerationError = MoveTemp(ErrorMessage);
        bGeneratedPreviewValid = false;
    }

    GeneratedNormalBrush.SetResourceObject(DisplayPreviewTexture.Get());
    GeneratedNormalBrush.ImageSize =
        GeneratedTextureWidth > 0 && GeneratedTextureHeight > 0
            ? FVector2D(static_cast<float>(GeneratedTextureWidth), static_cast<float>(GeneratedTextureHeight))
            : FVector2D(1024.0f, 1024.0f);

    if (GeneratedNormalImage.IsValid())
    {
        GeneratedNormalImage->SetImage(&GeneratedNormalBrush);
    }

    if (PreviewViewport.IsValid())
    {
        PreviewViewport->SetGeneratedNormalPreviewTexture(
            MaterialSlotIndex,
            UVChannelIndex,
            GeneratedNormalTexture.Get());
    }
}

FReply SWetWrinkleTextureGeneratorDialog::HandleCancelClicked()
{
    if (TSharedPtr<SWindow> Window = ParentWindow.Pin())
    {
        Window->RequestDestroyWindow();
    }
    return FReply::Handled();
}

FReply SWetWrinkleTextureGeneratorDialog::HandleApplyClicked()
{
    if (bPendingPreviewRebuild)
    {
        RebuildPreviewTexture();
    }

    if (!bGeneratedPreviewValid || GeneratedNormalTexture == nullptr)
    {
        FMessageDialog::Open(
            EAppMsgType::Ok,
            LastGenerationError.IsEmpty()
                ? LOCTEXT("ApplyGeneratedWrinkleTextureNoPreview", "A generated wrinkle texture preview is not available.")
                : FText::FromString(LastGenerationError));
        return FReply::Handled();
    }

    bApplied = true;
    if (TSharedPtr<SWindow> Window = ParentWindow.Pin())
    {
        Window->RequestDestroyWindow();
    }
    return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
