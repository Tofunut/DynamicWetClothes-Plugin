#include "SWetWrinkleEditorPanel.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Core/DWCEditorUtils.h"
#include "DataAssets/WetClothingAsset.h"
#include "DataAssets/WetWrinklePreset.h"
#include "Editor.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "AssetThumbnail.h"
#include "Brushes/SlateRoundedBoxBrush.h"
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
#include "Subsystems/AssetEditorSubsystem.h"
#include "Types/WidgetActiveTimerDelegate.h"
#include "WetClothing/Foundation/MeshAnalysis/WetClothingAssetMeshAnalyzer.h"
#include "WetClothing/Foundation/TextureAccess/WetClothingMaterialTextureResolver.h"
#include "WetClothing/WCAEditor/UI/Widgets/WCAEditorWidgets.h"
#include "WetClothing/Modes/Part/Partition/WetPartEditingService.h"
#include "WetClothing/DerivedAssets/Textures/Wrinkle/WetWrinkleNormalMapBaker.h"
#include "WetClothing/Modes/Wrinkle/Generate/WetWrinkleTextureGenerator.h"
#include "WetClothing/Modes/Wrinkle/Viewport/WetWrinkleViewport.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "UObject/GCObject.h"
#include "Widgets/SWindow.h"
#include "Framework/Application/SlateApplication.h"

#define LOCTEXT_NAMESPACE "WCAEditorPanel"

namespace
{
    float WrapWetRidgeDelta(const float Delta)
    {
        return Delta - FMath::RoundToFloat(Delta);
    }

    float WrapWetRidgeUnit(float Value)
    {
        Value = FMath::Fmod(Value, 1.0f);
        return Value < 0.0f ? Value + 1.0f : Value;
    }

    constexpr const TCHAR* WetWrinkleBaseNormalTextureFolderPath = TEXT("/DynamicWetClothes/Presets/WrinkleTextures");
    constexpr float WetWrinkleDefaultSizeCm = 8.0f;
    constexpr float WetWrinkleDefaultSizeUV = 0.0677f;
    constexpr float WetWrinkleUVPerCm = WetWrinkleDefaultSizeUV / WetWrinkleDefaultSizeCm;
    int32 ResolveWetWrinkleUVChannel(const UWetClothingAsset* Asset)
    {
        return Asset != nullptr ? Asset->GetDWCDataUVChannelIndex() : INDEX_NONE;
    }
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
            Collector.AddReferencedObject(WetClothingAsset);
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
        TObjectPtr<UWetClothingAsset> WetClothingAsset = nullptr;
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
    if (GEditor != nullptr)
    {
        GEditor->RegisterForUndo(this);
    }

    UVDisplayModeItems.Reset();
    UVDisplayModeItems.Add(MakeShared<EWCAUVDisplayMode>(EWCAUVDisplayMode::Normal));
    UVDisplayModeItems.Add(MakeShared<EWCAUVDisplayMode>(EWCAUVDisplayMode::OutlineOnly));
    SelectedUVDisplayModeItem = UVDisplayModeItems[0];
    CurrentUVDisplayMode = EWCAUVDisplayMode::Normal;

    if (UWetClothingAsset* Asset = WetClothingAsset.Get())
    {
        Asset->WrinkleData.WrinkleUVChannelIndex = ResolveWetWrinkleUVChannel(WetClothingAsset.Get());
        BrushSettings.UVChannelIndex = BrushSettings.MaterialSlotIndex != INDEX_NONE ? ResolveWetWrinkleUVChannel(WetClothingAsset.Get()) : INDEX_NONE;
    }
    RefreshMaterialSlotOptions();
    RefreshDWCDataUVChannel();
    RefreshMaterialTextures();
    RefreshBrushPresetOptions();
    RefreshWrinklePresetPalette();
    SizeCm = WetWrinkleDefaultSizeCm;
    SizeUV = WetWrinkleDefaultSizeUV;
    BrushSettings.BrushRadiusUV = SizeUV;
    SelectedWrinklePresetThumbnailBrush.SetImageSize(FVector2D(128.0f, 128.0f));
    WrinklePresetPaletteButtonStyle = FButtonStyle()
        .SetNormal(FSlateRoundedBoxBrush(FLinearColor::White, 6.0f))
        .SetHovered(FSlateRoundedBoxBrush(FLinearColor(1.15f, 1.15f, 1.15f, 1.0f), 6.0f))
        .SetPressed(FSlateRoundedBoxBrush(FLinearColor(0.85f, 0.85f, 0.85f, 1.0f), 6.0f))
        .SetDisabled(FSlateRoundedBoxBrush(FLinearColor::White, 6.0f))
        .SetNormalPadding(FMargin(0.0f))
        .SetPressedPadding(FMargin(0.0f));
    // Preset palette refresh is event-driven through Asset Registry notifications.
    // Avoid polling every second while the editor is open.
    FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    AssetRegistryModule.Get().OnAssetRemoved().AddSP(this, &SWetWrinkleEditorPanel::HandleWrinklePresetPaletteAssetRemoved);
    AssetRegistryModule.Get().OnAssetUpdated().AddSP(this, &SWetWrinkleEditorPanel::HandleWrinklePresetPaletteAssetUpdated);

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
                                              .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                                                  [SNew(SHorizontalBox)

                                                   + SHorizontalBox::Slot()
                                                         .AutoWidth()
                                                         .VAlign(VAlign_Center)
                                                         .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                                                             [SNew(STextBlock)
                                                                  .Text(LOCTEXT("UVChannelLabel", "DWC Data UV"))]

                                                   + SHorizontalBox::Slot()
                                                         .FillWidth(1.0f)
                                                         .VAlign(VAlign_Center)
                                                             [SNew(SBorder)
                                                                  .Padding(FMargin(8.0f, 3.0f))
                                                                  .BorderImage(FAppStyle::Get().GetBrush(TEXT("ToolPanel.GroupBorder")))
                                                                      [SNew(STextBlock)
                                                                           .Text(this, &SWetWrinkleEditorPanel::GetDWCDataUVChannelText)
                                                                           .ToolTipText(LOCTEXT(
                                                                               "DWCDataUVReadOnlyTooltip",
                                                                               "The generated DWC Data UV channel configured by Asset Setup."))]]]

