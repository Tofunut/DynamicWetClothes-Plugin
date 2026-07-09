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
#include "Styling/StyleColors.h"
#include "Types/WidgetActiveTimerDelegate.h"
#include "WetClothing/Common/Analysis/WetClothingAssetMeshAnalyzer.h"
#include "WetClothing/Common/Texture/WetClothingMaterialTextureResolver.h"
#include "WetClothing/Common/Widgets/WetClothingEditorCommonWidgets.h"
#include "WetClothing/PartEdit/Partition/WetPartEditingService.h"
#include "WetClothing/WrinkleEdit/Bake/WetWrinkleNormalMapBaker.h"
#include "WetClothing/WrinkleEdit/Generate/WetWrinkleTextureGenerator.h"
#include "WetClothing/WrinkleEdit/Viewport/WetWrinkleViewport.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "UObject/GCObject.h"
#include "Widgets/SWindow.h"
#include "Framework/Application/SlateApplication.h"

#define LOCTEXT_NAMESPACE "WetClothingAssetEditorPanel"

namespace
{
    constexpr const TCHAR* WetWrinklePreset0Path = TEXT("/DynamicWetClothes/Presets/WrinkleTextures/Wet_Wrinkle_Normal0.Wet_Wrinkle_Normal0");
    constexpr const TCHAR* WetWrinklePresetFolderPath = TEXT("/DynamicWetClothes/Presets/WrinkleTextures");
    constexpr float WetWrinkleDefaultSizeCm = 8.0f;
    constexpr float WetWrinkleDefaultSizeUV = 0.0677f;
    constexpr float WetWrinkleUVPerCm = WetWrinkleDefaultSizeUV / WetWrinkleDefaultSizeCm;
    constexpr int32 WetWrinkleFixedUVChannelIndex = 0;
    constexpr int32 WetWrinkleGeneratedPreviewMaxResolution = 1024;
    constexpr float WetWrinkleGeneratedPreviewRebuildDelaySeconds = 0.1f;

    FText FormatWetWrinkleBrushSizeCm(float SizeCm)
    {
        FNumberFormattingOptions Options;
        Options.MinimumFractionalDigits = 0;
        Options.MaximumFractionalDigits = 1;
        return FText::AsNumber(SizeCm, &Options);
    }


    DECLARE_DELEGATE_OneParam(FOnWetWrinkleGeneratedTextureFloatValueChanged, float);

    class SWetWrinkleZoomableImage : public SCompoundWidget
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