                                        + SVerticalBox::Slot()
                                              .FillHeight(1.0f)
                                                  [SNew(SSplitter)
                                                       .Orientation(Orient_Vertical)

                                                   + SSplitter::Slot()
                                                         .Value(0.52f)
                                                             [SNew(SVerticalBox)

                                                              + SVerticalBox::Slot()
                                                                    .AutoHeight()
                                                                    .Padding(0.0f, FWCAEditorWidgets::MaterialSlotListHeaderTopPadding, 0.0f, 4.0f)
                                                                        [FWCAEditorWidgets::BuildSectionHeader(
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
                                                                    .Padding(0.0f, 0.0f, 0.0f, FWCAEditorWidgets::MaterialSlotListSeparatorBottomPadding)
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
                                                                        [FWCAEditorWidgets::BuildSectionHeader(
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
                          .Value(0.44f)
                              [FWCAEditorWidgets::BuildPreviewSection(
                                  SNew(SSplitter)
                                      .Orientation(Orient_Vertical)

                                      + SSplitter::Slot()
                                            .Value(0.68f)
                                                [SAssignNew(PreviewViewport, SWetWrinkleViewport)
                                                     .WetClothingAsset(WetClothingAsset.Get())
                                                     .OnSurfaceHitChanged(FOnWetWrinkleSurfaceHitChanged::CreateSP(this, &SWetWrinkleEditorPanel::HandleSurfaceHitChanged))
                                                     .OnPaintStrokeStarted(FOnWetWrinklePaintStrokeStarted::CreateSP(this, &SWetWrinkleEditorPanel::HandlePaintStrokeStarted))
                                                     .OnPaintStampRequested(FOnWetWrinklePaintStampRequested::CreateSP(this, &SWetWrinkleEditorPanel::HandlePaintStampRequested))
                                                     .OnPaintStrokeEnded(FOnWetWrinklePaintStrokeEnded::CreateSP(this, &SWetWrinkleEditorPanel::HandlePaintStrokeEnded))
                                                     .OnPaintStrokeCanceled(FOnWetWrinklePaintStrokeCanceled::CreateSP(this, &SWetWrinkleEditorPanel::HandlePaintStrokeCanceled))]

                                      + SSplitter::Slot()
                                            .Value(0.32f)
                                                [BuildWrinkleUVViewSection()],
                                  FOnWetClothingPreviewFocusClicked::CreateSP(this, &SWetWrinkleEditorPanel::HandleFocusClicked))]

                    + SSplitter::Slot()
                          .Value(0.28f)
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
            [SNew(SScrollBox)
             + SScrollBox::Slot()
                   [SNew(SVerticalBox)

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                       [SNew(STextBlock)
                            .Text(this, &SWetWrinkleEditorPanel::GetBrushSectionHeadingText)
                            .Font(SectionHeadingFont)]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                       [SNew(SHorizontalBox)

                        + SHorizontalBox::Slot()
                              .FillWidth(1.0f)
                                  [SNew(SCheckBox)
                                       .Style(FAppStyle::Get(), "ToggleButtonCheckbox")
                                       .HAlign(HAlign_Center)
                                       .IsChecked(this, &SWetWrinkleEditorPanel::GetToolModeCheckState, EWetWrinkleToolMode::Patch)
                                       .OnCheckStateChanged(this, &SWetWrinkleEditorPanel::HandleToolModeChanged, EWetWrinkleToolMode::Patch)
                                           [SNew(STextBlock)
                                                .Text(LOCTEXT("PatchToolMode", "Patch"))]]

                        + SHorizontalBox::Slot()
                              .FillWidth(1.0f)
                              .Padding(4.0f, 0.0f, 0.0f, 0.0f)
                                  [SNew(SCheckBox)
                                       .Style(FAppStyle::Get(), "ToggleButtonCheckbox")
                                       .HAlign(HAlign_Center)
                                       .IsChecked(this, &SWetWrinkleEditorPanel::GetToolModeCheckState, EWetWrinkleToolMode::ProceduralRidgeStroke)
                                       .OnCheckStateChanged(this, &SWetWrinkleEditorPanel::HandleToolModeChanged, EWetWrinkleToolMode::ProceduralRidgeStroke)
                                           [SNew(STextBlock)
                                                .Text(LOCTEXT("RidgeStrokeToolMode", "Ridge Stroke"))]]]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                       [SNew(SBox)
                            .Visibility(this, &SWetWrinkleEditorPanel::GetProceduralRidgeToolVisibility)
                                [SNew(SHorizontalBox)

                        + SHorizontalBox::Slot()
                              .FillWidth(1.0f)
                                  [SNew(SCheckBox)
                                       .Style(FAppStyle::Get(), "ToggleButtonCheckbox")
                                       .HAlign(HAlign_Center)
                                       .IsChecked(this, &SWetWrinkleEditorPanel::GetRidgeEditModeCheckState, EWetProceduralRidgeEditMode::Draw)
                                       .OnCheckStateChanged(this, &SWetWrinkleEditorPanel::HandleRidgeEditModeChanged, EWetProceduralRidgeEditMode::Draw)
                                           [SNew(STextBlock).Text(LOCTEXT("RidgeDrawMode", "Draw"))]]

                        + SHorizontalBox::Slot()
                              .FillWidth(1.0f)
                              .Padding(4.0f, 0.0f, 0.0f, 0.0f)
                                  [SNew(SCheckBox)
                                       .Style(FAppStyle::Get(), "ToggleButtonCheckbox")
                                       .HAlign(HAlign_Center)
                                       .IsChecked(this, &SWetWrinkleEditorPanel::GetRidgeEditModeCheckState, EWetProceduralRidgeEditMode::Edit)
                                       .OnCheckStateChanged(this, &SWetWrinkleEditorPanel::HandleRidgeEditModeChanged, EWetProceduralRidgeEditMode::Edit)
                                           [SNew(STextBlock).Text(LOCTEXT("RidgeEditMode", "Edit"))]]]]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                       [SNew(SCheckBox)
                            .Visibility(this, &SWetWrinkleEditorPanel::GetProceduralRidgeToolVisibility)
                            .IsChecked(this, &SWetWrinkleEditorPanel::GetRidgeJunctionModeCheckState)
                            .OnCheckStateChanged(this, &SWetWrinkleEditorPanel::HandleRidgeJunctionModeChanged)
                            .ToolTipText(LOCTEXT("RidgeJunctionModeTooltip", "Automatically snap ridge endpoints to nearby ridge strokes and store a junction connection."))
                                [SNew(STextBlock).Text(LOCTEXT("RidgeJunctionMode", "Junction Mode"))]]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 4.0f)
                       [SNew(STextBlock)
                            .Visibility(this, &SWetWrinkleEditorPanel::GetProceduralRidgeToolVisibility)
                            .Text(LOCTEXT("RidgeShapeLabel", "Shape"))]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                       [SNew(SBox)
                            .Visibility(this, &SWetWrinkleEditorPanel::GetProceduralRidgeToolVisibility)
                                [SNew(SHorizontalBox)

                                 + SHorizontalBox::Slot()
                                       .FillWidth(1.0f)
                                           [SNew(SCheckBox)
                                                .Style(FAppStyle::Get(), "ToggleButtonCheckbox")
                                                .HAlign(HAlign_Center)
                                                .IsChecked(this, &SWetWrinkleEditorPanel::GetRidgeShapeCheckState, EWetProceduralRidgeShape::Convex)
                                                .OnCheckStateChanged(this, &SWetWrinkleEditorPanel::HandleRidgeShapeChanged, EWetProceduralRidgeShape::Convex)
                                                    [SNew(STextBlock).Text(LOCTEXT("RidgeShapeConvex", "Convex"))]]

                                 + SHorizontalBox::Slot()
                                       .FillWidth(1.0f)
                                       .Padding(4.0f, 0.0f, 0.0f, 0.0f)
                                           [SNew(SCheckBox)
                                                .Style(FAppStyle::Get(), "ToggleButtonCheckbox")
                                                .HAlign(HAlign_Center)
                                                .IsChecked(this, &SWetWrinkleEditorPanel::GetRidgeShapeCheckState, EWetProceduralRidgeShape::Concave)
                                                .OnCheckStateChanged(this, &SWetWrinkleEditorPanel::HandleRidgeShapeChanged, EWetProceduralRidgeShape::Concave)
                                                    [SNew(STextBlock).Text(LOCTEXT("RidgeShapeConcave", "Concave"))]]

                                 + SHorizontalBox::Slot()
                                       .FillWidth(1.0f)
                                       .Padding(4.0f, 0.0f, 0.0f, 0.0f)
                                           [SNew(SCheckBox)
                                                .Style(FAppStyle::Get(), "ToggleButtonCheckbox")
                                                .HAlign(HAlign_Center)
                                                .IsChecked(this, &SWetWrinkleEditorPanel::GetRidgeShapeCheckState, EWetProceduralRidgeShape::Fold)
                                                .OnCheckStateChanged(this, &SWetWrinkleEditorPanel::HandleRidgeShapeChanged, EWetProceduralRidgeShape::Fold)
                                                    [SNew(STextBlock).Text(LOCTEXT("RidgeShapeFold", "Fold"))]]]]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                       [SNew(SCheckBox)
                            .Visibility(this, &SWetWrinkleEditorPanel::GetFoldOptionsVisibility)
                            .IsChecked(this, &SWetWrinkleEditorPanel::GetFlipFoldSideCheckState)
                            .OnCheckStateChanged(this, &SWetWrinkleEditorPanel::HandleFlipFoldSideChanged)
                                [SNew(STextBlock)
                                     .Text(LOCTEXT("FlipRidgeFoldSide", "Flip Fold Side"))]]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                       [SNew(STextBlock)
                            .Text(LOCTEXT("WetWrinklePresetLabel", "Wet Wrinkle Preset"))]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                       [SNew(SHorizontalBox)

                        + SHorizontalBox::Slot()
                              .FillWidth(1.0f)
                                  [SNew(SObjectPropertyEntryBox)
                                       .IsEnabled_Lambda([this]() { return BrushSettings.ToolMode == EWetWrinkleToolMode::Patch; })
                                       .AllowedClass(UWetWrinklePreset::StaticClass())
                                       .ObjectPath(this, &SWetWrinkleEditorPanel::GetWrinklePresetObjectPath)
                                       .OnObjectChanged(this, &SWetWrinkleEditorPanel::HandleWrinklePresetChanged)]

                        + SHorizontalBox::Slot()
                              .AutoWidth()
                              .Padding(0.0f)
                              .VAlign(VAlign_Center)
                                  [SNew(SBox)
                                       .WidthOverride(18.0f)
                                       .HeightOverride(18.0f)
                                           [SNew(SButton)
                                                .ButtonStyle(FAppStyle::Get(), "SimpleButton")
                                                .ContentPadding(0.0f)
                                                .ToolTipText(LOCTEXT("RefreshWrinklePresetPaletteTooltip", "Refresh the Wet Wrinkle Preset palette."))
                                                .OnClicked(this, &SWetWrinkleEditorPanel::HandleRefreshWrinklePresetPaletteClicked)
                                                    [SNew(SImage)
                                                         .Image(FAppStyle::GetBrush("Icons.Refresh"))
                                                         .ColorAndOpacity(FSlateColor::UseForeground())]]]]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                       [SNew(SBox)
                            .IsEnabled_Lambda([this]() { return BrushSettings.ToolMode == EWetWrinkleToolMode::Patch; })
                                [BuildWrinklePresetPalette()]]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                       [SNew(SHorizontalBox)

                        + SHorizontalBox::Slot()
                              .FillWidth(1.0f)
                              .VAlign(VAlign_Center)
                                  [SNew(STextBlock)
                                       .AutoWrapText(true)
                                       .ColorAndOpacity(this, &SWetWrinkleEditorPanel::GetWrinklePresetStatusColor)
                                       .Text(this, &SWetWrinkleEditorPanel::GetWrinklePresetStatusText)]

                        + SHorizontalBox::Slot()
                              .AutoWidth()
                              .VAlign(VAlign_Center)
                              .Padding(6.0f, 0.0f, 0.0f, 0.0f)
                                      [SNew(SButton)
                                       .IsEnabled_Lambda([this]() { return BrushSettings.ToolMode == EWetWrinkleToolMode::Patch && CanOpenWrinklePreset(); })
                                       .Text(LOCTEXT("OpenWrinklePresetButton", "Open"))
                                       .OnClicked(this, &SWetWrinkleEditorPanel::HandleOpenWrinklePresetClicked)]]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                       [SNew(SVerticalBox)

                        + SVerticalBox::Slot()
                              .AutoHeight()
                              .Padding(0.0f, 0.0f, 0.0f, 4.0f)
                                  [SNew(STextBlock)
                                       .Text(LOCTEXT("SelectedWrinklePresetThumbnailLabel", "Wrinkle Normal"))]

                        + SVerticalBox::Slot()
                              .AutoHeight()
                                  [SNew(SBox)
                                           [SNew(SBorder)
                                                .Padding(1.0f)
                                                .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                                                .BorderBackgroundColor(FLinearColor(0.45f, 0.45f, 0.45f))
                                                    [SNew(SBorder)
                                                         .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
                                                         .BorderBackgroundColor(FLinearColor::Black)
                                                             [SNew(SScaleBox)
                                                                  .Stretch(EStretch::ScaleToFitX)
                                                                  .StretchDirection(EStretchDirection::DownOnly)
                                                                      [SNew(SImage)
                                                                           .Image(this, &SWetWrinkleEditorPanel::GetWrinklePresetThumbnailBrush)
                                                                           .Visibility(this, &SWetWrinkleEditorPanel::GetWrinklePresetThumbnailVisibility)]]]]]]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                       [SNew(STextBlock)
                            .Text(this, &SWetWrinkleEditorPanel::GetBrushSizeLabelText)]

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
                                                .OnBeginSliderMovement(this, &SWetWrinkleEditorPanel::HandleRidgePropertySliderBegin)
                                                .OnEndSliderMovement(this, &SWetWrinkleEditorPanel::HandleRidgePropertySliderEnd)
                                                .OnValueCommitted(this, &SWetWrinkleEditorPanel::HandleRidgePropertyCommitted)
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
                            .Value(this, &SWetWrinkleEditorPanel::GetRidgeStrengthValue)
                            .OnBeginSliderMovement(this, &SWetWrinkleEditorPanel::HandleRidgePropertySliderBegin)
                            .OnEndSliderMovement(this, &SWetWrinkleEditorPanel::HandleRidgePropertySliderEnd)
                            .OnValueCommitted(this, &SWetWrinkleEditorPanel::HandleRidgePropertyCommitted)
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
                            .Value(this, &SWetWrinkleEditorPanel::GetRidgeFalloffPercentValue)
                            .OnBeginSliderMovement(this, &SWetWrinkleEditorPanel::HandleRidgePropertySliderBegin)
                            .OnEndSliderMovement(this, &SWetWrinkleEditorPanel::HandleRidgePropertySliderEnd)
                            .OnValueCommitted(this, &SWetWrinkleEditorPanel::HandleRidgePropertyCommitted)
                            .OnValueChanged(this, &SWetWrinkleEditorPanel::HandleFalloffChanged)]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                        [SNew(STextBlock)
                            .Visibility(this, &SWetWrinkleEditorPanel::GetPatchToolVisibility)
                            .Text(LOCTEXT("RotationLabel", "Rotation (°)"))]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                        [SNew(SSpinBox<float>)
                            .Visibility(this, &SWetWrinkleEditorPanel::GetPatchToolVisibility)
                            .MinValue(-180.0f)
                            .MaxValue(180.0f)
                            .Value(FMath::RadiansToDegrees(BrushSettings.RotationRadians))
                            .OnValueChanged(this, &SWetWrinkleEditorPanel::HandleRotationChanged)]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 4.0f)
                       [SNew(STextBlock)
                            .Visibility(this, &SWetWrinkleEditorPanel::GetProceduralRidgeToolVisibility)
                            .Text(LOCTEXT("RidgeStartTaperLabel", "Start Taper"))]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                       [SNew(SSpinBox<float>)
                            .Visibility(this, &SWetWrinkleEditorPanel::GetProceduralRidgeToolVisibility)
                            .MinValue(0.0f)
                            .MaxValue(0.5f)
                            .Delta(0.01f)
                            .Value(this, &SWetWrinkleEditorPanel::GetRidgeStartTaperValue)
                            .OnBeginSliderMovement(this, &SWetWrinkleEditorPanel::HandleRidgePropertySliderBegin)
                            .OnEndSliderMovement(this, &SWetWrinkleEditorPanel::HandleRidgePropertySliderEnd)
                            .OnValueCommitted(this, &SWetWrinkleEditorPanel::HandleRidgePropertyCommitted)
                            .OnValueChanged(this, &SWetWrinkleEditorPanel::HandleRidgeStartTaperChanged)]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 4.0f)
                       [SNew(STextBlock)
                            .Visibility(this, &SWetWrinkleEditorPanel::GetProceduralRidgeToolVisibility)
                            .Text(LOCTEXT("RidgeEndTaperLabel", "End Taper"))]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                       [SNew(SSpinBox<float>)
                            .Visibility(this, &SWetWrinkleEditorPanel::GetProceduralRidgeToolVisibility)
                            .MinValue(0.0f)
                            .MaxValue(0.5f)
                            .Delta(0.01f)
                            .Value(this, &SWetWrinkleEditorPanel::GetRidgeEndTaperValue)
                            .OnBeginSliderMovement(this, &SWetWrinkleEditorPanel::HandleRidgePropertySliderBegin)
                            .OnEndSliderMovement(this, &SWetWrinkleEditorPanel::HandleRidgePropertySliderEnd)
                            .OnValueCommitted(this, &SWetWrinkleEditorPanel::HandleRidgePropertyCommitted)
                            .OnValueChanged(this, &SWetWrinkleEditorPanel::HandleRidgeEndTaperChanged)]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 4.0f)
                       [SNew(SBox)
                            .Visibility(this, &SWetWrinkleEditorPanel::GetProceduralRidgeToolVisibility)
                                [SNew(SVerticalBox)

                                 + SVerticalBox::Slot()
                                       .AutoHeight()
                                       .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                                           [SNew(SCheckBox)
                                                .IsChecked(this, &SWetWrinkleEditorPanel::GetRidgeNaturalVariationEnabledState)
                                                .OnCheckStateChanged(this, &SWetWrinkleEditorPanel::HandleRidgeNaturalVariationEnabledChanged)
                                                    [SNew(STextBlock).Text(LOCTEXT("RidgeNaturalVariation", "Natural Variation"))]]

                                 + SVerticalBox::Slot()
                                       .AutoHeight()
                                       .Padding(0.0f, 0.0f, 0.0f, 4.0f)
                                           [SNew(STextBlock).Text(LOCTEXT("RidgeCenterlineVariationLabel", "Centerline Variation"))]

                                 + SVerticalBox::Slot()
                                       .AutoHeight()
                                       .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                                           [SNew(SSpinBox<float>)
                                                .IsEnabled_Lambda([this]() { return BrushSettings.RidgeNaturalVariation.bEnabled; })
                                                .MinValue(0.0f)
                                                .MaxValue(0.5f)
                                                .Delta(0.01f)
                                                .Value(this, &SWetWrinkleEditorPanel::GetRidgeCenterlineVariationValue)
                                                .OnBeginSliderMovement(this, &SWetWrinkleEditorPanel::HandleRidgePropertySliderBegin)
                                                .OnEndSliderMovement(this, &SWetWrinkleEditorPanel::HandleRidgePropertySliderEnd)
                                                .OnValueCommitted(this, &SWetWrinkleEditorPanel::HandleRidgePropertyCommitted)
                                                .OnValueChanged(this, &SWetWrinkleEditorPanel::HandleRidgeCenterlineVariationChanged)]

                                 + SVerticalBox::Slot()
                                       .AutoHeight()
                                       .Padding(0.0f, 0.0f, 0.0f, 4.0f)
                                           [SNew(STextBlock).Text(LOCTEXT("RidgeCenterlineFrequencyLabel", "Centerline Frequency"))]

                                 + SVerticalBox::Slot()
                                       .AutoHeight()
                                       .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                                           [SNew(SSpinBox<float>)
                                                .IsEnabled_Lambda([this]() { return BrushSettings.RidgeNaturalVariation.bEnabled; })
                                                .MinValue(0.25f)
                                                .MaxValue(12.0f)
                                                .Delta(0.25f)
                                                .Value(this, &SWetWrinkleEditorPanel::GetRidgeCenterlineFrequencyValue)
                                                .OnBeginSliderMovement(this, &SWetWrinkleEditorPanel::HandleRidgePropertySliderBegin)
                                                .OnEndSliderMovement(this, &SWetWrinkleEditorPanel::HandleRidgePropertySliderEnd)
                                                .OnValueCommitted(this, &SWetWrinkleEditorPanel::HandleRidgePropertyCommitted)
                                                .OnValueChanged(this, &SWetWrinkleEditorPanel::HandleRidgeCenterlineFrequencyChanged)]

                                 + SVerticalBox::Slot()
                                       .AutoHeight()
                                       .Padding(0.0f, 0.0f, 0.0f, 4.0f)
                                           [SNew(STextBlock).Text(LOCTEXT("RidgeWidthVariationLabel", "Width Variation"))]

                                 + SVerticalBox::Slot()
                                       .AutoHeight()
                                       .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                                           [SNew(SSpinBox<float>)
                                                .IsEnabled_Lambda([this]() { return BrushSettings.RidgeNaturalVariation.bEnabled; })
                                                .MinValue(0.0f)
                                                .MaxValue(0.5f)
                                                .Delta(0.01f)
                                                .Value(this, &SWetWrinkleEditorPanel::GetRidgeWidthVariationValue)
                                                .OnBeginSliderMovement(this, &SWetWrinkleEditorPanel::HandleRidgePropertySliderBegin)
                                                .OnEndSliderMovement(this, &SWetWrinkleEditorPanel::HandleRidgePropertySliderEnd)
                                                .OnValueCommitted(this, &SWetWrinkleEditorPanel::HandleRidgePropertyCommitted)
                                                .OnValueChanged(this, &SWetWrinkleEditorPanel::HandleRidgeWidthVariationChanged)]

                                 + SVerticalBox::Slot()
                                       .AutoHeight()
                                       .Padding(0.0f, 0.0f, 0.0f, 4.0f)
                                           [SNew(STextBlock).Text(LOCTEXT("RidgeWidthFrequencyLabel", "Width Frequency"))]

                                 + SVerticalBox::Slot()
                                       .AutoHeight()
                                       .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                                           [SNew(SSpinBox<float>)
                                                .IsEnabled_Lambda([this]() { return BrushSettings.RidgeNaturalVariation.bEnabled; })
                                                .MinValue(0.25f)
                                                .MaxValue(12.0f)
                                                .Delta(0.25f)
                                                .Value(this, &SWetWrinkleEditorPanel::GetRidgeWidthFrequencyValue)
                                                .OnBeginSliderMovement(this, &SWetWrinkleEditorPanel::HandleRidgePropertySliderBegin)
                                                .OnEndSliderMovement(this, &SWetWrinkleEditorPanel::HandleRidgePropertySliderEnd)
                                                .OnValueCommitted(this, &SWetWrinkleEditorPanel::HandleRidgePropertyCommitted)
                                                .OnValueChanged(this, &SWetWrinkleEditorPanel::HandleRidgeWidthFrequencyChanged)]

                                 + SVerticalBox::Slot()
                                       .AutoHeight()
                                       .Padding(0.0f, 0.0f, 0.0f, 4.0f)
                                           [SNew(STextBlock).Text(LOCTEXT("RidgeNoiseSeedLabel", "Noise Seed"))]

                                 + SVerticalBox::Slot()
                                       .AutoHeight()
                                       .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                                           [SNew(SHorizontalBox)

                                            + SHorizontalBox::Slot()
                                                  .FillWidth(1.0f)
                                                      [SNew(SSpinBox<int32>)
                                                           .IsEnabled_Lambda([this]() { return BrushSettings.RidgeNaturalVariation.bEnabled; })
                                                           .Value(this, &SWetWrinkleEditorPanel::GetRidgeNoiseSeedValue)
                                                           .OnValueChanged(this, &SWetWrinkleEditorPanel::HandleRidgeNoiseSeedChanged)]

                                            + SHorizontalBox::Slot()
                                                  .AutoWidth()
                                                  .Padding(4.0f, 0.0f, 0.0f, 0.0f)
                                                      [SNew(SButton)
                                                           .ButtonStyle(FAppStyle::Get(), "SimpleButton")
                                                           .IsEnabled_Lambda([this]() { return BrushSettings.RidgeNaturalVariation.bEnabled; })
                                                           .ToolTipText(LOCTEXT("RandomizeRidgeNoiseSeedTooltip", "Generate a different deterministic variation for this ridge."))
                                                           .OnClicked(this, &SWetWrinkleEditorPanel::HandleRandomizeRidgeNoiseSeedClicked)
                                                               [SNew(SImage).Image(FAppStyle::GetBrush("Icons.Refresh"))]]]]]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 4.0f)
                       [SNew(SBox)
                            .Visibility(this, &SWetWrinkleEditorPanel::GetProceduralRidgeEditVisibility)
                                [SNew(SHorizontalBox)

                                 + SHorizontalBox::Slot()
                                       .FillWidth(1.0f)
                                           [SNew(STextBlock)
                                                .Text(this, &SWetWrinkleEditorPanel::GetSelectedRidgeEndpointStatusText, true)
                                                .ColorAndOpacity(this, &SWetWrinkleEditorPanel::GetSelectedRidgeEndpointStatusColor, true)]

                                 + SHorizontalBox::Slot()
                                       .FillWidth(1.0f)
                                       .Padding(6.0f, 0.0f, 0.0f, 0.0f)
                                           [SNew(STextBlock)
                                                .Text(this, &SWetWrinkleEditorPanel::GetSelectedRidgeEndpointStatusText, false)
                                                .ColorAndOpacity(this, &SWetWrinkleEditorPanel::GetSelectedRidgeEndpointStatusColor, false)]]]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                       [SNew(SBox)
                            .Visibility(this, &SWetWrinkleEditorPanel::GetProceduralRidgeEditVisibility)
                                [SNew(SHorizontalBox)

                                 + SHorizontalBox::Slot()
                                       .FillWidth(1.0f)
                                           [SNew(SCheckBox)
                                                .IsChecked(this, &SWetWrinkleEditorPanel::GetSelectedRidgeEndpointPointedState, true)
                                                .OnCheckStateChanged(this, &SWetWrinkleEditorPanel::HandleSelectedRidgeEndpointPointedChanged, true)
                                                    [SNew(STextBlock).Text(LOCTEXT("RidgePointedStart", "Pointed Start"))]]

                                 + SHorizontalBox::Slot()
                                       .FillWidth(1.0f)
                                       .Padding(6.0f, 0.0f, 0.0f, 0.0f)
                                           [SNew(SCheckBox)
                                                .IsChecked(this, &SWetWrinkleEditorPanel::GetSelectedRidgeEndpointPointedState, false)
                                                .OnCheckStateChanged(this, &SWetWrinkleEditorPanel::HandleSelectedRidgeEndpointPointedChanged, false)
                                                    [SNew(STextBlock).Text(LOCTEXT("RidgePointedEnd", "Pointed End"))]]]]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                       [SNew(SBox)
                            .Visibility(this, &SWetWrinkleEditorPanel::GetProceduralRidgeEditVisibility)
                                [SNew(SHorizontalBox)

                                 + SHorizontalBox::Slot()
                                       .FillWidth(1.0f)
                                           [SNew(SCheckBox)
                                                .IsChecked(this, &SWetWrinkleEditorPanel::GetSelectedRidgeEndpointFlaredState, true)
                                                .OnCheckStateChanged(this, &SWetWrinkleEditorPanel::HandleSelectedRidgeEndpointFlaredChanged, true)
                                                    [SNew(STextBlock).Text(LOCTEXT("RidgeFlaredStart", "Flared Start"))]]

                                 + SHorizontalBox::Slot()
                                       .FillWidth(1.0f)
                                       .Padding(6.0f, 0.0f, 0.0f, 0.0f)
                                           [SNew(SCheckBox)
                                                .IsChecked(this, &SWetWrinkleEditorPanel::GetSelectedRidgeEndpointFlaredState, false)
                                                .OnCheckStateChanged(this, &SWetWrinkleEditorPanel::HandleSelectedRidgeEndpointFlaredChanged, false)
                                                    [SNew(STextBlock).Text(LOCTEXT("RidgeFlaredEnd", "Flared End"))]]]]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 4.0f)
                       [SNew(SBox)
                            .Visibility(this, &SWetWrinkleEditorPanel::GetFlareOptionsVisibility)
                                [SNew(SVerticalBox)

                                 + SVerticalBox::Slot()
                                       .AutoHeight()
                                       .Padding(0.0f, 0.0f, 0.0f, 4.0f)
                                           [SNew(STextBlock).Text(LOCTEXT("RidgeFlareLengthLabel", "Flare Length"))]

                                 + SVerticalBox::Slot()
                                       .AutoHeight()
                                       .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                                           [SNew(SSpinBox<float>)
                                                .MinValue(0.01f)
                                                .MaxValue(0.5f)
                                                .Delta(0.01f)
                                                .Value(this, &SWetWrinkleEditorPanel::GetRidgeFlareLengthValue)
                                                .OnBeginSliderMovement(this, &SWetWrinkleEditorPanel::HandleRidgePropertySliderBegin)
                                                .OnEndSliderMovement(this, &SWetWrinkleEditorPanel::HandleRidgePropertySliderEnd)
                                                .OnValueCommitted(this, &SWetWrinkleEditorPanel::HandleRidgePropertyCommitted)
                                                .OnValueChanged(this, &SWetWrinkleEditorPanel::HandleRidgeFlareLengthChanged)]

                                 + SVerticalBox::Slot()
                                       .AutoHeight()
                                       .Padding(0.0f, 0.0f, 0.0f, 4.0f)
                                           [SNew(STextBlock).Text(LOCTEXT("RidgeFlareWidthLabel", "Flare Width"))]

                                 + SVerticalBox::Slot()
                                       .AutoHeight()
                                       .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                                           [SNew(SSpinBox<float>)
                                                .MinValue(1.0f)
                                                .MaxValue(5.0f)
                                                .Delta(0.1f)
                                                .Value(this, &SWetWrinkleEditorPanel::GetRidgeFlareWidthValue)
                                                .OnBeginSliderMovement(this, &SWetWrinkleEditorPanel::HandleRidgePropertySliderBegin)
                                                .OnEndSliderMovement(this, &SWetWrinkleEditorPanel::HandleRidgePropertySliderEnd)
                                                .OnValueCommitted(this, &SWetWrinkleEditorPanel::HandleRidgePropertyCommitted)
                                                .OnValueChanged(this, &SWetWrinkleEditorPanel::HandleRidgeFlareWidthChanged)]

                                 + SVerticalBox::Slot()
                                       .AutoHeight()
                                       .Padding(0.0f, 0.0f, 0.0f, 4.0f)
                                           [SNew(STextBlock).Text(LOCTEXT("RidgeFlareEndStrengthLabel", "Flare End Strength"))]

                                 + SVerticalBox::Slot()
                                       .AutoHeight()
                                       .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                                           [SNew(SSpinBox<float>)
                                                .MinValue(0.0f)
                                                .MaxValue(1.0f)
                                                .Delta(0.05f)
                                                .Value(this, &SWetWrinkleEditorPanel::GetRidgeFlareEndStrengthValue)
                                                .OnBeginSliderMovement(this, &SWetWrinkleEditorPanel::HandleRidgePropertySliderBegin)
                                                .OnEndSliderMovement(this, &SWetWrinkleEditorPanel::HandleRidgePropertySliderEnd)
                                                .OnValueCommitted(this, &SWetWrinkleEditorPanel::HandleRidgePropertyCommitted)
                                                .OnValueChanged(this, &SWetWrinkleEditorPanel::HandleRidgeFlareEndStrengthChanged)]

                                 + SVerticalBox::Slot()
                                       .AutoHeight()
                                       .Padding(0.0f, 0.0f, 0.0f, 4.0f)
                                           [SNew(STextBlock).Text(LOCTEXT("RidgeFlareSoftnessLabel", "Flare Softness"))]

                                 + SVerticalBox::Slot()
                                       .AutoHeight()
                                       .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                                           [SNew(SSpinBox<float>)
                                                .MinValue(0.0f)
                                                .MaxValue(1.0f)
                                                .Delta(0.05f)
                                                .Value(this, &SWetWrinkleEditorPanel::GetRidgeFlareSoftnessValue)
                                                .OnBeginSliderMovement(this, &SWetWrinkleEditorPanel::HandleRidgePropertySliderBegin)
                                                 .OnEndSliderMovement(this, &SWetWrinkleEditorPanel::HandleRidgePropertySliderEnd)
                                                 .OnValueCommitted(this, &SWetWrinkleEditorPanel::HandleRidgePropertyCommitted)
                                                 .OnValueChanged(this, &SWetWrinkleEditorPanel::HandleRidgeFlareSoftnessChanged)]]]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 4.0f)
                       [SNew(STextBlock)
                            .Visibility(this, &SWetWrinkleEditorPanel::GetProceduralRidgeToolVisibility)
                            .Text(LOCTEXT("RidgePointSpacingLabel", "Point Spacing"))]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                       [SNew(SSpinBox<float>)
                            .Visibility(this, &SWetWrinkleEditorPanel::GetProceduralRidgeToolVisibility)
                            .MinValue(0.05f)
                            .MaxValue(1.0f)
                            .Delta(0.05f)
                            .Value(BrushSettings.RidgePointSpacingScale)
                            .OnValueChanged(this, &SWetWrinkleEditorPanel::HandleRidgePointSpacingChanged)]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                       [SNew(SBox)
                            .Visibility(this, &SWetWrinkleEditorPanel::GetProceduralRidgeEditVisibility)
                                [SNew(SButton)
                                     .IsEnabled(this, &SWetWrinkleEditorPanel::CanDeleteSelectedRidgePoint)
                                     .Text(LOCTEXT("DeleteSelectedRidgePoint", "Delete Selected Point"))
                                     .ToolTipText(LOCTEXT("DeleteSelectedRidgePointTooltip", "Delete the selected control point. A ridge must keep at least two points."))
                                     .OnClicked(this, &SWetWrinkleEditorPanel::HandleDeleteSelectedRidgePointClicked)]]

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
                            .Delta(0.1f)
                            .Value(BrushSettings.PreviewWetness)
                            .OnValueChanged(this, &SWetWrinkleEditorPanel::HandlePreviewWetnessChanged)]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 0.0f)
                       [SNew(SCheckBox)
                            .IsChecked(this, &SWetWrinkleEditorPanel::GetPreviewToggleState)
                            .OnCheckStateChanged(this, &SWetWrinkleEditorPanel::HandlePreviewToggleChanged)
                                [SNew(STextBlock)
                                     .Text(LOCTEXT("PreviewToggle", "Show Preview Cursor"))]]]];
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
                                       .Text(LOCTEXT("PatchListHeading", "Wrinkle Elements"))
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
        Asset->WrinkleData.WrinkleUVChannelIndex = ResolveWetWrinkleUVChannel(WetClothingAsset.Get());
        BrushSettings.UVChannelIndex = BrushSettings.MaterialSlotIndex != INDEX_NONE ? ResolveWetWrinkleUVChannel(WetClothingAsset.Get()) : INDEX_NONE;
    }
    RefreshMaterialSlotOptions();
    RefreshDWCDataUVChannel();
    RefreshMaterialTextures();
    RefreshBrushPresetOptions();
    RefreshWrinklePresetPalette();
    RefreshPartMapItems();
    RefreshStrokeList();
    RefreshWrinklePresetThumbnail();
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

FReply SWetWrinkleEditorPanel::BakeSelectedWrinkleNormalMap()
{
    if (BrushSettings.MaterialSlotIndex == INDEX_NONE)
    {
        FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("BakeWrinkleNoSlot", "Select a material slot before baking a wrinkle normal map."));
        return FReply::Handled();
    }

    return BakeWrinkleNormalMapsForSlots({BrushSettings.MaterialSlotIndex}, true, false);
}

FReply SWetWrinkleEditorPanel::ExecuteBakeWrinkleNormalMap()
{
    return BakeSelectedWrinkleNormalMap();
}

FReply SWetWrinkleEditorPanel::BakeSelectedWrinkleMask()
{
    if (BrushSettings.MaterialSlotIndex == INDEX_NONE)
    {
        FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("BakeWrinkleMaskNoSlot", "Select a material slot before baking a wrinkle mask."));
        return FReply::Handled();
    }

    return BakeWrinkleNormalMapsForSlots({BrushSettings.MaterialSlotIndex}, false, true);
}

SWetWrinkleEditorPanel::~SWetWrinkleEditorPanel()
{
    ActiveRidgeEditTransaction.Reset();
    ActiveRidgePropertyTransaction.Reset();
    if (GEditor != nullptr)
    {
        GEditor->UnregisterForUndo(this);
    }
}

void SWetWrinkleEditorPanel::PostUndo(bool bSuccess)
{
    if (bSuccess)
    {
        ActiveRidgeEditTransaction.Reset();
        ActiveRidgePropertyTransaction.Reset();
        bEditingProceduralRidgePoint = false;
        bInsertedEditedProceduralRidgePoint = false;
        EditingProceduralRidgePointIndex = INDEX_NONE;
        SelectedProceduralRidgePointIndex = INDEX_NONE;
        if (PreviewViewport.IsValid())
        {
            PreviewViewport->ClearTransientProceduralStroke();
            PreviewViewport->SetEditingProceduralStrokeGuid(FGuid());
        }
        CancelProceduralRidgeStroke();
        RefreshFromAsset();
    }
}

void SWetWrinkleEditorPanel::PostRedo(bool bSuccess)
{
    PostUndo(bSuccess);
}

FReply SWetWrinkleEditorPanel::ExecuteBakeAllWrinkleNormalMaps()
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr)
    {
        FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("BakeWrinkleNoAsset", "Wet Clothing Asset is unavailable."));
        return FReply::Handled();
    }

    TSet<int32> UniqueMaterialSlots;
    for (const FWetWrinklePatchStroke& Stroke : Asset->WrinkleData.EditablePatchStrokes)
    {
        if (!Stroke.bEnabled && !Asset->WrinkleData.BakeSettings.bIncludeDisabledPatchStrokes)
        {
            continue;
        }

        for (const FWetWrinklePatchPlacement& Patch : Stroke.PatchPlacements)
        {
            if (Patch.MaterialSlotIndex != INDEX_NONE)
            {
                UniqueMaterialSlots.Add(Patch.MaterialSlotIndex);
            }
        }
    }

    for (const FWetProceduralRidgeStroke& Stroke : Asset->WrinkleData.EditableProceduralRidgeStrokes)
    {
        if ((!Stroke.bEnabled && !Asset->WrinkleData.BakeSettings.bIncludeDisabledPatchStrokes) ||
            Stroke.MaterialSlotIndex == INDEX_NONE || Stroke.Points.Num() < 2)
        {
            continue;
        }

        UniqueMaterialSlots.Add(Stroke.MaterialSlotIndex);
    }

    TArray<int32> MaterialSlotIndices = UniqueMaterialSlots.Array();
    MaterialSlotIndices.Sort();
    if (MaterialSlotIndices.Num() == 0)
    {
        FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("BakeAllWrinkleNoInputs", "No enabled wrinkle patches or procedural ridge strokes were found to bake."));
        return FReply::Handled();
    }

    return BakeWrinkleNormalMapsForSlots(MaterialSlotIndices, true, false);
}

FReply SWetWrinkleEditorPanel::BakeWrinkleNormalMapsForSlots(const TArray<int32>& MaterialSlotIndices, const bool bBakeNormalMap, const bool bBakeMask)
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr)
    {
        FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("BakeWrinkleNoAsset", "Wet Clothing Asset is unavailable."));
        return FReply::Handled();
    }

    if (MaterialSlotIndices.Num() == 0)
    {
        FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("BakeWrinkleNoSlots", "No material slots were provided for wrinkle baking."));
        return FReply::Handled();
    }

    const FString BakeLabel = bBakeNormalMap && bBakeMask
                                  ? TEXT("wrinkle maps")
                                  : (bBakeMask ? TEXT("wrinkle masks") : TEXT("wrinkle normal maps"));

    FWetWrinkleNormalMapBakeSettings Settings;
    Settings.Resolution = Asset->WrinkleData.BakeSettings.DefaultResolution;
    Settings.PaddingPixels = Asset->WrinkleData.BakeSettings.PaddingPixels;
    Settings.bIncludeDisabledPatchStrokes = Asset->WrinkleData.BakeSettings.bIncludeDisabledPatchStrokes;
    Settings.bBakeNormalMap = bBakeNormalMap;
    Settings.bBakeMask = bBakeMask;

    int32 BakedMapCount = 0;
    int32 BakedPatchCount = 0;
    int32 BakedProceduralStrokeCount = 0;
    TArray<FString> FailedSlots;
    for (const int32 MaterialSlotIndex : MaterialSlotIndices)
    {
        FWetWrinkleNormalMapBakeResult Result;
        FString ErrorMessage;
        if (!FWetWrinkleNormalMapBaker::BakeMaterialSlot(Asset, MaterialSlotIndex, Settings, Result, ErrorMessage))
        {
            FailedSlots.Add(FString::Printf(TEXT("Slot %d: %s"), MaterialSlotIndex, *ErrorMessage));
            continue;
        }

        BakedMapCount += Result.BakedMapCount;
        BakedPatchCount += Result.BakedStampCount;
        BakedProceduralStrokeCount += Result.BakedProceduralStrokeCount;
    }

    if (BakedMapCount == 0)
    {
        FMessageDialog::Open(
            EAppMsgType::Ok,
            FText::FromString(FailedSlots.Num() > 0 ? FString::Join(FailedSlots, TEXT("\n")) : FString::Printf(TEXT("No %s were generated."), *BakeLabel)));
        return FReply::Handled();
    }

    MarkAssetEdited();
    if (DetailsView.IsValid())
    {
        DetailsView->ForceRefresh();
    }

    const bool bSaved = DWCEditorUtils::SaveAsset(Asset);
    const EAppMsgCategory MessageCategory = FailedSlots.Num() > 0 || !bSaved
        ? EAppMsgCategory::Warning
        : EAppMsgCategory::Success;
    FString Summary = FString::Printf(
        TEXT("Baked %d %s from %d patch(es) and %d procedural ridge stroke(s)."),
        BakedMapCount,
        *BakeLabel,
        BakedPatchCount,
        BakedProceduralStrokeCount);
    if (bBakeNormalMap)
    {
        Summary += TEXT("\nConvex wrinkle coverage alpha was packed into the baked normal map.");
    }
    if (FailedSlots.Num() > 0)
    {
        Summary += FString::Printf(TEXT("\n\nSkipped:\n- %s"), *FString::Join(FailedSlots, TEXT("\n- ")));
    }
    if (!bSaved)
    {
        Summary += TEXT("\n\nThe maps were generated, but the generated textures or Wet Clothing Asset could not be saved.");
    }
    else
    {
        Summary += TEXT("\n\nThe baked maps and Wet Clothing Asset were saved.");
    }

    FMessageDialog::Open(
        MessageCategory,
        EAppMsgType::Ok,
        FText::FromString(Summary));
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
    if (bCapturingProceduralRidgeStroke && !SurfaceHit.bHit)
    {
        bProceduralRidgeCaptureBlocked = true;
    }
    CurrentHit = SurfaceHit;
    RefreshWrinkleUVViewMarkersOnly();
}

void SWetWrinkleEditorPanel::HandlePaintStrokeStarted(const FWetWrinkleSurfaceHit& SurfaceHit)
{
    if (BrushSettings.ToolMode == EWetWrinkleToolMode::ProceduralRidgeStroke)
    {
        if (BrushSettings.RidgeEditMode == EWetProceduralRidgeEditMode::Edit)
        {
            BeginProceduralRidgePointEdit(SurfaceHit);
        }
        else
        {
            BeginProceduralRidgeStroke(SurfaceHit);
        }
        return;
    }

    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || !SurfaceHit.bHit || BrushSettings.MaterialSlotIndex == INDEX_NONE || BrushSettings.UVChannelIndex == INDEX_NONE)
    {
        return;
    }

    FString PresetReason;
    if (!IsCurrentWrinklePresetUsable(&PresetReason))
    {
        FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(PresetReason));
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
    SelectedElementType = EWetWrinkleElementType::PatchStroke;
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
        PreviewViewport->SetSelectedProceduralStrokeGuid(FGuid());
        PreviewViewport->AppendAccumulatedPreviewStamp(NewStroke.PatchPlacements.Last());
    }
    RefreshWrinkleUVView();
}

void SWetWrinkleEditorPanel::HandlePaintStampRequested(const FWetWrinkleSurfaceHit& SurfaceHit)
{
    if (BrushSettings.ToolMode == EWetWrinkleToolMode::ProceduralRidgeStroke)
    {
        if (bEditingProceduralRidgePoint)
        {
            UpdateProceduralRidgePointEdit(SurfaceHit);
        }
        else
        {
            AppendProceduralRidgeStrokePoint(SurfaceHit);
        }
        return;
    }

    UWetClothingAsset* Asset = WetClothingAsset.Get();
    FWetWrinklePatchStroke* ActiveStroke = FindMutableStroke(ActiveStrokeGuid);
    if (Asset == nullptr || ActiveStroke == nullptr || !SurfaceHit.bHit || BrushSettings.MaterialSlotIndex == INDEX_NONE || BrushSettings.UVChannelIndex == INDEX_NONE)
    {
        return;
    }

    FString PresetReason;
    if (!IsCurrentWrinklePresetUsable(&PresetReason))
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
    if (bEditingProceduralRidgePoint)
    {
        EndProceduralRidgePointEdit(false);
        return;
    }

    if (bCapturingProceduralRidgeStroke)
    {
        CommitProceduralRidgeStroke();
        return;
    }

    ActiveStrokeGuid.Invalidate();
    bHasLastStamp = false;
    bAllowImmediateNextStrokeStamp = false;
    ActivePaintTransaction.Reset();
}