      private:
        const FSlateBrush* ImageBrush = nullptr;
        TSharedPtr<SImage> ImageWidget;
        TSharedPtr<SScaleBox> ImageScaleBox;
        float Zoom = 1.0f;
    };

    class SWetWrinkleTextureGeneratorDialog : public SCompoundWidget, public FGCObject
    {
      public:
        SLATE_BEGIN_ARGS(SWetWrinkleTextureGeneratorDialog) {}
        SLATE_ARGUMENT(TSharedPtr<SWindow>, ParentWindow)
        SLATE_ARGUMENT(UWetClothingAsset*, WetClothingAsset)
        SLATE_ARGUMENT(int32, MaterialSlotIndex)
        SLATE_ARGUMENT(int32, UVChannelIndex)
        SLATE_ARGUMENT(int32, Resolution)
        SLATE_ARGUMENT(TArray<TSharedPtr<FWetWrinkleBrushPresetOption>>, BaseNormalOptions)
        SLATE_END_ARGS()

        void Construct(const FArguments& InArgs)
        {
            ParentWindow = InArgs._ParentWindow;
            WetClothingAsset = InArgs._WetClothingAsset;
            MaterialSlotIndex = InArgs._MaterialSlotIndex;
            UVChannelIndex = InArgs._UVChannelIndex;
            Resolution = FMath::Clamp(InArgs._Resolution, 16, WetWrinkleGeneratedPreviewMaxResolution);
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
                                                               .WetClothingAsset(WetClothingAsset.Get())
                                                               .UseOriginalMeshMaterialForPreview(true)]]]]

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
                                                   FOnWetWrinkleGeneratedTextureFloatValueChanged::CreateSP(this, &SWetWrinkleTextureGeneratorDialog::HandleIntensityChanged),
                                                   0.0f,
                                                   4.0f)]

                                     + SVerticalBox::Slot()
                                           .AutoHeight()
                                           .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                                               [BuildFloatControl(
                                                   LOCTEXT("GeneratedWrinkleScaleLabel", "Scale"),
                                                   TAttribute<float>::Create(TAttribute<float>::FGetter::CreateSP(this, &SWetWrinkleTextureGeneratorDialog::GetPatternScale)),
                                                   FOnWetWrinkleGeneratedTextureFloatValueChanged::CreateSP(this, &SWetWrinkleTextureGeneratorDialog::HandlePatternScaleChanged),
                                                   0.25f,
                                                   4.0f)]

                                     + SVerticalBox::Slot()
                                           .AutoHeight()
                                           .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                                               [BuildFloatControl(
                                                   LOCTEXT("GeneratedWrinkleOffsetXLabel", "Offset X"),
                                                   TAttribute<float>::Create(TAttribute<float>::FGetter::CreateSP(this, &SWetWrinkleTextureGeneratorDialog::GetPatternOffsetX)),
                                                   FOnWetWrinkleGeneratedTextureFloatValueChanged::CreateSP(this, &SWetWrinkleTextureGeneratorDialog::HandlePatternOffsetXChanged),
                                                   -0.5f,
                                                   0.5f)]

                                     + SVerticalBox::Slot()
                                           .AutoHeight()
                                           .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                                               [BuildFloatControl(
                                                   LOCTEXT("GeneratedWrinkleOffsetYLabel", "Offset Y"),
                                                   TAttribute<float>::Create(TAttribute<float>::FGetter::CreateSP(this, &SWetWrinkleTextureGeneratorDialog::GetPatternOffsetY)),
                                                   FOnWetWrinkleGeneratedTextureFloatValueChanged::CreateSP(this, &SWetWrinkleTextureGeneratorDialog::HandlePatternOffsetYChanged),
                                                   -0.5f,
                                                   0.5f)]

                                     + SVerticalBox::Slot()
                                           .AutoHeight()
                                           .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                                               [BuildFloatControl(
                                                   LOCTEXT("GeneratedWrinkleDirectionLabel", "Direction"),
                                                   TAttribute<float>::Create(TAttribute<float>::FGetter::CreateSP(this, &SWetWrinkleTextureGeneratorDialog::GetDirectionDegrees)),
                                                   FOnWetWrinkleGeneratedTextureFloatValueChanged::CreateSP(this, &SWetWrinkleTextureGeneratorDialog::HandleDirectionDegreesChanged),
                                                   -180.0f,
                                                   180.0f)]

                                     + SVerticalBox::Slot()
                                           .AutoHeight()
                                               [BuildFloatControl(
                                                   LOCTEXT("GeneratedWrinkleNoiseLabel", "Wave Warp"),
                                                   TAttribute<float>::Create(TAttribute<float>::FGetter::CreateSP(this, &SWetWrinkleTextureGeneratorDialog::GetNoise)),
                                                   FOnWetWrinkleGeneratedTextureFloatValueChanged::CreateSP(this, &SWetWrinkleTextureGeneratorDialog::HandleNoiseChanged),
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

        virtual void AddReferencedObjects(FReferenceCollector& Collector) override
        {
            Collector.AddReferencedObject(GeneratedNormalTexture);
            Collector.AddReferencedObject(DisplayPreviewTexture);
            if (UWetClothingAsset* Asset = WetClothingAsset.Get())
            {
                Collector.AddReferencedObject(Asset);
            }
        }

        virtual FString GetReferencerName() const override
        {
            return TEXT("SWetWrinkleTextureGeneratorDialog");
        }

        bool WasApplied() const
        {
            return bApplied;
        }

        UTexture2D* GetGeneratedNormalTexture() const
        {
            return bGeneratedPreviewValid ? GeneratedNormalTexture.Get() : nullptr;
        }

      private:
        TSharedRef<SWidget> BuildFloatControl(
            const FText& Label,
            TAttribute<float> ValueAttribute,
            FOnWetWrinkleGeneratedTextureFloatValueChanged OnValueChanged,
            float MinValue,
            float MaxValue)
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
                               .OnValueChanged_Lambda([OnValueChanged](float NewValue) mutable
                                                     {
                                                         OnValueChanged.ExecuteIfBound(NewValue);
                                                     })];
        }

        TSharedRef<SWidget> GenerateBaseNormalComboRow(TSharedPtr<FWetWrinkleBrushPresetOption> Item) const
        {
            return SNew(STextBlock)
                .Text(Item.IsValid() ? Item->DisplayName : LOCTEXT("MissingGeneratedBaseNormal", "<missing>"));
        }

        FText GetSelectedBaseNormalText() const
        {
            return SelectedBaseNormalOption.IsValid()
                       ? SelectedBaseNormalOption->DisplayName
                       : LOCTEXT("NoGeneratedBaseNormalSelected", "None");
        }

        float GetIntensity() const
        {
            return Intensity;
        }

        float GetDirectionDegrees() const
        {
            return DirectionDegrees;
        }

        float GetPatternScale() const
        {
            return PatternScale;
        }

        float GetPatternOffsetX() const
        {
            return static_cast<float>(PatternOffset.X);
        }

        float GetPatternOffsetY() const
        {
            return static_cast<float>(PatternOffset.Y);
        }

        float GetNoise() const
        {
            return Noise;
        }

        FText GetGenerationStatusText() const
        {
            if (bGeneratedPreviewValid && GeneratedNormalTexture != nullptr)
            {
                return FText::FromString(FString::Printf(TEXT("Generated %dx%d preview normal."), GeneratedTextureWidth, GeneratedTextureHeight));
            }

            return LastGenerationError.IsEmpty()
                       ? LOCTEXT("GeneratedWrinkleTextureWaiting", "Waiting for a valid generated preview.")
                       : FText::FromString(LastGenerationError);
        }

        void HandleBaseNormalChanged(TSharedPtr<FWetWrinkleBrushPresetOption> Item, ESelectInfo::Type SelectInfo)
        {
            if (!Item.IsValid())
            {
                return;
            }

            SelectedBaseNormalOption = Item;
            RequestPreviewTextureRebuild();
        }

        void HandleIntensityChanged(float NewValue)
        {
            Intensity = FMath::Clamp(NewValue, 0.0f, 4.0f);
            RequestPreviewTextureRebuild();
        }

        void HandleDirectionDegreesChanged(float NewValue)
        {
            DirectionDegrees = FMath::Clamp(NewValue, -180.0f, 180.0f);
            RequestPreviewTextureRebuild();
        }

        void HandlePatternScaleChanged(float NewValue)
        {
            PatternScale = FMath::Clamp(NewValue, 0.25f, 4.0f);
            RequestPreviewTextureRebuild();
        }

        void HandlePatternOffsetXChanged(float NewValue)
        {
            PatternOffset.X = FMath::Clamp(NewValue, -0.5f, 0.5f);
            RequestPreviewTextureRebuild();
        }

        void HandlePatternOffsetYChanged(float NewValue)
        {
            PatternOffset.Y = FMath::Clamp(NewValue, -0.5f, 0.5f);
            RequestPreviewTextureRebuild();
        }

        void HandleNoiseChanged(float NewValue)
        {
            Noise = FMath::Clamp(NewValue, 0.0f, 1.0f);
            RequestPreviewTextureRebuild();
        }

        UTexture2D* ResolveSelectedBaseNormalTexture() const
        {
            return SelectedBaseNormalOption.IsValid()
                       ? Cast<UTexture2D>(SelectedBaseNormalOption->TexturePath.TryLoad())
                       : nullptr;
        }

        void ConfigurePreviewViewport()
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
            PreviewViewport->SetBrushSettings(PreviewBrushSettings);
            PreviewViewport->FocusOnPreviewMesh(true);
        }

        void RequestPreviewTextureRebuild()
        {
            bPendingPreviewRebuild = true;
            if (bPreviewRebuildTimerActive)
            {
                return;
            }

            bPreviewRebuildTimerActive = true;
            RegisterActiveTimer(
                WetWrinkleGeneratedPreviewRebuildDelaySeconds,
                FWidgetActiveTimerDelegate::CreateSP(this, &SWetWrinkleTextureGeneratorDialog::HandlePreviewRebuildTimer));
        }

        EActiveTimerReturnType HandlePreviewRebuildTimer(double, float)
        {
            bPreviewRebuildTimerActive = false;
            if (bPendingPreviewRebuild)
            {
                RebuildPreviewTexture();
            }
            return EActiveTimerReturnType::Stop;
        }

        void RebuildPreviewTexture()
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
                LastGenerationError = ErrorMessage;
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
                PreviewViewport->SetGeneratedNormalPreviewTexture(MaterialSlotIndex, UVChannelIndex, GeneratedNormalTexture.Get());
            }
        }

        FReply HandleCancelClicked()
        {
            if (TSharedPtr<SWindow> Window = ParentWindow.Pin())
            {
                Window->RequestDestroyWindow();
            }
            return FReply::Handled();
        }

        FReply HandleApplyClicked()
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

      private:
        TWeakPtr<SWindow> ParentWindow;
        TWeakObjectPtr<UWetClothingAsset> WetClothingAsset;
        TSharedPtr<SWetWrinkleViewport> PreviewViewport;
        TSharedPtr<SWetWrinkleZoomableImage> GeneratedNormalImage;
        TSharedPtr<SComboBox<TSharedPtr<FWetWrinkleBrushPresetOption>>> BaseNormalComboBox;
        TArray<TSharedPtr<FWetWrinkleBrushPresetOption>> BaseNormalOptions;
        TSharedPtr<FWetWrinkleBrushPresetOption> SelectedBaseNormalOption;
        FSlateBrush GeneratedNormalBrush;
        TObjectPtr<UTexture2D> GeneratedNormalTexture = nullptr;
        TObjectPtr<UTexture2D> DisplayPreviewTexture = nullptr;
        int32 MaterialSlotIndex = INDEX_NONE;
        int32 UVChannelIndex = INDEX_NONE;
        int32 Resolution = 1024;
        int32 GeneratedTextureWidth = 0;
        int32 GeneratedTextureHeight = 0;
        float Intensity = 1.0f;
        float PatternScale = 1.0f;
        FVector2D PatternOffset = FVector2D::ZeroVector;
        float DirectionDegrees = 0.0f;
        float Noise = 0.0f;
        FString LastGenerationError;
        bool bGeneratedPreviewValid = false;
        bool bPendingPreviewRebuild = false;
        bool bPreviewRebuildTimerActive = false;
        bool bApplied = false;
    };

}