void SWetWrinkleEditorPanel::HandlePaintStrokeCanceled()
{
    if (bEditingProceduralRidgePoint)
    {
        EndProceduralRidgePointEdit(true);
        return;
    }

    if (bCapturingProceduralRidgeStroke)
    {
        CancelProceduralRidgeStroke();
        return;
    }

    HandlePaintStrokeEnded();
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
            Item->ElementType = EWetWrinkleElementType::PatchStroke;
            StrokeListItems.Add(Item);
        }

        for (const FWetProceduralRidgeStroke& Stroke : Asset->WrinkleData.EditableProceduralRidgeStrokes)
        {
            FStrokeListItemPtr Item = MakeShared<FWetWrinklePatchStrokeListItem>();
            Item->StrokeGuid = Stroke.StrokeGuid;
            Item->ElementType = EWetWrinkleElementType::ProceduralRidgeStroke;
            StrokeListItems.Add(Item);
        }
    }

    if (StrokeListView.IsValid())
    {
        StrokeListView->RequestListRefresh();
        for (const FStrokeListItemPtr& Item : StrokeListItems)
        {
            if (Item.IsValid() && Item->StrokeGuid == SelectedStrokeGuid && Item->ElementType == SelectedElementType)
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
        PreviewViewport->SetSelectedStrokeGuid(
            SelectedElementType == EWetWrinkleElementType::PatchStroke ? SelectedStrokeGuid : FGuid());
        PreviewViewport->SetSelectedProceduralStrokeGuid(
            SelectedElementType == EWetWrinkleElementType::ProceduralRidgeStroke ? SelectedStrokeGuid : FGuid());
        PreviewViewport->SetSelectedProceduralStrokePointIndex(
            SelectedElementType == EWetWrinkleElementType::ProceduralRidgeStroke
                ? SelectedProceduralRidgePointIndex
                : INDEX_NONE);
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

    FMaterialSlotItemPtr AllSlotsItem = MakeShared<FWCAMaterialSlotItem>();
    AllSlotsItem->SlotIndex = INDEX_NONE;
    AllSlotsItem->SlotName = TEXT("All Slots");
    MaterialSlotItems.Add(AllSlotsItem);

    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const USkeletalMesh* TargetMesh = nullptr;
    if (Asset != nullptr)
    {
        TargetMesh = Asset->GetDWCSkeletalMesh() != nullptr ? Asset->GetDWCSkeletalMesh() : nullptr;
    }

    if (TargetMesh != nullptr)
    {
        const int32 MaterialCount = TargetMesh->GetMaterials().Num();
        for (int32 MaterialSlotIndex = 0; MaterialSlotIndex < MaterialCount; ++MaterialSlotIndex)
        {
            MaterialSlotOptions.Add(MakeShared<int32>(MaterialSlotIndex));

            const FSkeletalMaterial& SkeletalMaterial = TargetMesh->GetMaterials()[MaterialSlotIndex];
            FMaterialSlotItemPtr Item = MakeShared<FWCAMaterialSlotItem>();
            Item->SlotIndex = MaterialSlotIndex;
            Item->SlotName = SkeletalMaterial.MaterialSlotName;
            Item->Material = SkeletalMaterial.MaterialInterface;
            Item->bIsWettableSlot = FWCAEditorWidgets::IsMaterialSlotWettable(Asset, MaterialSlotIndex);
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
        SelectedTextureItem,
        true);

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
        FWCAEditorWidgets::BuildUVViewTextureSelector(
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
    return FWCAEditorWidgets::GenerateTextureComboItem(Item, MaterialThumbnailPool, &TextureThumbnails);
}

void SWetWrinkleEditorPanel::HandleTextureSelectionChanged(FTextureItemPtr Item, ESelectInfo::Type SelectInfo)
{
    SelectedTextureItem = Item;
    bShowMaterialTextureInUVView = SelectedTextureItem.IsValid() && SelectedTextureItem->Texture.IsValid();
    SaveSelectedTexture();

    if (SelectedTextureComboContentBox.IsValid())
    {
        SelectedTextureComboContentBox->SetContent(
            FWCAEditorWidgets::BuildTextureComboContent(SelectedTextureItem, 24.0f, true, MaterialThumbnailPool, &TextureThumbnails));
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



bool SWetWrinkleEditorPanel::HasUsableWrinkleUVChannel() const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const USkeletalMesh* TargetMesh = Asset != nullptr ? Asset->GetDWCSkeletalMesh() : nullptr;
    if (Asset == nullptr || TargetMesh == nullptr)
    {
        return false;
    }

    const int32 NumUVChannels = FWetClothingAssetMeshAnalyzer::GetNumUVChannels(TargetMesh, 0);
    const int32 DataUVChannelIndex = ResolveWetWrinkleUVChannel(Asset);
    return DataUVChannelIndex >= 0 && DataUVChannelIndex < NumUVChannels;
}


bool SWetWrinkleEditorPanel::EnsureWrinkleUVChannelForMaterialSlot(int32 MaterialSlotIndex, bool bShowFailureDialog)
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr)
    {
        return false;
    }

    const USkeletalMesh* TargetMesh = Asset->GetDWCSkeletalMesh();
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

    if (!FWCAEditorWidgets::IsMaterialSlotWettable(Asset, MaterialSlotIndex))
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
    const int32 ConfiguredDataUVChannelIndex = ResolveWetWrinkleUVChannel(Asset);
    const int32 CandidateUVChannelIndex =
        ConfiguredDataUVChannelIndex >= 0 && ConfiguredDataUVChannelIndex < NumUVChannels
            ? ConfiguredDataUVChannelIndex
            : INDEX_NONE;

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

    if (Asset->WrinkleData.WrinkleUVChannelIndex != CandidateUVChannelIndex)
    {
        Asset->Modify();
        Asset->WrinkleData.WrinkleUVChannelIndex = CandidateUVChannelIndex;
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

void SWetWrinkleEditorPanel::RefreshDWCDataUVChannel()
{
    // Wrinkle Edit is permanently bound to the generated DWC Data UV channel.
    if (UWetClothingAsset* Asset = WetClothingAsset.Get())
    {
        const int32 DataUVChannelIndex = GetWrinkleUVViewChannelIndex();
        Asset->WrinkleData.WrinkleUVChannelIndex = DataUVChannelIndex;
        BrushSettings.UVChannelIndex = BrushSettings.MaterialSlotIndex != INDEX_NONE
            ? DataUVChannelIndex
            : INDEX_NONE;
    }
}

int32 SWetWrinkleEditorPanel::GetWrinkleUVViewChannelIndex() const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const USkeletalMesh* TargetMesh = Asset != nullptr ? Asset->GetDWCSkeletalMesh() : nullptr;
    const int32 NumUVChannels = FWetClothingAssetMeshAnalyzer::GetNumUVChannels(TargetMesh, 0);
    const int32 DataUVChannelIndex = ResolveWetWrinkleUVChannel(Asset);
    return DataUVChannelIndex >= 0 && DataUVChannelIndex < NumUVChannels
        ? DataUVChannelIndex
        : INDEX_NONE;
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
                       [FWCAEditorWidgets::BuildSectionHeader(
                           LOCTEXT("WrinkleUVViewLabel", "Wrinkle UV View"),
                           TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &SWetWrinkleEditorPanel::GetDWCDataUVChannelText)))]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                       [SNew(SSeparator)
                            .Orientation(Orient_Horizontal)]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                       [FWCAEditorWidgets::BuildUVViewTextureAndViewRow(
                           SAssignNew(TextureSelectionContainer, SBox),
                           FWCAEditorWidgets::BuildUVViewOptionsButton(
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
                       [SAssignNew(WrinkleUVView, SWCAUVView)]];
}

void SWetWrinkleEditorPanel::RefreshWrinkleUVView()
{
    if (!WrinkleUVView.IsValid())
    {
        return;
    }

    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const USkeletalMesh* TargetMesh = Asset != nullptr ? Asset->GetDWCSkeletalMesh() : nullptr;
    const int32 MaterialSlotIndex = BrushSettings.MaterialSlotIndex;
    const int32 UVChannelIndex = GetWrinkleUVViewChannelIndex();
    const int32 NumUVChannels = FWetClothingAssetMeshAnalyzer::GetNumUVChannels(TargetMesh, 0);
    UTexture* BackgroundTexture = ResolveTextureAddressTexture();

    WrinkleUVView->SetDisplayMode(CurrentUVDisplayMode);
    WrinkleUVView->SetBackgroundTextureOpacity(UVViewBackgroundTextureOpacity);
    WrinkleUVView->SetUVIslandLineOpacity(UVViewIslandLineOpacity);
    WrinkleUVView->SetUVIslandLineThicknessScale(UVViewIslandLineThicknessScale);
    WrinkleUVView->SetNormalizeToContentBounds(true);
    // Important: set the texture before SetIslands(). SWCAUVView uses the
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
        WrinkleUVView->SetCircleMarkers(TArray<FWCAUVViewCircleMarker>());
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

            FWCAUVViewCircleMarker Marker;
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

    TArray<FWCAUVViewCircleMarker> CircleMarkers = CachedWrinkleUVViewPatchMarkers;
    if (BrushSettings.bShowPreview &&
        CurrentHit.bHit &&
        CurrentHit.MaterialSlotIndex == MaterialSlotIndex &&
        CurrentHit.UVChannelIndex == UVChannelIndex)
    {
        FWCAUVViewCircleMarker Marker;
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

    FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    TArray<FAssetData> TextureAssets;
    AssetRegistryModule.Get().GetAssetsByPath(FName(WetWrinkleBaseNormalTextureFolderPath), TextureAssets, false);
    TextureAssets.Sort([](const FAssetData& A, const FAssetData& B)
    {
        return A.AssetName.ToString() < B.AssetName.ToString();
    });

    for (const FAssetData& TextureAsset : TextureAssets)
    {
        AddPreset(FText::FromName(TextureAsset.AssetName), TextureAsset.ToSoftObjectPath());
    }

}

void SWetWrinkleEditorPanel::RefreshWrinklePresetPalette(bool bForceAssetScan)
{
    WrinklePresetPaletteItems.Reset();

    FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    if (bForceAssetScan)
    {
        AssetRegistryModule.Get().SearchAllAssets(true);
    }

    TArray<FAssetData> PresetAssets;
    AssetRegistryModule.Get().GetAssetsByClass(UWetWrinklePreset::StaticClass()->GetClassPathName(), PresetAssets, true);
    PresetAssets.Sort([](const FAssetData& A, const FAssetData& B)
    {
        return A.AssetName.ToString() < B.AssetName.ToString();
    });

    for (const FAssetData& PresetAsset : PresetAssets)
    {
        UWetWrinklePreset* Preset = Cast<UWetWrinklePreset>(PresetAsset.GetAsset());
        if (Preset == nullptr)
        {
            continue;
        }

        TSharedPtr<FWetWrinklePresetPaletteItem> Item = MakeShared<FWetWrinklePresetPaletteItem>();
        Item->DisplayName = FText::FromName(PresetAsset.AssetName);
        Item->PresetPath = PresetAsset.ToSoftObjectPath();
        Item->Preset = Preset;
        Item->bRemoved = false;
        RefreshWrinklePresetPaletteItemState(Item);
        WrinklePresetPaletteItems.Add(Item);
    }

    RebuildWrinklePresetPaletteWidget();
}

void SWetWrinkleEditorPanel::RebuildWrinklePresetPaletteWidget()
{
    if (!WrinklePresetPaletteWrapBox.IsValid())
    {
        return;
    }

    WrinklePresetPaletteWrapBox->ClearChildren();

    if (WrinklePresetPaletteItems.Num() == 0)
    {
        WrinklePresetPaletteWrapBox->AddSlot()
            [SNew(STextBlock)
                 .AutoWrapText(true)
                 .ColorAndOpacity(FSlateColor(FLinearColor(0.65f, 0.65f, 0.65f)))
                 .Text(LOCTEXT("NoWetWrinklePresetPaletteItems", "No Wet Wrinkle Presets found."))];
        return;
    }

    for (TSharedPtr<FWetWrinklePresetPaletteItem> Item : WrinklePresetPaletteItems)
    {
        WrinklePresetPaletteWrapBox->AddSlot()
            [GenerateWrinklePresetPaletteTile(Item)];
    }
}

void SWetWrinkleEditorPanel::RefreshWrinklePresetPaletteState()
{
    for (const TSharedPtr<FWetWrinklePresetPaletteItem>& Item : WrinklePresetPaletteItems)
    {
        RefreshWrinklePresetPaletteItemState(Item);
    }

    RefreshWrinklePresetThumbnail();
}

void SWetWrinkleEditorPanel::RefreshWrinklePresetPaletteItemState(const TSharedPtr<FWetWrinklePresetPaletteItem>& Item)
{
    if (!Item.IsValid())
    {
        return;
    }

    if (Item->bRemoved)
    {
        Item->Preset.Reset();
        Item->ThumbnailTexturePath.Reset();
        Item->ThumbnailBrush.SetResourceObject(nullptr);
        return;
    }

    UWetWrinklePreset* Preset = Item->Preset.Get();
    if (Preset == nullptr && Item->PresetPath.IsValid())
    {
        Preset = Cast<UWetWrinklePreset>(Item->PresetPath.TryLoad());
        Item->Preset = Preset;
    }

    UTexture2D* ThumbnailTexture = Preset != nullptr ? Preset->GetNormalTextureForBrush() : nullptr;

    Item->ThumbnailTexturePath = ThumbnailTexture != nullptr ? FSoftObjectPath(ThumbnailTexture) : FSoftObjectPath();
    Item->ThumbnailBrush.SetResourceObject(ThumbnailTexture);
    Item->ThumbnailBrush.SetImageSize(
        ThumbnailTexture != nullptr
            ? FVector2D(FMath::Max(ThumbnailTexture->GetSizeX(), 1), FMath::Max(ThumbnailTexture->GetSizeY(), 1))
            : FVector2D(144.0f, 144.0f));
}

EActiveTimerReturnType SWetWrinkleEditorPanel::HandleWrinklePresetPaletteRefreshTimer(double, float)
{
    RefreshWrinklePresetPaletteState();
    return EActiveTimerReturnType::Continue;
}

void SWetWrinkleEditorPanel::HandleWrinklePresetPaletteAssetRemoved(const FAssetData& AssetData)
{
    const FSoftObjectPath RemovedAssetPath = AssetData.ToSoftObjectPath();
    bool bClearedSelectedThumbnail = false;

    for (const TSharedPtr<FWetWrinklePresetPaletteItem>& Item : WrinklePresetPaletteItems)
    {
        if (!Item.IsValid())
        {
            continue;
        }

        if (Item->PresetPath == RemovedAssetPath)
        {
            Item->Preset.Reset();
            Item->bRemoved = true;
            Item->ThumbnailTexturePath.Reset();
            Item->ThumbnailBrush.SetResourceObject(nullptr);
        }
        else if (Item->ThumbnailTexturePath == RemovedAssetPath)
        {
            Item->ThumbnailTexturePath.Reset();
            Item->ThumbnailBrush.SetResourceObject(nullptr);
        }
    }

    if (SelectedWrinklePresetThumbnailBrush.GetResourceObject() != nullptr &&
        FSoftObjectPath(SelectedWrinklePresetThumbnailBrush.GetResourceObject()) == RemovedAssetPath)
    {
        SelectedWrinklePresetThumbnailBrush.SetResourceObject(nullptr);
        bClearedSelectedThumbnail = true;
    }

    if (BrushSettings.WrinklePreset != nullptr && FSoftObjectPath(BrushSettings.WrinklePreset.Get()) == RemovedAssetPath)
    {
        BrushSettings.WrinklePreset = nullptr;
        RefreshWrinklePresetThumbnail();
        PushBrushSettingsToViewport();
        RefreshStrokeOverlay();
    }

    if (!bClearedSelectedThumbnail)
    {
        RefreshWrinklePresetThumbnail();
    }
}

void SWetWrinkleEditorPanel::HandleWrinklePresetPaletteAssetUpdated(const FAssetData& AssetData)
{
    const FSoftObjectPath UpdatedAssetPath = AssetData.ToSoftObjectPath();
    for (const TSharedPtr<FWetWrinklePresetPaletteItem>& Item : WrinklePresetPaletteItems)
    {
        if (Item.IsValid() && (Item->PresetPath == UpdatedAssetPath || Item->ThumbnailTexturePath == UpdatedAssetPath))
        {
            RefreshWrinklePresetPaletteItemState(Item);
        }
    }

    RefreshWrinklePresetThumbnail();
}

TSharedRef<SWidget> SWetWrinkleEditorPanel::BuildWrinklePresetPalette()
{
    TSharedRef<SWrapBox> WrapBox = SAssignNew(WrinklePresetPaletteWrapBox, SWrapBox)
        .UseAllottedSize(true)
        .InnerSlotPadding(FVector2D(4.0f, 4.0f));

    RebuildWrinklePresetPaletteWidget();

    return SNew(SBox)
        .HeightOverride(332.0f)
            [SNew(SScrollBox)
             + SScrollBox::Slot()
                   [WrapBox]];
}

TSharedRef<SWidget> SWetWrinkleEditorPanel::GenerateWrinklePresetPaletteTile(TSharedPtr<FWetWrinklePresetPaletteItem> Item)
{
    return SNew(SButton)
        .ButtonStyle(&WrinklePresetPaletteButtonStyle)
        .ContentPadding(6.0f)
        .ButtonColorAndOpacity(this, &SWetWrinkleEditorPanel::GetWrinklePresetPaletteTileColor, Item)
        .Visibility(this, &SWetWrinkleEditorPanel::GetWrinklePresetPaletteTileVisibility, Item)
        .ToolTipText(this, &SWetWrinkleEditorPanel::GetWrinklePresetPaletteTooltipText, Item)
        .OnClicked(this, &SWetWrinkleEditorPanel::HandleWrinklePresetPaletteClicked, Item)
            [SNew(SBox)
                 .WidthOverride(144.0f)
                 .HeightOverride(144.0f)
                     [SNew(SScaleBox)
                          .Stretch(EStretch::ScaleToFit)
                          .StretchDirection(EStretchDirection::Both)
                              [SNew(SImage)
                                   .Image(Item.IsValid() ? &Item->ThumbnailBrush : nullptr)
                                   .Visibility(this, &SWetWrinkleEditorPanel::GetWrinklePresetPaletteThumbnailVisibility, Item)]]];
}

FReply SWetWrinkleEditorPanel::HandleWrinklePresetPaletteClicked(TSharedPtr<FWetWrinklePresetPaletteItem> Item)
{
    if (!Item.IsValid())
    {
        return FReply::Handled();
    }

    UWetWrinklePreset* Preset = Item->Preset.Get();
    if (Preset == nullptr && Item->PresetPath.IsValid())
    {
        Preset = Cast<UWetWrinklePreset>(Item->PresetPath.TryLoad());
        Item->Preset = Preset;
    }

    BrushSettings.WrinklePreset = Preset;
    if (Preset != nullptr)
    {
        const FWetWrinklePresetBrushDefaults& Defaults = Preset->BrushDefaults;
        SizeCm = FMath::Max(Defaults.DefaultSizeCm, 0.1f);
        SizeUV = SizeCm * WetWrinkleUVPerCm;
        BrushSettings.BrushRadiusUV = SizeUV;
        BrushSettings.Strength = FMath::Clamp(Defaults.DefaultStrength, 0.0f, 4.0f);
        BrushSettings.Falloff = FMath::Clamp(Defaults.DefaultFalloff, 0.0f, 1.0f);
    }

    RefreshWrinklePresetThumbnail();
    PushBrushSettingsToViewport();
    RefreshStrokeOverlay();
    RefreshStrokeList();
    RefreshWrinkleUVViewMarkersOnly();
    return FReply::Handled();
}

FReply SWetWrinkleEditorPanel::HandleRefreshWrinklePresetPaletteClicked()
{
    RefreshWrinklePresetPalette(true);
    RefreshWrinklePresetThumbnail();
    PushBrushSettingsToViewport();
    RefreshStrokeOverlay();
    RefreshStrokeList();
    RefreshWrinkleUVViewMarkersOnly();
    return FReply::Handled();
}

EVisibility SWetWrinkleEditorPanel::GetWrinklePresetPaletteThumbnailVisibility(TSharedPtr<FWetWrinklePresetPaletteItem> Item) const
{
    return Item.IsValid() && IsValid(Item->ThumbnailBrush.GetResourceObject()) ? EVisibility::Visible : EVisibility::Hidden;
}

EVisibility SWetWrinkleEditorPanel::GetWrinklePresetPaletteTileVisibility(TSharedPtr<FWetWrinklePresetPaletteItem> Item) const
{
    return Item.IsValid() && !Item->bRemoved ? EVisibility::Visible : EVisibility::Collapsed;
}

FText SWetWrinkleEditorPanel::GetWrinklePresetPaletteTooltipText(TSharedPtr<FWetWrinklePresetPaletteItem> Item) const
{
    if (!Item.IsValid())
    {
        return FText::GetEmpty();
    }

    const UWetWrinklePreset* Preset = Item->Preset.Get();
    FText StateText = LOCTEXT("WrinklePresetPaletteMissingTooltip", "Missing");
    if (Preset != nullptr)
    {
        if (Preset->IsUsableForBrush())
        {
            StateText = Preset->IsBuildStale()
                            ? LOCTEXT("WrinklePresetPaletteStaleTooltip", "Stale")
                            : LOCTEXT("WrinklePresetPaletteReadyTooltip", "Ready");
        }
    }

    return FText::Format(
        LOCTEXT("WrinklePresetPaletteTooltip", "{0}\nTexture Status : {1}"),
        Item->DisplayName,
        StateText);
}

FSlateColor SWetWrinkleEditorPanel::GetWrinklePresetPaletteTileColor(TSharedPtr<FWetWrinklePresetPaletteItem> Item) const
{
    const UWetWrinklePreset* ItemPreset = Item.IsValid() ? Item->Preset.Get() : nullptr;
    const bool bIsSelected = ItemPreset != nullptr &&
        (BrushSettings.WrinklePreset == ItemPreset ||
         (BrushSettings.WrinklePreset != nullptr && Item.IsValid() && Item->PresetPath == FSoftObjectPath(BrushSettings.WrinklePreset.Get())));
    if (bIsSelected)
    {
        return FSlateColor(FLinearColor(0.18f, 0.42f, 0.80f, 1.0f));
    }

    const bool bUsable = ItemPreset != nullptr && ItemPreset->IsUsableForBrush();
    if (!bUsable)
    {
        return FSlateColor(FLinearColor(0.15f, 0.08f, 0.08f, 1.0f));
    }

    if (ItemPreset->IsBuildStale())
    {
        return FSlateColor(FLinearColor(0.22f, 0.17f, 0.05f, 1.0f));
    }

    return FSlateColor(FLinearColor(0.10f, 0.10f, 0.10f, 1.0f));
}


FText SWetWrinkleEditorPanel::GetDWCDataUVChannelText() const
{
    const int32 DataUVChannelIndex = GetWrinkleUVViewChannelIndex();
    if (DataUVChannelIndex == INDEX_NONE)
    {
        return LOCTEXT("NoMeshUVChannelSelected", "Unavailable");
    }

    return FText::Format(
        LOCTEXT("SelectedDWCDataUVChannel", "UV {0}"),
        FText::AsNumber(DataUVChannelIndex));
}

TSharedRef<SWidget> SWetWrinkleEditorPanel::GenerateUVDisplayModeComboItem(FUVDisplayModeItemPtr Item) const
{
    return FWCAEditorWidgets::GenerateUVDisplayModeComboItem(Item);
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
    return FWCAEditorWidgets::GetUVDisplayModeLabel(
        SelectedUVDisplayModeItem.IsValid() ? *SelectedUVDisplayModeItem : EWCAUVDisplayMode::Normal);
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

    const int32 UVChannelIndex = ResolveWetWrinkleUVChannel(WetClothingAsset.Get());
    if (!HasUsableWrinkleUVChannel())
    {
        FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("GenerateWrinkleTextureNoUV", "The target mesh does not have the configured DWC Data UV channel."));
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
    if (bCapturingProceduralRidgeStroke && bProceduralRidgeCaptureBlocked)
    {
        return LOCTEXT(
            "RidgeStrokeCaptureStopped",
            "Ridge stroke stopped at a material, UV island, or mesh-surface boundary. Release the mouse to keep the valid segment.");
    }

    if (!CurrentHit.bHit)
    {
        return LOCTEXT("NoSurfaceHit", "No mesh surface under cursor.");
    }

    return FText::FromString(FString::Printf(
        TEXT("Slot: %d\nTriangle: %d / UV Island: %d\nUV%d: %.4f, %.4f\nPosition: %.1f, %.1f, %.1f"),
        CurrentHit.MaterialSlotIndex,
        CurrentHit.TriangleID,
        CurrentHit.UVIslandID,
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
    const int32 RidgeStrokeCount = Asset != nullptr ? Asset->WrinkleData.EditableProceduralRidgeStrokes.Num() : 0;
    int32 StampCount = 0;
    if (Asset != nullptr)
    {
        for (const FWetWrinklePatchStroke& Stroke : Asset->WrinkleData.EditablePatchStrokes)
        {
            StampCount += Stroke.PatchPlacements.Num();
        }
    }

    return FText::Format(
        LOCTEXT("PatchListSummary", "{0} patch list(s), {1} patch(es), {2} ridge stroke(s)."),
        FText::AsNumber(StrokeCount),
        FText::AsNumber(StampCount),
        FText::AsNumber(RidgeStrokeCount));
}

FText SWetWrinkleEditorPanel::GetBrushSectionHeadingText() const
{
    return BrushSettings.ToolMode == EWetWrinkleToolMode::ProceduralRidgeStroke
               ? LOCTEXT("RidgeStrokeBrushHeading", "Procedural Ridge Stroke")
               : LOCTEXT("PatchBrushHeading", "Patch Brush");
}

FText SWetWrinkleEditorPanel::GetBrushSizeLabelText() const
{
    return BrushSettings.ToolMode == EWetWrinkleToolMode::ProceduralRidgeStroke
               ? LOCTEXT("RidgeWidthLabel", "Width (cm)")
               : LOCTEXT("SizeLabel", "Size (cm)");
}

ECheckBoxState SWetWrinkleEditorPanel::GetToolModeCheckState(EWetWrinkleToolMode ToolMode) const
{
    return BrushSettings.ToolMode == ToolMode ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SWetWrinkleEditorPanel::HandleToolModeChanged(ECheckBoxState NewState, EWetWrinkleToolMode ToolMode)
{
    if (NewState != ECheckBoxState::Checked || BrushSettings.ToolMode == ToolMode)
    {
        return;
    }

    EndProceduralRidgePointEdit(true);
    ActiveRidgePropertyTransaction.Reset();
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->ClearTransientProceduralStroke();
        PreviewViewport->SetEditingProceduralStrokeGuid(FGuid());
    }
    CancelProceduralRidgeStroke();
    BrushSettings.ToolMode = ToolMode;
    SelectedProceduralRidgePointIndex = INDEX_NONE;
    CurrentHit = FWetWrinkleSurfaceHit();
    PushBrushSettingsToViewport();
    RefreshWrinkleUVViewMarkersOnly();
}

ECheckBoxState SWetWrinkleEditorPanel::GetRidgeEditModeCheckState(EWetProceduralRidgeEditMode EditMode) const
{
    return BrushSettings.RidgeEditMode == EditMode ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SWetWrinkleEditorPanel::HandleRidgeEditModeChanged(ECheckBoxState NewState, EWetProceduralRidgeEditMode EditMode)
{
    if (NewState != ECheckBoxState::Checked || BrushSettings.RidgeEditMode == EditMode)
    {
        return;
    }

    EndProceduralRidgePointEdit(true);
    ActiveRidgePropertyTransaction.Reset();
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->ClearTransientProceduralStroke();
        PreviewViewport->SetEditingProceduralStrokeGuid(FGuid());
    }
    CancelProceduralRidgeStroke();
    BrushSettings.RidgeEditMode = EditMode;
    SelectedProceduralRidgePointIndex = INDEX_NONE;
    PushBrushSettingsToViewport();
    RefreshStrokeOverlay(false);
}

ECheckBoxState SWetWrinkleEditorPanel::GetRidgeJunctionModeCheckState() const
{
    return BrushSettings.bRidgeJunctionModeEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SWetWrinkleEditorPanel::HandleRidgeJunctionModeChanged(const ECheckBoxState NewState)
{
    const bool bEnabled = NewState == ECheckBoxState::Checked;
    if (BrushSettings.bRidgeJunctionModeEnabled == bEnabled)
    {
        return;
    }

    EndProceduralRidgePointEdit(true);
    CancelProceduralRidgeStroke();
    BrushSettings.bRidgeJunctionModeEnabled = bEnabled;
    PushBrushSettingsToViewport();
    RefreshStrokeOverlay(false);
}

ECheckBoxState SWetWrinkleEditorPanel::GetRidgeShapeCheckState(const EWetProceduralRidgeShape Shape) const
{
    return BrushSettings.RidgeShape == Shape ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SWetWrinkleEditorPanel::HandleRidgeShapeChanged(
    const ECheckBoxState NewState,
    const EWetProceduralRidgeShape Shape)
{
    if (NewState != ECheckBoxState::Checked || BrushSettings.RidgeShape == Shape)
    {
        return;
    }

    BrushSettings.RidgeShape = Shape;
    ApplyBrushSettingsToSelectedProceduralStroke();
    PushBrushSettingsToViewport();

    if (ActiveRidgePropertyTransaction.IsValid())
    {
        ActiveRidgePropertyTransaction.Reset();
        if (PreviewViewport.IsValid())
        {
            PreviewViewport->ClearTransientProceduralStroke();
            PreviewViewport->SetEditingProceduralStrokeGuid(FGuid());
        }
        RefreshStrokeOverlay(true);
    }
}

EVisibility SWetWrinkleEditorPanel::GetFoldOptionsVisibility() const
{
    return BrushSettings.ToolMode == EWetWrinkleToolMode::ProceduralRidgeStroke &&
                   BrushSettings.RidgeShape == EWetProceduralRidgeShape::Fold
               ? EVisibility::Visible
               : EVisibility::Collapsed;
}

ECheckBoxState SWetWrinkleEditorPanel::GetFlipFoldSideCheckState() const
{
    return BrushSettings.bFlipRidgeFoldSide ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SWetWrinkleEditorPanel::HandleFlipFoldSideChanged(const ECheckBoxState NewState)
{
    const bool bNewFlipFoldSide = NewState == ECheckBoxState::Checked;
    if (BrushSettings.bFlipRidgeFoldSide == bNewFlipFoldSide)
    {
        return;
    }

    BrushSettings.bFlipRidgeFoldSide = bNewFlipFoldSide;
    ApplyBrushSettingsToSelectedProceduralStroke();
    PushBrushSettingsToViewport();

    if (ActiveRidgePropertyTransaction.IsValid())
    {
        ActiveRidgePropertyTransaction.Reset();
        if (PreviewViewport.IsValid())
        {
            PreviewViewport->ClearTransientProceduralStroke();
            PreviewViewport->SetEditingProceduralStrokeGuid(FGuid());
        }
        RefreshStrokeOverlay(true);
    }
}

EVisibility SWetWrinkleEditorPanel::GetPatchToolVisibility() const
{
    return BrushSettings.ToolMode == EWetWrinkleToolMode::Patch ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SWetWrinkleEditorPanel::GetProceduralRidgeToolVisibility() const
{
    return BrushSettings.ToolMode == EWetWrinkleToolMode::ProceduralRidgeStroke ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SWetWrinkleEditorPanel::GetProceduralRidgeEditVisibility() const
{
    return BrushSettings.ToolMode == EWetWrinkleToolMode::ProceduralRidgeStroke &&
                   BrushSettings.RidgeEditMode == EWetProceduralRidgeEditMode::Edit
               ? EVisibility::Visible
               : EVisibility::Collapsed;
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
    RefreshDWCDataUVChannel();
    RefreshMaterialTextures();
    PushBrushSettingsToViewport();
    RefreshStrokeOverlay();
    RefreshPartMapItems();
    RefreshWrinkleUVView();
}


FText SWetWrinkleEditorPanel::GetMaterialSlotStatusText(const int32 MaterialSlotIndex) const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || MaterialSlotIndex == INDEX_NONE)
    {
        return FText::GetEmpty();
    }

    int32 PatchCount = 0;
    for (const FWetWrinklePatchStroke& Stroke : Asset->WrinkleData.EditablePatchStrokes)
    {
        for (const FWetWrinklePatchPlacement& Patch : Stroke.PatchPlacements)
        {
            if (Patch.MaterialSlotIndex == MaterialSlotIndex)
            {
                ++PatchCount;
            }
        }
    }

    if (PatchCount <= 0)
    {
        return NSLOCTEXT("SWetWrinkleEditorPanel", "MaterialSlotStatusNoPatches", "No Patches");
    }

    return PatchCount == 1
        ? NSLOCTEXT("SWetWrinkleEditorPanel", "MaterialSlotStatusOnePatch", "1 Patch")
        : FText::Format(NSLOCTEXT("SWetWrinkleEditorPanel", "MaterialSlotStatusManyPatches", "{0} Patches"), FText::AsNumber(PatchCount));
}

TSharedRef<ITableRow> SWetWrinkleEditorPanel::GenerateMaterialSlotRow(FMaterialSlotItemPtr Item, const TSharedRef<STableViewBase>& OwnerTable)
{
    FWCAMaterialSlotRowArgs Args;
    Args.WetClothingAsset = WetClothingAsset.Get();
    Args.GeneratedDataUV = WetClothingAsset.IsValid() ? WetClothingAsset->GetRuntimeSkeletalMesh() : nullptr;
    Args.ThumbnailPool = MaterialThumbnailPool;
    Args.ThumbnailSink = &MaterialSlotThumbnails;
    Args.OnWettableSlotClicked = FOnWettableMaterialSlotClicked::CreateSP(this, &SWetWrinkleEditorPanel::HandleWettableMaterialSlotClicked);
    Args.GetMaterialSlotStatusText = [this](const int32 MaterialSlotIndex)
    {
        return GetMaterialSlotStatusText(MaterialSlotIndex);
    };

    return FWCAEditorWidgets::GenerateMaterialSlotRow(Item, OwnerTable, Args);
}

void SWetWrinkleEditorPanel::HandleMaterialSlotSelectionChanged(FMaterialSlotItemPtr Item, ESelectInfo::Type SelectInfo)
{
    BrushSettings.MaterialSlotIndex = Item.IsValid() ? Item->SlotIndex : INDEX_NONE;
    CurrentHit = FWetWrinkleSurfaceHit();
    EnsureWrinkleUVChannelForMaterialSlot(BrushSettings.MaterialSlotIndex, false);
    RefreshDWCDataUVChannel();
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

    const bool bNewWettable = !FWCAEditorWidgets::IsMaterialSlotWettable(Asset, MaterialSlotIndex);
    FWCAEditorWidgets::SetMaterialSlotWettable(Asset, MaterialSlotIndex, bNewWettable);

    if (FMaterialSlotItemPtr SlotItem = FindMaterialSlotItem(MaterialSlotIndex))
    {
        SlotItem->bIsWettableSlot = bNewWettable;
    }
    if (MaterialSlotListView.IsValid())
    {
        MaterialSlotListView->Invalidate(EInvalidateWidget::Paint);
    }

    return FReply::Handled();
}

TSharedRef<ITableRow> SWetWrinkleEditorPanel::GeneratePartMapRow(FWetPartEntryPtr Item, const TSharedRef<STableViewBase>& OwnerTable)
{
    return FWCAEditorWidgets::GeneratePartMapRow(Item, OwnerTable);
}

FString SWetWrinkleEditorPanel::GetWrinklePresetObjectPath() const
{
    return BrushSettings.WrinklePreset != nullptr ? BrushSettings.WrinklePreset->GetPathName() : FString();
}

void SWetWrinkleEditorPanel::HandleWrinklePresetChanged(const FAssetData& AssetData)
{
    BrushSettings.WrinklePreset = Cast<UWetWrinklePreset>(AssetData.GetAsset());
    FString UnusableReason;
    IsCurrentWrinklePresetUsable(&UnusableReason);

    if (BrushSettings.WrinklePreset != nullptr)
    {
        const FWetWrinklePresetBrushDefaults& Defaults = BrushSettings.WrinklePreset->BrushDefaults;
        SizeCm = FMath::Max(Defaults.DefaultSizeCm, 0.1f);
        SizeUV = SizeCm * WetWrinkleUVPerCm;
        BrushSettings.BrushRadiusUV = SizeUV;
        BrushSettings.Strength = FMath::Clamp(Defaults.DefaultStrength, 0.0f, 4.0f);
        BrushSettings.Falloff = FMath::Clamp(Defaults.DefaultFalloff, 0.0f, 1.0f);
    }

    RefreshWrinklePresetThumbnail();
    PushBrushSettingsToViewport();
    RefreshStrokeOverlay();
    RefreshStrokeList();
    RefreshWrinkleUVViewMarkersOnly();
}

void SWetWrinkleEditorPanel::RefreshWrinklePresetThumbnail()
{
    UTexture2D* CorrectedNormalTexture = BrushSettings.WrinklePreset != nullptr
                                             ? BrushSettings.WrinklePreset->GetNormalTextureForBrush()
                                             : nullptr;
    SelectedWrinklePresetThumbnailBrush.SetResourceObject(CorrectedNormalTexture);
    SelectedWrinklePresetThumbnailBrush.SetImageSize(
        CorrectedNormalTexture != nullptr
            ? FVector2D(FMath::Max(CorrectedNormalTexture->GetSizeX(), 1), FMath::Max(CorrectedNormalTexture->GetSizeY(), 1))
            : FVector2D(128.0f, 128.0f));
}

const FSlateBrush* SWetWrinkleEditorPanel::GetWrinklePresetThumbnailBrush() const
{
    return &SelectedWrinklePresetThumbnailBrush;
}

EVisibility SWetWrinkleEditorPanel::GetWrinklePresetThumbnailVisibility() const
{
    return IsValid(SelectedWrinklePresetThumbnailBrush.GetResourceObject()) ? EVisibility::Visible : EVisibility::Hidden;
}

FText SWetWrinkleEditorPanel::GetWrinklePresetStatusText() const
{
    const UWetWrinklePreset* Preset = BrushSettings.WrinklePreset.Get();
    if (Preset == nullptr)
    {
        return LOCTEXT("WrinklePresetNoSelection", "No preset selected.");
    }

    FString Reason;
    if (!Preset->IsUsableForBrush(&Reason))
    {
        return FText::FromString(Reason);
    }

    if (Preset->IsBuildStale())
    {
        return LOCTEXT("WrinklePresetStale", "Preset is stale. Existing generated textures will be used; rebuild is recommended.");
    }

    return LOCTEXT("WrinklePresetReady", "Ready.");
}

FSlateColor SWetWrinkleEditorPanel::GetWrinklePresetStatusColor() const
{
    const UWetWrinklePreset* Preset = BrushSettings.WrinklePreset.Get();
    if (Preset == nullptr || !Preset->IsUsableForBrush())
    {
        return FSlateColor(FLinearColor(1.0f, 0.35f, 0.25f));
    }

    if (Preset->IsBuildStale())
    {
        return FSlateColor(FLinearColor(1.0f, 0.75f, 0.25f));
    }

    return FSlateColor(FLinearColor(0.35f, 0.9f, 0.45f));
}

FReply SWetWrinkleEditorPanel::HandleOpenWrinklePresetClicked()
{
    if (UAssetEditorSubsystem* AssetEditorSubsystem = GEditor != nullptr ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr)
    {
        if (BrushSettings.WrinklePreset != nullptr)
        {
            AssetEditorSubsystem->OpenEditorForAsset(BrushSettings.WrinklePreset.Get());
        }
    }

    return FReply::Handled();
}

bool SWetWrinkleEditorPanel::CanOpenWrinklePreset() const
{
    return BrushSettings.WrinklePreset != nullptr;
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
                                                  if (!Item.IsValid())
                                                  {
                                                      return ECheckBoxState::Unchecked;
                                                  }
                                                  if (Item->ElementType == EWetWrinkleElementType::ProceduralRidgeStroke)
                                                  {
                                                      const FWetProceduralRidgeStroke* Stroke = FindProceduralRidgeStroke(Item->StrokeGuid);
                                                      return Stroke != nullptr && Stroke->bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
                                                  }
                                                  const FWetWrinklePatchStroke* Stroke = FindStroke(Item->StrokeGuid);
                                                  return Stroke != nullptr && Stroke->bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
                                              })
                            .OnCheckStateChanged(this, &SWetWrinkleEditorPanel::HandleStrokeEnabledChanged, Item)]

             + SHorizontalBox::Slot()
                   .FillWidth(1.0f)
                   .VAlign(VAlign_Center)
                       [SNew(SInlineEditableTextBlock)
                            .Text_Lambda([this, Item]()
                                         {
                                             if (Item.IsValid() && Item->ElementType == EWetWrinkleElementType::ProceduralRidgeStroke)
                                             {
                                                 const FWetProceduralRidgeStroke* Stroke = FindProceduralRidgeStroke(Item->StrokeGuid);
                                                 return Stroke != nullptr ? FText::FromString(Stroke->DisplayName) : LOCTEXT("MissingRidgeStrokeName", "<missing ridge>");
                                             }
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
                                             if (Item.IsValid() && Item->ElementType == EWetWrinkleElementType::ProceduralRidgeStroke)
                                             {
                                                 const FWetProceduralRidgeStroke* Stroke = FindProceduralRidgeStroke(Item->StrokeGuid);
                                                 return FText::Format(
                                                     LOCTEXT("RidgeStrokePointCount", "Ridge / {0}"),
                                                     FText::AsNumber(Stroke != nullptr ? Stroke->Points.Num() : 0));
                                             }
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
    EndProceduralRidgePointEdit(true);
    ActiveRidgePropertyTransaction.Reset();
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->ClearTransientProceduralStroke();
        PreviewViewport->SetEditingProceduralStrokeGuid(FGuid());
    }
    SelectedStrokeGuid = Item.IsValid() ? Item->StrokeGuid : FGuid();
    SelectedElementType = Item.IsValid() ? Item->ElementType : EWetWrinkleElementType::PatchStroke;
    SelectedProceduralRidgePointIndex = INDEX_NONE;
    if (const FWetProceduralRidgeStroke* Stroke =
            SelectedElementType == EWetWrinkleElementType::ProceduralRidgeStroke
                ? FindProceduralRidgeStroke(SelectedStrokeGuid)
                : nullptr)
    {
        BrushSettings.RidgeShape = Stroke->Shape;
        BrushSettings.bFlipRidgeFoldSide = Stroke->bFlipFoldSide;
        SizeUV = Stroke->WidthUV;
        SizeCm = FMath::Clamp(SizeUV / WetWrinkleUVPerCm, 0.1f, 100.0f);
        BrushSettings.BrushRadiusUV = Stroke->WidthUV;
        BrushSettings.Strength = Stroke->Strength;
        BrushSettings.Falloff = Stroke->Falloff;
        BrushSettings.RidgeStartTaper = Stroke->StartTaper;
        BrushSettings.RidgeEndTaper = Stroke->EndTaper;
        BrushSettings.RidgeFlareSettings = Stroke->FlareSettings;
        BrushSettings.RidgeNaturalVariation = Stroke->NaturalVariation;
        PushBrushSettingsToViewport();
    }
    RefreshStrokeOverlay(false);
}

FReply SWetWrinkleEditorPanel::HandleClearStrokesClicked()
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr ||
        (Asset->WrinkleData.EditablePatchStrokes.Num() == 0 && Asset->WrinkleData.EditableProceduralRidgeStrokes.Num() == 0))
    {
        return FReply::Handled();
    }

    const FScopedTransaction Transaction(LOCTEXT("ClearWetWrinkleStrokesTransaction", "Clear Wet Wrinkle Patch Lists"));
    Asset->Modify();
    Asset->WrinkleData.EditablePatchStrokes.Reset();
    Asset->WrinkleData.EditableProceduralRidgeStrokes.Reset();
    ActiveStrokeGuid.Invalidate();
    SelectedStrokeGuid.Invalidate();
    SelectedElementType = EWetWrinkleElementType::PatchStroke;
    bHasLastStamp = false;
    CancelProceduralRidgeStroke();
    MarkAssetEdited();
    RefreshStrokeList();
    RefreshStrokeOverlay();
    return FReply::Handled();
}

bool SWetWrinkleEditorPanel::IsClearStrokesEnabled() const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    return Asset != nullptr &&
           (Asset->WrinkleData.EditablePatchStrokes.Num() > 0 || Asset->WrinkleData.EditableProceduralRidgeStrokes.Num() > 0);
}

void SWetWrinkleEditorPanel::HandleStrokeEnabledChanged(ECheckBoxState NewState, FStrokeListItemPtr Item)
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || !Item.IsValid())
    {
        return;
    }

    const bool bNewEnabled = NewState == ECheckBoxState::Checked;
    if (Item->ElementType == EWetWrinkleElementType::ProceduralRidgeStroke)
    {
        FWetProceduralRidgeStroke* Stroke = FindMutableProceduralRidgeStroke(Item->StrokeGuid);
        if (Stroke == nullptr || Stroke->bEnabled == bNewEnabled)
        {
            return;
        }

        const FScopedTransaction Transaction(LOCTEXT("ToggleProceduralRidgeStrokeTransaction", "Toggle Procedural Ridge Stroke"));
        Asset->Modify();
        Stroke->bEnabled = bNewEnabled;
        MarkAssetEdited();
        RefreshStrokeOverlay();
        return;
    }

    FWetWrinklePatchStroke* Stroke = FindMutableStroke(Item->StrokeGuid);
    if (Stroke == nullptr)
    {
        return;
    }
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
    if (Asset == nullptr || !Item.IsValid())
    {
        return;
    }

    const FString NewName = InText.ToString().TrimStartAndEnd();
    if (NewName.IsEmpty())
    {
        return;
    }

    if (Item->ElementType == EWetWrinkleElementType::ProceduralRidgeStroke)
    {
        FWetProceduralRidgeStroke* Stroke = FindMutableProceduralRidgeStroke(Item->StrokeGuid);
        if (Stroke == nullptr || Stroke->DisplayName == NewName)
        {
            return;
        }
        const FScopedTransaction Transaction(LOCTEXT("RenameProceduralRidgeStrokeTransaction", "Rename Procedural Ridge Stroke"));
        Asset->Modify();
        Stroke->DisplayName = NewName;
        MarkAssetEdited();
        RefreshStrokeList();
        return;
    }

    FWetWrinklePatchStroke* Stroke = FindMutableStroke(Item->StrokeGuid);
    if (Stroke == nullptr || Stroke->DisplayName == NewName)
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

    if (Item->ElementType == EWetWrinkleElementType::ProceduralRidgeStroke)
    {
        const int32 RidgeIndex = Asset->WrinkleData.EditableProceduralRidgeStrokes.IndexOfByPredicate(
            [Item](const FWetProceduralRidgeStroke& Stroke)
            {
                return Stroke.StrokeGuid == Item->StrokeGuid;
            });
        if (RidgeIndex == INDEX_NONE)
        {
            return FReply::Handled();
        }

        const FScopedTransaction Transaction(LOCTEXT("DeleteProceduralRidgeStrokeTransaction", "Delete Procedural Ridge Stroke"));
        Asset->Modify();
        const FGuid DeletedGuid = Asset->WrinkleData.EditableProceduralRidgeStrokes[RidgeIndex].StrokeGuid;
        ClearConnectionsToStroke(DeletedGuid);
        Asset->WrinkleData.EditableProceduralRidgeStrokes.RemoveAt(RidgeIndex);
        if (SelectedStrokeGuid == DeletedGuid && SelectedElementType == EWetWrinkleElementType::ProceduralRidgeStroke)
        {
            SelectedStrokeGuid.Invalidate();
            SelectedProceduralRidgePointIndex = INDEX_NONE;
        }
        MarkAssetEdited();
        RefreshStrokeList();
        RefreshStrokeOverlay();
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
    if (BrushSettings.ToolMode == EWetWrinkleToolMode::ProceduralRidgeStroke &&
        BrushSettings.RidgeEditMode == EWetProceduralRidgeEditMode::Edit &&
        SelectedElementType == EWetWrinkleElementType::ProceduralRidgeStroke)
    {
        if (const FWetProceduralRidgeStroke* Stroke = FindProceduralRidgeStroke(SelectedStrokeGuid))
        {
            return FMath::Clamp(Stroke->WidthUV / WetWrinkleUVPerCm, 0.1f, 100.0f);
        }
    }
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
    ApplyBrushSettingsToSelectedProceduralStroke();
    PushBrushSettingsToViewport();
    RefreshWrinkleUVView();
}

FReply SWetWrinkleEditorPanel::HandleBrushSizePresetClicked(float NewValue)
{
    HandleBrushRadiusChanged(NewValue);
    HandleRidgePropertyCommitted(NewValue, ETextCommit::Default);
    if (BrushSizeComboButton.IsValid())
    {
        BrushSizeComboButton->SetIsOpen(false);
    }

    return FReply::Handled();
}

void SWetWrinkleEditorPanel::HandleStrengthChanged(float NewValue)
{
    BrushSettings.Strength = FMath::Clamp(NewValue, 0.0f, 4.0f);
    ApplyBrushSettingsToSelectedProceduralStroke();
    PushBrushSettingsToViewport();
}

void SWetWrinkleEditorPanel::HandleFalloffChanged(float NewValue)
{
    BrushSettings.Falloff = FMath::Clamp(NewValue / 100.0f, 0.0f, 1.0f);
    ApplyBrushSettingsToSelectedProceduralStroke();
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

void SWetWrinkleEditorPanel::HandleRidgeStartTaperChanged(float NewValue)
{
    BrushSettings.RidgeStartTaper = FMath::Clamp(NewValue, 0.0f, 0.5f);
    ApplyBrushSettingsToSelectedProceduralStroke();
    PushBrushSettingsToViewport();
}

void SWetWrinkleEditorPanel::HandleRidgeEndTaperChanged(float NewValue)
{
    BrushSettings.RidgeEndTaper = FMath::Clamp(NewValue, 0.0f, 0.5f);
    ApplyBrushSettingsToSelectedProceduralStroke();
    PushBrushSettingsToViewport();
}

float SWetWrinkleEditorPanel::GetRidgeFlareLengthValue() const
{
    return BrushSettings.RidgeFlareSettings.Length;
}

void SWetWrinkleEditorPanel::HandleRidgeFlareLengthChanged(const float NewValue)
{
    BrushSettings.RidgeFlareSettings.Length = FMath::Clamp(NewValue, 0.01f, 0.5f);
    ApplyBrushSettingsToSelectedProceduralStroke();
    PushBrushSettingsToViewport();
}

float SWetWrinkleEditorPanel::GetRidgeFlareWidthValue() const
{
    return BrushSettings.RidgeFlareSettings.WidthScale;
}

void SWetWrinkleEditorPanel::HandleRidgeFlareWidthChanged(const float NewValue)
{
    BrushSettings.RidgeFlareSettings.WidthScale = FMath::Clamp(NewValue, 1.0f, 5.0f);
    ApplyBrushSettingsToSelectedProceduralStroke();
    PushBrushSettingsToViewport();
}

float SWetWrinkleEditorPanel::GetRidgeFlareEndStrengthValue() const
{
    return BrushSettings.RidgeFlareSettings.EndStrength;
}

void SWetWrinkleEditorPanel::HandleRidgeFlareEndStrengthChanged(const float NewValue)
{
    BrushSettings.RidgeFlareSettings.EndStrength = FMath::Clamp(NewValue, 0.0f, 1.0f);
    ApplyBrushSettingsToSelectedProceduralStroke();
    PushBrushSettingsToViewport();
}

float SWetWrinkleEditorPanel::GetRidgeFlareSoftnessValue() const
{
    return BrushSettings.RidgeFlareSettings.Softness;
}

void SWetWrinkleEditorPanel::HandleRidgeFlareSoftnessChanged(const float NewValue)
{
    BrushSettings.RidgeFlareSettings.Softness = FMath::Clamp(NewValue, 0.0f, 1.0f);
    ApplyBrushSettingsToSelectedProceduralStroke();
    PushBrushSettingsToViewport();
}

void SWetWrinkleEditorPanel::HandleRidgePointSpacingChanged(float NewValue)
{
    BrushSettings.RidgePointSpacingScale = FMath::Clamp(NewValue, 0.05f, 1.0f);
}

ECheckBoxState SWetWrinkleEditorPanel::GetRidgeNaturalVariationEnabledState() const
{
    return BrushSettings.RidgeNaturalVariation.bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SWetWrinkleEditorPanel::HandleRidgeNaturalVariationEnabledChanged(const ECheckBoxState NewState)
{
    const bool bEnabled = NewState == ECheckBoxState::Checked;
    if (BrushSettings.RidgeNaturalVariation.bEnabled == bEnabled)
    {
        return;
    }

    BrushSettings.RidgeNaturalVariation.bEnabled = bEnabled;
    ApplyBrushSettingsToSelectedProceduralStroke();
    PushBrushSettingsToViewport();
    if (ActiveRidgePropertyTransaction.IsValid())
    {
        ActiveRidgePropertyTransaction.Reset();
        if (PreviewViewport.IsValid())
        {
            PreviewViewport->ClearTransientProceduralStroke();
            PreviewViewport->SetEditingProceduralStrokeGuid(FGuid());
        }
        RefreshStrokeOverlay(true);
    }
}

float SWetWrinkleEditorPanel::GetRidgeCenterlineVariationValue() const
{
    return BrushSettings.RidgeNaturalVariation.CenterlineAmount;
}

void SWetWrinkleEditorPanel::HandleRidgeCenterlineVariationChanged(const float NewValue)
{
    BrushSettings.RidgeNaturalVariation.CenterlineAmount = FMath::Clamp(NewValue, 0.0f, 0.5f);
    ApplyBrushSettingsToSelectedProceduralStroke();
    PushBrushSettingsToViewport();
}

float SWetWrinkleEditorPanel::GetRidgeCenterlineFrequencyValue() const
{
    return BrushSettings.RidgeNaturalVariation.CenterlineFrequency;
}

void SWetWrinkleEditorPanel::HandleRidgeCenterlineFrequencyChanged(const float NewValue)
{
    BrushSettings.RidgeNaturalVariation.CenterlineFrequency = FMath::Clamp(NewValue, 0.25f, 12.0f);
    ApplyBrushSettingsToSelectedProceduralStroke();
    PushBrushSettingsToViewport();
}

float SWetWrinkleEditorPanel::GetRidgeWidthVariationValue() const
{
    return BrushSettings.RidgeNaturalVariation.WidthVariation;
}

void SWetWrinkleEditorPanel::HandleRidgeWidthVariationChanged(const float NewValue)
{
    BrushSettings.RidgeNaturalVariation.WidthVariation = FMath::Clamp(NewValue, 0.0f, 0.5f);
    ApplyBrushSettingsToSelectedProceduralStroke();
    PushBrushSettingsToViewport();
}

float SWetWrinkleEditorPanel::GetRidgeWidthFrequencyValue() const
{
    return BrushSettings.RidgeNaturalVariation.WidthFrequency;
}

void SWetWrinkleEditorPanel::HandleRidgeWidthFrequencyChanged(const float NewValue)
{
    BrushSettings.RidgeNaturalVariation.WidthFrequency = FMath::Clamp(NewValue, 0.25f, 12.0f);
    ApplyBrushSettingsToSelectedProceduralStroke();
    PushBrushSettingsToViewport();
}

int32 SWetWrinkleEditorPanel::GetRidgeNoiseSeedValue() const
{
    return BrushSettings.RidgeNaturalVariation.NoiseSeed;
}

void SWetWrinkleEditorPanel::HandleRidgeNoiseSeedChanged(const int32 NewValue)
{
    if (BrushSettings.RidgeNaturalVariation.NoiseSeed == NewValue)
    {
        return;
    }

    BrushSettings.RidgeNaturalVariation.NoiseSeed = NewValue;
    ApplyBrushSettingsToSelectedProceduralStroke();
    PushBrushSettingsToViewport();
    if (ActiveRidgePropertyTransaction.IsValid())
    {
        ActiveRidgePropertyTransaction.Reset();
        if (PreviewViewport.IsValid())
        {
            PreviewViewport->ClearTransientProceduralStroke();
            PreviewViewport->SetEditingProceduralStrokeGuid(FGuid());
        }
        RefreshStrokeOverlay(true);
    }
}

FReply SWetWrinkleEditorPanel::HandleRandomizeRidgeNoiseSeedClicked()
{
    int32 NewSeed = static_cast<int32>(FPlatformTime::Cycles64() & 0x7FFFFFFF);
    if (NewSeed == BrushSettings.RidgeNaturalVariation.NoiseSeed)
    {
        ++NewSeed;
    }
    HandleRidgeNoiseSeedChanged(NewSeed);
    return FReply::Handled();
}

float SWetWrinkleEditorPanel::GetRidgeStrengthValue() const
{
    if (BrushSettings.ToolMode == EWetWrinkleToolMode::ProceduralRidgeStroke &&
        BrushSettings.RidgeEditMode == EWetProceduralRidgeEditMode::Edit &&
        SelectedElementType == EWetWrinkleElementType::ProceduralRidgeStroke)
    {
        if (const FWetProceduralRidgeStroke* Stroke = FindProceduralRidgeStroke(SelectedStrokeGuid))
        {
            return Stroke->Strength;
        }
    }
    return BrushSettings.Strength;
}

float SWetWrinkleEditorPanel::GetRidgeFalloffPercentValue() const
{
    if (BrushSettings.ToolMode == EWetWrinkleToolMode::ProceduralRidgeStroke &&
        BrushSettings.RidgeEditMode == EWetProceduralRidgeEditMode::Edit &&
        SelectedElementType == EWetWrinkleElementType::ProceduralRidgeStroke)
    {
        if (const FWetProceduralRidgeStroke* Stroke = FindProceduralRidgeStroke(SelectedStrokeGuid))
        {
            return Stroke->Falloff * 100.0f;
        }
    }
    return BrushSettings.Falloff * 100.0f;
}

float SWetWrinkleEditorPanel::GetRidgeStartTaperValue() const
{
    if (BrushSettings.ToolMode == EWetWrinkleToolMode::ProceduralRidgeStroke &&
        BrushSettings.RidgeEditMode == EWetProceduralRidgeEditMode::Edit &&
        SelectedElementType == EWetWrinkleElementType::ProceduralRidgeStroke)
    {
        if (const FWetProceduralRidgeStroke* Stroke = FindProceduralRidgeStroke(SelectedStrokeGuid))
        {
            return Stroke->StartTaper;
        }
    }
    return BrushSettings.RidgeStartTaper;
}

float SWetWrinkleEditorPanel::GetRidgeEndTaperValue() const
{
    if (BrushSettings.ToolMode == EWetWrinkleToolMode::ProceduralRidgeStroke &&
        BrushSettings.RidgeEditMode == EWetProceduralRidgeEditMode::Edit &&
        SelectedElementType == EWetWrinkleElementType::ProceduralRidgeStroke)
    {
        if (const FWetProceduralRidgeStroke* Stroke = FindProceduralRidgeStroke(SelectedStrokeGuid))
        {
            return Stroke->EndTaper;
        }
    }
    return BrushSettings.RidgeEndTaper;
}

void SWetWrinkleEditorPanel::ApplyBrushSettingsToSelectedProceduralStroke()
{
    if (BrushSettings.ToolMode != EWetWrinkleToolMode::ProceduralRidgeStroke ||
        BrushSettings.RidgeEditMode != EWetProceduralRidgeEditMode::Edit ||
        SelectedElementType != EWetWrinkleElementType::ProceduralRidgeStroke)
    {
        return;
    }

    UWetClothingAsset* Asset = WetClothingAsset.Get();
    FWetProceduralRidgeStroke* Stroke = FindMutableProceduralRidgeStroke(SelectedStrokeGuid);
    if (Asset == nullptr || Stroke == nullptr)
    {
        return;
    }

    if (Stroke->Shape == BrushSettings.RidgeShape &&
        Stroke->bFlipFoldSide == BrushSettings.bFlipRidgeFoldSide &&
        FMath::IsNearlyEqual(Stroke->WidthUV, BrushSettings.BrushRadiusUV) &&
        FMath::IsNearlyEqual(Stroke->Strength, BrushSettings.Strength) &&
        FMath::IsNearlyEqual(Stroke->Falloff, BrushSettings.Falloff) &&
        FMath::IsNearlyEqual(Stroke->StartTaper, BrushSettings.RidgeStartTaper) &&
        FMath::IsNearlyEqual(Stroke->EndTaper, BrushSettings.RidgeEndTaper) &&
        FMath::IsNearlyEqual(Stroke->FlareSettings.Length, BrushSettings.RidgeFlareSettings.Length) &&
        FMath::IsNearlyEqual(Stroke->FlareSettings.WidthScale, BrushSettings.RidgeFlareSettings.WidthScale) &&
        FMath::IsNearlyEqual(Stroke->FlareSettings.EndStrength, BrushSettings.RidgeFlareSettings.EndStrength) &&
        FMath::IsNearlyEqual(Stroke->FlareSettings.Softness, BrushSettings.RidgeFlareSettings.Softness) &&
        Stroke->NaturalVariation.bEnabled == BrushSettings.RidgeNaturalVariation.bEnabled &&
        FMath::IsNearlyEqual(Stroke->NaturalVariation.CenterlineAmount, BrushSettings.RidgeNaturalVariation.CenterlineAmount) &&
        FMath::IsNearlyEqual(Stroke->NaturalVariation.CenterlineFrequency, BrushSettings.RidgeNaturalVariation.CenterlineFrequency) &&
        FMath::IsNearlyEqual(Stroke->NaturalVariation.WidthVariation, BrushSettings.RidgeNaturalVariation.WidthVariation) &&
        FMath::IsNearlyEqual(Stroke->NaturalVariation.WidthFrequency, BrushSettings.RidgeNaturalVariation.WidthFrequency) &&
        Stroke->NaturalVariation.NoiseSeed == BrushSettings.RidgeNaturalVariation.NoiseSeed)
    {
        return;
    }

    if (!ActiveRidgePropertyTransaction.IsValid())
    {
        ActiveRidgePropertyTransaction = MakeUnique<FScopedTransaction>(
            LOCTEXT("EditProceduralRidgeSettingsTransaction", "Edit Procedural Ridge Settings"));
    }
    Asset->Modify();
    Stroke->Shape = BrushSettings.RidgeShape;
    Stroke->bFlipFoldSide = BrushSettings.bFlipRidgeFoldSide;
    Stroke->WidthUV = BrushSettings.BrushRadiusUV;
    Stroke->Strength = BrushSettings.Strength;
    Stroke->Falloff = BrushSettings.Falloff;
    Stroke->StartTaper = BrushSettings.RidgeStartTaper;
    Stroke->EndTaper = BrushSettings.RidgeEndTaper;
    Stroke->FlareSettings = BrushSettings.RidgeFlareSettings;
    Stroke->NaturalVariation = BrushSettings.RidgeNaturalVariation;
    MarkAssetEdited();
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->SetEditingProceduralStrokeGuid(Stroke->StrokeGuid);
        PreviewViewport->PreviewEditedProceduralStroke(*Stroke);
    }
    RefreshStrokeOverlay(false);
}

void SWetWrinkleEditorPanel::HandleRidgePropertySliderBegin()
{
    if (!ActiveRidgePropertyTransaction.IsValid() &&
        BrushSettings.ToolMode == EWetWrinkleToolMode::ProceduralRidgeStroke &&
        BrushSettings.RidgeEditMode == EWetProceduralRidgeEditMode::Edit)
    {
        ActiveRidgePropertyTransaction = MakeUnique<FScopedTransaction>(
            LOCTEXT("EditProceduralRidgeSettingsTransaction", "Edit Procedural Ridge Settings"));
    }
}

void SWetWrinkleEditorPanel::HandleRidgePropertySliderEnd(float NewValue)
{
    if (!ActiveRidgePropertyTransaction.IsValid())
    {
        return;
    }
    ActiveRidgePropertyTransaction.Reset();
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->ClearTransientProceduralStroke();
        PreviewViewport->SetEditingProceduralStrokeGuid(FGuid());
    }
    RefreshStrokeOverlay(true);
}

void SWetWrinkleEditorPanel::HandleRidgePropertyCommitted(float NewValue, ETextCommit::Type CommitType)
{
    if (!ActiveRidgePropertyTransaction.IsValid())
    {
        return;
    }
    ActiveRidgePropertyTransaction.Reset();
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->ClearTransientProceduralStroke();
        PreviewViewport->SetEditingProceduralStrokeGuid(FGuid());
    }
    RefreshStrokeOverlay(true);
}

ECheckBoxState SWetWrinkleEditorPanel::GetSelectedRidgeEndpointPointedState(const bool bStartEndpoint) const
{
    const FWetProceduralRidgeStroke* Stroke = FindProceduralRidgeStroke(SelectedStrokeGuid);
    if (Stroke == nullptr || SelectedElementType != EWetWrinkleElementType::ProceduralRidgeStroke)
    {
        return ECheckBoxState::Undetermined;
    }
    const FWetProceduralRidgeEndpoint& Endpoint = bStartEndpoint ? Stroke->StartEndpoint : Stroke->EndEndpoint;
    return Endpoint.Mode == EWetProceduralRidgeEndpointMode::Pointed
        ? ECheckBoxState::Checked
        : ECheckBoxState::Unchecked;
}

ECheckBoxState SWetWrinkleEditorPanel::GetSelectedRidgeEndpointFlaredState(const bool bStartEndpoint) const
{
    const FWetProceduralRidgeStroke* Stroke = FindProceduralRidgeStroke(SelectedStrokeGuid);
    if (Stroke == nullptr || SelectedElementType != EWetWrinkleElementType::ProceduralRidgeStroke)
    {
        return ECheckBoxState::Undetermined;
    }
    const FWetProceduralRidgeEndpoint& Endpoint = bStartEndpoint ? Stroke->StartEndpoint : Stroke->EndEndpoint;
    return Endpoint.Mode == EWetProceduralRidgeEndpointMode::Flared
        ? ECheckBoxState::Checked
        : ECheckBoxState::Unchecked;
}

EVisibility SWetWrinkleEditorPanel::GetFlareOptionsVisibility() const
{
    const FWetProceduralRidgeStroke* Stroke = FindProceduralRidgeStroke(SelectedStrokeGuid);
    return BrushSettings.ToolMode == EWetWrinkleToolMode::ProceduralRidgeStroke &&
            BrushSettings.RidgeEditMode == EWetProceduralRidgeEditMode::Edit &&
            SelectedElementType == EWetWrinkleElementType::ProceduralRidgeStroke &&
            Stroke != nullptr &&
            (Stroke->StartEndpoint.Mode == EWetProceduralRidgeEndpointMode::Flared ||
             Stroke->EndEndpoint.Mode == EWetProceduralRidgeEndpointMode::Flared)
        ? EVisibility::Visible
        : EVisibility::Collapsed;
}

FText SWetWrinkleEditorPanel::GetSelectedRidgeEndpointStatusText(const bool bStartEndpoint) const
{
    const FWetProceduralRidgeStroke* Stroke = FindProceduralRidgeStroke(SelectedStrokeGuid);
    if (Stroke == nullptr || SelectedElementType != EWetWrinkleElementType::ProceduralRidgeStroke)
    {
        return bStartEndpoint
            ? LOCTEXT("RidgeStartEndpointNoSelection", "Start: No stroke")
            : LOCTEXT("RidgeEndEndpointNoSelection", "End: No stroke");
    }

    const EWetProceduralRidgeEndpointMode Mode =
        (bStartEndpoint ? Stroke->StartEndpoint : Stroke->EndEndpoint).Mode;
    FText ModeText;
    switch (Mode)
    {
    case EWetProceduralRidgeEndpointMode::Rounded:
        ModeText = LOCTEXT("RidgeEndpointRounded", "Rounded");
        break;
    case EWetProceduralRidgeEndpointMode::Junction:
        ModeText = LOCTEXT("RidgeEndpointJunction", "Junction");
        break;
    case EWetProceduralRidgeEndpointMode::Flared:
        ModeText = LOCTEXT("RidgeEndpointFlared", "Flared");
        break;
    case EWetProceduralRidgeEndpointMode::Pointed:
    default:
        ModeText = LOCTEXT("RidgeEndpointPointed", "Pointed");
        break;
    }

    return FText::Format(
        bStartEndpoint
            ? LOCTEXT("RidgeStartEndpointStatus", "Start: {0}")
            : LOCTEXT("RidgeEndEndpointStatus", "End: {0}"),
        ModeText);
}

FSlateColor SWetWrinkleEditorPanel::GetSelectedRidgeEndpointStatusColor(const bool bStartEndpoint) const
{
    const FWetProceduralRidgeStroke* Stroke = FindProceduralRidgeStroke(SelectedStrokeGuid);
    if (Stroke != nullptr && SelectedElementType == EWetWrinkleElementType::ProceduralRidgeStroke)
    {
        const EWetProceduralRidgeEndpointMode Mode =
            (bStartEndpoint ? Stroke->StartEndpoint : Stroke->EndEndpoint).Mode;
        if (Mode == EWetProceduralRidgeEndpointMode::Junction)
        {
            return FSlateColor(FLinearColor(1.0f, 0.72f, 0.05f, 1.0f));
        }
        if (Mode == EWetProceduralRidgeEndpointMode::Flared)
        {
            return FSlateColor(FLinearColor(0.85f, 0.45f, 1.0f, 1.0f));
        }
    }
    return FSlateColor::UseForeground();
}

void SWetWrinkleEditorPanel::HandleSelectedRidgeEndpointPointedChanged(
    const ECheckBoxState NewState,
    const bool bStartEndpoint)
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    FWetProceduralRidgeStroke* Stroke = FindMutableProceduralRidgeStroke(SelectedStrokeGuid);
    if (Asset == nullptr || Stroke == nullptr || SelectedElementType != EWetWrinkleElementType::ProceduralRidgeStroke)
    {
        return;
    }

    FWetProceduralRidgeEndpoint& Endpoint = bStartEndpoint ? Stroke->StartEndpoint : Stroke->EndEndpoint;
    const EWetProceduralRidgeEndpointMode NewMode = NewState == ECheckBoxState::Checked
        ? EWetProceduralRidgeEndpointMode::Pointed
        : EWetProceduralRidgeEndpointMode::Rounded;
    if (Endpoint.Mode == NewMode)
    {
        return;
    }

    const FScopedTransaction Transaction(LOCTEXT("EditProceduralRidgeEndpointTransaction", "Edit Procedural Ridge Endpoint"));
    Asset->Modify();
    Endpoint.Mode = NewMode;
    Endpoint.ResetConnection();
    MarkAssetEdited();
    RefreshStrokeOverlay(true);
}

void SWetWrinkleEditorPanel::HandleSelectedRidgeEndpointFlaredChanged(
    const ECheckBoxState NewState,
    const bool bStartEndpoint)
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    FWetProceduralRidgeStroke* Stroke = FindMutableProceduralRidgeStroke(SelectedStrokeGuid);
    if (Asset == nullptr || Stroke == nullptr || SelectedElementType != EWetWrinkleElementType::ProceduralRidgeStroke)
    {
        return;
    }

    FWetProceduralRidgeEndpoint& Endpoint = bStartEndpoint ? Stroke->StartEndpoint : Stroke->EndEndpoint;
    const EWetProceduralRidgeEndpointMode NewMode = NewState == ECheckBoxState::Checked
        ? EWetProceduralRidgeEndpointMode::Flared
        : EWetProceduralRidgeEndpointMode::Rounded;
    if (Endpoint.Mode == NewMode)
    {
        return;
    }

    const FScopedTransaction Transaction(LOCTEXT("EditProceduralRidgeFlaredEndpointTransaction", "Edit Flared Ridge Endpoint"));
    Asset->Modify();
    Endpoint.Mode = NewMode;
    Endpoint.ResetConnection();
    MarkAssetEdited();
    RefreshStrokeOverlay(true);
}

bool SWetWrinkleEditorPanel::CanDeleteSelectedRidgePoint() const
{
    const FWetProceduralRidgeStroke* Stroke =
        SelectedElementType == EWetWrinkleElementType::ProceduralRidgeStroke
            ? FindProceduralRidgeStroke(SelectedStrokeGuid)
            : nullptr;
    return Stroke != nullptr && Stroke->Points.Num() > 2 && Stroke->Points.IsValidIndex(SelectedProceduralRidgePointIndex);
}

FReply SWetWrinkleEditorPanel::HandleDeleteSelectedRidgePointClicked()
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    FWetProceduralRidgeStroke* Stroke = FindMutableProceduralRidgeStroke(SelectedStrokeGuid);
    if (Asset == nullptr || Stroke == nullptr || !CanDeleteSelectedRidgePoint())
    {
        return FReply::Handled();
    }

    const FScopedTransaction Transaction(LOCTEXT("DeleteProceduralRidgePointTransaction", "Delete Procedural Ridge Point"));
    Asset->Modify();
    const bool bRemovedStart = SelectedProceduralRidgePointIndex == 0;
    const bool bRemovedEnd = SelectedProceduralRidgePointIndex == Stroke->Points.Num() - 1;
    Stroke->Points.RemoveAt(SelectedProceduralRidgePointIndex);
    if (bRemovedStart)
    {
        Stroke->StartEndpoint.Mode = EWetProceduralRidgeEndpointMode::Pointed;
        Stroke->StartEndpoint.ResetConnection();
    }
    if (bRemovedEnd)
    {
        Stroke->EndEndpoint.Mode = EWetProceduralRidgeEndpointMode::Pointed;
        Stroke->EndEndpoint.ResetConnection();
    }
    SelectedProceduralRidgePointIndex = INDEX_NONE;
    MarkAssetEdited();
    RefreshStrokeList();
    RefreshStrokeOverlay(true);
    return FReply::Handled();
}

void SWetWrinkleEditorPanel::ClearConnectionsToStroke(const FGuid& DeletedStrokeGuid)
{
    if (!DeletedStrokeGuid.IsValid())
    {
        return;
    }

    if (UWetClothingAsset* Asset = WetClothingAsset.Get())
    {
        for (FWetProceduralRidgeStroke& Stroke : Asset->WrinkleData.EditableProceduralRidgeStrokes)
        {
            if (Stroke.StartEndpoint.ConnectedStrokeGuid == DeletedStrokeGuid)
            {
                Stroke.StartEndpoint.Mode = EWetProceduralRidgeEndpointMode::Pointed;
                Stroke.StartEndpoint.ResetConnection();
            }
            if (Stroke.EndEndpoint.ConnectedStrokeGuid == DeletedStrokeGuid)
            {
                Stroke.EndEndpoint.Mode = EWetProceduralRidgeEndpointMode::Pointed;
                Stroke.EndEndpoint.ResetConnection();
            }
        }
    }
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

FWetProceduralRidgeStroke* SWetWrinkleEditorPanel::FindMutableProceduralRidgeStroke(const FGuid& StrokeGuid) const
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || !StrokeGuid.IsValid())
    {
        return nullptr;
    }

    return Asset->WrinkleData.EditableProceduralRidgeStrokes.FindByPredicate(
        [StrokeGuid](const FWetProceduralRidgeStroke& Stroke)
        {
            return Stroke.StrokeGuid == StrokeGuid;
        });
}