void SWetWrinkleEditorPanel::Construct(const FArguments& InArgs)
{
    WetClothingAsset = InArgs._WetClothingAsset;
    DetailsView = InArgs._DetailsView;
    MaterialThumbnailPool = MakeShared<FAssetThumbnailPool>(32);
    PatchTextureThumbnailPool = MakeShared<FAssetThumbnailPool>(32);

    UVDisplayModeItems.Reset();
    UVDisplayModeItems.Add(MakeShared<EWetClothingAssetUVDisplayMode>(EWetClothingAssetUVDisplayMode::Normal));
    UVDisplayModeItems.Add(MakeShared<EWetClothingAssetUVDisplayMode>(EWetClothingAssetUVDisplayMode::OutlineOnly));
    SelectedUVDisplayModeItem = UVDisplayModeItems[0];
    CurrentUVDisplayMode = EWetClothingAssetUVDisplayMode::Normal;

    if (UWetClothingAsset* Asset = WetClothingAsset.Get())
    {
        Asset->WrinkleData.WrinkleUVChannelIndex = WetWrinkleFixedUVChannelIndex;
        BrushSettings.UVChannelIndex = BrushSettings.MaterialSlotIndex != INDEX_NONE ? WetWrinkleFixedUVChannelIndex : INDEX_NONE;
    }
    RefreshMaterialSlotOptions();
    RefreshUVChannelOptions();
    RefreshMaterialTextures();
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
                                                                    .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                                                                        [SNew(SButton)
                                                                             .ToolTipText(LOCTEXT("AutoGenerateWrinkleTextureTooltip", "Generate wrinkle texture maps for the currently selected material slot."))
                                                                             .ContentPadding(FMargin(8.0f, 5.0f))
                                                                             .OnClicked(this, &SWetWrinkleEditorPanel::HandleAutoGenerateClicked)
                                                                                 [SNew(SHorizontalBox)

                                                                                  + SHorizontalBox::Slot()
                                                                                        .AutoWidth()
                                                                                        .VAlign(VAlign_Center)
                                                                                        .Padding(0.0f, 0.0f, 8.0f, 0.0f)
                                                                                            [SNew(SImage)
                                                                                                 .DesiredSizeOverride(FVector2D(24.0f, 24.0f))
                                                                                                 .Image(FDWCEditorStyle::GetBrush(TEXT("DWCEditor.MagicWandTool.Large")))]

                                                                                  + SHorizontalBox::Slot()
                                                                                        .FillWidth(1.0f)
                                                                                        .VAlign(VAlign_Center)
                                                                                            [SNew(STextBlock)
                                                                                                 .Text(LOCTEXT("AutoGenerateWrinkleButton", "Generate Wrinkle Textures"))]]]

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
                   .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                       [SNew(STextBlock)
                            .Text(LOCTEXT("PreviewWetnessLabel", "Preview Wetness"))]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                       [SNew(SSpinBox<float>)
                            .MinValue(0.0f)
                            .MaxValue(1.0f)
                            .MinSliderValue(0.0f)
                            .MaxSliderValue(1.0f)
                            .Delta(0.05f)
                            .Value(BrushSettings.PreviewWetness)
                            .OnValueChanged(this, &SWetWrinkleEditorPanel::HandlePreviewWetnessChanged)]

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
        Asset->WrinkleData.WrinkleUVChannelIndex = WetWrinkleFixedUVChannelIndex;
        BrushSettings.UVChannelIndex = BrushSettings.MaterialSlotIndex != INDEX_NONE ? WetWrinkleFixedUVChannelIndex : INDEX_NONE;
    }
    RefreshMaterialSlotOptions();
    RefreshUVChannelOptions();
    RefreshMaterialTextures();
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
    RefreshWrinkleUVViewMarkersOnly();
}

void SWetWrinkleEditorPanel::HandlePaintStrokeStarted(const FWetWrinkleSurfaceHit& SurfaceHit)
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || !SurfaceHit.bHit || BrushSettings.MaterialSlotIndex == INDEX_NONE || BrushSettings.UVChannelIndex == INDEX_NONE)
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
    if (Asset == nullptr || ActiveStroke == nullptr || !SurfaceHit.bHit || BrushSettings.MaterialSlotIndex == INDEX_NONE || BrushSettings.UVChannelIndex == INDEX_NONE)
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


void SWetWrinkleEditorPanel::RefreshMaterialTextures()
{
    const int32 UVChannelIndex = GetWrinkleUVViewChannelIndex();

    TextureThumbnails.Reset();
    FWetClothingMaterialTextureResolver::BuildTextureItemsForMaterialSlot(
        WetClothingAsset.Get(),
        BrushSettings.MaterialSlotIndex,
        UVChannelIndex,
        TextureItems,
        SelectedTextureItem);

    bShowMaterialTextureInUVView = SelectedTextureItem.IsValid() && SelectedTextureItem->Texture.IsValid();

    RefreshTextureToggleWidgets();
    RefreshWrinkleUVView();
}

void SWetWrinkleEditorPanel::RefreshTextureToggleWidgets()
{
    TextureThumbnails.Reset();

    if (!TextureSelectionContainer.IsValid())
    {
        return;
    }

    if (BrushSettings.MaterialSlotIndex == INDEX_NONE)
    {
        TextureSelectionContainer->SetContent(
            SNew(STextBlock)
                .Text(LOCTEXT("AllSlotsPreviewOnlyTextureNotice", "All Slots is preview-only. Select a single material slot to choose its texture."))
                .ColorAndOpacity(FSlateColor(FStyleColors::ForegroundHover)));
        return;
    }

    if (!FindMaterialSlotItem(BrushSettings.MaterialSlotIndex).IsValid())
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
        FWetClothingEditorCommonWidgets::BuildUVViewTextureSelector(
            &TextureItems,
            SelectedTextureItem,
            MaterialThumbnailPool,
            &TextureThumbnails,
            &TextureComboBox,
            &SelectedTextureComboContentBox,
            [this](FTextureItemPtr Item, ESelectInfo::Type SelectInfo)
            {
                HandleTextureSelectionChanged(Item, SelectInfo);
            }));
}

TSharedRef<SWidget> SWetWrinkleEditorPanel::GenerateTextureComboItem(FTextureItemPtr Item)
{
    return FWetClothingEditorCommonWidgets::GenerateTextureComboItem(Item, MaterialThumbnailPool, &TextureThumbnails);
}

void SWetWrinkleEditorPanel::HandleTextureSelectionChanged(FTextureItemPtr Item, ESelectInfo::Type SelectInfo)
{
    SelectedTextureItem = Item;
    bShowMaterialTextureInUVView = SelectedTextureItem.IsValid() && SelectedTextureItem->Texture.IsValid();
    SaveSelectedTexture();

    if (SelectedTextureComboContentBox.IsValid())
    {
        SelectedTextureComboContentBox->SetContent(
            FWetClothingEditorCommonWidgets::BuildTextureComboContent(SelectedTextureItem, 24.0f, true, MaterialThumbnailPool, &TextureThumbnails));
    }

    RefreshWrinkleUVView();
}

UTexture* SWetWrinkleEditorPanel::ResolveSelectedMaterialTexture() const
{
    return SelectedTextureItem.IsValid() ? SelectedTextureItem->Texture.Get() : nullptr;
}

UTexture* SWetWrinkleEditorPanel::ResolveTextureAddressTexture() const
{
    if (UTexture* SelectedTexture = ResolveSelectedMaterialTexture())
    {
        return SelectedTexture;
    }

    for (const FTextureItemPtr& TextureItem : TextureItems)
    {
        if (TextureItem.IsValid() && TextureItem->Texture.IsValid())
        {
            return TextureItem->Texture.Get();
        }
    }

    return nullptr;
}

void SWetWrinkleEditorPanel::SaveSelectedTexture()
{
    FWetClothingMaterialTextureResolver::SaveTextureSelection(
        WetClothingAsset.Get(),
        BrushSettings.MaterialSlotIndex,
        GetWrinkleUVViewChannelIndex(),
        ResolveSelectedMaterialTexture());
}


void SWetWrinkleEditorPanel::EnsureWrinkleUVChannelForModeEntry()
{
    // The wrinkle editor is locked to imported UV 0 and must not generate mesh UV channels.
}