const FWetProceduralRidgeStroke* SWetWrinkleEditorPanel::FindProceduralRidgeStroke(const FGuid& StrokeGuid) const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || !StrokeGuid.IsValid())
    {
        return nullptr;
    }

    return Asset->WrinkleData.EditableProceduralRidgeStrokes.FindByPredicate(
        [StrokeGuid](const FWetProceduralRidgeStroke& Stroke)
        {
            return Stroke.StrokeGuid == StrokeGuid;
        });
}

FWetProceduralRidgeStrokePoint SWetWrinkleEditorPanel::MakeProceduralRidgePointFromHit(const FWetWrinkleSurfaceHit& SurfaceHit) const
{
    FWetProceduralRidgeStrokePoint Point;
    Point.PositionUV = SurfaceHit.UV;
    Point.AnchorTriangleID = SurfaceHit.TriangleID;
    Point.AnchorBarycentric = FVector3f(SurfaceHit.Barycentric);
    return Point;
}

int32 SWetWrinkleEditorPanel::FindNearestProceduralRidgeSegment(
    const FWetProceduralRidgeStroke& Stroke,
    const FVector2D& UV,
    float& OutSegmentT) const
{
    int32 NearestSegmentIndex = INDEX_NONE;
    double NearestDistanceSq = TNumericLimits<double>::Max();
    OutSegmentT = 0.0f;
    for (int32 SegmentIndex = 0; SegmentIndex + 1 < Stroke.Points.Num(); ++SegmentIndex)
    {
        const FVector2D Start = Stroke.Points[SegmentIndex].PositionUV;
        const FVector2D Delta(
            WrapWetRidgeDelta(Stroke.Points[SegmentIndex + 1].PositionUV.X - Start.X),
            WrapWetRidgeDelta(Stroke.Points[SegmentIndex + 1].PositionUV.Y - Start.Y));
        const FVector2D WrappedUV(
            Start.X + WrapWetRidgeDelta(UV.X - Start.X),
            Start.Y + WrapWetRidgeDelta(UV.Y - Start.Y));
        const double LengthSq = Delta.SizeSquared();
        const float SegmentT = LengthSq > UE_SMALL_NUMBER
            ? FMath::Clamp(static_cast<float>(FVector2D::DotProduct(WrappedUV - Start, Delta) / LengthSq), 0.0f, 1.0f)
            : 0.0f;
        const double DistanceSq = FVector2D::DistSquared(WrappedUV, Start + Delta * SegmentT);
        if (DistanceSq < NearestDistanceSq)
        {
            NearestDistanceSq = DistanceSq;
            NearestSegmentIndex = SegmentIndex;
            OutSegmentT = SegmentT;
        }
    }
    return NearestSegmentIndex;
}