bool SWetWrinkleEditorPanel::HasUsableWrinkleUVChannel() const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const USkeletalMesh* TargetMesh = Asset != nullptr ? Asset->TargetMesh.Get() : nullptr;
    if (Asset == nullptr || TargetMesh == nullptr)
    {
        return false;
    }

    const int32 NumUVChannels = FWetClothingAssetMeshAnalyzer::GetNumUVChannels(TargetMesh, 0);
    return NumUVChannels > WetWrinkleFixedUVChannelIndex;
}

bool SWetWrinkleEditorPanel::HasGeneratedWrinkleUVForMaterialSlot(int32 MaterialSlotIndex) const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || MaterialSlotIndex == INDEX_NONE || !HasUsableWrinkleUVChannel())
    {
        return false;
    }

    const int32 WrinkleUVChannelIndex = WetWrinkleFixedUVChannelIndex;
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

    const USkeletalMesh* TargetMesh = Asset->TargetMesh.Get();
    if (TargetMesh == nullptr)
    {
        BrushSettings.UVChannelIndex = INDEX_NONE;
        InvalidateWrinkleUVViewCache();
        if (bShowFailureDialog)
        {
            FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("WrinkleUVNoTargetMesh", "Assign a Target Mesh before editing wrinkles."));
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
                LOCTEXT("WrinkleUVSlotNotWettable", "This material slot is not marked wettable. Enable the wettable toggle for the slot before editing wrinkles."));
        }
        return false;
    }

    const int32 NumUVChannels = FWetClothingAssetMeshAnalyzer::GetNumUVChannels(TargetMesh, 0);
    const int32 CandidateUVChannelIndex = NumUVChannels > WetWrinkleFixedUVChannelIndex ? WetWrinkleFixedUVChannelIndex : INDEX_NONE;

    if (CandidateUVChannelIndex == INDEX_NONE)
    {
        BrushSettings.UVChannelIndex = INDEX_NONE;
        InvalidateWrinkleUVViewCache();
        if (bShowFailureDialog)
        {
            FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("WrinkleUVNoValidChannel", "The target mesh does not have a usable UV channel."));
        }
        return false;
    }

    BrushSettings.UVChannelIndex = CandidateUVChannelIndex;
    SelectedMeshUVChannelIndex = CandidateUVChannelIndex;

    if (Asset->WrinkleData.WrinkleUVChannelIndex != WetWrinkleFixedUVChannelIndex)
    {
        Asset->Modify();
        Asset->WrinkleData.WrinkleUVChannelIndex = WetWrinkleFixedUVChannelIndex;
        MarkAssetEdited();
    }

    return true;
}

void SWetWrinkleEditorPanel::InvalidateWrinkleUVViewCache()
{
    CachedWrinkleUVViewChannelIndex = INDEX_NONE;
    CachedWrinkleUVViewMaterialSlotIndex = INDEX_NONE;
    CachedWrinkleUVViewPatchMarkerChannelIndex = INDEX_NONE;
    CachedWrinkleUVViewPatchMarkerMaterialSlotIndex = INDEX_NONE;
    CachedWrinkleUVViewPatchMarkers.Reset();
}

void SWetWrinkleEditorPanel::RefreshUVChannelOptions()
{
    MeshUVChannelOptions.Reset();

    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const USkeletalMesh* TargetMesh = Asset != nullptr ? Asset->TargetMesh.Get() : nullptr;
    const int32 NumUVChannels = FWetClothingAssetMeshAnalyzer::GetNumUVChannels(TargetMesh, 0);

    if (NumUVChannels > WetWrinkleFixedUVChannelIndex)
    {
        MeshUVChannelOptions.Add(MakeShared<int32>(WetWrinkleFixedUVChannelIndex));
    }

    SelectedMeshUVChannelIndex = NumUVChannels > WetWrinkleFixedUVChannelIndex ? WetWrinkleFixedUVChannelIndex : INDEX_NONE;
    if (UWetClothingAsset* MutableAsset = WetClothingAsset.Get())
    {
        MutableAsset->WrinkleData.WrinkleUVChannelIndex = WetWrinkleFixedUVChannelIndex;
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

    return NumUVChannels > WetWrinkleFixedUVChannelIndex ? WetWrinkleFixedUVChannelIndex : INDEX_NONE;
}

int32 SWetWrinkleEditorPanel::GetProtectedBaseUVChannelCount() const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const USkeletalMesh* TargetMesh = Asset != nullptr ? Asset->TargetMesh.Get() : nullptr;
    return FWetClothingAssetMeshAnalyzer::GetNumUVChannels(TargetMesh, 0);
}

bool SWetWrinkleEditorPanel::IsUVChannelDeleteAllowed(int32 UVChannelIndex) const
{
    return false;
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
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                       [FWetClothingEditorCommonWidgets::BuildUVViewTextureAndViewRow(
                           SAssignNew(TextureSelectionContainer, SBox),
                           FWetClothingEditorCommonWidgets::BuildUVViewOptionsButton(
                                      &UVDisplayModeItems,
                                      SelectedUVDisplayModeItem,
                                      TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &SWetWrinkleEditorPanel::GetSelectedUVDisplayModeText)),
                                      [this](FUVDisplayModeItemPtr Item)
                                      {
                                          HandleUVDisplayModeSelectionChanged(Item, ESelectInfo::Direct);
                                      },
                                      TAttribute<float>::Create(TAttribute<float>::FGetter::CreateSP(this, &SWetWrinkleEditorPanel::GetUVViewBackgroundTextureOpacity)),
                                      [this](float NewValue)
                                      {
                                          HandleUVViewBackgroundTextureOpacityChanged(NewValue);
                                      },
                                      TAttribute<float>::Create(TAttribute<float>::FGetter::CreateSP(this, &SWetWrinkleEditorPanel::GetUVViewIslandLineOpacity)),
                                      [this](float NewValue)
                                      {
                                          HandleUVViewIslandLineOpacityChanged(NewValue);
                                      },
                                      TAttribute<float>::Create(TAttribute<float>::FGetter::CreateSP(this, &SWetWrinkleEditorPanel::GetUVViewIslandLineThicknessScale)),
                                      [this](float NewValue)
                                      {
                                          HandleUVViewIslandLineThicknessScaleChanged(NewValue);
                                      }))]

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
    UTexture* BackgroundTexture = ResolveTextureAddressTexture();

    WrinkleUVView->SetDisplayMode(CurrentUVDisplayMode);
    WrinkleUVView->SetBackgroundTextureOpacity(UVViewBackgroundTextureOpacity);
    WrinkleUVView->SetUVIslandLineOpacity(UVViewIslandLineOpacity);
    WrinkleUVView->SetUVIslandLineThicknessScale(UVViewIslandLineThicknessScale);
    WrinkleUVView->SetNormalizeToContentBounds(true);
    // Important: set the texture before SetIslands(). SWetClothingAssetUVView uses the
    // selected texture address mode to normalize wrapped / mirrored UV islands for display.
    // If the texture is changed after the islands are cached, the background and island lines
    // drift apart visually.
    WrinkleUVView->SetBackgroundTexture(BackgroundTexture);
    WrinkleUVView->SetDrawBackgroundTexture(bShowMaterialTextureInUVView && BackgroundTexture != nullptr);

    if (MaterialSlotIndex == INDEX_NONE)
    {
        WrinkleUVIslandItems.Reset();
        CachedWrinkleUVViewChannelIndex = UVChannelIndex;
        CachedWrinkleUVViewMaterialSlotIndex = MaterialSlotIndex;
        WrinkleUVView->SetBackgroundTexture(nullptr);
        WrinkleUVView->SetDrawBackgroundTexture(false);
        WrinkleUVView->SetIslands(TArray<TSharedPtr<FWetClothingAssetUVIsland>>());
        WrinkleUVView->SetIslandColors(TMap<int32, FLinearColor>());
        WrinkleUVView->SetHiddenUVIslandIDs(TSet<int32>());
        WrinkleUVView->SetSelectedIslands(TSet<int32>());
        WrinkleUVView->SetCircleMarkers(TArray<FWetClothingAssetUVViewCircleMarker>());
        return;
    }

    const bool bNeedsIslandRebuild = CachedWrinkleUVViewChannelIndex != UVChannelIndex ||
                                     CachedWrinkleUVViewMaterialSlotIndex != MaterialSlotIndex ||
                                     WrinkleUVIslandItems.Num() == 0;

    if (bNeedsIslandRebuild)
    {
        WrinkleUVIslandItems.Reset();
        CachedWrinkleUVViewChannelIndex = UVChannelIndex;
        CachedWrinkleUVViewMaterialSlotIndex = MaterialSlotIndex;

        if (TargetMesh != nullptr &&
            UVChannelIndex >= 0 &&
            UVChannelIndex < NumUVChannels)
        {
            const int32 MaterialCount = TargetMesh->GetMaterials().Num();
            const int32 FirstSlotIndex = MaterialSlotIndex == INDEX_NONE ? 0 : MaterialSlotIndex;
            const int32 SlotCount = MaterialSlotIndex == INDEX_NONE ? MaterialCount : 1;
            int32       NextUVIslandID = 0;

            for (int32 SlotOffset = 0; SlotOffset < SlotCount; ++SlotOffset)
            {
                const int32 SlotIndex = FirstSlotIndex + SlotOffset;
                TArray<FWetClothingAssetUVIsland> BuiltIslands;
                if (FWetClothingAssetMeshAnalyzer::BuildMaterialSlotUVIslands(TargetMesh, 0, UVChannelIndex, SlotIndex, BuiltIslands, nullptr))
                {
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
        }

    }

    TMap<int32, FLinearColor> WrinkleUVIslandColors;
    for (const TSharedPtr<FWetClothingAssetUVIsland>& IslandItem : WrinkleUVIslandItems)
    {
        if (IslandItem.IsValid())
        {
            WrinkleUVIslandColors.Add(IslandItem->UVIslandID, FLinearColor::White);
        }
    }

    // Always re-apply islands after setting the texture. This keeps UV island display coordinates
    // synchronized with the currently selected background texture and its address mode, even when
    // only the texture dropdown changes and the cached island list is reused.
    WrinkleUVView->SetIslands(WrinkleUVIslandItems);
    WrinkleUVView->SetIslandColors(WrinkleUVIslandColors);
    WrinkleUVView->SetHiddenUVIslandIDs(TSet<int32>());
    WrinkleUVView->SetSelectedIslands(TSet<int32>());

    RebuildWrinkleUVViewPatchMarkerCache();
    RefreshWrinkleUVViewMarkersOnly();
}

void SWetWrinkleEditorPanel::RebuildWrinkleUVViewPatchMarkerCache()
{
    CachedWrinkleUVViewPatchMarkers.Reset();

    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const int32 MaterialSlotIndex = BrushSettings.MaterialSlotIndex;
    const int32 UVChannelIndex = GetWrinkleUVViewChannelIndex();
    CachedWrinkleUVViewPatchMarkerMaterialSlotIndex = MaterialSlotIndex;
    CachedWrinkleUVViewPatchMarkerChannelIndex = UVChannelIndex;

    if (Asset == nullptr || MaterialSlotIndex == INDEX_NONE || UVChannelIndex == INDEX_NONE)
    {
        return;
    }

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
            CachedWrinkleUVViewPatchMarkers.Add(Marker);
        }
    }
}