bool SWetWrinkleEditorPanel::FindProceduralRidgeJunctionSnap(
    const FWetWrinkleSurfaceHit& SurfaceHit,
    const FGuid& ExcludedStrokeGuid,
    FWetWrinkleSurfaceHit& OutSnappedHit,
    FGuid& OutConnectedStrokeGuid,
    int32& OutConnectedSegmentIndex,
    float& OutConnectedSegmentT) const
{
    OutConnectedStrokeGuid.Invalidate();
    OutConnectedSegmentIndex = INDEX_NONE;
    OutConnectedSegmentT = 0.0f;
    if (!BrushSettings.bRidgeJunctionModeEnabled)
    {
        return false;
    }

    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || !PreviewViewport.IsValid() || !SurfaceHit.bHit)
    {
        return false;
    }

    const double MaxWorldDistance = FMath::Max(static_cast<double>(SizeCm * 0.75f), 1.0);
    double BestWorldDistanceSq = FMath::Square(MaxWorldDistance);
    for (const FWetProceduralRidgeStroke& Candidate : Asset->WrinkleData.EditableProceduralRidgeStrokes)
    {
        if (!Candidate.bEnabled || Candidate.StrokeGuid == ExcludedStrokeGuid || Candidate.Points.Num() < 2 ||
            Candidate.MaterialSlotIndex != SurfaceHit.MaterialSlotIndex || Candidate.UVChannelIndex != SurfaceHit.UVChannelIndex)
        {
            continue;
        }

        // Control points already have an exact triangle/barycentric anchor. Prefer
        // that data so clicking a visible guide point never depends on UV projection.
        for (int32 PointIndex = 0; PointIndex < Candidate.Points.Num(); ++PointIndex)
        {
            FVector PointWorld = FVector::ZeroVector;
            FVector PointNormal = FVector::UpVector;
            if (!PreviewViewport->ResolveProceduralStrokePointWorld(
                    Candidate.Points[PointIndex],
                    Candidate.MaterialSlotIndex,
                    PointWorld,
                    PointNormal))
            {
                continue;
            }

            const double PointDistanceSq = FVector::DistSquared(PointWorld, SurfaceHit.WorldPosition);
            if (PointDistanceSq > BestWorldDistanceSq)
            {
                continue;
            }

            FWetWrinkleSurfaceHit PointHit;
            if (!PreviewViewport->TryBuildSurfaceHitFromProceduralStrokePoint(
                    Candidate.Points[PointIndex],
                    Candidate.MaterialSlotIndex,
                    Candidate.UVChannelIndex,
                    PointHit))
            {
                continue;
            }

            BestWorldDistanceSq = PointDistanceSq;
            OutSnappedHit = PointHit;
            OutConnectedStrokeGuid = Candidate.StrokeGuid;
            if (PointIndex + 1 < Candidate.Points.Num())
            {
                OutConnectedSegmentIndex = PointIndex;
                OutConnectedSegmentT = 0.0f;
            }
            else
            {
                OutConnectedSegmentIndex = PointIndex - 1;
                OutConnectedSegmentT = 1.0f;
            }
        }

        for (int32 SegmentIndex = 0; SegmentIndex + 1 < Candidate.Points.Num(); ++SegmentIndex)
        {
            FVector StartWorld = FVector::ZeroVector;
            FVector EndWorld = FVector::ZeroVector;
            FVector StartNormal = FVector::UpVector;
            FVector EndNormal = FVector::UpVector;
            if (!PreviewViewport->ResolveProceduralStrokePointWorld(
                    Candidate.Points[SegmentIndex],
                    Candidate.MaterialSlotIndex,
                    StartWorld,
                    StartNormal) ||
                !PreviewViewport->ResolveProceduralStrokePointWorld(
                    Candidate.Points[SegmentIndex + 1],
                    Candidate.MaterialSlotIndex,
                    EndWorld,
                    EndNormal))
            {
                continue;
            }

            const FVector SegmentDelta = EndWorld - StartWorld;
            const double SegmentLengthSq = SegmentDelta.SizeSquared();
            const float SegmentT = SegmentLengthSq > UE_SMALL_NUMBER
                ? FMath::Clamp(
                      static_cast<float>(FVector::DotProduct(SurfaceHit.WorldPosition - StartWorld, SegmentDelta) / SegmentLengthSq),
                      0.0f,
                      1.0f)
                : 0.0f;
            const FVector ClosestWorld = StartWorld + SegmentDelta * SegmentT;
            const double WorldDistanceSq = FVector::DistSquared(ClosestWorld, SurfaceHit.WorldPosition);
            if (WorldDistanceSq > BestWorldDistanceSq)
            {
                continue;
            }

            const FVector2D StartUV = Candidate.Points[SegmentIndex].PositionUV;
            const FVector2D UVDelta(
                WrapWetRidgeDelta(Candidate.Points[SegmentIndex + 1].PositionUV.X - StartUV.X),
                WrapWetRidgeDelta(Candidate.Points[SegmentIndex + 1].PositionUV.Y - StartUV.Y));
            const FVector2D ClosestUV = StartUV + UVDelta * SegmentT;
            FWetWrinkleSurfaceHit CandidateHit;
            if (!PreviewViewport->TryBuildSurfaceHitAtUVNearWorldPosition(
                    SurfaceHit.MaterialSlotIndex,
                    SurfaceHit.UVChannelIndex,
                    FVector2D(WrapWetRidgeUnit(ClosestUV.X), WrapWetRidgeUnit(ClosestUV.Y)),
                    ClosestWorld,
                    CandidateHit))
            {
                continue;
            }

            BestWorldDistanceSq = WorldDistanceSq;
            OutSnappedHit = CandidateHit;
            OutConnectedStrokeGuid = Candidate.StrokeGuid;
            OutConnectedSegmentIndex = SegmentIndex;
            OutConnectedSegmentT = SegmentT;
        }
    }
    return OutConnectedStrokeGuid.IsValid();
}