void SWetWrinkleEditorPanel::RefreshWrinkleUVViewMarkersOnly()
{
    if (!WrinkleUVView.IsValid())
    {
        return;
    }

    const int32 MaterialSlotIndex = BrushSettings.MaterialSlotIndex;
    const int32 UVChannelIndex = GetWrinkleUVViewChannelIndex();
    if (CachedWrinkleUVViewPatchMarkerMaterialSlotIndex != MaterialSlotIndex ||
        CachedWrinkleUVViewPatchMarkerChannelIndex != UVChannelIndex)
    {
        RebuildWrinkleUVViewPatchMarkerCache();
    }

    TArray<FWetClothingAssetUVViewCircleMarker> CircleMarkers = CachedWrinkleUVViewPatchMarkers;
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

    if (UVChannelIndex == WetWrinkleFixedUVChannelIndex)
    {
        return LOCTEXT("OriginalMeshUVChannel0Label", "UV 0 (Original)");
    }

    return FText::Format(LOCTEXT("DisabledMeshUVChannelLabel", "UV {0} (Disabled)"), FText::AsNumber(UVChannelIndex));
}

TSharedRef<SWidget> SWetWrinkleEditorPanel::GenerateMeshUVChannelComboRow(TSharedPtr<int32> Item) const
{
    const int32 UVChannelIndex = Item.IsValid() ? *Item : INDEX_NONE;
    return SNew(STextBlock)
        .Text(GetMeshUVChannelDisplayText(UVChannelIndex));
}

TSharedRef<SWidget> SWetWrinkleEditorPanel::GenerateUVDisplayModeComboItem(FUVDisplayModeItemPtr Item) const
{
    return FWetClothingEditorCommonWidgets::GenerateUVDisplayModeComboItem(Item);
}

void SWetWrinkleEditorPanel::HandleUVDisplayModeSelectionChanged(FUVDisplayModeItemPtr Item, ESelectInfo::Type SelectInfo)
{
    if (!Item.IsValid())
    {
        return;
    }

    SelectedUVDisplayModeItem = Item;
    CurrentUVDisplayMode = *Item;

    if (WrinkleUVView.IsValid())
    {
        WrinkleUVView->SetDisplayMode(CurrentUVDisplayMode);
    }
}

FText SWetWrinkleEditorPanel::GetSelectedUVDisplayModeText() const
{
    return FWetClothingEditorCommonWidgets::GetUVDisplayModeLabel(
        SelectedUVDisplayModeItem.IsValid() ? *SelectedUVDisplayModeItem : EWetClothingAssetUVDisplayMode::Normal);
}

float SWetWrinkleEditorPanel::GetUVViewBackgroundTextureOpacity() const
{
    return UVViewBackgroundTextureOpacity;
}

float SWetWrinkleEditorPanel::GetUVViewIslandLineOpacity() const
{
    return UVViewIslandLineOpacity;
}

float SWetWrinkleEditorPanel::GetUVViewIslandLineThicknessScale() const
{
    return UVViewIslandLineThicknessScale;
}

void SWetWrinkleEditorPanel::HandleUVViewBackgroundTextureOpacityChanged(float NewValue)
{
    UVViewBackgroundTextureOpacity = FMath::Clamp(NewValue, 0.0f, 1.0f);
    if (WrinkleUVView.IsValid())
    {
        WrinkleUVView->SetBackgroundTextureOpacity(UVViewBackgroundTextureOpacity);
    }
}

void SWetWrinkleEditorPanel::HandleUVViewIslandLineOpacityChanged(float NewValue)
{
    UVViewIslandLineOpacity = FMath::Clamp(NewValue, 0.0f, 1.0f);
    if (WrinkleUVView.IsValid())
    {
        WrinkleUVView->SetUVIslandLineOpacity(UVViewIslandLineOpacity);
    }
}

void SWetWrinkleEditorPanel::HandleUVViewIslandLineThicknessScaleChanged(float NewValue)
{
    UVViewIslandLineThicknessScale = FMath::Clamp(NewValue, 0.25f, 6.0f);
    if (WrinkleUVView.IsValid())
    {
        WrinkleUVView->SetUVIslandLineThicknessScale(UVViewIslandLineThicknessScale);
    }
}


void SWetWrinkleEditorPanel::HandleMeshUVChannelComboChanged(TSharedPtr<int32> Item, ESelectInfo::Type SelectInfo)
{
    const int32 NewUVChannelIndex = WetWrinkleFixedUVChannelIndex;
    SelectedMeshUVChannelIndex = NewUVChannelIndex;
    BrushSettings.UVChannelIndex = NewUVChannelIndex;

    if (UWetClothingAsset* Asset = WetClothingAsset.Get())
    {
        if (Asset->WrinkleData.WrinkleUVChannelIndex != WetWrinkleFixedUVChannelIndex)
        {
            Asset->Modify();
            Asset->WrinkleData.WrinkleUVChannelIndex = WetWrinkleFixedUVChannelIndex;
            MarkAssetEdited();
        }
    }

    CurrentHit = FWetWrinkleSurfaceHit();
    bHasLastStamp = false;
    LastStampUVChannelIndex = INDEX_NONE;
    InvalidateWrinkleUVViewCache();
    PushBrushSettingsToViewport();
    RefreshPartMapItems();
    RefreshWrinkleUVView();
}

bool SWetWrinkleEditorPanel::IsDeleteMeshUVChannelEnabled() const
{
    return false;
}

FReply SWetWrinkleEditorPanel::HandleDeleteMeshUVChannelClicked()
{
    FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("DeleteMeshUVChannelDisabled", "DWC is locked to UV 0 right now, so the wrinkle editor will not delete or modify mesh UV channels."));
    return FReply::Handled();
}

FText SWetWrinkleEditorPanel::GetWrinkleUVChannelText() const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const USkeletalMesh* TargetMesh = Asset != nullptr ? Asset->TargetMesh.Get() : nullptr;
    const int32 MaterialSlotIndex = BrushSettings.MaterialSlotIndex;
    const int32 UVChannelIndex = Asset != nullptr ? WetWrinkleFixedUVChannelIndex : INDEX_NONE;

    if (TargetMesh == nullptr)
    {
        return LOCTEXT("WrinkleUVNoTargetMeshText", "Assign a Target Mesh first. Wrinkle editing uses UV 0.");
    }

    if (MaterialSlotIndex == INDEX_NONE)
    {
        return LOCTEXT(
            "WrinkleUVNoSelectedSlotText",
            "Select a wettable material slot. Wrinkle editing uses UV 0.");
    }

    if (!FWetClothingEditorCommonWidgets::IsMaterialSlotWettable(Asset, MaterialSlotIndex))
    {
        return FText::Format(
            LOCTEXT("WrinkleUVSlotNotWettableText", "Slot {0} is not wettable. Enable wettable for this slot before editing wrinkles."),
            FText::AsNumber(MaterialSlotIndex));
    }

    if (UVChannelIndex == INDEX_NONE)
    {
        return FText::Format(
            LOCTEXT("WrinkleUVChannelNotGeneratedText", "Slot {0}: UV 0 is not available on the target mesh."),
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

    return FText::Format(
        LOCTEXT(
            "WrinkleUVChannelGeneratedText",
            "Slot {0}: UV Channel {1}. Wrinkle editing is locked to imported UV 0."),
        FText::AsNumber(MaterialSlotIndex),
        FText::AsNumber(UVChannelIndex));
}

FReply SWetWrinkleEditorPanel::HandleGenerateWrinkleUVChannelClicked()
{
    const int32 MaterialSlotIndex = BrushSettings.MaterialSlotIndex;
    if (MaterialSlotIndex == INDEX_NONE)
    {
        FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("GenerateWrinkleUVNoSelectedSlot", "Select a material slot before editing wrinkles on UV 0."));
        return FReply::Handled();
    }

    const bool bUsable = EnsureWrinkleUVChannelForMaterialSlot(MaterialSlotIndex, true);
    if (bUsable)
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
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr)
    {
        FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("GenerateWrinkleTextureNoAsset", "Wet Clothing Asset is unavailable."));
        return FReply::Handled();
    }

    const int32 MaterialSlotIndex = BrushSettings.MaterialSlotIndex;
    if (MaterialSlotIndex == INDEX_NONE)
    {
        FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("GenerateWrinkleTextureNoSlot", "Select a single material slot before generating wrinkle textures. All Slots is preview-only."));
        return FReply::Handled();
    }

    const int32 UVChannelIndex = WetWrinkleFixedUVChannelIndex;
    if (!HasUsableWrinkleUVChannel())
    {
        FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("GenerateWrinkleTextureNoUV", "The target mesh does not have UV channel 0."));
        return FReply::Handled();
    }

    RefreshBrushPresetOptions();
    if (BrushPresetOptions.Num() == 0)
    {
        FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("GenerateWrinkleTextureNoBaseNormal", "No base normal texture presets were found."));
        return FReply::Handled();
    }

    TSharedRef<SWindow> DialogWindow = SNew(SWindow)
        .Title(LOCTEXT("GenerateWrinkleTexturesWindowTitle", "Generate Wrinkle Textures"))
        .ClientSize(FVector2D(1040.0f, 720.0f))
        .SupportsMaximize(false)
        .SupportsMinimize(false);

    TSharedPtr<SWetWrinkleTextureGeneratorDialog> DialogWidget;
    DialogWindow->SetContent(
        SAssignNew(DialogWidget, SWetWrinkleTextureGeneratorDialog)
            .ParentWindow(DialogWindow)
            .WetClothingAsset(Asset)
            .MaterialSlotIndex(MaterialSlotIndex)
            .UVChannelIndex(UVChannelIndex)
            .Resolution(Asset->WrinkleData.BakeSettings.DefaultResolution)
            .BaseNormalOptions(BrushPresetOptions));

    FSlateApplication::Get().AddModalWindow(DialogWindow, nullptr);

    if (DialogWidget.IsValid() && DialogWidget->WasApplied())
    {
        UTexture2D* GeneratedNormalTexture = DialogWidget->GetGeneratedNormalTexture();
        if (GeneratedNormalTexture != nullptr && PreviewViewport.IsValid())
        {
            PreviewViewport->SetGeneratedNormalPreviewTexture(MaterialSlotIndex, UVChannelIndex, GeneratedNormalTexture);
        }
    }

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
    RefreshMaterialTextures();
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
    RefreshMaterialTextures();
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

    RefreshMaterialSlotOptions();
    if (DetailsView.IsValid())
    {
        DetailsView->ForceRefresh();
    }

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
    const int32 NewUVChannelIndex = WetWrinkleFixedUVChannelIndex;
    BrushSettings.UVChannelIndex = NewUVChannelIndex;
    if (UWetClothingAsset* Asset = WetClothingAsset.Get())
    {
        if (Asset->WrinkleData.WrinkleUVChannelIndex != WetWrinkleFixedUVChannelIndex)
        {
            Asset->Modify();
            Asset->WrinkleData.WrinkleUVChannelIndex = WetWrinkleFixedUVChannelIndex;
            MarkAssetEdited();
        }
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

void SWetWrinkleEditorPanel::HandlePreviewWetnessChanged(float NewValue)
{
    BrushSettings.PreviewWetness = FMath::Clamp(NewValue, 0.0f, 1.0f);
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
    return FWetClothingMaterialTextureResolver::ResolveOrSaveTextureSelection(
        const_cast<UWetClothingAsset*>(WetClothingAsset.Get()),
        MaterialSlotIndex,
        UVChannelIndex);
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
        RefreshWrinkleUVViewMarkersOnly();
        return;
    }

    PreviewViewport->ClearExternalBrushPreview();
    CurrentHit = FWetWrinkleSurfaceHit();
    RefreshWrinkleUVViewMarkersOnly();
}

void SWetWrinkleEditorPanel::HandleTextureUVHoverEnded()
{
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->ClearExternalBrushPreview();
    }
    CurrentHit = FWetWrinkleSurfaceHit();
    RefreshWrinkleUVViewMarkersOnly();
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