void SWetWrinkleEditorPanel::BeginProceduralRidgePointEdit(const FWetWrinkleSurfaceHit& SurfaceHit)
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    FWetProceduralRidgeStroke* Stroke =
        SelectedElementType == EWetWrinkleElementType::ProceduralRidgeStroke
            ? FindMutableProceduralRidgeStroke(SelectedStrokeGuid)
            : nullptr;
    if (Asset == nullptr || Stroke == nullptr || !PreviewViewport.IsValid() || !SurfaceHit.bHit ||
        SurfaceHit.MaterialSlotIndex != Stroke->MaterialSlotIndex || SurfaceHit.UVChannelIndex != Stroke->UVChannelIndex)
    {
        return;
    }

    const bool bInsertPoint = FSlateApplication::Get().GetModifierKeys().IsShiftDown();
    int32 PointIndex = INDEX_NONE;
    if (bInsertPoint)
    {
        float SegmentT = 0.0f;
        const int32 SegmentIndex = FindNearestProceduralRidgeSegment(*Stroke, SurfaceHit.UV, SegmentT);
        if (SegmentIndex != INDEX_NONE)
        {
            PointIndex = SegmentIndex + 1;
        }
    }
    else
    {
        PointIndex = PreviewViewport->FindNearestProceduralStrokePoint(
            *Stroke,
            SurfaceHit.WorldPosition,
            FMath::Max(SizeCm * 0.75f, 1.0f));
    }

    if (PointIndex == INDEX_NONE)
    {
        return;
    }

    ActiveRidgeEditTransaction = MakeUnique<FScopedTransaction>(
        bInsertPoint
            ? LOCTEXT("InsertProceduralRidgePointTransaction", "Insert Procedural Ridge Point")
            : LOCTEXT("MoveProceduralRidgePointTransaction", "Move Procedural Ridge Point"));
    Asset->Modify();
    bInsertedEditedProceduralRidgePoint = bInsertPoint;
    if (bInsertPoint)
    {
        Stroke->Points.Insert(MakeProceduralRidgePointFromHit(SurfaceHit), PointIndex);
    }

    EditingProceduralRidgePointIndex = PointIndex;
    SelectedProceduralRidgePointIndex = PointIndex;
    OriginalEditedProceduralRidgePoint = Stroke->Points[PointIndex];
    OriginalEditedStartEndpoint = Stroke->StartEndpoint;
    OriginalEditedEndEndpoint = Stroke->EndEndpoint;
    EditingProceduralRidgeUVIslandID = SurfaceHit.UVIslandID;
    bEditingProceduralRidgePoint = true;
    PreviewViewport->SetEditingProceduralStrokeGuid(Stroke->StrokeGuid);
    PreviewViewport->PreviewEditedProceduralStroke(*Stroke);
    RefreshStrokeOverlay(false);
}

void SWetWrinkleEditorPanel::UpdateProceduralRidgePointEdit(const FWetWrinkleSurfaceHit& SurfaceHit)
{
    FWetProceduralRidgeStroke* Stroke = FindMutableProceduralRidgeStroke(SelectedStrokeGuid);
    if (!bEditingProceduralRidgePoint || Stroke == nullptr || !PreviewViewport.IsValid() || !SurfaceHit.bHit ||
        !Stroke->Points.IsValidIndex(EditingProceduralRidgePointIndex) ||
        SurfaceHit.MaterialSlotIndex != Stroke->MaterialSlotIndex || SurfaceHit.UVChannelIndex != Stroke->UVChannelIndex ||
        SurfaceHit.UVIslandID != EditingProceduralRidgeUVIslandID)
    {
        return;
    }

    FWetWrinkleSurfaceHit FinalHit = SurfaceHit;
    const bool bEndpoint = EditingProceduralRidgePointIndex == 0 || EditingProceduralRidgePointIndex == Stroke->Points.Num() - 1;
    FGuid ConnectedStrokeGuid;
    int32 ConnectedSegmentIndex = INDEX_NONE;
    float ConnectedSegmentT = 0.0f;
    if (bEndpoint)
    {
        FindProceduralRidgeJunctionSnap(
            SurfaceHit,
            Stroke->StrokeGuid,
            FinalHit,
            ConnectedStrokeGuid,
            ConnectedSegmentIndex,
            ConnectedSegmentT);
    }

    Stroke->Points[EditingProceduralRidgePointIndex] = MakeProceduralRidgePointFromHit(FinalHit);
    if (EditingProceduralRidgePointIndex == 0)
    {
        Stroke->StartEndpoint.Mode = ConnectedStrokeGuid.IsValid()
            ? EWetProceduralRidgeEndpointMode::Junction
            : EWetProceduralRidgeEndpointMode::Pointed;
        Stroke->StartEndpoint.ConnectedStrokeGuid = ConnectedStrokeGuid;
        Stroke->StartEndpoint.ConnectedSegmentIndex = ConnectedSegmentIndex;
        Stroke->StartEndpoint.ConnectedSegmentT = ConnectedSegmentT;
    }
    else if (EditingProceduralRidgePointIndex == Stroke->Points.Num() - 1)
    {
        Stroke->EndEndpoint.Mode = ConnectedStrokeGuid.IsValid()
            ? EWetProceduralRidgeEndpointMode::Junction
            : EWetProceduralRidgeEndpointMode::Pointed;
        Stroke->EndEndpoint.ConnectedStrokeGuid = ConnectedStrokeGuid;
        Stroke->EndEndpoint.ConnectedSegmentIndex = ConnectedSegmentIndex;
        Stroke->EndEndpoint.ConnectedSegmentT = ConnectedSegmentT;
    }
    PreviewViewport->PreviewEditedProceduralStroke(*Stroke);
    RefreshStrokeOverlay(false);
}

void SWetWrinkleEditorPanel::EndProceduralRidgePointEdit(const bool bCancel)
{
    if (!bEditingProceduralRidgePoint)
    {
        return;
    }

    if (FWetProceduralRidgeStroke* Stroke = FindMutableProceduralRidgeStroke(SelectedStrokeGuid))
    {
        if (bCancel)
        {
            if (bInsertedEditedProceduralRidgePoint && Stroke->Points.IsValidIndex(EditingProceduralRidgePointIndex))
            {
                Stroke->Points.RemoveAt(EditingProceduralRidgePointIndex);
            }
            else if (Stroke->Points.IsValidIndex(EditingProceduralRidgePointIndex))
            {
                Stroke->Points[EditingProceduralRidgePointIndex] = OriginalEditedProceduralRidgePoint;
            }
            Stroke->StartEndpoint = OriginalEditedStartEndpoint;
            Stroke->EndEndpoint = OriginalEditedEndEndpoint;
        }
        else
        {
            MarkAssetEdited();
        }
    }

    if (PreviewViewport.IsValid())
    {
        PreviewViewport->ClearTransientProceduralStroke();
        PreviewViewport->SetEditingProceduralStrokeGuid(FGuid());
    }
    ActiveRidgeEditTransaction.Reset();
    EditingProceduralRidgePointIndex = INDEX_NONE;
    EditingProceduralRidgeUVIslandID = INDEX_NONE;
    bEditingProceduralRidgePoint = false;
    bInsertedEditedProceduralRidgePoint = false;
    RefreshStrokeList();
    RefreshStrokeOverlay(true);
}

void SWetWrinkleEditorPanel::BeginProceduralRidgeStroke(const FWetWrinkleSurfaceHit& SurfaceHit)
{
    if (WetClothingAsset.Get() == nullptr ||
        !SurfaceHit.bHit ||
        SurfaceHit.MaterialSlotIndex != BrushSettings.MaterialSlotIndex ||
        SurfaceHit.UVChannelIndex != BrushSettings.UVChannelIndex ||
        SurfaceHit.UVIslandID == INDEX_NONE)
    {
        return;
    }

    if (BrushSettings.RidgeNaturalVariation.bEnabled)
    {
        BrushSettings.RidgeNaturalVariation.NoiseSeed = static_cast<int32>(FPlatformTime::Cycles64() & 0x7FFFFFFF);
        PushBrushSettingsToViewport();
    }

    FWetWrinkleSurfaceHit StartHit = SurfaceHit;
    PendingStartConnectionStrokeGuid.Invalidate();
    PendingStartConnectionSegmentIndex = INDEX_NONE;
    PendingStartConnectionSegmentT = 0.0f;
    FindProceduralRidgeJunctionSnap(
        SurfaceHit,
        FGuid(),
        StartHit,
        PendingStartConnectionStrokeGuid,
        PendingStartConnectionSegmentIndex,
        PendingStartConnectionSegmentT);

    CapturedProceduralRidgeHits.Reset();
    CapturedProceduralRidgeHits.Add(StartHit);
    ActiveProceduralRidgeMaterialSlotIndex = StartHit.MaterialSlotIndex;
    ActiveProceduralRidgeUVChannelIndex = StartHit.UVChannelIndex;
    ActiveProceduralRidgeUVIslandID = StartHit.UVIslandID;
    bCapturingProceduralRidgeStroke = true;
    bProceduralRidgeCaptureBlocked = false;

    if (PreviewViewport.IsValid())
    {
        PreviewViewport->SetTransientProceduralStroke(
            BuildSmoothedProceduralRidgeHits(),
            PendingStartConnectionStrokeGuid.IsValid());
    }
}

void SWetWrinkleEditorPanel::AppendProceduralRidgeStrokePoint(const FWetWrinkleSurfaceHit& SurfaceHit)
{
    if (!bCapturingProceduralRidgeStroke || bProceduralRidgeCaptureBlocked || !SurfaceHit.bHit)
    {
        return;
    }

    if (SurfaceHit.MaterialSlotIndex != ActiveProceduralRidgeMaterialSlotIndex ||
        SurfaceHit.UVChannelIndex != ActiveProceduralRidgeUVChannelIndex ||
        SurfaceHit.UVIslandID != ActiveProceduralRidgeUVIslandID)
    {
        bProceduralRidgeCaptureBlocked = true;
        return;
    }

    if (!ShouldAddProceduralRidgePoint(SurfaceHit))
    {
        return;
    }

    CapturedProceduralRidgeHits.Add(SurfaceHit);
    if (PreviewViewport.IsValid())
    {
        TArray<FWetWrinkleSurfaceHit> PreviewHits = BuildSmoothedProceduralRidgeHits();
        bool bEndJunction = false;
        if (PreviewHits.Num() >= 2)
        {
            FWetWrinkleSurfaceHit SnappedEndHit;
            FGuid ConnectedStrokeGuid;
            int32 ConnectedSegmentIndex = INDEX_NONE;
            float ConnectedSegmentT = 0.0f;
            bEndJunction = FindProceduralRidgeJunctionSnap(
                PreviewHits.Last(),
                FGuid(),
                SnappedEndHit,
                ConnectedStrokeGuid,
                ConnectedSegmentIndex,
                ConnectedSegmentT);
            if (bEndJunction)
            {
                PreviewHits.Last() = SnappedEndHit;
            }
        }
        PreviewViewport->SetTransientProceduralStroke(
            PreviewHits,
            PendingStartConnectionStrokeGuid.IsValid(),
            bEndJunction);
    }
}

void SWetWrinkleEditorPanel::CommitProceduralRidgeStroke()
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    TArray<FWetWrinkleSurfaceHit> FinalHits = BuildSmoothedProceduralRidgeHits();
    if (Asset != nullptr && FinalHits.Num() >= 2)
    {
        FGuid EndConnectionStrokeGuid;
        int32 EndConnectionSegmentIndex = INDEX_NONE;
        float EndConnectionSegmentT = 0.0f;
        FWetWrinkleSurfaceHit SnappedEndHit;
        if (FindProceduralRidgeJunctionSnap(
                FinalHits.Last(),
                FGuid(),
                SnappedEndHit,
                EndConnectionStrokeGuid,
                EndConnectionSegmentIndex,
                EndConnectionSegmentT))
        {
            FinalHits.Last() = SnappedEndHit;
        }

        const FScopedTransaction Transaction(LOCTEXT("CreateProceduralRidgeStrokeTransaction", "Create Procedural Ridge Stroke"));
        Asset->Modify();

        FWetProceduralRidgeStroke NewStroke;
        NewStroke.StrokeGuid = FGuid::NewGuid();
        NewStroke.DisplayName = FString::Printf(
            TEXT("Ridge %03d"),
            Asset->WrinkleData.EditableProceduralRidgeStrokes.Num() + 1);
        NewStroke.MaterialSlotIndex = ActiveProceduralRidgeMaterialSlotIndex;
        NewStroke.UVChannelIndex = ActiveProceduralRidgeUVChannelIndex;
        NewStroke.LODIndex = 0;
        NewStroke.Shape = BrushSettings.RidgeShape;
        NewStroke.bFlipFoldSide = BrushSettings.bFlipRidgeFoldSide;
        NewStroke.WidthUV = BrushSettings.BrushRadiusUV;
        NewStroke.Strength = BrushSettings.Strength;
        NewStroke.Falloff = BrushSettings.Falloff;
        NewStroke.StartTaper = BrushSettings.RidgeStartTaper;
        NewStroke.EndTaper = BrushSettings.RidgeEndTaper;
        NewStroke.FlareSettings = BrushSettings.RidgeFlareSettings;
        NewStroke.NaturalVariation = BrushSettings.RidgeNaturalVariation;
        if (PendingStartConnectionStrokeGuid.IsValid())
        {
            NewStroke.StartEndpoint.Mode = EWetProceduralRidgeEndpointMode::Junction;
            NewStroke.StartEndpoint.ConnectedStrokeGuid = PendingStartConnectionStrokeGuid;
            NewStroke.StartEndpoint.ConnectedSegmentIndex = PendingStartConnectionSegmentIndex;
            NewStroke.StartEndpoint.ConnectedSegmentT = PendingStartConnectionSegmentT;
        }
        if (EndConnectionStrokeGuid.IsValid())
        {
            NewStroke.EndEndpoint.Mode = EWetProceduralRidgeEndpointMode::Junction;
            NewStroke.EndEndpoint.ConnectedStrokeGuid = EndConnectionStrokeGuid;
            NewStroke.EndEndpoint.ConnectedSegmentIndex = EndConnectionSegmentIndex;
            NewStroke.EndEndpoint.ConnectedSegmentT = EndConnectionSegmentT;
        }
        NewStroke.Points.Reserve(FinalHits.Num());
        for (const FWetWrinkleSurfaceHit& Hit : FinalHits)
        {
            NewStroke.Points.Add(MakeProceduralRidgePointFromHit(Hit));
        }

        Asset->WrinkleData.EditableProceduralRidgeStrokes.Add(NewStroke);
        if (PreviewViewport.IsValid())
        {
            PreviewViewport->AppendAccumulatedPreviewProceduralStroke(NewStroke);
        }
        SelectedStrokeGuid = NewStroke.StrokeGuid;
        SelectedElementType = EWetWrinkleElementType::ProceduralRidgeStroke;
        MarkAssetEdited();
        RefreshStrokeList();
    }

    CancelProceduralRidgeStroke();
    RefreshStrokeOverlay(false);
}

void SWetWrinkleEditorPanel::CancelProceduralRidgeStroke()
{
    bCapturingProceduralRidgeStroke = false;
    bProceduralRidgeCaptureBlocked = false;
    ActiveProceduralRidgeMaterialSlotIndex = INDEX_NONE;
    ActiveProceduralRidgeUVChannelIndex = INDEX_NONE;
    ActiveProceduralRidgeUVIslandID = INDEX_NONE;
    PendingStartConnectionStrokeGuid.Invalidate();
    PendingStartConnectionSegmentIndex = INDEX_NONE;
    PendingStartConnectionSegmentT = 0.0f;
    CapturedProceduralRidgeHits.Reset();
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->ClearTransientProceduralStroke();
    }
}

bool SWetWrinkleEditorPanel::ShouldAddProceduralRidgePoint(const FWetWrinkleSurfaceHit& SurfaceHit) const
{
    if (CapturedProceduralRidgeHits.IsEmpty())
    {
        return true;
    }

    const FWetWrinkleSurfaceHit& LastHit = CapturedProceduralRidgeHits.Last();
    const float SpacingScale = FMath::Clamp(BrushSettings.RidgePointSpacingScale, 0.05f, 1.0f);
    const double MinSurfaceSpacing = FMath::Max(static_cast<double>(SizeCm * SpacingScale), 0.1);
    const double MinUVSpacing = FMath::Max(static_cast<double>(BrushSettings.BrushRadiusUV * SpacingScale), 0.00025);
    return FVector::Distance(LastHit.WorldPosition, SurfaceHit.WorldPosition) >= MinSurfaceSpacing ||
           FVector2D::Distance(LastHit.UV, SurfaceHit.UV) >= MinUVSpacing;
}

TArray<FWetWrinkleSurfaceHit> SWetWrinkleEditorPanel::BuildSmoothedProceduralRidgeHits() const
{
    TArray<FWetWrinkleSurfaceHit> Result = CapturedProceduralRidgeHits;
    if (Result.Num() < 3 || !PreviewViewport.IsValid())
    {
        return Result;
    }

    constexpr double SmoothingAlpha = 0.25;
    const double MaxProjectionDistance = FMath::Max(static_cast<double>(SizeCm * 0.5f), 0.5);
    for (int32 PointIndex = 1; PointIndex + 1 < CapturedProceduralRidgeHits.Num(); ++PointIndex)
    {
        const FWetWrinkleSurfaceHit& Previous = CapturedProceduralRidgeHits[PointIndex - 1];
        const FWetWrinkleSurfaceHit& Current = CapturedProceduralRidgeHits[PointIndex];
        const FWetWrinkleSurfaceHit& Next = CapturedProceduralRidgeHits[PointIndex + 1];
        const FVector2D NeighborMidpoint = (Previous.UV + Next.UV) * 0.5;
        const FVector2D SmoothedUV = FMath::Lerp(Current.UV, NeighborMidpoint, SmoothingAlpha);

        FWetWrinkleSurfaceHit SmoothedHit;
        if (PreviewViewport->TryBuildSurfaceHitAtUVNearWorldPosition(
                ActiveProceduralRidgeMaterialSlotIndex,
                ActiveProceduralRidgeUVChannelIndex,
                SmoothedUV,
                Current.WorldPosition,
                SmoothedHit) &&
            SmoothedHit.UVIslandID == ActiveProceduralRidgeUVIslandID &&
            FVector::Distance(SmoothedHit.WorldPosition, Current.WorldPosition) <= MaxProjectionDistance)
        {
            Result[PointIndex] = SmoothedHit;
        }
    }

    return Result;
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
    Stamp.WrinklePreset = BrushSettings.WrinklePreset.Get();
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

bool SWetWrinkleEditorPanel::IsCurrentWrinklePresetUsable(FString* OutReason) const
{
    const UWetWrinklePreset* Preset = BrushSettings.WrinklePreset.Get();
    if (Preset == nullptr)
    {
        if (OutReason != nullptr)
        {
            *OutReason = TEXT("Select a Wet Wrinkle Preset before painting wrinkle patches.");
        }
        return false;
    }

    return Preset->IsUsableForBrush(OutReason);
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
        TargetMesh = Asset->GetDWCSkeletalMesh() != nullptr ? Asset->GetDWCSkeletalMesh() : nullptr;
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
