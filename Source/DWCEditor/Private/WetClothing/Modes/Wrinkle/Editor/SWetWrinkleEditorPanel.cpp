//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "SWetWrinkleEditorPanel.h"

#include "SWetWrinkleElementListPanel.h"
#include "SWetWrinklePalettePanel.h"
#include "SWetWrinkleUVPanel.h"
#include "WetWrinklePreviewController.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Core/DWCEditorUtils.h"
#include "DataAssets/WetClothingAsset.h"
#include "WetClothing/Foundation/Authoring/DWCEditorAuthoringDocument.h"
#include "WetClothing/Foundation/Authoring/State/DWCEditorSessionStore.h"
#include "WetClothing/Foundation/Bake/DWCEditorBakeCoordinator.h"
#include "WetClothing/Foundation/TextureWorkspace/DWCEditorRenderUploadQueue.h"
#include "WetClothing/Foundation/TextureWorkspace/DWCEditorTextureWorkspace.h"
#include "Editor.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "AssetThumbnail.h"
#include "Core/DWCEditorStyle.h"
#include "Brushes/SlateImageBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Materials/MaterialInterface.h"
#include "Modules/ModuleManager.h"
#include "IDetailsView.h"
#include "Misc/MessageDialog.h"
#include "PropertyCustomizationHelpers.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Styling/StyleColors.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Types/WidgetActiveTimerDelegate.h"
#include "WetClothing/Foundation/MeshAnalysis/WetClothingAssetMeshAnalyzer.h"
#include "WetClothing/Foundation/TextureAccess/WetClothingMaterialTextureResolver.h"
#include "WetClothing/Foundation/Preview/Commit/DWCEditorPreviewCommitCoordinator.h"
#include "WetClothing/Foundation/Jobs/DWCEditorWorkerJobScheduler.h"
#include "WetClothing/WCAEditor/UI/Widgets/WCAEditorWidgets.h"
#include "WetClothing/WCAEditor/UI/Widgets/SWCAMaterialSlotPreview.h"
#include "WetClothing/WCAEditor/UI/UVView/WCAUVIslandViewCache.h"
#include "WetClothing/Modes/Part/Partition/WetPartEditingService.h"
#include "WetClothing/DerivedAssets/Textures/Wrinkle/WetWrinkleBakeService.h"
#include "WetClothing/Modes/Wrinkle/Correction/SWetWrinkleNormalCorrectionDialog.h"
#include "WetClothing/Modes/Wrinkle/Generate/WetWrinkleTextureGenerator.h"
#include "WetClothing/Modes/Wrinkle/Editor/WetWrinkleEditorSettings.h"
#include "WetClothing/Modes/Wrinkle/Authoring/WetWrinkleAuthoringController.h"
#include "WetClothing/Modes/Wrinkle/Editor/SWetWrinkleCustomNormalPanel.h"
#include "WetClothing/Modes/Wrinkle/Viewport/WetWrinkleViewport.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"
#include "UObject/GCObject.h"
#include "Widgets/SWindow.h"
#include "Widgets/SToolTip.h"
#include "Framework/Application/SlateApplication.h"

#define LOCTEXT_NAMESPACE "WCAEditorPanel"

namespace
{
    DECLARE_DELEGATE_RetVal_OneParam(FReply, FOnWetWrinkleTextureTileContextMenu, const FPointerEvent&);

    class SWetWrinkleTexturePaletteTile : public SCompoundWidget
    {
      public:
        SLATE_BEGIN_ARGS(SWetWrinkleTexturePaletteTile) {}
        SLATE_DEFAULT_SLOT(FArguments, Content)
        SLATE_EVENT(FOnWetWrinkleTextureTileContextMenu, OnContextMenu)
        SLATE_END_ARGS()

        void Construct(const FArguments& InArgs)
        {
            OnContextMenu = InArgs._OnContextMenu;
            ChildSlot[InArgs._Content.Widget];
        }

        virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
        {
            if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton && OnContextMenu.IsBound())
            {
                return OnContextMenu.Execute(MouseEvent);
            }
            return SCompoundWidget::OnMouseButtonUp(MyGeometry, MouseEvent);
        }

      private:
        FOnWetWrinkleTextureTileContextMenu OnContextMenu;
    };

    float WrapWetRidgeDelta(const float Delta)
    {
        return Delta - FMath::RoundToFloat(Delta);
    }

    float WrapWetRidgeUnit(float Value)
    {
        Value = FMath::Fmod(Value, 1.0f);
        return Value < 0.0f ? Value + 1.0f : Value;
    }

    constexpr const TCHAR* WetWrinkleBaseNormalTextureFolderPath = TEXT("/DynamicWetClothes/Textures/Wrinkles");
    constexpr float WetWrinkleDefaultSizeCm = 8.0f;
    constexpr float WetWrinkleDefaultSizeUV = 0.0677f;
    constexpr float WetWrinkleUVPerCm = WetWrinkleDefaultSizeUV / WetWrinkleDefaultSizeCm;
    int32 ResolveWetWrinkleUVChannel(const UWetClothingAsset* Asset)
    {
        return Asset != nullptr ? Asset->GetDWCDataUVChannelIndex() : INDEX_NONE;
    }
    constexpr int32 WetWrinkleGeneratedPreviewMaxResolution = 1024;
    constexpr float WetWrinkleGeneratedPreviewRebuildDelaySeconds = 0.1f;
    constexpr int32 WetWrinkleUVIslandCacheLimit = 8;

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
                                                               ]]]]

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
            PreviewViewport->SynchronizeBrushSettings(PreviewBrushSettings);
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
    AuthoringDocument = InArgs._AuthoringDocument;
    if (!AuthoringDocument.IsValid())
    {
        AuthoringDocument = MakeShared<FDWCEditorAuthoringDocument>(WetClothingAsset.Get());
    }
    SessionStore = InArgs._SessionStore;
    if (!SessionStore.IsValid())
    {
        SessionStore = MakeShared<FDWCEditorSessionStore>();
    }
    WorkerJobScheduler = InArgs._WorkerJobScheduler;
    BakeCoordinator = InArgs._BakeCoordinator;
    SpatialQueryService = InArgs._SpatialQueryService;
    TextureWorkspace = InArgs._TextureWorkspace;
    PreviewCommitCoordinator = InArgs._PreviewCommitCoordinator;
    RenderUploadQueue = InArgs._RenderUploadQueue;
    if (!RenderUploadQueue.IsValid())
    {
        RenderUploadQueue = MakeShared<FDWCEditorRenderUploadQueue>();
    }
    if (!TextureWorkspace.IsValid())
    {
        TextureWorkspace = MakeShared<FDWCEditorTextureWorkspace>(RenderUploadQueue.ToSharedRef());
    }
    if (!PreviewCommitCoordinator.IsValid())
    {
        PreviewCommitCoordinator = MakeShared<FDWCEditorPreviewCommitCoordinator>(
            TextureWorkspace.ToSharedRef(),
            WorkerJobScheduler.IsValid() ? WorkerJobScheduler->GetSessionEpoch() : FGuid());
    }
    SessionStore->OnChanged().AddSP(this, &SWetWrinkleEditorPanel::HandleSessionStateChanged);
    DetailsView = InArgs._DetailsView;
    MaterialThumbnailPool = MakeShared<FAssetThumbnailPool>(32);
    AuthoringController = MakeShared<FWetWrinkleAuthoringController>(
        WetClothingAsset.Get(),
        AuthoringDocument,
        SessionStore);
    PreviewController = MakeUnique<FWetWrinklePreviewController>();
    SAssignNew(WrinklePalettePanel, SWetWrinklePalettePanel)
        .OnGenerateTile(FOnGenerateWetWrinklePaletteTile::CreateSP(
            this,
            &SWetWrinkleEditorPanel::GenerateWrinkleTexturePaletteTileRow));
    if (GEditor != nullptr)
    {
        GEditor->RegisterForUndo(this);
    }

    if (UWetClothingAsset* Asset = WetClothingAsset.Get())
    {
        BrushSettings.UVChannelIndex = BrushSettings.MaterialSlotIndex != INDEX_NONE ? ResolveWetWrinkleUVChannel(WetClothingAsset.Get()) : INDEX_NONE;
    }
    RefreshMaterialSlotOptions();
    RefreshDWCDataUVChannel();
    RefreshBrushPresetOptions();
    RefreshWrinkleTexturePalette();
    SizeCm = WetWrinkleDefaultSizeCm;
    SizeUV = WetWrinkleDefaultSizeUV;
    BrushSettings.BrushRadiusUV = SizeUV;
    DispatchWrinkleBrushState(EDWCEditorSessionEffect::None);
    SelectedWrinkleNormalThumbnailBrush.SetImageSize(FVector2D(40.0f, 40.0f));
    FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    AssetRegistryModule.Get().OnAssetAdded().AddSP(this, &SWetWrinkleEditorPanel::HandleWrinkleTextureAssetAdded);
    AssetRegistryModule.Get().OnAssetRemoved().AddSP(this, &SWetWrinkleEditorPanel::HandleWrinkleTextureAssetRemoved);
    AssetRegistryModule.Get().OnAssetUpdated().AddSP(this, &SWetWrinkleEditorPanel::HandleWrinkleTextureAssetUpdated);

    ChildSlot
        [SNew(SVerticalBox)

         + SVerticalBox::Slot()
               .FillHeight(1.0f)
               .Padding(10.0f)
                   [SNew(SSplitter)

                    + SSplitter::Slot()
                          .Value(0.28f)
                              [SNew(SBorder)
                                   .Padding(10.0f)
                                       [SNew(SVerticalBox)

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
                                                                    .Padding(6.0f, 0.0f, 6.0f, 4.0f)
                                                                        [SNew(SHorizontalBox)
                                                                         + SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(FWCAEditorWidgets::MaterialSlotSlotColumnWidth)[SNew(STextBlock).Text(LOCTEXT("WrinkleSlotColumn", "Slot")).Font(FAppStyle::GetFontStyle(TEXT("SmallFontBold")))]]
                                                                         + SHorizontalBox::Slot().FillWidth(1.0f).Padding(10.0f, 0.0f)[SNew(STextBlock).Text(LOCTEXT("WrinkleNameColumn", "Name")).Font(FAppStyle::GetFontStyle(TEXT("SmallFontBold")))]
                                                                         + SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(FWCAEditorWidgets::MaterialSlotThumbnailColumnWidth).HAlign(HAlign_Center)[SNew(STextBlock).Text(LOCTEXT("WrinkleThumbnailColumn", "Thumbnail")).Font(FAppStyle::GetFontStyle(TEXT("SmallFontBold")))]]
                                                                         + SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(FWCAEditorWidgets::MaterialSlotDataUVColumnWidth).HAlign(HAlign_Center)[SNew(STextBlock).Text(LOCTEXT("WrinkleUVColumn", "Wrinkle UV")).Font(FAppStyle::GetFontStyle(TEXT("SmallFontBold")))]]
                                                                         + SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(FWCAEditorWidgets::MaterialSlotAuxColumnWidth).HAlign(HAlign_Center)[SNew(STextBlock).Text(LOCTEXT("CustomWrinkleMapColumn", "Custom")).Font(FAppStyle::GetFontStyle(TEXT("SmallFontBold")))]]
                                                                         + SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(FWCAEditorWidgets::MaterialSlotWettableColumnWidth).HAlign(HAlign_Center)[SNew(STextBlock).Text(LOCTEXT("WrinkleWettableColumn", "Wettable")).Font(FAppStyle::GetFontStyle(TEXT("SmallFontBold")))]]]

                                                              + SVerticalBox::Slot()
                                                                    .AutoHeight()
                                                                    .Padding(0.0f, 0.0f, 0.0f, FWCAEditorWidgets::MaterialSlotListSeparatorBottomPadding)
                                                                        [SNew(SSeparator)
                                                                             .Orientation(Orient_Horizontal)]

                                                               + SVerticalBox::Slot()
                                                                     .FillHeight(1.0f)
                                                                         [SNew(SOverlay)

                                                                          + SOverlay::Slot()
                                                                                [SAssignNew(MaterialSlotListView, SListView<FMaterialSlotItemPtr>)
                                                                                     .ListItemsSource(&MaterialSlotItems)
                                                                                     .OnGenerateRow(this, &SWetWrinkleEditorPanel::GenerateMaterialSlotRow)
                                                                                     .OnSelectionChanged(this, &SWetWrinkleEditorPanel::HandleMaterialSlotSelectionChanged)
                                                                                     .SelectionMode(ESelectionMode::Single)]

                                                                          + SOverlay::Slot()
                                                                                .HAlign(HAlign_Center)
                                                                                .VAlign(VAlign_Center)
                                                                                .Padding(16.0f)
                                                                                    [SNew(STextBlock)
                                                                                         .Text(LOCTEXT(
                                                                                             "NoWettableMaterialSlots",
                                                                                             "No Wettable material slots.\nEnable a slot in WetPart mode first."))
                                                                                         .Justification(ETextJustify::Center)
                                                                                         .AutoWrapText(true)
                                                                                         .ColorAndOpacity(FSlateColor::UseSubduedForeground())
                                                                                         .Visibility_Lambda([this]()
                                                                                         {
                                                                                             return MaterialSlotItems.IsEmpty()
                                                                                                        ? EVisibility::HitTestInvisible
                                                                                                        : EVisibility::Collapsed;
                                                                                         })]]]

                                                   + SSplitter::Slot()
                                                         .Value(0.48f)
                                                             [BuildRuntimeWrinkleNormalStatusSection()]]]]

                    + SSplitter::Slot()
                          .Value(0.44f)
                              [FWCAEditorWidgets::BuildPreviewSection(
                                  SNew(SSplitter)
                                      .Orientation(Orient_Vertical)

                                      + SSplitter::Slot()
                                            .Value(0.68f)
                                                [SNew(SOverlay)
                                                 + SOverlay::Slot()
                                                       [SAssignNew(PreviewViewport, SWetWrinkleViewport)
                                                            .WetClothingAsset(WetClothingAsset.Get())
                                                            .WorkerJobScheduler(WorkerJobScheduler)
                                                            .SessionStore(SessionStore)
                                                            .SpatialQueryService(SpatialQueryService)
                                                            .TextureWorkspace(TextureWorkspace)
                                                            .PreviewCommitCoordinator(PreviewCommitCoordinator)
                                                            .RenderUploadQueue(RenderUploadQueue)
                                                            .OnSurfaceHitChanged(FOnWetWrinkleSurfaceHitChanged::CreateSP(this, &SWetWrinkleEditorPanel::HandleSurfaceHitChanged))]
                                                 + SOverlay::Slot()
                                                       .HAlign(HAlign_Left)
                                                       .VAlign(VAlign_Top)
                                                       .Padding(14.0f, 42.0f, 14.0f, 14.0f)
                                                           [BuildPreviewDisplayPanel()]]

                                      + SSplitter::Slot()
                                            .Value(0.32f)
                                                [BuildWrinkleUVViewSection()],
                                  FOnWetClothingPreviewFocusClicked::CreateSP(this, &SWetWrinkleEditorPanel::HandleFocusClicked))]

                    + SSplitter::Slot()
                          .Value(0.28f)
                              [SAssignNew(RightPanelSwitcher, SWidgetSwitcher)

                               + SWidgetSwitcher::Slot()
                                     [BuildAuthoringRightPanel()]

                               + SWidgetSwitcher::Slot()
                                     [BuildCustomNormalRightPanel()]]]];
    PreviewController->AttachViewport(PreviewViewport);
    AuthoringController->AttachViewport(PreviewViewport);
    PreviewViewport->SetAuthoringController(AuthoringController);
    RefreshFromAsset();
}

TSharedRef<SWidget> SWetWrinkleEditorPanel::BuildRuntimeWrinkleNormalStatusSection()
{
    auto BuildStatusRow = [](const FText& Label, const TAttribute<FText>& Value)
    {
        return SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
                  .AutoWidth()
                  .VAlign(VAlign_Top)
                  .Padding(0.0f, 0.0f, 8.0f, 4.0f)
                      [SNew(STextBlock)
                           .Text(Label)
                           .ColorAndOpacity(FSlateColor::UseSubduedForeground())]
            + SHorizontalBox::Slot()
                  .FillWidth(1.0f)
                  .VAlign(VAlign_Top)
                      [SNew(STextBlock)
                           .Text(Value)
                           .AutoWrapText(true)];
    };

    return SNew(SBorder)
        .Padding(FMargin(0.0f, 8.0f, 0.0f, 0.0f))
        .BorderImage(FAppStyle::GetBrush(TEXT("NoBorder")))
            [SNew(SVerticalBox)

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 4.0f)
                       [FWCAEditorWidgets::BuildSectionHeader(LOCTEXT("RuntimeWrinkleNormalHeading", "Runtime Wrinkle Normal"))]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                       [SNew(SSeparator)]

             + SVerticalBox::Slot()
                   .AutoHeight()
                       [BuildStatusRow(
                           LOCTEXT("RuntimeWrinkleSourceLabel", "Source"),
                           TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &SWetWrinkleEditorPanel::GetRuntimeNormalSourceText)))]

             + SVerticalBox::Slot()
                   .AutoHeight()
                       [BuildStatusRow(
                           LOCTEXT("RuntimeWrinkleTextureLabel", "Texture"),
                           TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &SWetWrinkleEditorPanel::GetRuntimeNormalTextureText)))]

             + SVerticalBox::Slot()
                   .AutoHeight()
                       [BuildStatusRow(
                           LOCTEXT("RuntimeWrinkleUVLabel", "UV"),
                           TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &SWetWrinkleEditorPanel::GetRuntimeNormalUVText)))]

             + SVerticalBox::Slot()
                   .AutoHeight()
                       [BuildStatusRow(
                           LOCTEXT("RuntimeWrinkleCoverageLabel", "Separation Mask"),
                           TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &SWetWrinkleEditorPanel::GetRuntimeNormalCoverageText)))]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 8.0f, 0.0f, 0.0f)
                       [SNew(STextBlock)
                            .Text(this, &SWetWrinkleEditorPanel::GetRuntimeNormalStatusText)
                            .ColorAndOpacity(this, &SWetWrinkleEditorPanel::GetRuntimeNormalStatusColor)
                            .AutoWrapText(true)]];
}

TSharedRef<SWidget> SWetWrinkleEditorPanel::BuildAuthoringRightPanel()
{
    return SNew(SSplitter)
        .Orientation(Orient_Vertical)

        + SSplitter::Slot()
              .Value(0.58f)
                  [BuildPatchBrushSection()]

        + SSplitter::Slot()
              .Value(0.42f)
                  [BuildPatchListSection()];
}

TSharedRef<SWidget> SWetWrinkleEditorPanel::BuildCustomNormalRightPanel()
{
    return SAssignNew(CustomNormalPanel, SWetWrinkleCustomNormalPanel)
        .WetClothingAsset(WetClothingAsset.Get())
        .AuthoringDocument(AuthoringDocument)
        .MaterialSlotIndex_Lambda([this]()
        {
            return BrushSettings.MaterialSlotIndex;
        })
        .OnSettingsChanged(FSimpleDelegate::CreateSP(this, &SWetWrinkleEditorPanel::HandleCustomNormalSettingsChanged));
}

TSharedRef<SWidget> SWetWrinkleEditorPanel::BuildPreviewDisplayPanel()
{
    static const FSlateRoundedBoxBrush PreviewControlsBackgroundBrush(
        FLinearColor(0.025f, 0.025f, 0.025f, 0.86f), 6.0f,
        FLinearColor(0.22f, 0.22f, 0.22f, 0.9f), 1.0f);

    return SNew(SBox)
        .WidthOverride(280.0f)
        [SNew(SBorder)
            .BorderImage(&PreviewControlsBackgroundBrush)
            .Padding(8.0f)
            [SNew(SVerticalBox)
             + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)
               [SNew(STextBlock)
                    .Text(LOCTEXT("WrinklePreviewDisplayHeading", "Display"))
                    .Font(FAppStyle::GetFontStyle(TEXT("NormalFontBold")))]
             + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
               [SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(112.0f)[SNew(STextBlock).Text(LOCTEXT("WrinklePreviewWetnessShort", "Wetness"))]]
                + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[SNew(SSlider).MinValue(0.0f).MaxValue(1.0f).Value_Lambda([this](){ return BrushSettings.PreviewWetness; }).OnValueChanged(this, &SWetWrinkleEditorPanel::HandlePreviewWetnessChanged)]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.0f,0.0f,0.0f,0.0f)[SNew(SBox).WidthOverride(36.0f)[SNew(STextBlock).Justification(ETextJustify::Right).Text_Lambda([this](){ return FText::AsNumber(BrushSettings.PreviewWetness); })]]]
             + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
               [SNew(SCheckBox).IsChecked(this, &SWetWrinkleEditorPanel::GetShowBakedTransparencyState).OnCheckStateChanged(this, &SWetWrinkleEditorPanel::HandleShowBakedTransparencyChanged).ToolTipText(LOCTEXT("ShowBakedTransparencyTooltip", "Show the current baked Transparency Map. Live Transparency Editor paint data is not included."))[SNew(STextBlock).Text(LOCTEXT("BakedTransparencyShort", "Baked Transparency"))]]
             + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f, 0.0f, 0.0f)
               [SNew(SCheckBox).IsChecked(this, &SWetWrinkleEditorPanel::GetPreviewToggleState).OnCheckStateChanged(this, &SWetWrinkleEditorPanel::HandlePreviewToggleChanged)[SNew(STextBlock).Text(LOCTEXT("BrushCursorShort", "Brush Cursor"))]]]];
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
                   .Padding(0.0f, 0.0f, 0.0f, 16.0f)
                       [SNew(SHorizontalBox)

                        + SHorizontalBox::Slot()
                              .FillWidth(1.0f)
                                  [SNew(SBox)
                                       .HeightOverride(32.0f)
                                           [SNew(SCheckBox)
                                                 .Style(FAppStyle::Get(), TEXT("DetailsView.SectionButton"))
                                                 .Type(ESlateCheckBoxType::ToggleButton)
                                                  .HAlign(HAlign_Center)
                                                  .IsChecked(this, &SWetWrinkleEditorPanel::GetToolModeCheckState, EWetWrinkleToolMode::Patch)
                                                  .OnCheckStateChanged(this, &SWetWrinkleEditorPanel::HandleToolModeChanged, EWetWrinkleToolMode::Patch)
                                                     [SNew(SBox)
                                                          .VAlign(VAlign_Center)
                                                              [SNew(STextBlock)
                                                                   .Text(LOCTEXT("PatchToolMode", "Patch"))
                                                                   .Justification(ETextJustify::Center)]]]]

                        + SHorizontalBox::Slot()
                              .FillWidth(1.0f)
                              .Padding(4.0f, 0.0f, 0.0f, 0.0f)
                                  [SNew(SBox)
                                       .HeightOverride(32.0f)
                                           [SNew(SCheckBox)
                                                 .Style(FAppStyle::Get(), TEXT("DetailsView.SectionButton"))
                                                 .Type(ESlateCheckBoxType::ToggleButton)
                                                  .HAlign(HAlign_Center)
                                                  .IsChecked(this, &SWetWrinkleEditorPanel::GetToolModeCheckState, EWetWrinkleToolMode::ProceduralRidgeStroke)
                                                  .OnCheckStateChanged(this, &SWetWrinkleEditorPanel::HandleToolModeChanged, EWetWrinkleToolMode::ProceduralRidgeStroke)
                                                     [SNew(SBox)
                                                          .VAlign(VAlign_Center)
                                                              [SNew(STextBlock)
                                                                   .Text(LOCTEXT("RidgeStrokeToolMode", "Ridge Stroke"))
                                                                   .Justification(ETextJustify::Center)]]]]]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                       [SNew(SSeparator)]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                       [SNew(STextBlock)
                            .Visibility(this, &SWetWrinkleEditorPanel::GetProceduralRidgeToolVisibility)
                            .Text(LOCTEXT("RidgeStrokeSettingsHeading", "Stroke Setup"))
                            .Font(FAppStyle::GetFontStyle(TEXT("NormalFontBold")))]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                       [SNew(SBox)
                            .Visibility(this, &SWetWrinkleEditorPanel::GetProceduralRidgeToolVisibility)
                                [SNew(SHorizontalBox)

                         + SHorizontalBox::Slot()
                               .AutoWidth()
                               .VAlign(VAlign_Center)
                                   [SNew(SCheckBox)
                                        .Style(FAppStyle::Get(), TEXT("RadioButton"))
                                        .IsChecked(this, &SWetWrinkleEditorPanel::GetRidgeEditModeCheckState, EWetProceduralRidgeEditMode::Draw)
                                        .OnCheckStateChanged(this, &SWetWrinkleEditorPanel::HandleRidgeEditModeChanged, EWetProceduralRidgeEditMode::Draw)
                                            [SNew(STextBlock).Text(LOCTEXT("RidgeDrawMode", "Draw"))]]

                         + SHorizontalBox::Slot()
                               .AutoWidth()
                               .VAlign(VAlign_Center)
                               .Padding(16.0f, 0.0f, 0.0f, 0.0f)
                                   [SNew(SCheckBox)
                                        .Style(FAppStyle::Get(), TEXT("RadioButton"))
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
                                        .AutoWidth()
                                        .VAlign(VAlign_Center)
                                            [SNew(SCheckBox)
                                                  .Style(FAppStyle::Get(), TEXT("RadioButton"))
                                                  .IsChecked(this, &SWetWrinkleEditorPanel::GetRidgeShapeCheckState, EWetProceduralRidgeShape::Convex)
                                                  .OnCheckStateChanged(this, &SWetWrinkleEditorPanel::HandleRidgeShapeChanged, EWetProceduralRidgeShape::Convex)
                                                     [SNew(STextBlock).Text(LOCTEXT("RidgeShapeConvex", "Convex"))]]

                                  + SHorizontalBox::Slot()
                                        .AutoWidth()
                                        .VAlign(VAlign_Center)
                                        .Padding(16.0f, 0.0f, 0.0f, 0.0f)
                                            [SNew(SCheckBox)
                                                  .Style(FAppStyle::Get(), TEXT("RadioButton"))
                                                  .IsChecked(this, &SWetWrinkleEditorPanel::GetRidgeShapeCheckState, EWetProceduralRidgeShape::Concave)
                                                  .OnCheckStateChanged(this, &SWetWrinkleEditorPanel::HandleRidgeShapeChanged, EWetProceduralRidgeShape::Concave)
                                                     [SNew(STextBlock).Text(LOCTEXT("RidgeShapeConcave", "Concave"))]]

                                  + SHorizontalBox::Slot()
                                        .AutoWidth()
                                        .VAlign(VAlign_Center)
                                        .Padding(16.0f, 0.0f, 0.0f, 0.0f)
                                            [SNew(SCheckBox)
                                                  .Style(FAppStyle::Get(), TEXT("RadioButton"))
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
                       [SNew(SBox)
                            .Visibility(this, &SWetWrinkleEditorPanel::GetPatchToolVisibility)
                                [SNew(SVerticalBox)

              + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                        [SNew(STextBlock)
                             .Text(LOCTEXT("WrinkleNormalTextureLabel", "Wrinkle Normal Texture"))
                             .Font(FAppStyle::GetFontStyle(TEXT("NormalFontBold")))]

              + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                        [SNew(SComboButton)
                             .IsEnabled_Lambda([this]() { return BrushSettings.ToolMode == EWetWrinkleToolMode::Patch; })
                             .ButtonStyle(FAppStyle::Get(), TEXT("Button"))
                             .ContentPadding(FMargin(8.0f, 5.0f))
                             .OnGetMenuContent(this, &SWetWrinkleEditorPanel::BuildWrinkleNormalTextureMenu)
                             .ButtonContent()
                             [SNew(STextBlock)
                                  .Text(this, &SWetWrinkleEditorPanel::GetWrinkleNormalTextureDisplayName)
                                  .OverflowPolicy(ETextOverflowPolicy::Ellipsis)]]

              + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(0.0f, 0.0f, 0.0f, 16.0f)
                        [SNew(SBox)
                             .WidthOverride(160.0f)
                             .HeightOverride(160.0f)
                             .HAlign(HAlign_Center)
                             .VAlign(VAlign_Center)
                                 [SNew(SBorder)
                                      .Padding(2.0f)
                                      .BorderImage(FAppStyle::Get().GetBrush(TEXT("ToolPanel.GroupBorder")))
                                          [SNew(SScaleBox)
                                               .Stretch(EStretch::ScaleToFit)
                                                   [SNew(SImage)
                                                        .Image(this, &SWetWrinkleEditorPanel::GetWrinkleNormalThumbnailBrush)
                                                        .Visibility(this, &SWetWrinkleEditorPanel::GetWrinkleNormalThumbnailVisibility)]]]]]]

             + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
               [SNew(SSeparator)]

             + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
               [SNew(STextBlock).Text(LOCTEXT("WrinkleBrushSettingsHeading", "Brush Settings")).Font(FAppStyle::GetFontStyle(TEXT("NormalFontBold")))]

             + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
               [SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(104.0f)[SNew(STextBlock).Text(LOCTEXT("CompactBrushSizeLabel", "Size"))]]
                + SHorizontalBox::Slot().FillWidth(1.0f)[SNew(SSpinBox<float>).MinValue(0.1f).MaxValue(100.0f).Value(this, &SWetWrinkleEditorPanel::GetBrushSizeCm).OnBeginSliderMovement(this, &SWetWrinkleEditorPanel::HandleRidgePropertySliderBegin).OnEndSliderMovement(this, &SWetWrinkleEditorPanel::HandleRidgePropertySliderEnd).OnValueCommitted(this, &SWetWrinkleEditorPanel::HandleRidgePropertyCommitted).OnValueChanged(this, &SWetWrinkleEditorPanel::HandleBrushRadiusChanged).ToolTipText(LOCTEXT("BrushSizeCmTooltip", "Brush size in centimeters."))]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.0f,0.0f)[SNew(STextBlock).Text(LOCTEXT("CentimeterUnit", "cm"))]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2.0f,0.0f,0.0f,0.0f)
                  [SAssignNew(BrushSizeComboButton, SComboButton)
                    .ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton"))
                    .ToolTipText(LOCTEXT("BrushSizePresetTooltip", "Choose a brush size preset."))
                    .MenuContent()[BuildBrushSizeMenu()]
                    .ButtonContent()[SNew(STextBlock).Text(FText::FromString(TEXT("▼")))]]
                + SHorizontalBox::Slot().AutoWidth()[SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton").Visibility_Lambda([this](){ return FMath::IsNearlyEqual(GetBrushSizeCm(), WetWrinkleDefaultSizeCm) ? EVisibility::Hidden : EVisibility::Visible; }).ToolTipText(LOCTEXT("ResetBrushSize", "Reset Size to default.")).OnClicked_Lambda([this](){ HandleBrushRadiusChanged(WetWrinkleDefaultSizeCm); return FReply::Handled(); })[SNew(SImage).Image(FAppStyle::GetBrush("PropertyWindow.DiffersFromDefault"))]]]

             + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
               [SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(104.0f)[SNew(STextBlock).Text(LOCTEXT("StrengthLabel", "Strength"))]]
                + SHorizontalBox::Slot().FillWidth(1.0f)[SNew(SSpinBox<float>).MinValue(0.0f).MaxValue(4.0f).MinSliderValue(0.0f).MaxSliderValue(4.0f).Value(this, &SWetWrinkleEditorPanel::GetRidgeStrengthValue).OnBeginSliderMovement(this, &SWetWrinkleEditorPanel::HandleRidgePropertySliderBegin).OnEndSliderMovement(this, &SWetWrinkleEditorPanel::HandleRidgePropertySliderEnd).OnValueCommitted(this, &SWetWrinkleEditorPanel::HandleRidgePropertyCommitted).OnValueChanged(this, &SWetWrinkleEditorPanel::HandleStrengthChanged)]
                + SHorizontalBox::Slot().AutoWidth()[SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton").Visibility_Lambda([this](){ return FMath::IsNearlyEqual(GetRidgeStrengthValue(), 1.0f) ? EVisibility::Hidden : EVisibility::Visible; }).ToolTipText(LOCTEXT("ResetBrushStrength", "Reset Strength to default.")).OnClicked_Lambda([this](){ HandleStrengthChanged(1.0f); return FReply::Handled(); })[SNew(SImage).Image(FAppStyle::GetBrush("PropertyWindow.DiffersFromDefault"))]]]

             + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)
               [SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(104.0f)[SNew(STextBlock).Text(LOCTEXT("EdgeSoftnessLabel", "Edge Softness"))]]
                + SHorizontalBox::Slot().FillWidth(1.0f)[SNew(SSpinBox<float>).MinValue(0.0f).MaxValue(100.0f).Value(this, &SWetWrinkleEditorPanel::GetRidgeFalloffPercentValue).OnBeginSliderMovement(this, &SWetWrinkleEditorPanel::HandleRidgePropertySliderBegin).OnEndSliderMovement(this, &SWetWrinkleEditorPanel::HandleRidgePropertySliderEnd).OnValueCommitted(this, &SWetWrinkleEditorPanel::HandleRidgePropertyCommitted).OnValueChanged(this, &SWetWrinkleEditorPanel::HandleFalloffChanged)]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.0f,0.0f)[SNew(STextBlock).Text(LOCTEXT("PercentUnit", "%"))]
                + SHorizontalBox::Slot().AutoWidth()[SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton").Visibility_Lambda([this](){ return FMath::IsNearlyEqual(GetRidgeFalloffPercentValue(), 50.0f) ? EVisibility::Hidden : EVisibility::Visible; }).ToolTipText(LOCTEXT("ResetEdgeSoftness", "Reset Edge Softness to default.")).OnClicked_Lambda([this](){ HandleFalloffChanged(50.0f); return FReply::Handled(); })[SNew(SImage).Image(FAppStyle::GetBrush("PropertyWindow.DiffersFromDefault"))]]]

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

             + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)
               [SNew(SHorizontalBox).Visibility(this, &SWetWrinkleEditorPanel::GetPatchToolVisibility)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(104.0f)[SNew(STextBlock).Text(LOCTEXT("RotationLabel", "Rotation"))]]
                + SHorizontalBox::Slot().FillWidth(1.0f)[SNew(SSpinBox<float>).MinValue(-180.0f).MaxValue(180.0f).Value_Lambda([this](){ return FMath::RadiansToDegrees(BrushSettings.RotationRadians); }).OnValueChanged(this, &SWetWrinkleEditorPanel::HandleRotationChanged)]
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.0f,0.0f)[SNew(STextBlock).Text(LOCTEXT("DegreeUnit", "°"))]
                + SHorizontalBox::Slot().AutoWidth()[SNew(SButton).ButtonStyle(FAppStyle::Get(), "SimpleButton").Visibility_Lambda([this](){ return FMath::IsNearlyZero(BrushSettings.RotationRadians) ? EVisibility::Hidden : EVisibility::Visible; }).ToolTipText(LOCTEXT("ResetBrushRotation", "Reset Rotation to default.")).OnClicked_Lambda([this](){ HandleRotationChanged(0.0f); return FReply::Handled(); })[SNew(SImage).Image(FAppStyle::GetBrush("PropertyWindow.DiffersFromDefault"))]]]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 6.0f, 0.0f, 8.0f)
                       [SNew(SSeparator)
                            .Visibility(this, &SWetWrinkleEditorPanel::GetProceduralRidgeToolVisibility)]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                       [SNew(STextBlock)
                            .Visibility(this, &SWetWrinkleEditorPanel::GetProceduralRidgeToolVisibility)
                            .Text(LOCTEXT("RidgeTaperHeading", "Taper"))
                            .Font(FAppStyle::GetFontStyle(TEXT("NormalFontBold")))]

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
                                       .Padding(0.0f, 6.0f, 0.0f, 8.0f)
                                           [SNew(SSeparator)]

                                 + SVerticalBox::Slot()
                                       .AutoHeight()
                                       .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                                           [SNew(SHorizontalBox)
                                            + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
                                              [SNew(STextBlock)
                                                .Text(LOCTEXT("RidgeVariationHeading", "Variation"))
                                                .Font(FAppStyle::GetFontStyle(TEXT("NormalFontBold")))]
                                            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                                              [SNew(SCheckBox)
                                                .IsChecked(this, &SWetWrinkleEditorPanel::GetRidgeNaturalVariationEnabledState)
                                                .OnCheckStateChanged(this, &SWetWrinkleEditorPanel::HandleRidgeNaturalVariationEnabledChanged)
                                                [SNew(STextBlock).Text(LOCTEXT("RidgeNaturalVariationEnabled", "Enabled"))]]]

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
                   .Padding(0.0f, 8.0f, 0.0f, 8.0f)
                       [SNew(SSeparator)
                            .Visibility(this, &SWetWrinkleEditorPanel::GetProceduralRidgeEditVisibility)]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                       [SNew(STextBlock)
                            .Visibility(this, &SWetWrinkleEditorPanel::GetProceduralRidgeEditVisibility)
                            .Text(LOCTEXT("RidgeEndpointEditingHeading", "Endpoint Editing"))
                            .Font(FAppStyle::GetFontStyle(TEXT("NormalFontBold")))]

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
                   .Padding(0.0f, 0.0f, 0.0f, 10.0f)
                       [SNew(SBox)
                            .Visibility(this, &SWetWrinkleEditorPanel::GetProceduralRidgeEditVisibility)
                                [SNew(SButton)
                                     .IsEnabled(this, &SWetWrinkleEditorPanel::CanDeleteSelectedRidgePoint)
                                     .Text(LOCTEXT("DeleteSelectedRidgePoint", "Delete Selected Point"))
                                     .ToolTipText(LOCTEXT("DeleteSelectedRidgePointTooltip", "Delete the selected control point. A ridge must keep at least two points."))
                                     .OnClicked(this, &SWetWrinkleEditorPanel::HandleDeleteSelectedRidgePointClicked)]]

             ]];
}

TSharedRef<SWidget> SWetWrinkleEditorPanel::BuildPatchListSection()
{
    return SAssignNew(ElementListPanel, SWetWrinkleElementListPanel)
        .SummaryText(this, &SWetWrinkleEditorPanel::GetPatchListSummaryText)
        .CanClear(this, &SWetWrinkleEditorPanel::IsClearStrokesEnabled)
        .OnClear(FOnClicked::CreateSP(this, &SWetWrinkleEditorPanel::HandleClearStrokesClicked))
        .OnGenerateRow(FOnGenerateWetWrinkleElementRow::CreateSP(
            this,
            &SWetWrinkleEditorPanel::GenerateStrokeRow))
        .OnSelectionChanged(FOnWetWrinkleElementSelectionChanged::CreateSP(
            this,
            &SWetWrinkleEditorPanel::HandleStrokeSelectionChanged));
}

void SWetWrinkleEditorPanel::RefreshFromAsset()
{
    RefreshFromAssetInternal(true, true);
}

void SWetWrinkleEditorPanel::RefreshFromAssetLightweight()
{
    RefreshFromAssetInternal(false, false);
}

void SWetWrinkleEditorPanel::SuspendPreview(const EDWCEditorPreviewSuspendReason Reason)
{
    if (bPreviewSuspended)
    {
        return;
    }

    if (AuthoringController.IsValid())
    {
        AuthoringController->CancelActiveInteraction(false);
    }
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->SuspendPreview(Reason);
    }
    bPreviewSuspended = true;
}

void SWetWrinkleEditorPanel::ResumePreviewIfNeeded()
{
    if (!bPreviewSuspended)
    {
        return;
    }

    bPreviewSuspended = false;
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->ResumePreviewIfNeeded();
        PushStrokeSelectionToViewport();
        PushBrushSettingsToViewport();
    }
}

void SWetWrinkleEditorPanel::RefreshFromAssetInternal(
    const bool bForcePreviewMaterialRebuild,
    const bool bRebuildAccumulatedPreview)
{
    if (UWetClothingAsset* Asset = WetClothingAsset.Get())
    {
        BrushSettings.UVChannelIndex = BrushSettings.MaterialSlotIndex != INDEX_NONE ? ResolveWetWrinkleUVChannel(WetClothingAsset.Get()) : INDEX_NONE;
    }
    RefreshMaterialSlotOptions();
    RefreshDWCDataUVChannel();
    if (bForcePreviewMaterialRebuild)
    {
        RefreshBrushPresetOptions();
    }
    RefreshRuntimeNormalUI(bRebuildAccumulatedPreview, false);
    RefreshStrokeList();
    RefreshWrinkleNormalThumbnail();
    InvalidateWrinkleUVViewCache();
    WrinkleUVIslandCache.Reset();
    WrinkleUVIslandCacheUseSerial = 0;

    if (PreviewViewport.IsValid())
    {
        PreviewViewport->RefreshPreviewMesh(bForcePreviewMaterialRebuild);
        PushStrokeSelectionToViewport();
        PushBrushSettingsToViewport();
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

    if (IsUsingCustomWrinkleMap())
    {
        FMessageDialog::Open(
            EAppMsgType::Ok,
            LOCTEXT(
                "BakeWrinkleCustomSourceActive",
                "This slot uses a Custom Wrinkle Map. Disable Custom Wrinkle Map before baking authored wrinkle elements."));
        return FReply::Handled();
    }

    const FDWCEditorPreviewSlotCollection CurrentSlotStates =
        FDWCEditorPreviewSlotResolver::Resolve(WetClothingAsset.Get());
    if (!CurrentSlotStates.IsReady(BrushSettings.MaterialSlotIndex))
    {
        const FDWCEditorPreviewSlotState* State =
            CurrentSlotStates.Find(BrushSettings.MaterialSlotIndex);
        FMessageDialog::Open(
            EAppMsgType::Ok,
            State != nullptr
                ? FDWCEditorPreviewSlotResolver::GetIssueText(State->Issue)
                : LOCTEXT("BakeWrinkleSlotUnavailable", "The selected material slot is unavailable for preview and bake."));
        return FReply::Handled();
    }

    return BakeWrinkleNormalMapsForSlots({BrushSettings.MaterialSlotIndex});
}

SWetWrinkleEditorPanel::~SWetWrinkleEditorPanel()
{
    if (AuthoringController.IsValid())
    {
        AuthoringController->CancelActiveInteraction(false);
        AuthoringController->DetachViewport();
    }
    if (SessionStore.IsValid())
    {
        SessionStore->OnChanged().RemoveAll(this);
    }
    if (PreviewController.IsValid())
    {
        PreviewController->DetachViewport();
    }
    TransientEditedProceduralRidgeStroke.Reset();
    bRidgePointEditActive = false;
    bRidgePropertyEditActive = false;
    if (GEditor != nullptr)
    {
        GEditor->UnregisterForUndo(this);
    }
}

void SWetWrinkleEditorPanel::PostUndo(bool bSuccess)
{
    if (bSuccess)
    {
        if (AuthoringController.IsValid())
        {
            AuthoringController->CancelActiveInteraction(false);
        }
        bRidgePropertyEditActive = false;
        TransientEditedProceduralRidgeStroke.Reset();
        SelectedProceduralRidgePointIndex = INDEX_NONE;
        if (PreviewViewport.IsValid())
        {
            PreviewViewport->ClearTransientProceduralStroke();
            PreviewViewport->SetEditingProceduralStrokeGuid(FGuid());
        }
        if (AuthoringDocument.IsValid())
        {
            FDWCEditorAuthoringChange Change;
            Change.Domain = EDWCEditorAuthoringDomain::Wrinkle;
            Change.Phase = EDWCEditorAuthoringChangePhase::UndoRedo;
            Change.Impact = EDWCEditorAuthoringImpact::ElementList |
                EDWCEditorAuthoringImpact::Preview |
                EDWCEditorAuthoringImpact::Details;
            AuthoringDocument->NotifyUndoRedo(Change);
        }
        RefreshFromAsset();
    }
}

void SWetWrinkleEditorPanel::PostRedo(bool bSuccess)
{
    PostUndo(bSuccess);
}

FReply SWetWrinkleEditorPanel::BakeWrinkleNormalMapsForSlots(const TArray<int32>& MaterialSlotIndices)
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

    if (!BakeCoordinator.IsValid())
    {
        FMessageDialog::Open(
            EAppMsgCategory::Warning,
            EAppMsgType::Ok,
            LOCTEXT("BakeWrinkleCoordinatorUnavailable", "The asynchronous bake service is unavailable."));
        return FReply::Handled();
    }
    if (BakeCoordinator->IsWrinkleBakeActive())
    {
        return FReply::Handled();
    }

    TWeakPtr<SWetWrinkleEditorPanel> WeakThis = SharedThis(this);
    FString RequestError;
    if (!BakeCoordinator->RequestWrinkleBake(
            MaterialSlotIndices,
            true,
            [WeakThis](const FDWCEditorBakeBatchResult& Result)
            {
                const TSharedPtr<SWetWrinkleEditorPanel> Panel = WeakThis.Pin();
                if (!Panel.IsValid())
                {
                    return;
                }
                Panel->MarkAssetEdited();
                Panel->RefreshFromAssetLightweight();
                FMessageDialog::Open(
                    Result.bSucceeded
                        ? EAppMsgCategory::Success
                        : EAppMsgCategory::Warning,
                    EAppMsgType::Ok,
                    FText::FromString(Result.Summary));
            },
            &RequestError))
    {
        FMessageDialog::Open(
            EAppMsgCategory::Warning,
            EAppMsgType::Ok,
            FText::FromString(RequestError));
    }
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

FWetWrinkleBrushSettings SWetWrinkleEditorPanel::MakeViewportBrushSettings() const
{
    FWetWrinkleBrushSettings PreviewSettings = BrushSettings;
    if (IsUsingCustomWrinkleMap())
    {
        PreviewSettings.bShowPreview = false;
    }
    return PreviewSettings;
}

void SWetWrinkleEditorPanel::PushBrushSettingsToViewport()
{
    DispatchWrinkleBrushState(EDWCEditorSessionEffect::None);
    if (bPreviewSuspended)
    {
        return;
    }
    PreviewController->SynchronizeBrushSettings(MakeViewportBrushSettings());
}

void SWetWrinkleEditorPanel::PushBrushTopologyToViewport()
{
    DispatchWrinkleBrushState(EDWCEditorSessionEffect::None);
    if (bPreviewSuspended)
    {
        return;
    }
    PreviewController->UpdateBrushTopology(MakeViewportBrushSettings());
}

void SWetWrinkleEditorPanel::PushBrushPreviewSettingsToViewport()
{
    DispatchWrinkleBrushState(EDWCEditorSessionEffect::None);
    if (bPreviewSuspended)
    {
        return;
    }
    PreviewController->UpdateBrushPreview(MakeViewportBrushSettings());
}

void SWetWrinkleEditorPanel::PushPreviewWetnessToViewport()
{
    DispatchWrinkleBrushState(EDWCEditorSessionEffect::None);
    if (bPreviewSuspended)
    {
        return;
    }
    PreviewController->UpdatePreviewWetness(BrushSettings.PreviewWetness);
    if (PreviewViewport.IsValid() && !bPreviewSuspended)
    {
        PreviewViewport->SetShowBakedTransparency(bShowBakedTransparency);
    }
}

void SWetWrinkleEditorPanel::DispatchWrinkleBrushState(const EDWCEditorSessionEffect Effects)
{
    if (!SessionStore.IsValid() || bApplyingSessionState)
    {
        return;
    }

    FDWCSetWrinkleBrushAction Action;
    Action.Brush = BrushSettings;
    Action.BrushSizeCm = SizeCm;
    Action.BrushSizeUV = SizeUV;
    Action.Effects = Effects;
    SessionStore->Dispatch(Action);
}

void SWetWrinkleEditorPanel::DispatchWrinkleSelectionState()
{
    if (!SessionStore.IsValid() || bApplyingSessionState)
    {
        return;
    }

    FDWCSelectWrinkleElementAction Action;
    Action.ElementGuid = SelectedStrokeGuid;
    Action.ElementType = SelectedElementType;
    Action.RidgePointIndex = SelectedProceduralRidgePointIndex;
    SessionStore->Dispatch(Action);
}

void SWetWrinkleEditorPanel::HandleSessionStateChanged(
    const FDWCEditorSessionState& State,
    const EDWCEditorSessionEffect Effects,
    uint64)
{
    TGuardValue<bool> Guard(bApplyingSessionState, true);
    const FDWCEditorWrinkleSessionState& WrinkleState = State.Wrinkle;
    BrushSettings = WrinkleState.Brush;
    SizeCm = WrinkleState.BrushSizeCm;
    SizeUV = WrinkleState.BrushSizeUV;
    bShowBakedTransparency = WrinkleState.bShowBakedTransparency;
    SelectedStrokeGuid = WrinkleState.SelectedElementGuid;
    SelectedElementType = WrinkleState.SelectedElementType;
    SelectedProceduralRidgePointIndex = WrinkleState.SelectedRidgePointIndex;

    if (State.ActiveMode != EWCAEditorMode::WrinkleEdit)
    {
        return;
    }

    if (EnumHasAnyFlags(Effects, EDWCEditorSessionEffect::RefreshElementList))
    {
        RefreshStrokeList();
    }
    if (EnumHasAnyFlags(Effects, EDWCEditorSessionEffect::SyncSelection))
    {
        PushStrokeSelectionToViewport();
    }
    if (EnumHasAnyFlags(Effects, EDWCEditorSessionEffect::RebuildHitTopology))
    {
        PushBrushTopologyToViewport();
    }
    else if (EnumHasAnyFlags(Effects, EDWCEditorSessionEffect::UpdatePreviewParameters))
    {
        PushBrushPreviewSettingsToViewport();
        PushPreviewWetnessToViewport();
    }
    if (EnumHasAnyFlags(Effects, EDWCEditorSessionEffect::RebuildPreviewContent))
    {
        RefreshStrokeOverlay(true);
    }
    if (EnumHasAnyFlags(Effects, EDWCEditorSessionEffect::RefreshUVView))
    {
        RefreshWrinkleUVView();
    }
    if (EnumHasAnyFlags(Effects, EDWCEditorSessionEffect::RefreshDetails) && DetailsView.IsValid())
    {
        DetailsView->ForceRefresh();
    }
}

void SWetWrinkleEditorPanel::RefreshStrokeList()
{
    TArray<FStrokeListItemPtr> NewStrokeListItems;

    if (const UWetClothingAsset* Asset = WetClothingAsset.Get())
    {
        const TArray<FWetWrinklePatchPlacement>& Patches = Asset->Authored.WrinkleData.EditablePatches;
        for (int32 PatchIndex = 0; PatchIndex < Patches.Num(); ++PatchIndex)
        {
            const FWetWrinklePatchPlacement& Patch = Patches[PatchIndex];
            if (!IsPatchVisibleForCurrentMaterialSlot(Patch))
            {
                continue;
            }

            FStrokeListItemPtr Item = MakeShared<FWetWrinkleElementListItem>();
            Item->StrokeGuid = Patch.PatchGuid;
            Item->ElementType = EWetWrinkleElementType::Patch;
            Item->SourceIndex = PatchIndex;
            NewStrokeListItems.Add(Item);
        }

        const TArray<FWetProceduralRidgeStroke>& RidgeStrokes =
            Asset->Authored.WrinkleData.EditableProceduralRidgeStrokes;
        for (int32 StrokeIndex = 0; StrokeIndex < RidgeStrokes.Num(); ++StrokeIndex)
        {
            const FWetProceduralRidgeStroke& Stroke = RidgeStrokes[StrokeIndex];
            if (!IsProceduralRidgeStrokeVisibleForCurrentMaterialSlot(Stroke))
            {
                continue;
            }

            FStrokeListItemPtr Item = MakeShared<FWetWrinkleElementListItem>();
            Item->StrokeGuid = Stroke.StrokeGuid;
            Item->ElementType = EWetWrinkleElementType::ProceduralRidgeStroke;
            Item->SourceIndex = StrokeIndex;
            NewStrokeListItems.Add(Item);
        }
    }

    const bool bSelectedStrokeIsVisible = NewStrokeListItems.ContainsByPredicate(
        [this](const FStrokeListItemPtr& Item)
        {
            return Item.IsValid() &&
                   Item->StrokeGuid == SelectedStrokeGuid &&
                   Item->ElementType == SelectedElementType;
        });
    if (!bSelectedStrokeIsVisible)
    {
        SelectedStrokeGuid.Invalidate();
        SelectedElementType = EWetWrinkleElementType::Patch;
        SelectedProceduralRidgePointIndex = INDEX_NONE;
    }

    if (ElementListPanel.IsValid())
    {
        ElementListPanel->SetItems(
            MoveTemp(NewStrokeListItems),
            SelectedStrokeGuid,
            SelectedElementType);
    }
}

bool SWetWrinkleEditorPanel::IsPatchVisibleForCurrentMaterialSlot(const FWetWrinklePatchPlacement& Patch) const
{
    return Patch.MaterialSlotIndex != INDEX_NONE &&
           (BrushSettings.MaterialSlotIndex == INDEX_NONE || Patch.MaterialSlotIndex == BrushSettings.MaterialSlotIndex);
}

bool SWetWrinkleEditorPanel::IsProceduralRidgeStrokeVisibleForCurrentMaterialSlot(const FWetProceduralRidgeStroke& Stroke) const
{
    return Stroke.MaterialSlotIndex != INDEX_NONE &&
           (BrushSettings.MaterialSlotIndex == INDEX_NONE || Stroke.MaterialSlotIndex == BrushSettings.MaterialSlotIndex);
}

void SWetWrinkleEditorPanel::RefreshStrokeOverlay(bool bRebuildAccumulatedPreview)
{
    if (bPreviewSuspended)
    {
        return;
    }
    PushStrokeSelectionToViewport();
    PreviewController->RefreshStoredElements(bRebuildAccumulatedPreview);
    RebuildWrinkleUVViewPatchMarkerCache();
    RefreshWrinkleUVViewMarkersOnly();
}

void SWetWrinkleEditorPanel::PushStrokeSelectionToViewport()
{
    DispatchWrinkleSelectionState();
    if (bPreviewSuspended)
    {
        return;
    }
    PreviewController->UpdateElementSelection(
        SelectedElementType,
        SelectedStrokeGuid,
        SelectedProceduralRidgePointIndex);
}

void SWetWrinkleEditorPanel::RefreshMaterialSlotOptions()
{
    MaterialSlotItems.Reset();
    MaterialSlotThumbnails.Reset();
    PreviewSlotStates = FDWCEditorPreviewSlotResolver::Resolve(WetClothingAsset.Get());

    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const USkeletalMesh* TargetMesh = nullptr;
    if (Asset != nullptr)
    {
        TargetMesh = Asset->GetDWCSkeletalMesh() != nullptr ? Asset->GetDWCSkeletalMesh() : nullptr;
    }

    if (TargetMesh != nullptr)
    {
        for (const FDWCEditorPreviewSlotState& State : PreviewSlotStates.Slots)
        {
            if (!State.bWettable)
            {
                continue;
            }

            const int32 MaterialSlotIndex = State.MaterialSlotIndex;
            const FSkeletalMaterial& SkeletalMaterial = TargetMesh->GetMaterials()[MaterialSlotIndex];
            FMaterialSlotItemPtr Item = MakeShared<FWCAMaterialSlotItem>();
            Item->SlotIndex = MaterialSlotIndex;
            Item->SlotName = SkeletalMaterial.MaterialSlotName;
            Item->Material = SkeletalMaterial.MaterialInterface;
            Item->bIsWettableSlot = true;
            MaterialSlotItems.Add(Item);
        }
    }

    if (!PreviewSlotStates.ReadyWettableSlotIndices.IsEmpty())
    {
        FMaterialSlotItemPtr AllSlotsItem = MakeShared<FWCAMaterialSlotItem>();
        AllSlotsItem->SlotIndex = INDEX_NONE;
        AllSlotsItem->SlotName = TEXT("All Wettable Slots");
        MaterialSlotItems.Insert(AllSlotsItem, 0);
    }

    const bool bSelectedSlotStillAvailable = BrushSettings.MaterialSlotIndex == INDEX_NONE
        ? !PreviewSlotStates.ReadyWettableSlotIndices.IsEmpty()
        : PreviewSlotStates.IsReady(BrushSettings.MaterialSlotIndex);
    if (!bSelectedSlotStillAvailable)
    {
        BrushSettings.MaterialSlotIndex = INDEX_NONE;
        BrushSettings.UVChannelIndex = INDEX_NONE;
        CurrentHit = FWetWrinkleSurfaceHit();
    }

    if (MaterialSlotListView.IsValid())
    {
        MaterialSlotListView->RequestListRefresh();
        MaterialSlotListView->SetSelection(FindMaterialSlotItem(BrushSettings.MaterialSlotIndex), ESelectInfo::Direct);
    }
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

    const FDWCEditorPreviewSlotState* PreviewSlotState = FindPreviewSlotState(MaterialSlotIndex);
    if (PreviewSlotState == nullptr || !PreviewSlotState->bPreviewReady)
    {
        BrushSettings.UVChannelIndex = INDEX_NONE;
        InvalidateWrinkleUVViewCache();
        if (bShowFailureDialog)
        {
            FMessageDialog::Open(
                EAppMsgType::Ok,
                PreviewSlotState != nullptr
                    ? FDWCEditorPreviewSlotResolver::GetIssueText(PreviewSlotState->Issue)
                    : LOCTEXT("WrinkleUVSlotUnavailable", "This material slot is unavailable for preview."));
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

    return true;
}

void SWetWrinkleEditorPanel::InvalidateWrinkleUVViewCache()
{
    CachedWrinkleUVViewMesh = nullptr;
    CachedWrinkleUVViewLODRenderDataIdentity = nullptr;
    CachedWrinkleUVViewTopologySignature.Reset();
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
    TSharedRef<SWetWrinkleUVPanel> Panel =
        SAssignNew(WrinkleUVPanel, SWetWrinkleUVPanel)
            .ChannelText(this, &SWetWrinkleEditorPanel::GetDWCDataUVChannelText);
    WrinkleUVView = Panel->GetView();
    return Panel;
}

void SWetWrinkleEditorPanel::RefreshWrinkleUVView()
{
    TRACE_CPUPROFILER_EVENT_SCOPE(SWetWrinkleEditorPanel_RefreshWrinkleUVView);

    if (!WrinkleUVView.IsValid())
    {
        return;
    }

    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const USkeletalMesh* TargetMesh = Asset != nullptr ? Asset->GetDWCSkeletalMesh() : nullptr;
    const int32 MaterialSlotIndex = BrushSettings.MaterialSlotIndex;
    const int32 UVChannelIndex = GetWrinkleUVViewChannelIndex();
    const int32 NumUVChannels = FWetClothingAssetMeshAnalyzer::GetNumUVChannels(TargetMesh, 0);
    if (WrinkleUVPanel.IsValid())
    {
        WrinkleUVView->SetDisplayMode(WrinkleUVPanel->GetDisplayMode());
        WrinkleUVView->SetUVIslandLineOpacity(WrinkleUVPanel->GetIslandLineOpacity());
        WrinkleUVView->SetUVIslandLineThicknessScale(WrinkleUVPanel->GetIslandLineThicknessScale());
    }
    WrinkleUVView->SetNormalizeToContentBounds(false);
    WrinkleUVView->SetBackgroundTexture(nullptr);
    WrinkleUVView->SetDrawBackgroundTexture(false);

    if (MaterialSlotIndex == INDEX_NONE)
    {
        const bool bGeometryChanged =
            CachedWrinkleUVViewMesh != TargetMesh ||
            CachedWrinkleUVViewChannelIndex != UVChannelIndex ||
            CachedWrinkleUVViewMaterialSlotIndex != MaterialSlotIndex;
        CachedWrinkleUVViewMesh = TargetMesh;
        CachedWrinkleUVViewLODRenderDataIdentity = nullptr;
        CachedWrinkleUVViewTopologySignature.Reset();
        CachedWrinkleUVViewChannelIndex = UVChannelIndex;
        CachedWrinkleUVViewMaterialSlotIndex = MaterialSlotIndex;
        if (bGeometryChanged)
        {
            WrinkleUVIslandItems.Reset();
            WrinkleUVView->SetIslands(TArray<TSharedPtr<FWetClothingAssetUVIsland>>());
            WrinkleUVView->SetIslandColors(TMap<int32, FLinearColor>());
            WrinkleUVView->SetHiddenUVIslandIDs(TSet<int32>());
            WrinkleUVView->SetSelectedIslands(TSet<int32>());
        }
        WrinkleUVView->SetCircleMarkers(TArray<FWCAUVViewCircleMarker>());
        return;
    }

    const FSkeletalMeshRenderData* RenderData = TargetMesh != nullptr
                                                    ? TargetMesh->GetResourceForRendering()
                                                    : nullptr;
    const void* LODRenderDataIdentity =
        RenderData != nullptr && RenderData->LODRenderData.IsValidIndex(0)
            ? static_cast<const void*>(&RenderData->LODRenderData[0])
            : nullptr;
    FString TopologySignature;
    if (Asset != nullptr)
    {
        if (const FDWCDataUVLODMetadata* DataUVMetadata = Asset->FindDataUVMetadataForLOD(0);
            DataUVMetadata != nullptr && DataUVMetadata->UVChannelIndex == UVChannelIndex)
        {
            TopologySignature = DataUVMetadata->DataUVOutputSignature;
        }
#if WITH_EDITORONLY_DATA
        else if (const FDWCEditorUVTopologyData* OriginalUVTopology = Asset->FindOriginalUVTopologyForLOD(0);
                 OriginalUVTopology != nullptr && OriginalUVTopology->UVChannelIndex == UVChannelIndex)
        {
            TopologySignature = OriginalUVTopology->BuildSignature;
        }
#endif
        else
        {
            TopologySignature = Asset->GetSourceMeshSignature();
        }
    }

    const bool bGeometryChanged =
        CachedWrinkleUVViewMesh != TargetMesh ||
        CachedWrinkleUVViewLODRenderDataIdentity != LODRenderDataIdentity ||
        CachedWrinkleUVViewChannelIndex != UVChannelIndex ||
        CachedWrinkleUVViewMaterialSlotIndex != MaterialSlotIndex ||
        CachedWrinkleUVViewTopologySignature != TopologySignature;

    FWrinkleUVIslandCacheEntry* CachedEntry = WrinkleUVIslandCache.FindByPredicate(
        [TargetMesh, LODRenderDataIdentity, UVChannelIndex, MaterialSlotIndex, &TopologySignature](
            const FWrinkleUVIslandCacheEntry& Entry)
        {
            return Entry.Mesh == TargetMesh &&
                   Entry.LODRenderDataIdentity == LODRenderDataIdentity &&
                   Entry.UVChannelIndex == UVChannelIndex &&
                   Entry.MaterialSlotIndex == MaterialSlotIndex &&
                   Entry.TopologySignature == TopologySignature;
        });

    if (CachedEntry != nullptr)
    {
        CachedEntry->LastUsedSerial = ++WrinkleUVIslandCacheUseSerial;
        WrinkleUVIslandItems = CachedEntry->Islands;
    }
    else
    {
        WrinkleUVIslandItems.Reset();
        if (TargetMesh != nullptr &&
            LODRenderDataIdentity != nullptr &&
            UVChannelIndex >= 0 &&
            UVChannelIndex < NumUVChannels)
        {
            TArray<FWetClothingAssetUVIsland> BuiltIslands;
            if (FWetClothingAssetMeshAnalyzer::BuildMaterialSlotUVIslands(
                    TargetMesh,
                    0,
                    UVChannelIndex,
                    MaterialSlotIndex,
                    BuiltIslands,
                    nullptr))
            {
                int32 NextUVIslandID = 0;
                for (FWetClothingAssetUVIsland& Island : BuiltIslands)
                {
                    Island.UVIslandID = NextUVIslandID++;
                    for (FWetClothingAssetUVTriangle& Triangle : Island.UVTriangles)
                    {
                        Triangle.UVIslandID = Island.UVIslandID;
                    }
                    WrinkleUVIslandItems.Add(MakeShared<FWetClothingAssetUVIsland>(MoveTemp(Island)));
                }
            }
        }

        FWrinkleUVIslandCacheEntry& NewEntry = WrinkleUVIslandCache.AddDefaulted_GetRef();
        NewEntry.Mesh = TargetMesh;
        NewEntry.LODRenderDataIdentity = LODRenderDataIdentity;
        NewEntry.UVChannelIndex = UVChannelIndex;
        NewEntry.MaterialSlotIndex = MaterialSlotIndex;
        NewEntry.TopologySignature = TopologySignature;
        NewEntry.Islands = WrinkleUVIslandItems;
        NewEntry.LastUsedSerial = ++WrinkleUVIslandCacheUseSerial;

        while (WrinkleUVIslandCache.Num() > WetWrinkleUVIslandCacheLimit)
        {
            int32 OldestIndex = INDEX_NONE;
            uint64 OldestSerial = MAX_uint64;
            for (int32 EntryIndex = 0; EntryIndex < WrinkleUVIslandCache.Num(); ++EntryIndex)
            {
                if (WrinkleUVIslandCache[EntryIndex].LastUsedSerial < OldestSerial)
                {
                    OldestSerial = WrinkleUVIslandCache[EntryIndex].LastUsedSerial;
                    OldestIndex = EntryIndex;
                }
            }
            if (OldestIndex == INDEX_NONE)
            {
                break;
            }
            WrinkleUVIslandCache.RemoveAtSwap(OldestIndex, 1, EAllowShrinking::No);
        }
    }

    CachedWrinkleUVViewMesh = TargetMesh;
    CachedWrinkleUVViewLODRenderDataIdentity = LODRenderDataIdentity;
    CachedWrinkleUVViewTopologySignature = TopologySignature;
    CachedWrinkleUVViewChannelIndex = UVChannelIndex;
    CachedWrinkleUVViewMaterialSlotIndex = MaterialSlotIndex;

    if (bGeometryChanged)
    {
        TMap<int32, FLinearColor> WrinkleUVIslandColors;
        for (const TSharedPtr<FWetClothingAssetUVIsland>& IslandItem : WrinkleUVIslandItems)
        {
            if (IslandItem.IsValid())
            {
                WrinkleUVIslandColors.Add(IslandItem->UVIslandID, FLinearColor::White);
            }
        }

        WrinkleUVView->SetIslands(WrinkleUVIslandItems);
        WrinkleUVView->SetIslandColors(WrinkleUVIslandColors);
        WrinkleUVView->SetHiddenUVIslandIDs(TSet<int32>());
        WrinkleUVView->SetSelectedIslands(TSet<int32>());
    }

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

    if (Asset == nullptr || MaterialSlotIndex == INDEX_NONE || UVChannelIndex == INDEX_NONE || IsUsingCustomWrinkleMap(MaterialSlotIndex))
    {
        return;
    }

    const FLinearColor PatchFillColor(0.35f, 0.82f, 1.0f, 0.38f);
    const FLinearColor PatchOutlineColor(0.35f, 0.82f, 1.0f, 0.95f);
    for (const FWetWrinklePatchPlacement& Patch : Asset->Authored.WrinkleData.EditablePatches)
    {
        if (!Patch.bEnabled)
        {
            continue;
        }

        if (Patch.MaterialSlotIndex != MaterialSlotIndex)
        {
            continue;
        }

        FWCAUVViewCircleMarker Marker;
        Marker.CenterUV = Patch.PositionUV;
        Marker.RadiusUV = FMath::Max(Patch.BrushRadiusUV * static_cast<float>(FMath::Max(Patch.Scale.X, Patch.Scale.Y)), 0.001f);
        const bool bSelectedPatch = SelectedElementType == EWetWrinkleElementType::Patch && Patch.PatchGuid == SelectedStrokeGuid;
        Marker.FillColor = bSelectedPatch ? FLinearColor(1.0f, 0.45f, 0.05f, 0.45f) : PatchFillColor;
        Marker.OutlineColor = bSelectedPatch ? FLinearColor(1.0f, 0.55f, 0.08f, 1.0f) : PatchOutlineColor;
        Marker.OutlineThickness = bSelectedPatch ? 1.5f : 1.0f;
        CachedWrinkleUVViewPatchMarkers.Add(Marker);
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
    if (!IsUsingCustomWrinkleMap(MaterialSlotIndex) &&
        BrushSettings.bShowPreview &&
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
        if (!TexturePath.IsValid())
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
        if (TextureAsset.AssetClassPath == UTexture2D::StaticClass()->GetClassPathName())
        {
            AddPreset(FText::FromName(TextureAsset.AssetName), TextureAsset.ToSoftObjectPath());
        }
    }

}

void SWetWrinkleEditorPanel::RefreshWrinkleTexturePalette(bool bForceAssetScan)
{
    FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    TArray<FString> SearchPaths;
    GetDefault<UWetWrinkleEditorSettings>()->GetNormalTextureSearchPaths(SearchPaths);
    if (bForceAssetScan && !SearchPaths.IsEmpty())
    {
        AssetRegistryModule.Get().ScanPathsSynchronous(SearchPaths, true);
    }

    TMap<FSoftObjectPath, FAssetData> UniqueTextureAssets;
    for (const FString& SearchPath : SearchPaths)
    {
        TArray<FAssetData> PathAssets;
        AssetRegistryModule.Get().GetAssetsByPath(FName(*SearchPath), PathAssets, true);
        for (const FAssetData& AssetData : PathAssets)
        {
            if (AssetData.AssetClassPath == UTexture2D::StaticClass()->GetClassPathName())
            {
                UniqueTextureAssets.FindOrAdd(AssetData.ToSoftObjectPath()) = AssetData;
            }
        }
    }

    TArray<FAssetData> TextureAssets;
    UniqueTextureAssets.GenerateValueArray(TextureAssets);
    TextureAssets.Sort([](const FAssetData& A, const FAssetData& B)
    {
        const int32 NameCompare = A.AssetName.ToString().Compare(B.AssetName.ToString());
        return NameCompare == 0 ? A.PackageName.ToString() < B.PackageName.ToString() : NameCompare < 0;
    });

    TMap<FSoftObjectPath, FWrinkleTexturePaletteItemPtr> PreviousItems =
        MoveTemp(WrinklePalettePanel->GetItemsByPath());
    WrinklePalettePanel->GetAllItems().Reset(TextureAssets.Num());
    WrinklePalettePanel->GetItemsByPath().Reset();

    const UWetWrinkleEditorSettings* UserSettings = GetDefault<UWetWrinkleEditorSettings>();
    for (const FAssetData& TextureAsset : TextureAssets)
    {
        const FSoftObjectPath TexturePath = TextureAsset.ToSoftObjectPath();
        FWrinkleTexturePaletteItemPtr Item = PreviousItems.FindRef(TexturePath);
        if (!Item.IsValid())
        {
            Item = MakeShared<FWetWrinkleTexturePaletteItem>();
        }
        Item->DisplayName = FText::FromName(TextureAsset.AssetName);
        Item->TexturePath = TexturePath;
        Item->bHidden = UserSettings->IsNormalTextureHidden(Item->TexturePath);
        Item->bAssetAvailable = true;
        Item->bRemoved = false;
        WrinklePalettePanel->GetAllItems().Add(Item);
        WrinklePalettePanel->GetItemsByPath().Add(TexturePath, Item);
    }

    RefreshWrinkleTexturePaletteView();
}

void SWetWrinkleEditorPanel::RefreshWrinkleTexturePaletteView()
{
    WrinklePalettePanel->GetVisibleItems().Reset();
    const bool bShowHidden = GetDefault<UWetWrinkleEditorSettings>()->bShowHiddenNormalTextures;
    for (const FWrinkleTexturePaletteItemPtr& Item : WrinklePalettePanel->GetAllItems())
    {
        if (Item.IsValid() && !Item->bRemoved && (!Item->bHidden || bShowHidden))
        {
            WrinklePalettePanel->GetVisibleItems().Add(Item);
        }
    }

    WrinklePalettePanel->RequestRefresh();
}

void SWetWrinkleEditorPanel::SortWrinkleTexturePaletteItems()
{
    WrinklePalettePanel->GetAllItems().Sort(
        [](const FWrinkleTexturePaletteItemPtr& A, const FWrinkleTexturePaletteItemPtr& B)
        {
            if (!A.IsValid() || !B.IsValid())
            {
                return A.IsValid();
            }
            const int32 NameCompare = A->DisplayName.ToString().Compare(B->DisplayName.ToString());
            return NameCompare == 0
                ? A->TexturePath.ToString() < B->TexturePath.ToString()
                : NameCompare < 0;
        });
}

SWetWrinkleEditorPanel::FWrinkleTexturePaletteItemPtr SWetWrinkleEditorPanel::UpsertWrinkleTexturePaletteItem(
    const FAssetData& AssetData)
{
    if (!IsAssetInsideWrinkleTextureSearchPaths(AssetData))
    {
        return nullptr;
    }

    const FSoftObjectPath TexturePath = AssetData.ToSoftObjectPath();
    FWrinkleTexturePaletteItemPtr Item = WrinklePalettePanel->GetItemsByPath().FindRef(TexturePath);
    if (!Item.IsValid())
    {
        Item = MakeShared<FWetWrinkleTexturePaletteItem>();
        WrinklePalettePanel->GetAllItems().Add(Item);
        WrinklePalettePanel->GetItemsByPath().Add(TexturePath, Item);
    }

    Item->DisplayName = FText::FromName(AssetData.AssetName);
    Item->TexturePath = TexturePath;
    Item->bHidden = GetDefault<UWetWrinkleEditorSettings>()->IsNormalTextureHidden(TexturePath);
    Item->bAssetAvailable = true;
    Item->bRemoved = false;
    SortWrinkleTexturePaletteItems();
    return Item;
}

bool SWetWrinkleEditorPanel::RemoveWrinkleTexturePaletteItem(const FSoftObjectPath& TexturePath)
{
    FWrinkleTexturePaletteItemPtr Item;
    if (!WrinklePalettePanel->GetItemsByPath().RemoveAndCopyValue(TexturePath, Item))
    {
        return false;
    }

    WrinklePalettePanel->GetAllItems().RemoveSingle(Item);
    if (Item.IsValid())
    {
        Item->Texture.Reset();
        Item->AssetThumbnail.Reset();
        Item->bAssetAvailable = false;
        Item->bRemoved = true;
    }
    return true;
}

void SWetWrinkleEditorPanel::RefreshWrinkleTexturePaletteItemState(const FWrinkleTexturePaletteItemPtr& Item)
{
    if (!Item.IsValid())
    {
        return;
    }

    if (Item->bRemoved)
    {
        Item->Texture.Reset();
        Item->AssetThumbnail.Reset();
        Item->bAssetAvailable = false;
        return;
    }

    Item->bHidden = GetDefault<UWetWrinkleEditorSettings>()->IsNormalTextureHidden(Item->TexturePath);
    const FAssetData AssetData = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"))
                                     .Get()
                                     .GetAssetByObjectPath(Item->TexturePath);
    Item->bAssetAvailable = AssetData.IsValid() && AssetData.AssetClassPath == UTexture2D::StaticClass()->GetClassPathName();
    if (!Item->bAssetAvailable)
    {
        Item->Texture.Reset();
        Item->AssetThumbnail.Reset();
    }
}

bool SWetWrinkleEditorPanel::IsAssetInsideWrinkleTextureSearchPaths(const FAssetData& AssetData) const
{
    if (AssetData.AssetClassPath != UTexture2D::StaticClass()->GetClassPathName())
    {
        return false;
    }

    TArray<FString> SearchPaths;
    GetDefault<UWetWrinkleEditorSettings>()->GetNormalTextureSearchPaths(SearchPaths);
    const FString PackagePath = AssetData.PackagePath.ToString();
    return SearchPaths.ContainsByPredicate(
        [&PackagePath](const FString& SearchPath)
        {
            return PackagePath == SearchPath || PackagePath.StartsWith(SearchPath + TEXT("/"));
        });
}

void SWetWrinkleEditorPanel::HandleWrinkleTextureAssetAdded(const FAssetData& AssetData)
{
    if (UpsertWrinkleTexturePaletteItem(AssetData).IsValid())
    {
        RefreshBrushPresetOptions();
        RefreshWrinkleTexturePaletteView();
    }
}

void SWetWrinkleEditorPanel::HandleWrinkleTextureAssetRemoved(const FAssetData& AssetData)
{
    const FSoftObjectPath RemovedPath = AssetData.ToSoftObjectPath();
    GetMutableDefault<UWetWrinkleEditorSettings>()->SetNormalTextureHidden(RemovedPath, false);
    if (BrushSettings.WrinkleNormalTexture != nullptr &&
        FSoftObjectPath(BrushSettings.WrinkleNormalTexture.Get()) == RemovedPath)
    {
        BrushSettings.WrinkleNormalTexture = nullptr;
        PushBrushPreviewSettingsToViewport();
    }
    RemoveWrinkleTexturePaletteItem(RemovedPath);
    RefreshBrushPresetOptions();
    RefreshWrinkleTexturePaletteView();
    RefreshWrinkleNormalThumbnail();
}

void SWetWrinkleEditorPanel::HandleWrinkleTextureAssetUpdated(const FAssetData& AssetData)
{
    if (FWrinkleTexturePaletteItemPtr Item = UpsertWrinkleTexturePaletteItem(AssetData))
    {
        Item->AssetThumbnail.Reset();
        RefreshBrushPresetOptions();
        RefreshWrinkleTexturePaletteView();
        RefreshWrinkleNormalThumbnail();
    }
}

TSharedRef<SWidget> SWetWrinkleEditorPanel::BuildWrinkleTexturePalette()
{
    check(WrinklePalettePanel.IsValid());
    return WrinklePalettePanel.ToSharedRef();
}

TSharedRef<ITableRow> SWetWrinkleEditorPanel::GenerateWrinkleTexturePaletteTileRow(
    FWrinkleTexturePaletteItemPtr Item,
    const TSharedRef<STableViewBase>& OwnerTable)
{
    return SNew(STableRow<FWrinkleTexturePaletteItemPtr>, OwnerTable)
        .Padding(2.0f)
            [GenerateWrinkleTexturePaletteTile(Item)];
}

TSharedRef<SWidget> SWetWrinkleEditorPanel::GenerateWrinkleTexturePaletteTile(FWrinkleTexturePaletteItemPtr Item)
{
    if (Item.IsValid() && Item->bAssetAvailable && !Item->AssetThumbnail.IsValid())
    {
        const FAssetData AssetData = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"))
                                         .Get()
                                         .GetAssetByObjectPath(Item->TexturePath);
        if (AssetData.IsValid())
        {
            Item->AssetThumbnail = MakeShared<FAssetThumbnail>(AssetData, 144, 144, MaterialThumbnailPool);
        }
    }

    FAssetThumbnailConfig ThumbnailConfig;
    ThumbnailConfig.bAllowHintText = false;
    ThumbnailConfig.AllowAssetSpecificThumbnailOverlay = false;
    ThumbnailConfig.AllowAssetStatusThumbnailOverlay = false;
    ThumbnailConfig.ShowAssetColor = false;
    ThumbnailConfig.ShowAssetBorder = false;
    ThumbnailConfig.BorderPadding = FMargin(0.0f);

    return SNew(SWetWrinkleTexturePaletteTile)
        .OnContextMenu(FOnWetWrinkleTextureTileContextMenu::CreateSP(
            this,
            &SWetWrinkleEditorPanel::HandleWrinkleTexturePaletteContextMenu,
            Item))
            [SNew(SButton)
             .ButtonStyle(&WrinklePalettePanel->GetButtonStyle())
             .ContentPadding(6.0f)
             .ButtonColorAndOpacity(this, &SWetWrinkleEditorPanel::GetWrinkleTexturePaletteTileColor, Item)
             .ToolTipText(this, &SWetWrinkleEditorPanel::GetWrinkleTexturePaletteTooltipText, Item)
             .OnClicked(this, &SWetWrinkleEditorPanel::HandleWrinkleTexturePaletteClicked, Item)
                 [SNew(SBox)
                  .WidthOverride(144.0f)
                  .HeightOverride(144.0f)
                      [SNew(SScaleBox)
                       .Stretch(EStretch::ScaleToFit)
                       .StretchDirection(EStretchDirection::Both)
                            [Item.IsValid() && Item->AssetThumbnail.IsValid()
                                 ? Item->AssetThumbnail->MakeThumbnailWidget(ThumbnailConfig)
                                 : SNullWidget::NullWidget]]]];
}

FReply SWetWrinkleEditorPanel::HandleWrinkleTexturePaletteClicked(TSharedPtr<FWetWrinkleTexturePaletteItem> Item)
{
    if (!Item.IsValid())
    {
        return FReply::Handled();
    }

    UTexture2D* Texture = Item->Texture.Get();
    if (Texture == nullptr && Item->TexturePath.IsValid())
    {
        Texture = Cast<UTexture2D>(Item->TexturePath.TryLoad());
        Item->Texture = Texture;
    }

    BrushSettings.WrinkleNormalTexture = Texture;
    RefreshWrinkleNormalThumbnail();
    PushBrushPreviewSettingsToViewport();
    WrinklePalettePanel->RequestRefresh();
    return FReply::Handled();
}

FReply SWetWrinkleEditorPanel::HandleWrinkleTexturePaletteContextMenu(
    const FPointerEvent& MouseEvent,
    TSharedPtr<FWetWrinkleTexturePaletteItem> Item)
{
    if (!Item.IsValid())
    {
        return FReply::Handled();
    }

    FMenuBuilder MenuBuilder(true, nullptr);
    MenuBuilder.AddMenuEntry(
        Item->bHidden ? LOCTEXT("UnhideWrinkleTexture", "Unhide") : LOCTEXT("HideWrinkleTexture", "Hide"),
        LOCTEXT("HideWrinkleTextureTooltip", "Change whether this texture is shown in the Wrinkle Editor palette."),
        FSlateIcon(),
        FUIAction(FExecuteAction::CreateSP(
            this,
            &SWetWrinkleEditorPanel::HandleSetWrinkleTextureHidden,
            Item,
            !Item->bHidden)));
    MenuBuilder.AddMenuEntry(
        LOCTEXT("CorrectWrinkleTexture", "Correct Normal"),
        LOCTEXT("CorrectWrinkleTextureTooltip", "Open the normal correction preview and create a corrected Texture2D."),
        FSlateIcon(),
        FUIAction(FExecuteAction::CreateSP(this, &SWetWrinkleEditorPanel::HandleCorrectWrinkleTexture, Item)));
    MenuBuilder.AddMenuEntry(
        LOCTEXT("BrowseWrinkleTexture", "Browse to Asset"),
        LOCTEXT("BrowseWrinkleTextureTooltip", "Select this texture in the Content Browser."),
        FSlateIcon(),
        FUIAction(FExecuteAction::CreateLambda([Item]()
        {
            if (GEditor != nullptr && Item.IsValid())
            {
                UObject* Asset = Item->Texture.Get();
                if (Asset == nullptr && Item->TexturePath.IsValid())
                {
                    Asset = Item->TexturePath.TryLoad();
                }
                if (Asset != nullptr)
                {
                    TArray<UObject*> Objects{Asset};
                    GEditor->SyncBrowserToObjects(Objects);
                }
            }
        })));

    FSlateApplication::Get().PushMenu(
        AsShared(),
        FWidgetPath(),
        MenuBuilder.MakeWidget(),
        MouseEvent.GetScreenSpacePosition(),
        FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));
    return FReply::Handled();
}

FReply SWetWrinkleEditorPanel::HandleRefreshWrinkleTexturePaletteClicked()
{
    RefreshWrinkleTexturePalette(true);
    RefreshWrinkleNormalThumbnail();
    PushBrushPreviewSettingsToViewport();
    return FReply::Handled();
}

FText SWetWrinkleEditorPanel::GetWrinkleTexturePaletteTooltipText(TSharedPtr<FWetWrinkleTexturePaletteItem> Item) const
{
    if (!Item.IsValid())
    {
        return FText::GetEmpty();
    }

    return FText::Format(
        LOCTEXT("WrinkleTexturePaletteTooltip", "{0}\n{1}\nTexture Status : {2}"),
        Item->DisplayName,
        FText::FromString(Item->TexturePath.ToString()),
        Item->bHidden ? LOCTEXT("TextureHidden", "Hidden") : LOCTEXT("TextureVisible", "Visible"));
}

FSlateColor SWetWrinkleEditorPanel::GetWrinkleTexturePaletteTileColor(TSharedPtr<FWetWrinkleTexturePaletteItem> Item) const
{
    if (!Item.IsValid() || !Item->bAssetAvailable)
    {
        return FSlateColor(FLinearColor(0.15f, 0.08f, 0.08f, 1.0f));
    }
    const UTexture2D* SelectedTexture = BrushSettings.WrinkleNormalTexture.Get();
    if (SelectedTexture != nullptr && Item->TexturePath == FSoftObjectPath(SelectedTexture))
    {
        return FSlateColor(FLinearColor(0.18f, 0.42f, 0.80f, 1.0f));
    }
    if (Item->bHidden)
    {
        return FSlateColor(FLinearColor(0.16f, 0.12f, 0.06f, 1.0f));
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

// Internal auto-generation workflow. The implementation is intentionally not exposed in the public editor UI.
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
        FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("GenerateWrinkleTextureNoSlot", "Select a single material slot before generating wrinkle textures. All Wettable Slots is preview-only."));
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
            .Resolution(Asset->Authored.WrinkleData.BakeSettings.DefaultResolution)
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

FText SWetWrinkleEditorPanel::GetPatchListSummaryText() const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const int32 PatchCount = Asset != nullptr ? Asset->Authored.WrinkleData.EditablePatches.Num() : 0;
    const int32 RidgeStrokeCount = Asset != nullptr ? Asset->Authored.WrinkleData.EditableProceduralRidgeStrokes.Num() : 0;

    return FText::Format(
        LOCTEXT("PatchListSummary", "{0} patch(es), {1} ridge stroke(s)."),
        FText::AsNumber(PatchCount),
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

    if (AuthoringController.IsValid())
    {
        AuthoringController->CancelActiveInteraction();
    }
    CommitRidgePropertyEdit();
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->ClearTransientProceduralStroke();
        PreviewViewport->SetEditingProceduralStrokeGuid(FGuid());
    }
    BrushSettings.ToolMode = ToolMode;
    SelectedProceduralRidgePointIndex = INDEX_NONE;
    CurrentHit = FWetWrinkleSurfaceHit();
    PushBrushPreviewSettingsToViewport();
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

    if (AuthoringController.IsValid())
    {
        AuthoringController->CancelActiveInteraction();
    }
    CommitRidgePropertyEdit();
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->ClearTransientProceduralStroke();
        PreviewViewport->SetEditingProceduralStrokeGuid(FGuid());
    }
    BrushSettings.RidgeEditMode = EditMode;
    SelectedProceduralRidgePointIndex = INDEX_NONE;
    PushBrushPreviewSettingsToViewport();
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

    if (AuthoringController.IsValid())
    {
        AuthoringController->CancelActiveInteraction();
    }
    BrushSettings.bRidgeJunctionModeEnabled = bEnabled;
    PushBrushPreviewSettingsToViewport();
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
    PushBrushPreviewSettingsToViewport();
    CommitRidgePropertyEdit();
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
    PushBrushPreviewSettingsToViewport();
    CommitRidgePropertyEdit();
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

TSharedRef<ITableRow> SWetWrinkleEditorPanel::GenerateMaterialSlotRow(FMaterialSlotItemPtr Item, const TSharedRef<STableViewBase>& OwnerTable)
{
    FWCAMaterialSlotRowArgs Args;
    Args.WetClothingAsset = WetClothingAsset.Get();
    Args.GeneratedDataUV = WetClothingAsset.IsValid() ? WetClothingAsset->GetRuntimeSkeletalMesh() : nullptr;
    Args.ThumbnailPool = MaterialThumbnailPool;
    Args.ThumbnailSink = &MaterialSlotThumbnails;
    Args.AllSlotsTitle = LOCTEXT("AllWettableMaterialSlotsRow", "All Wettable Slots");
    Args.AllSlotsTooltip = FDWCEditorPreviewSlotResolver::GetAggregateTooltip(PreviewSlotStates);
    Args.GetMaterialSlotStatusText = [this](const int32 MaterialSlotIndex)
    {
        const FDWCEditorPreviewSlotState* State = FindPreviewSlotState(MaterialSlotIndex);
        return State != nullptr && State->bPreviewReady
            ? LOCTEXT("PreviewSlotReadyStatus", "Ready")
            : LOCTEXT("PreviewSlotUnavailableStatus", "Unavailable");
    };
    Args.IsMaterialSlotEnabled = [this](const int32 MaterialSlotIndex)
    {
        const FDWCEditorPreviewSlotState* State = FindPreviewSlotState(MaterialSlotIndex);
        return State != nullptr && State->bPreviewReady;
    };
    Args.GetMaterialSlotTooltipText = [this](const int32 MaterialSlotIndex)
    {
        const FDWCEditorPreviewSlotState* State = FindPreviewSlotState(MaterialSlotIndex);
        return State != nullptr
            ? FDWCEditorPreviewSlotResolver::GetIssueText(State->Issue)
            : FText::GetEmpty();
    };
    Args.BuildBeforeWettableWidget = [this](const int32 MaterialSlotIndex)
    {
        return BuildCustomWrinkleMapToggle(MaterialSlotIndex);
    };
    Args.BuildThumbnailWidget = [this](const int32 MaterialSlotIndex) -> TSharedRef<SWidget>
    {
        return BuildMaterialSlotPreviewWidget(MaterialSlotIndex);
    };

    return FWCAEditorWidgets::GenerateMaterialSlotRow(Item, OwnerTable, Args);
}

TSharedRef<SWidget> SWetWrinkleEditorPanel::BuildMaterialSlotPreviewWidget(const int32 MaterialSlotIndex) const
{
    TArray<FWetClothingAssetUVTriangle> PreviewTriangles;
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const USkeletalMesh* RuntimeMesh = Asset != nullptr ? Asset->GetRuntimeSkeletalMesh() : nullptr;
    const USkeletalMesh* SourceMesh = Asset != nullptr ? Asset->GetSourceSkeletalMesh() : nullptr;
    const int32 PreferredLODIndex = Asset != nullptr ? Asset->GetSimulationLODIndex() : 0;
    const int32 PreferredUVChannelIndex = Asset != nullptr ? Asset->GetOriginalUVChannelIndex() : 0;

    FWCAUVIslandViewCache::BuildMaterialSlotPreviewTriangles(Asset, MaterialSlotIndex, PreviewTriangles);

    const auto TryUVPreview = [&PreviewTriangles, MaterialSlotIndex](
                                  const USkeletalMesh* Mesh,
                                  const int32 LODIndex,
                                  const int32 UVChannelIndex)
    {
        if (PreviewTriangles.IsEmpty() && Mesh != nullptr)
        {
            FWCAUVIslandViewCache::BuildMaterialSlotPreviewTriangles(
                Mesh, LODIndex, UVChannelIndex, MaterialSlotIndex, PreviewTriangles);
        }
    };

    TryUVPreview(SourceMesh, PreferredLODIndex, PreferredUVChannelIndex);
    if (PreferredLODIndex != 0)
    {
        TryUVPreview(RuntimeMesh, 0, PreferredUVChannelIndex);
        TryUVPreview(SourceMesh, 0, PreferredUVChannelIndex);
    }

    const auto TryGeometryPreview = [&PreviewTriangles, MaterialSlotIndex](
                                        const USkeletalMesh* Mesh,
                                        const int32 LODIndex,
                                        const int32 UVChannelIndex)
    {
        if (PreviewTriangles.IsEmpty() && Mesh != nullptr)
        {
            FWCAUVIslandViewCache::BuildMaterialSlotGeometryPreviewTriangles(
                Mesh, LODIndex, UVChannelIndex, MaterialSlotIndex, PreviewTriangles);
        }
    };

    TryGeometryPreview(RuntimeMesh, PreferredLODIndex, PreferredUVChannelIndex);
    TryGeometryPreview(SourceMesh, PreferredLODIndex, PreferredUVChannelIndex);
    if (PreferredLODIndex != 0)
    {
        TryGeometryPreview(RuntimeMesh, 0, PreferredUVChannelIndex);
        TryGeometryPreview(SourceMesh, 0, PreferredUVChannelIndex);
    }

    TArray<TSharedPtr<FWCATextureItem>> LocalTextureItems;
    TSharedPtr<FWCATextureItem> LocalSelectedTextureItem;
    FWetClothingMaterialTextureResolver::BuildTextureItemsForMaterialSlot(
        WetClothingAsset.Get(), MaterialSlotIndex, LocalTextureItems, LocalSelectedTextureItem);

    UTexture* PreviewTexture = LocalSelectedTextureItem.IsValid() ? LocalSelectedTextureItem->Texture.Get() : nullptr;
    if (Cast<UTexture2D>(PreviewTexture) == nullptr)
    {
        const auto ResolveFallbackTexture = [MaterialSlotIndex](const USkeletalMesh* Mesh) -> UTexture*
        {
            if (Mesh != nullptr)
            {
                const TArray<FSkeletalMaterial>& Materials = Mesh->GetMaterials();
                if (Materials.IsValidIndex(MaterialSlotIndex))
                {
                    return FWetClothingMaterialTextureResolver::ResolveBestMaterialTexture(
                        Materials[MaterialSlotIndex].MaterialInterface);
                }
            }
            return nullptr;
        };

        PreviewTexture = ResolveFallbackTexture(RuntimeMesh);
        if (Cast<UTexture2D>(PreviewTexture) == nullptr)
        {
            PreviewTexture = ResolveFallbackTexture(SourceMesh);
        }
    }

    return SNew(SWCAMaterialSlotPreview)
        .Triangles(PreviewTriangles)
        .PreviewTexture(PreviewTexture)
        .DrawWireframe(true);
}

void SWetWrinkleEditorPanel::HandleMaterialSlotSelectionChanged(FMaterialSlotItemPtr Item, ESelectInfo::Type SelectInfo)
{
    ApplyMaterialSlotSelection(Item.IsValid() ? Item->SlotIndex : INDEX_NONE, false);
}

void SWetWrinkleEditorPanel::ApplyMaterialSlotSelection(
    int32 MaterialSlotIndex,
    const bool bShowFailureDialog)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(SWetWrinkleEditorPanel_ApplyMaterialSlotSelection);

    if (bSynchronizingMaterialSlotSelection)
    {
        return;
    }

    MaterialSlotIndex = MaterialSlotIndex >= 0 ? MaterialSlotIndex : INDEX_NONE;
    if (BrushSettings.MaterialSlotIndex == MaterialSlotIndex)
    {
        return;
    }

    TGuardValue<bool> SynchronizationGuard(bSynchronizingMaterialSlotSelection, true);
    if (AuthoringController.IsValid())
    {
        AuthoringController->CancelActiveInteraction(false);
    }
    BrushSettings.MaterialSlotIndex = MaterialSlotIndex;
    CurrentHit = FWetWrinkleSurfaceHit();
    EnsureWrinkleUVChannelForMaterialSlot(MaterialSlotIndex, bShowFailureDialog);
    RefreshDWCDataUVChannel();

    if (MaterialSlotListView.IsValid())
    {
        MaterialSlotListView->SetSelection(FindMaterialSlotItem(MaterialSlotIndex), ESelectInfo::Direct);
    }

    RefreshStrokeList();
    RefreshRuntimeNormalUI(false, false);
    PushStrokeSelectionToViewport();
    PushBrushTopologyToViewport();
    RefreshWrinkleUVView();
    DispatchWrinkleBrushState(EDWCEditorSessionEffect::None);
}

TSharedRef<SWidget> SWetWrinkleEditorPanel::BuildCustomWrinkleMapToggle(const int32 MaterialSlotIndex)
{
    return SNew(SCheckBox)
        .ToolTipText(LOCTEXT("CustomWrinkleMapToggleTooltip", "Use a user-provided packed wrinkle normal map for this material slot instead of the baked wrinkle map."))
        .IsChecked(this, &SWetWrinkleEditorPanel::GetCustomWrinkleMapCheckState, MaterialSlotIndex)
        .OnCheckStateChanged(this, &SWetWrinkleEditorPanel::HandleCustomWrinkleMapCheckStateChanged, MaterialSlotIndex);
}

ECheckBoxState SWetWrinkleEditorPanel::GetCustomWrinkleMapCheckState(const int32 MaterialSlotIndex) const
{
    return IsUsingCustomWrinkleMap(MaterialSlotIndex)
               ? ECheckBoxState::Checked
               : ECheckBoxState::Unchecked;
}

void SWetWrinkleEditorPanel::HandleCustomWrinkleMapCheckStateChanged(
    const ECheckBoxState NewState,
    const int32 MaterialSlotIndex)
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || MaterialSlotIndex == INDEX_NONE)
    {
        return;
    }

    const EDWCWrinkleNormalSource NewSource = NewState == ECheckBoxState::Checked
                                                  ? EDWCWrinkleNormalSource::CustomTexture
                                                  : EDWCWrinkleNormalSource::Baked;
    if (!EditWrinkleData(
            LOCTEXT("SetRuntimeWrinkleNormalSource", "Set Runtime Wrinkle Normal Source"),
            EDWCEditorAuthoringImpact::AssetDirty |
                EDWCEditorAuthoringImpact::Preview |
                EDWCEditorAuthoringImpact::RuntimeBinding |
                EDWCEditorAuthoringImpact::Details,
            MaterialSlotIndex,
            FGuid(),
            [MaterialSlotIndex, NewSource](FWetClothingWrinkleData& WrinkleData)
            {
                FWetWrinkleRuntimeNormalSource* Source = WrinkleData.FindRuntimeNormalSource(MaterialSlotIndex);
                if (Source == nullptr)
                {
                    Source = &WrinkleData.RuntimeNormalSources.AddDefaulted_GetRef();
                    Source->MaterialSlotIndex = MaterialSlotIndex;
                }
                if (Source->Source == NewSource)
                {
                    return false;
                }
                Source->Source = NewSource;
                return true;
            }))
    {
        return;
    }

    BrushSettings.MaterialSlotIndex = MaterialSlotIndex;
    EnsureWrinkleUVChannelForMaterialSlot(MaterialSlotIndex, false);
    if (MaterialSlotListView.IsValid())
    {
        MaterialSlotListView->SetSelection(FindMaterialSlotItem(MaterialSlotIndex), ESelectInfo::Direct);
        MaterialSlotListView->RequestListRefresh();
    }
    RefreshStrokeList();
    RefreshRuntimeNormalUI();
    RefreshWrinkleUVView();
}

bool SWetWrinkleEditorPanel::IsUsingCustomWrinkleMap(int32 MaterialSlotIndex) const
{
    if (MaterialSlotIndex == INDEX_NONE)
    {
        MaterialSlotIndex = BrushSettings.MaterialSlotIndex;
    }
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    return Asset != nullptr &&
           MaterialSlotIndex != INDEX_NONE &&
           Asset->Authored.WrinkleData.IsUsingCustomWrinkleNormalMap(MaterialSlotIndex);
}

void SWetWrinkleEditorPanel::HandleCustomNormalSettingsChanged()
{
    RefreshRuntimeNormalUI();
}

void SWetWrinkleEditorPanel::RefreshRuntimeNormalUI(
    const bool bRebuildAccumulatedPreview,
    const bool bPushViewportSettings)
{
    if (RightPanelSwitcher.IsValid())
    {
        RightPanelSwitcher->SetActiveWidgetIndex(IsUsingCustomWrinkleMap() ? 1 : 0);
    }
    if (CustomNormalPanel.IsValid())
    {
        CustomNormalPanel->Refresh();
    }

    if (PreviewViewport.IsValid())
    {
        if (IsUsingCustomWrinkleMap())
        {
            const UWetClothingAsset* Asset = WetClothingAsset.Get();
            const int32 UVChannelIndex = Asset != nullptr ? ResolveWetWrinkleUVChannel(Asset) : INDEX_NONE;
            const FWetWrinkleResolvedNormalMap Resolved = Asset != nullptr
                                                              ? Asset->Authored.WrinkleData.ResolveRuntimeWrinkleNormalMap(
                                                                    BrushSettings.MaterialSlotIndex)
                                                              : FWetWrinkleResolvedNormalMap();
            PreviewViewport->SetGeneratedNormalPreviewTexture(
                BrushSettings.MaterialSlotIndex,
                UVChannelIndex,
                Resolved.Texture,
                false);
            PreviewViewport->ClearTransientProceduralStroke();
            PreviewViewport->SetEditingProceduralStrokeGuid(FGuid());
        }
        else
        {
            PreviewViewport->ClearGeneratedNormalPreviewTexture(false);
            if (bRebuildAccumulatedPreview)
            {
                PreviewViewport->InvalidateAccumulatedPreviewTextures();
            }
        }

        if (bPushViewportSettings)
        {
            PushStrokeSelectionToViewport();
            PushBrushTopologyToViewport();
        }
    }

    RebuildWrinkleUVViewPatchMarkerCache();
    RefreshWrinkleUVViewMarkersOnly();
}

FText SWetWrinkleEditorPanel::GetRuntimeNormalSourceText() const
{
    if (BrushSettings.MaterialSlotIndex == INDEX_NONE)
    {
        return LOCTEXT("RuntimeNormalSourceAllSlots", "Select a material slot");
    }
    return IsUsingCustomWrinkleMap()
               ? LOCTEXT("RuntimeNormalSourceCustom", "Custom texture")
               : LOCTEXT("RuntimeNormalSourceBaked", "Baked map");
}

FText SWetWrinkleEditorPanel::GetRuntimeNormalTextureText() const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || BrushSettings.MaterialSlotIndex == INDEX_NONE)
    {
        return LOCTEXT("RuntimeNormalTextureNone", "None");
    }
    const FWetWrinkleResolvedNormalMap Resolved =
        Asset->Authored.WrinkleData.ResolveRuntimeWrinkleNormalMap(BrushSettings.MaterialSlotIndex);
    return Resolved.Texture != nullptr
               ? FText::FromString(Resolved.Texture->GetName())
               : LOCTEXT("RuntimeNormalTextureMissing", "None");
}

FText SWetWrinkleEditorPanel::GetRuntimeNormalUVText() const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    return Asset != nullptr && BrushSettings.MaterialSlotIndex != INDEX_NONE
               ? FText::Format(LOCTEXT("RuntimeNormalUVValue", "UV {0}"), FText::AsNumber(ResolveWetWrinkleUVChannel(Asset)))
               : LOCTEXT("RuntimeNormalUVNone", "-");
}

FText SWetWrinkleEditorPanel::GetRuntimeNormalCoverageText() const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || BrushSettings.MaterialSlotIndex == INDEX_NONE)
    {
        return LOCTEXT("RuntimeNormalCoverageNone", "-");
    }
    const FWetWrinkleBakedMapSet* BakedMap =
        Asset->Authored.WrinkleData.FindBakedWrinkleMap(BrushSettings.MaterialSlotIndex);
    return BakedMap != nullptr && BakedMap->BakedWrinkleMask != nullptr
               ? LOCTEXT("RuntimeNormalCoverageSeparateMask", "Separate baked mask")
               : LOCTEXT("RuntimeNormalCoverageUnused", "None");
}

FText SWetWrinkleEditorPanel::GetRuntimeNormalStatusText() const
{
    if (BrushSettings.MaterialSlotIndex == INDEX_NONE)
    {
        return LOCTEXT("RuntimeNormalStatusSelectSlot", "Select a single material slot to inspect its runtime wrinkle normal source.");
    }
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr)
    {
        return LOCTEXT("RuntimeNormalStatusNoAsset", "Wet Clothing Asset is unavailable.");
    }
    const FWetWrinkleResolvedNormalMap Resolved =
        Asset->Authored.WrinkleData.ResolveRuntimeWrinkleNormalMap(BrushSettings.MaterialSlotIndex);
    if (Resolved.IsValid())
    {
        return IsUsingCustomWrinkleMap()
                   ? LOCTEXT("RuntimeNormalStatusCustomReady", "Runtime will use the selected custom wrinkle normal map.")
                   : LOCTEXT("RuntimeNormalStatusBakedReady", "Runtime will use the current baked wrinkle normal map.");
    }
    return IsUsingCustomWrinkleMap()
               ? LOCTEXT("RuntimeNormalStatusCustomMissing", "Custom Wrinkle Map is enabled, but no texture is assigned. Runtime wrinkle normal is disabled for this slot.")
               : LOCTEXT("RuntimeNormalStatusBakedMissing", "No baked wrinkle normal map is available for this slot.");
}

FSlateColor SWetWrinkleEditorPanel::GetRuntimeNormalStatusColor() const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || BrushSettings.MaterialSlotIndex == INDEX_NONE)
    {
        return FSlateColor::UseSubduedForeground();
    }
    const FWetWrinkleResolvedNormalMap Resolved =
        Asset->Authored.WrinkleData.ResolveRuntimeWrinkleNormalMap(BrushSettings.MaterialSlotIndex);
    if (Resolved.IsValid())
    {
        return FSlateColor(FLinearColor(0.25f, 0.9f, 0.35f));
    }
    return IsUsingCustomWrinkleMap()
               ? FSlateColor(FLinearColor(1.0f, 0.25f, 0.2f))
               : FSlateColor(FLinearColor(1.0f, 0.75f, 0.15f));
}

FString SWetWrinkleEditorPanel::GetWrinkleNormalTextureObjectPath() const
{
    return BrushSettings.WrinkleNormalTexture != nullptr ? BrushSettings.WrinkleNormalTexture->GetPathName() : FString();
}

FText SWetWrinkleEditorPanel::GetWrinkleNormalTextureDisplayName() const
{
    return BrushSettings.WrinkleNormalTexture != nullptr
        ? FText::FromString(BrushSettings.WrinkleNormalTexture->GetName())
        : LOCTEXT("NoWrinkleNormalTextureCompact", "None");
}

TSharedRef<SWidget> SWetWrinkleEditorPanel::BuildWrinkleNormalTextureMenu()
{
    RefreshBrushPresetOptions();
    WrinkleNormalMenuThumbnails.Reset();

    TSharedRef<SVerticalBox> Menu = SNew(SVerticalBox);
    Menu->AddSlot()
        .AutoHeight()
        .Padding(8.0f, 6.0f, 8.0f, 4.0f)
        [SNew(STextBlock)
            .Text(LOCTEXT("DWCWrinkleNormalsMenuHeading", "DWC Wrinkle Normal Textures"))
            .Font(FAppStyle::GetFontStyle(TEXT("SmallFontBold")))];

    if (BrushPresetOptions.IsEmpty())
    {
        Menu->AddSlot()
            .AutoHeight()
            .Padding(8.0f, 4.0f, 8.0f, 8.0f)
            [SNew(STextBlock)
                .Text(LOCTEXT("NoDWCWrinkleNormals", "No textures found in the DWC wrinkle texture directory."))
                .ColorAndOpacity(FSlateColor::UseSubduedForeground())];
    }
    else
    {
        TSharedRef<SUniformGridPanel> PresetGrid = SNew(SUniformGridPanel)
            .SlotPadding(FMargin(3.0f));
        FAssetThumbnailConfig ThumbnailConfig;
        ThumbnailConfig.bAllowHintText = false;
        ThumbnailConfig.AllowAssetSpecificThumbnailOverlay = false;
        ThumbnailConfig.AllowAssetStatusThumbnailOverlay = false;
        ThumbnailConfig.ShowAssetColor = false;
        ThumbnailConfig.ShowAssetBorder = false;
        ThumbnailConfig.BorderPadding = FMargin(0.0f);

        constexpr int32 ColumnCount = 4;
        int32 TileIndex = 0;
        FAssetRegistryModule& AssetRegistryModule =
            FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
        for (const TSharedPtr<FWetWrinkleBrushPresetOption>& Option : BrushPresetOptions)
        {
            if (!Option.IsValid())
            {
                continue;
            }
            const FSoftObjectPath TexturePath = Option->TexturePath;
            const FAssetData AssetData = AssetRegistryModule.Get().GetAssetByObjectPath(TexturePath);
            TSharedPtr<FAssetThumbnail> Thumbnail;
            if (AssetData.IsValid())
            {
                Thumbnail = MakeShared<FAssetThumbnail>(AssetData, 72, 72, MaterialThumbnailPool);
                WrinkleNormalMenuThumbnails.Add(Thumbnail);
            }

            PresetGrid->AddSlot(TileIndex % ColumnCount, TileIndex / ColumnCount)
                [SNew(SButton)
                    .ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton"))
                    .ContentPadding(FMargin(5.0f))
                    .ToolTipText(FText::FromString(TexturePath.ToString()))
                    .OnClicked(this, &SWetWrinkleEditorPanel::HandleWrinkleNormalPresetClicked, TexturePath)
                    [SNew(SBox)
                        .WidthOverride(88.0f)
                        .HeightOverride(104.0f)
                        [SNew(SVerticalBox)
                            + SVerticalBox::Slot()
                                .AutoHeight()
                                .HAlign(HAlign_Center)
                                [SNew(SBox)
                                    .WidthOverride(72.0f)
                                    .HeightOverride(72.0f)
                                    [Thumbnail.IsValid()
                                        ? Thumbnail->MakeThumbnailWidget(ThumbnailConfig)
                                        : SNullWidget::NullWidget]]
                            + SVerticalBox::Slot()
                                .AutoHeight()
                                .Padding(0.0f, 4.0f, 0.0f, 0.0f)
                                [SNew(STextBlock)
                                    .Text(Option->DisplayName)
                                    .Justification(ETextJustify::Center)
                                    .OverflowPolicy(ETextOverflowPolicy::Ellipsis)]]]];
            ++TileIndex;
        }

        Menu->AddSlot()
            .AutoHeight()
            [SNew(SBox)
                .MaxDesiredHeight(340.0f)
                [SNew(SScrollBox)
                    + SScrollBox::Slot()
                    [PresetGrid]]];
    }

    Menu->AddSlot()
        .AutoHeight()
        .Padding(6.0f, 6.0f, 6.0f, 4.0f)
        [SNew(SSeparator)];

    Menu->AddSlot()
        .AutoHeight()
        .Padding(8.0f, 2.0f, 8.0f, 8.0f)
        [SNew(SVerticalBox)
            + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(0.0f, 0.0f, 0.0f, 4.0f)
                [SNew(STextBlock)
                    .Text(LOCTEXT("OtherWrinkleNormalTextureHeading", "Other Texture"))
                    .Font(FAppStyle::GetFontStyle(TEXT("SmallFontBold")))]
            + SVerticalBox::Slot()
                .AutoHeight()
                [SNew(SObjectPropertyEntryBox)
                    .AllowedClass(UTexture2D::StaticClass())
                    .AllowClear(true)
                    .AllowCreate(false)
                    .DisplayThumbnail(false)
                    .DisplayUseSelected(true)
                    .DisplayBrowse(true)
                    .EnableContentPicker(true)
                    .ObjectPath(this, &SWetWrinkleEditorPanel::GetWrinkleNormalTextureObjectPath)
                    .OnObjectChanged(this, &SWetWrinkleEditorPanel::HandleWrinkleNormalTextureChanged)]];

    return SNew(SBox)
        .WidthOverride(420.0f)
        [Menu];
}

FReply SWetWrinkleEditorPanel::HandleWrinkleNormalPresetClicked(const FSoftObjectPath TexturePath)
{
    UTexture2D* Texture = Cast<UTexture2D>(TexturePath.TryLoad());
    if (Texture == nullptr)
    {
        return FReply::Handled();
    }

    BrushSettings.WrinkleNormalTexture = Texture;
    RefreshWrinkleNormalThumbnail();
    PushBrushPreviewSettingsToViewport();
    if (WrinklePalettePanel.IsValid())
    {
        WrinklePalettePanel->RequestRefresh();
    }
    FSlateApplication::Get().DismissAllMenus();
    return FReply::Handled();
}

void SWetWrinkleEditorPanel::HandleWrinkleNormalTextureChanged(const FAssetData& AssetData)
{
    BrushSettings.WrinkleNormalTexture = Cast<UTexture2D>(AssetData.GetAsset());
    RefreshWrinkleNormalThumbnail();
    PushBrushPreviewSettingsToViewport();
    WrinklePalettePanel->RequestRefresh();
}

void SWetWrinkleEditorPanel::RefreshWrinkleNormalThumbnail()
{
    UTexture2D* Texture = BrushSettings.WrinkleNormalTexture;
    SelectedWrinkleNormalThumbnailBrush.SetResourceObject(Texture);
    SelectedWrinkleNormalThumbnailBrush.SetImageSize(
        Texture != nullptr
            ? FVector2D(FMath::Max(Texture->GetSizeX(), 1), FMath::Max(Texture->GetSizeY(), 1))
            : FVector2D(128.0f, 128.0f));
}

const FSlateBrush* SWetWrinkleEditorPanel::GetWrinkleNormalThumbnailBrush() const
{
    return &SelectedWrinkleNormalThumbnailBrush;
}

EVisibility SWetWrinkleEditorPanel::GetWrinkleNormalThumbnailVisibility() const
{
    return IsValid(SelectedWrinkleNormalThumbnailBrush.GetResourceObject()) ? EVisibility::Visible : EVisibility::Hidden;
}

FText SWetWrinkleEditorPanel::GetWrinkleNormalStatusText() const
{
    return BrushSettings.WrinkleNormalTexture != nullptr
               ? FText::Format(
                     LOCTEXT("WrinkleNormalSelected", "Texture: {0}"),
                     FText::FromString(BrushSettings.WrinkleNormalTexture->GetName()))
               : LOCTEXT("WrinkleNormalNoSelection", "No wrinkle normal texture selected.");
}

FSlateColor SWetWrinkleEditorPanel::GetWrinkleNormalStatusColor() const
{
    return BrushSettings.WrinkleNormalTexture != nullptr
               ? FSlateColor(FLinearColor(0.35f, 0.9f, 0.45f))
               : FSlateColor(FLinearColor(1.0f, 0.55f, 0.25f));
}

FReply SWetWrinkleEditorPanel::HandleOpenWrinkleNormalTextureClicked()
{
    if (UAssetEditorSubsystem* AssetEditorSubsystem = GEditor != nullptr ? GEditor->GetEditorSubsystem<UAssetEditorSubsystem>() : nullptr)
    {
        if (BrushSettings.WrinkleNormalTexture != nullptr)
        {
            AssetEditorSubsystem->OpenEditorForAsset(BrushSettings.WrinkleNormalTexture.Get());
        }
    }
    return FReply::Handled();
}

bool SWetWrinkleEditorPanel::CanOpenWrinkleNormalTexture() const
{
    return BrushSettings.WrinkleNormalTexture != nullptr;
}

FReply SWetWrinkleEditorPanel::HandleAddWrinkleTextureSearchPathClicked()
{
    const FString Path = WrinkleTextureSearchPathTextBox.IsValid()
                             ? WrinkleTextureSearchPathTextBox->GetText().ToString()
                             : FString();
    if (!GetMutableDefault<UWetWrinkleEditorSettings>()->AddNormalTextureSearchPath(Path))
    {
        FMessageDialog::Open(
            EAppMsgType::Ok,
            LOCTEXT("InvalidWrinkleTexturePath", "Enter a unique Unreal Content path beginning with '/', for example /DynamicWetClothes/Textures/Wrinkles."));
        return FReply::Handled();
    }

    WrinkleTextureSearchPathTextBox->SetText(FText::GetEmpty());
    RefreshWrinkleTexturePalette(true);
    return FReply::Handled();
}

TSharedRef<SWidget> SWetWrinkleEditorPanel::BuildWrinkleTextureSearchPathMenu()
{
    FMenuBuilder MenuBuilder(true, nullptr);
    TArray<FString> SearchPaths;
    GetDefault<UWetWrinkleEditorSettings>()->GetNormalTextureSearchPaths(SearchPaths);
    for (const FString& Path : SearchPaths)
    {
        const bool bDefaultPath = Path == UWetWrinkleEditorSettings::DefaultNormalTexturePath;
        MenuBuilder.AddWidget(
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
                  .FillWidth(1.0f)
                  .VAlign(VAlign_Center)
                      [SNew(STextBlock).Text(FText::FromString(Path))]
            + SHorizontalBox::Slot()
                  .AutoWidth()
                      [SNew(SButton)
                       .Visibility(bDefaultPath ? EVisibility::Collapsed : EVisibility::Visible)
                       .ButtonStyle(FAppStyle::Get(), "SimpleButton")
                       .ToolTipText(LOCTEXT("RemoveWrinkleTexturePath", "Remove this search path."))
                       .OnClicked_Lambda([this, Path]()
                       {
                           HandleRemoveWrinkleTextureSearchPath(Path);
                           return FReply::Handled();
                       })
                           [SNew(SImage).Image(FAppStyle::GetBrush("Icons.Delete"))]],
            FText::GetEmpty(),
            true);
    }
    return MenuBuilder.MakeWidget();
}

void SWetWrinkleEditorPanel::HandleRemoveWrinkleTextureSearchPath(FString Path)
{
    GetMutableDefault<UWetWrinkleEditorSettings>()->RemoveNormalTextureSearchPath(Path);
    RefreshWrinkleTexturePalette();
}

ECheckBoxState SWetWrinkleEditorPanel::GetShowHiddenWrinkleTexturesState() const
{
    return GetDefault<UWetWrinkleEditorSettings>()->bShowHiddenNormalTextures
               ? ECheckBoxState::Checked
               : ECheckBoxState::Unchecked;
}

void SWetWrinkleEditorPanel::HandleShowHiddenWrinkleTexturesChanged(const ECheckBoxState NewState)
{
    UWetWrinkleEditorSettings* UserSettings = GetMutableDefault<UWetWrinkleEditorSettings>();
    UserSettings->bShowHiddenNormalTextures = NewState == ECheckBoxState::Checked;
    UserSettings->SaveConfig();
    RefreshWrinkleTexturePaletteView();
}

void SWetWrinkleEditorPanel::HandleSetWrinkleTextureHidden(
    TSharedPtr<FWetWrinkleTexturePaletteItem> Item,
    const bool bHidden)
{
    if (!Item.IsValid())
    {
        return;
    }
    GetMutableDefault<UWetWrinkleEditorSettings>()->SetNormalTextureHidden(Item->TexturePath, bHidden);
    Item->bHidden = bHidden;
    RefreshWrinkleTexturePaletteView();
}

void SWetWrinkleEditorPanel::HandleCorrectWrinkleTexture(TSharedPtr<FWetWrinkleTexturePaletteItem> Item)
{
    if (!Item.IsValid())
    {
        return;
    }

    UTexture2D* Texture = Item->Texture.Get();
    if (Texture == nullptr)
    {
        Texture = Cast<UTexture2D>(Item->TexturePath.TryLoad());
        Item->Texture = Texture;
    }
    if (Texture == nullptr)
    {
        return;
    }

    TSharedRef<SWindow> CorrectionWindow =
        SNew(SWindow)
        .Title(FText::Format(LOCTEXT("CorrectWrinkleNormalWindowTitle", "Correct Wrinkle Normal - {0}"), Item->DisplayName))
        .ClientSize(FVector2D(1200.0f, 800.0f))
        .SupportsMaximize(true)
        .SupportsMinimize(false);
    CorrectionWindow->SetContent(
        SNew(SWetWrinkleNormalCorrectionDialog)
        .ParentWindow(CorrectionWindow)
        .SourceTexture(Texture)
        .WetClothingAsset(WetClothingAsset.Get())
        .OnCorrectedTextureCreated(FOnWetWrinkleCorrectedTextureCreated::CreateSP(
            this,
            &SWetWrinkleEditorPanel::HandleCorrectedWrinkleTextureCreated,
            Item->TexturePath)));

    if (const TSharedPtr<SWindow> OwnerWindow = FSlateApplication::Get().FindWidgetWindow(AsShared()))
    {
        FSlateApplication::Get().AddWindowAsNativeChild(CorrectionWindow, OwnerWindow.ToSharedRef());
    }
    else
    {
        FSlateApplication::Get().AddWindow(CorrectionWindow);
    }
}

void SWetWrinkleEditorPanel::HandleCorrectedWrinkleTextureCreated(
    UTexture2D* CorrectedTexture,
    const bool bHideOriginal,
    const FSoftObjectPath OriginalPath)
{
    if (CorrectedTexture == nullptr)
    {
        return;
    }

    UWetWrinkleEditorSettings* UserSettings = GetMutableDefault<UWetWrinkleEditorSettings>();
    if (bHideOriginal)
    {
        UserSettings->SetNormalTextureHidden(OriginalPath, true);
        if (const FWrinkleTexturePaletteItemPtr OriginalItem =
                WrinklePalettePanel->GetItemsByPath().FindRef(OriginalPath))
        {
            OriginalItem->bHidden = true;
        }
    }

    BrushSettings.WrinkleNormalTexture = CorrectedTexture;
    UpsertWrinkleTexturePaletteItem(FAssetData(CorrectedTexture));
    RefreshWrinkleTexturePaletteView();
    RefreshWrinkleNormalThumbnail();
    PushBrushPreviewSettingsToViewport();
}

TSharedRef<ITableRow> SWetWrinkleEditorPanel::GenerateStrokeRow(FStrokeListItemPtr Item, const TSharedRef<STableViewBase>& OwnerTable)
{
    return SNew(STableRow<FStrokeListItemPtr>, OwnerTable)
        .Padding(2.0f)
        [SNew(SHorizontalBox)
         + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
           [SNew(SBox).WidthOverride(28.0f).HAlign(HAlign_Center)
            [SNew(SCheckBox)
             .IsChecked_Lambda([this, Item]()
             {
                 if (!Item.IsValid()) return ECheckBoxState::Unchecked;
                 if (Item->ElementType == EWetWrinkleElementType::ProceduralRidgeStroke)
                 {
                     const FWetProceduralRidgeStroke* Stroke = ResolveProceduralRidgeListItem(Item);
                     return Stroke != nullptr && Stroke->bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
                 }
                 const FWetWrinklePatchPlacement* Patch = ResolvePatchListItem(Item);
                 return Patch != nullptr && Patch->bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
             })
             .OnCheckStateChanged(this, &SWetWrinkleEditorPanel::HandleStrokeEnabledChanged, Item)]]
         + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.0f,0.0f)
           [SNew(SBox).WidthOverride(118.0f).VAlign(VAlign_Center)
            [SNew(SInlineEditableTextBlock)
            .Text_Lambda([this, Item]()
            {
                if (Item.IsValid() && Item->ElementType == EWetWrinkleElementType::ProceduralRidgeStroke)
                {
                    const FWetProceduralRidgeStroke* Stroke = ResolveProceduralRidgeListItem(Item);
                    return Stroke != nullptr ? FText::FromString(Stroke->DisplayName) : LOCTEXT("MissingRidgeStrokeName", "<missing ridge>");
                }
                const FWetWrinklePatchPlacement* Patch = ResolvePatchListItem(Item);
                return Patch != nullptr ? FText::FromString(Patch->DisplayName) : LOCTEXT("MissingPatchListName", "<missing>");
            })
            .OnTextCommitted(this, &SWetWrinkleEditorPanel::HandleStrokeNameCommitted, Item)]]
         + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
           [SNew(SBox).WidthOverride(62.0f).HAlign(HAlign_Center)
            [SNew(STextBlock).Text_Lambda([Item]()
            {
                return Item.IsValid() && Item->ElementType == EWetWrinkleElementType::ProceduralRidgeStroke
                    ? LOCTEXT("RidgeListItemType", "Ridge") : LOCTEXT("PatchListItemType", "Patch");
            }).Font(FAppStyle::GetFontStyle(TEXT("SmallFont")))]]
         + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(4.0f,0.0f)
           [SNew(STextBlock)
             .Text_Lambda([this, Item]()
             {
                 if (!Item.IsValid() || Item->ElementType == EWetWrinkleElementType::ProceduralRidgeStroke) return FText::FromString(TEXT("-"));
                 const FWetWrinklePatchPlacement* Patch = ResolvePatchListItem(Item);
                 return Patch != nullptr && Patch->WrinkleNormalTexture != nullptr
                     ? FText::FromString(Patch->WrinkleNormalTexture->GetName()) : FText::FromString(TEXT("-"));
             })
             .ToolTipText_Lambda([this, Item]()
             {
                 if (!Item.IsValid() || Item->ElementType == EWetWrinkleElementType::ProceduralRidgeStroke) return FText::GetEmpty();
                 const FWetWrinklePatchPlacement* Patch = ResolvePatchListItem(Item);
                 return Patch != nullptr && Patch->WrinkleNormalTexture != nullptr
                     ? FText::FromString(Patch->WrinkleNormalTexture->GetPathName()) : FText::GetEmpty();
             })
             .OverflowPolicy(ETextOverflowPolicy::Ellipsis)
             .Font(FAppStyle::GetFontStyle(TEXT("SmallFont")))]
         + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
           [SNew(SBox).WidthOverride(28.0f).HAlign(HAlign_Center)
            [SNew(SButton).ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton")).ContentPadding(1.0f).ToolTipText(LOCTEXT("DeleteStrokeButton", "Delete element")).OnClicked(this, &SWetWrinkleEditorPanel::HandleDeleteStrokeClicked, Item)
             [SNew(SImage).Image(FAppStyle::GetBrush(TEXT("Icons.Delete")))]]]];
}

void SWetWrinkleEditorPanel::HandleStrokeSelectionChanged(FStrokeListItemPtr Item, ESelectInfo::Type SelectInfo)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(SWetWrinkleEditorPanel_HandleStrokeSelectionChanged);

    const bool bEndedRidgePointEdit = AuthoringController.IsValid() &&
        AuthoringController->CancelActiveInteraction(false);
    const bool bCommittedRidgePropertyEdit = CommitTransientSelectedRidgeEdit(false);
    bRidgePropertyEditActive = false;
    bool bNeedsPreviewRefresh = bEndedRidgePointEdit || bCommittedRidgePropertyEdit;
    if (PreviewViewport.IsValid())
    {
        bNeedsPreviewRefresh |= PreviewViewport->ClearTransientProceduralStroke(false);
        bNeedsPreviewRefresh |= PreviewViewport->SetEditingProceduralStrokeGuid(FGuid(), false);
    }
    SelectedStrokeGuid = Item.IsValid() ? Item->StrokeGuid : FGuid();
    SelectedElementType = Item.IsValid() ? Item->ElementType : EWetWrinkleElementType::Patch;
    SelectedProceduralRidgePointIndex = INDEX_NONE;
    DispatchWrinkleSelectionState();
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
        PushBrushPreviewSettingsToViewport();
    }

    PushStrokeSelectionToViewport();
    RebuildWrinkleUVViewPatchMarkerCache();
    RefreshWrinkleUVViewMarkersOnly();
    if (PreviewViewport.IsValid())
    {
        if (bNeedsPreviewRefresh)
        {
            PreviewViewport->RefreshStoredStampOverlay(false);
        }
    }
}

FReply SWetWrinkleEditorPanel::HandleClearStrokesClicked()
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || !IsClearStrokesEnabled())
    {
        return FReply::Handled();
    }
    if (AuthoringController.IsValid())
    {
        AuthoringController->CancelActiveInteraction(false);
    }

    TArray<FGuid> RemovedProceduralRidgeStrokeGuids;
    const int32 MaterialSlotIndex = BrushSettings.MaterialSlotIndex;
    if (!EditWrinkleData(
            LOCTEXT("ClearWetWrinkleElementsTransaction", "Clear Wet Wrinkle Elements"),
            EDWCEditorAuthoringImpact::AssetDirty |
                EDWCEditorAuthoringImpact::ElementList |
                EDWCEditorAuthoringImpact::Preview |
                EDWCEditorAuthoringImpact::WrinkleBake,
            MaterialSlotIndex,
            FGuid(),
            [MaterialSlotIndex, &RemovedProceduralRidgeStrokeGuids](FWetClothingWrinkleData& WrinkleData)
            {
                const int32 PreviousPatchCount = WrinkleData.EditablePatches.Num();
                const int32 PreviousRidgeCount = WrinkleData.EditableProceduralRidgeStrokes.Num();
                for (const FWetProceduralRidgeStroke& Stroke : WrinkleData.EditableProceduralRidgeStrokes)
                {
                    if (MaterialSlotIndex == INDEX_NONE || Stroke.MaterialSlotIndex == MaterialSlotIndex)
                    {
                        RemovedProceduralRidgeStrokeGuids.Add(Stroke.StrokeGuid);
                    }
                }
                WrinkleData.EditablePatches.RemoveAll(
                    [MaterialSlotIndex](const FWetWrinklePatchPlacement& Patch)
                    {
                        return MaterialSlotIndex == INDEX_NONE || Patch.MaterialSlotIndex == MaterialSlotIndex;
                    });
                WrinkleData.EditableProceduralRidgeStrokes.RemoveAll(
                    [MaterialSlotIndex](const FWetProceduralRidgeStroke& Stroke)
                    {
                        return MaterialSlotIndex == INDEX_NONE || Stroke.MaterialSlotIndex == MaterialSlotIndex;
                    });
                for (FWetProceduralRidgeStroke& Stroke : WrinkleData.EditableProceduralRidgeStrokes)
                {
                    if (RemovedProceduralRidgeStrokeGuids.Contains(Stroke.StartEndpoint.ConnectedStrokeGuid))
                    {
                        Stroke.StartEndpoint.Mode = EWetProceduralRidgeEndpointMode::Pointed;
                        Stroke.StartEndpoint.ResetConnection();
                    }
                    if (RemovedProceduralRidgeStrokeGuids.Contains(Stroke.EndEndpoint.ConnectedStrokeGuid))
                    {
                        Stroke.EndEndpoint.Mode = EWetProceduralRidgeEndpointMode::Pointed;
                        Stroke.EndEndpoint.ResetConnection();
                    }
                }
                return PreviousPatchCount != WrinkleData.EditablePatches.Num() ||
                       PreviousRidgeCount != WrinkleData.EditableProceduralRidgeStrokes.Num();
            }))
    {
        return FReply::Handled();
    }
    SelectedStrokeGuid.Invalidate();
    SelectedElementType = EWetWrinkleElementType::Patch;
    RefreshStrokeList();
    RefreshStrokeOverlay();
    return FReply::Handled();
}

bool SWetWrinkleEditorPanel::IsClearStrokesEnabled() const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr)
    {
        return false;
    }

    return Asset->Authored.WrinkleData.EditablePatches.ContainsByPredicate(
               [this](const FWetWrinklePatchPlacement& Patch)
               {
                   return IsPatchVisibleForCurrentMaterialSlot(Patch);
               }) ||
           Asset->Authored.WrinkleData.EditableProceduralRidgeStrokes.ContainsByPredicate(
               [this](const FWetProceduralRidgeStroke& Stroke)
               {
                   return IsProceduralRidgeStrokeVisibleForCurrentMaterialSlot(Stroke);
               });
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

        if (!EditWrinkleData(
                LOCTEXT("ToggleProceduralRidgeStrokeTransaction", "Toggle Procedural Ridge Stroke"),
                EDWCEditorAuthoringImpact::AssetDirty | EDWCEditorAuthoringImpact::Preview |
                    EDWCEditorAuthoringImpact::WrinkleBake,
                Stroke->MaterialSlotIndex,
                Item->StrokeGuid,
                [Guid = Item->StrokeGuid, bNewEnabled](FWetClothingWrinkleData& WrinkleData)
                {
                    FWetProceduralRidgeStroke* MutableStroke = WrinkleData.EditableProceduralRidgeStrokes.FindByPredicate(
                        [Guid](const FWetProceduralRidgeStroke& Candidate) { return Candidate.StrokeGuid == Guid; });
                    if (MutableStroke == nullptr || MutableStroke->bEnabled == bNewEnabled) return false;
                    MutableStroke->bEnabled = bNewEnabled;
                    return true;
                })) return;
        RefreshStrokeOverlay();
        return;
    }

    FWetWrinklePatchPlacement* Patch = FindMutablePatch(Item->StrokeGuid);
    if (Patch == nullptr)
    {
        return;
    }
    if (Patch->bEnabled == bNewEnabled)
    {
        return;
    }

    if (!EditWrinkleData(
            LOCTEXT("ToggleWetWrinklePatchTransaction", "Toggle Wet Wrinkle Patch"),
            EDWCEditorAuthoringImpact::AssetDirty | EDWCEditorAuthoringImpact::Preview |
                EDWCEditorAuthoringImpact::WrinkleBake,
            Patch->MaterialSlotIndex,
            Item->StrokeGuid,
            [Guid = Item->StrokeGuid, bNewEnabled](FWetClothingWrinkleData& WrinkleData)
            {
                FWetWrinklePatchPlacement* MutablePatch = WrinkleData.EditablePatches.FindByPredicate(
                    [Guid](const FWetWrinklePatchPlacement& Candidate) { return Candidate.PatchGuid == Guid; });
                if (MutablePatch == nullptr || MutablePatch->bEnabled == bNewEnabled) return false;
                MutablePatch->bEnabled = bNewEnabled;
                return true;
            })) return;
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
        if (!EditWrinkleData(
                LOCTEXT("RenameProceduralRidgeStrokeTransaction", "Rename Procedural Ridge Stroke"),
                EDWCEditorAuthoringImpact::AssetDirty | EDWCEditorAuthoringImpact::ElementList,
                Stroke->MaterialSlotIndex,
                Item->StrokeGuid,
                [Guid = Item->StrokeGuid, NewName](FWetClothingWrinkleData& WrinkleData)
                {
                    FWetProceduralRidgeStroke* MutableStroke = WrinkleData.EditableProceduralRidgeStrokes.FindByPredicate(
                        [Guid](const FWetProceduralRidgeStroke& Candidate) { return Candidate.StrokeGuid == Guid; });
                    if (MutableStroke == nullptr || MutableStroke->DisplayName == NewName) return false;
                    MutableStroke->DisplayName = NewName;
                    return true;
                })) return;
        RefreshStrokeList();
        return;
    }

    FWetWrinklePatchPlacement* Patch = FindMutablePatch(Item->StrokeGuid);
    if (Patch == nullptr || Patch->DisplayName == NewName)
    {
        return;
    }

    if (!EditWrinkleData(
            LOCTEXT("RenameWetWrinklePatchTransaction", "Rename Wet Wrinkle Patch"),
            EDWCEditorAuthoringImpact::AssetDirty | EDWCEditorAuthoringImpact::ElementList,
            Patch->MaterialSlotIndex,
            Item->StrokeGuid,
            [Guid = Item->StrokeGuid, NewName](FWetClothingWrinkleData& WrinkleData)
            {
                FWetWrinklePatchPlacement* MutablePatch = WrinkleData.EditablePatches.FindByPredicate(
                    [Guid](const FWetWrinklePatchPlacement& Candidate) { return Candidate.PatchGuid == Guid; });
                if (MutablePatch == nullptr || MutablePatch->DisplayName == NewName) return false;
                MutablePatch->DisplayName = NewName;
                return true;
            })) return;
    RefreshStrokeList();
}

FReply SWetWrinkleEditorPanel::HandleDeleteStrokeClicked(FStrokeListItemPtr Item)
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || !Item.IsValid())
    {
        return FReply::Handled();
    }
    if (AuthoringController.IsValid())
    {
        AuthoringController->CancelActiveInteraction(false);
    }

    if (Item->ElementType == EWetWrinkleElementType::ProceduralRidgeStroke)
    {
        const int32 RidgeIndex = Asset->Authored.WrinkleData.EditableProceduralRidgeStrokes.IndexOfByPredicate(
            [Item](const FWetProceduralRidgeStroke& Stroke)
            {
                return Stroke.StrokeGuid == Item->StrokeGuid;
            });
        if (RidgeIndex == INDEX_NONE)
        {
            return FReply::Handled();
        }

        const FGuid DeletedGuid = Asset->Authored.WrinkleData.EditableProceduralRidgeStrokes[RidgeIndex].StrokeGuid;
        const int32 DeletedSlot = Asset->Authored.WrinkleData.EditableProceduralRidgeStrokes[RidgeIndex].MaterialSlotIndex;
        if (!EditWrinkleData(
                LOCTEXT("DeleteProceduralRidgeStrokeTransaction", "Delete Procedural Ridge Stroke"),
                EDWCEditorAuthoringImpact::AssetDirty |
                    EDWCEditorAuthoringImpact::ElementList |
                    EDWCEditorAuthoringImpact::Preview |
                    EDWCEditorAuthoringImpact::WrinkleBake,
                DeletedSlot,
                DeletedGuid,
                [DeletedGuid](FWetClothingWrinkleData& WrinkleData)
                {
                    const int32 MutableIndex = WrinkleData.EditableProceduralRidgeStrokes.IndexOfByPredicate(
                        [DeletedGuid](const FWetProceduralRidgeStroke& Candidate) { return Candidate.StrokeGuid == DeletedGuid; });
                    if (MutableIndex == INDEX_NONE) return false;
                    WrinkleData.EditableProceduralRidgeStrokes.RemoveAt(MutableIndex);
                    for (FWetProceduralRidgeStroke& Stroke : WrinkleData.EditableProceduralRidgeStrokes)
                    {
                        if (Stroke.StartEndpoint.ConnectedStrokeGuid == DeletedGuid)
                        {
                            Stroke.StartEndpoint.Mode = EWetProceduralRidgeEndpointMode::Pointed;
                            Stroke.StartEndpoint.ResetConnection();
                        }
                        if (Stroke.EndEndpoint.ConnectedStrokeGuid == DeletedGuid)
                        {
                            Stroke.EndEndpoint.Mode = EWetProceduralRidgeEndpointMode::Pointed;
                            Stroke.EndEndpoint.ResetConnection();
                        }
                    }
                    return true;
                })) return FReply::Handled();
        if (SelectedStrokeGuid == DeletedGuid && SelectedElementType == EWetWrinkleElementType::ProceduralRidgeStroke)
        {
            SelectedStrokeGuid.Invalidate();
            SelectedProceduralRidgePointIndex = INDEX_NONE;
        }
        RefreshStrokeList();
        RefreshStrokeOverlay();
        return FReply::Handled();
    }

    const int32 PatchIndex = Asset->Authored.WrinkleData.EditablePatches.IndexOfByPredicate(
        [Item](const FWetWrinklePatchPlacement& Patch)
        {
            return Patch.PatchGuid == Item->StrokeGuid;
        });
    if (PatchIndex == INDEX_NONE)
    {
        return FReply::Handled();
    }

    const FGuid DeletedGuid = Asset->Authored.WrinkleData.EditablePatches[PatchIndex].PatchGuid;
    const int32 DeletedSlot = Asset->Authored.WrinkleData.EditablePatches[PatchIndex].MaterialSlotIndex;
    if (!EditWrinkleData(
            LOCTEXT("DeleteWetWrinklePatchTransaction", "Delete Wet Wrinkle Patch"),
            EDWCEditorAuthoringImpact::AssetDirty |
                EDWCEditorAuthoringImpact::ElementList |
                EDWCEditorAuthoringImpact::Preview |
                EDWCEditorAuthoringImpact::WrinkleBake,
            DeletedSlot,
            DeletedGuid,
            [DeletedGuid](FWetClothingWrinkleData& WrinkleData)
            {
                const int32 MutableIndex = WrinkleData.EditablePatches.IndexOfByPredicate(
                    [DeletedGuid](const FWetWrinklePatchPlacement& Candidate) { return Candidate.PatchGuid == DeletedGuid; });
                if (MutableIndex == INDEX_NONE) return false;
                WrinkleData.EditablePatches.RemoveAt(MutableIndex);
                return true;
            })) return FReply::Handled();
    if (SelectedStrokeGuid == DeletedGuid)
    {
        SelectedStrokeGuid.Invalidate();
    }
    RefreshStrokeList();
    RefreshStrokeOverlay();
    return FReply::Handled();
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
    DispatchWrinkleBrushState(
        EDWCEditorSessionEffect::UpdatePreviewParameters |
        EDWCEditorSessionEffect::RefreshUVView);
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
    DispatchWrinkleBrushState(EDWCEditorSessionEffect::UpdatePreviewParameters);
}

void SWetWrinkleEditorPanel::HandleFalloffChanged(float NewValue)
{
    BrushSettings.Falloff = FMath::Clamp(NewValue / 100.0f, 0.0f, 1.0f);
    ApplyBrushSettingsToSelectedProceduralStroke();
    DispatchWrinkleBrushState(EDWCEditorSessionEffect::UpdatePreviewParameters);
}

void SWetWrinkleEditorPanel::HandleRotationChanged(float NewValue)
{
    BrushSettings.RotationRadians = FMath::DegreesToRadians(NewValue);
    DispatchWrinkleBrushState(EDWCEditorSessionEffect::UpdatePreviewParameters);
}

void SWetWrinkleEditorPanel::HandlePreviewWetnessChanged(float NewValue)
{
    BrushSettings.PreviewWetness = FMath::Clamp(NewValue, 0.0f, 1.0f);
    DispatchWrinkleBrushState(EDWCEditorSessionEffect::UpdatePreviewParameters);
}

ECheckBoxState SWetWrinkleEditorPanel::GetShowBakedTransparencyState() const
{
    return bShowBakedTransparency ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SWetWrinkleEditorPanel::HandleShowBakedTransparencyChanged(
    const ECheckBoxState NewState)
{
    bShowBakedTransparency = NewState == ECheckBoxState::Checked;
    if (SessionStore.IsValid())
    {
        SessionStore->Dispatch(FDWCSetWrinkleCrossPreviewAction{bShowBakedTransparency});
    }
}

void SWetWrinkleEditorPanel::HandleRidgeStartTaperChanged(float NewValue)
{
    BrushSettings.RidgeStartTaper = FMath::Clamp(NewValue, 0.0f, 0.5f);
    ApplyBrushSettingsToSelectedProceduralStroke();
    PushBrushPreviewSettingsToViewport();
}

void SWetWrinkleEditorPanel::HandleRidgeEndTaperChanged(float NewValue)
{
    BrushSettings.RidgeEndTaper = FMath::Clamp(NewValue, 0.0f, 0.5f);
    ApplyBrushSettingsToSelectedProceduralStroke();
    PushBrushPreviewSettingsToViewport();
}

float SWetWrinkleEditorPanel::GetRidgeFlareLengthValue() const
{
    return BrushSettings.RidgeFlareSettings.Length;
}

void SWetWrinkleEditorPanel::HandleRidgeFlareLengthChanged(const float NewValue)
{
    BrushSettings.RidgeFlareSettings.Length = FMath::Clamp(NewValue, 0.01f, 0.5f);
    ApplyBrushSettingsToSelectedProceduralStroke();
    PushBrushPreviewSettingsToViewport();
}

float SWetWrinkleEditorPanel::GetRidgeFlareWidthValue() const
{
    return BrushSettings.RidgeFlareSettings.WidthScale;
}

void SWetWrinkleEditorPanel::HandleRidgeFlareWidthChanged(const float NewValue)
{
    BrushSettings.RidgeFlareSettings.WidthScale = FMath::Clamp(NewValue, 1.0f, 5.0f);
    ApplyBrushSettingsToSelectedProceduralStroke();
    PushBrushPreviewSettingsToViewport();
}

float SWetWrinkleEditorPanel::GetRidgeFlareEndStrengthValue() const
{
    return BrushSettings.RidgeFlareSettings.EndStrength;
}

void SWetWrinkleEditorPanel::HandleRidgeFlareEndStrengthChanged(const float NewValue)
{
    BrushSettings.RidgeFlareSettings.EndStrength = FMath::Clamp(NewValue, 0.0f, 1.0f);
    ApplyBrushSettingsToSelectedProceduralStroke();
    PushBrushPreviewSettingsToViewport();
}

float SWetWrinkleEditorPanel::GetRidgeFlareSoftnessValue() const
{
    return BrushSettings.RidgeFlareSettings.Softness;
}

void SWetWrinkleEditorPanel::HandleRidgeFlareSoftnessChanged(const float NewValue)
{
    BrushSettings.RidgeFlareSettings.Softness = FMath::Clamp(NewValue, 0.0f, 1.0f);
    ApplyBrushSettingsToSelectedProceduralStroke();
    PushBrushPreviewSettingsToViewport();
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
    PushBrushPreviewSettingsToViewport();
    CommitRidgePropertyEdit();
}

float SWetWrinkleEditorPanel::GetRidgeCenterlineVariationValue() const
{
    return BrushSettings.RidgeNaturalVariation.CenterlineAmount;
}

void SWetWrinkleEditorPanel::HandleRidgeCenterlineVariationChanged(const float NewValue)
{
    BrushSettings.RidgeNaturalVariation.CenterlineAmount = FMath::Clamp(NewValue, 0.0f, 0.5f);
    ApplyBrushSettingsToSelectedProceduralStroke();
    PushBrushPreviewSettingsToViewport();
}

float SWetWrinkleEditorPanel::GetRidgeCenterlineFrequencyValue() const
{
    return BrushSettings.RidgeNaturalVariation.CenterlineFrequency;
}

void SWetWrinkleEditorPanel::HandleRidgeCenterlineFrequencyChanged(const float NewValue)
{
    BrushSettings.RidgeNaturalVariation.CenterlineFrequency = FMath::Clamp(NewValue, 0.25f, 12.0f);
    ApplyBrushSettingsToSelectedProceduralStroke();
    PushBrushPreviewSettingsToViewport();
}

float SWetWrinkleEditorPanel::GetRidgeWidthVariationValue() const
{
    return BrushSettings.RidgeNaturalVariation.WidthVariation;
}

void SWetWrinkleEditorPanel::HandleRidgeWidthVariationChanged(const float NewValue)
{
    BrushSettings.RidgeNaturalVariation.WidthVariation = FMath::Clamp(NewValue, 0.0f, 0.5f);
    ApplyBrushSettingsToSelectedProceduralStroke();
    PushBrushPreviewSettingsToViewport();
}

float SWetWrinkleEditorPanel::GetRidgeWidthFrequencyValue() const
{
    return BrushSettings.RidgeNaturalVariation.WidthFrequency;
}

void SWetWrinkleEditorPanel::HandleRidgeWidthFrequencyChanged(const float NewValue)
{
    BrushSettings.RidgeNaturalVariation.WidthFrequency = FMath::Clamp(NewValue, 0.25f, 12.0f);
    ApplyBrushSettingsToSelectedProceduralStroke();
    PushBrushPreviewSettingsToViewport();
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
    PushBrushPreviewSettingsToViewport();
    CommitRidgePropertyEdit();
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
        if (const FWetProceduralRidgeStroke* Stroke = GetSelectedProceduralRidgeEditState())
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
        if (const FWetProceduralRidgeStroke* Stroke = GetSelectedProceduralRidgeEditState())
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
        if (const FWetProceduralRidgeStroke* Stroke = GetSelectedProceduralRidgeEditState())
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
        if (const FWetProceduralRidgeStroke* Stroke = GetSelectedProceduralRidgeEditState())
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

    const FWetProceduralRidgeStroke* CurrentStroke = GetSelectedProceduralRidgeEditState();
    if (CurrentStroke == nullptr)
    {
        return;
    }

    if (CurrentStroke->Shape == BrushSettings.RidgeShape &&
        CurrentStroke->bFlipFoldSide == BrushSettings.bFlipRidgeFoldSide &&
        FMath::IsNearlyEqual(CurrentStroke->WidthUV, BrushSettings.BrushRadiusUV) &&
        FMath::IsNearlyEqual(CurrentStroke->Strength, BrushSettings.Strength) &&
        FMath::IsNearlyEqual(CurrentStroke->Falloff, BrushSettings.Falloff) &&
        FMath::IsNearlyEqual(CurrentStroke->StartTaper, BrushSettings.RidgeStartTaper) &&
        FMath::IsNearlyEqual(CurrentStroke->EndTaper, BrushSettings.RidgeEndTaper) &&
        FMath::IsNearlyEqual(CurrentStroke->FlareSettings.Length, BrushSettings.RidgeFlareSettings.Length) &&
        FMath::IsNearlyEqual(CurrentStroke->FlareSettings.WidthScale, BrushSettings.RidgeFlareSettings.WidthScale) &&
        FMath::IsNearlyEqual(CurrentStroke->FlareSettings.EndStrength, BrushSettings.RidgeFlareSettings.EndStrength) &&
        FMath::IsNearlyEqual(CurrentStroke->FlareSettings.Softness, BrushSettings.RidgeFlareSettings.Softness) &&
        CurrentStroke->NaturalVariation.bEnabled == BrushSettings.RidgeNaturalVariation.bEnabled &&
        FMath::IsNearlyEqual(CurrentStroke->NaturalVariation.CenterlineAmount, BrushSettings.RidgeNaturalVariation.CenterlineAmount) &&
        FMath::IsNearlyEqual(CurrentStroke->NaturalVariation.CenterlineFrequency, BrushSettings.RidgeNaturalVariation.CenterlineFrequency) &&
        FMath::IsNearlyEqual(CurrentStroke->NaturalVariation.WidthVariation, BrushSettings.RidgeNaturalVariation.WidthVariation) &&
        FMath::IsNearlyEqual(CurrentStroke->NaturalVariation.WidthFrequency, BrushSettings.RidgeNaturalVariation.WidthFrequency) &&
        CurrentStroke->NaturalVariation.NoiseSeed == BrushSettings.RidgeNaturalVariation.NoiseSeed)
    {
        return;
    }

    if (!bRidgePropertyEditActive)
    {
        bRidgePropertyEditActive = true;
    }

    FWetProceduralRidgeStroke* Stroke = BeginTransientSelectedRidgeEdit();
    if (Stroke == nullptr)
    {
        bRidgePropertyEditActive = false;
        return;
    }

    Stroke->Shape = BrushSettings.RidgeShape;
    Stroke->bFlipFoldSide = BrushSettings.bFlipRidgeFoldSide;
    Stroke->WidthUV = BrushSettings.BrushRadiusUV;
    Stroke->Strength = BrushSettings.Strength;
    Stroke->Falloff = BrushSettings.Falloff;
    Stroke->StartTaper = BrushSettings.RidgeStartTaper;
    Stroke->EndTaper = BrushSettings.RidgeEndTaper;
    Stroke->FlareSettings = BrushSettings.RidgeFlareSettings;
    Stroke->NaturalVariation = BrushSettings.RidgeNaturalVariation;
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->PreviewEditedProceduralStroke(*Stroke);
    }
}

const FWetProceduralRidgeStroke* SWetWrinkleEditorPanel::GetSelectedProceduralRidgeEditState() const
{
    if (TransientEditedProceduralRidgeStroke.IsSet() &&
        TransientEditedProceduralRidgeStroke->StrokeGuid == SelectedStrokeGuid)
    {
        return &TransientEditedProceduralRidgeStroke.GetValue();
    }
    return FindProceduralRidgeStroke(SelectedStrokeGuid);
}

FWetProceduralRidgeStroke* SWetWrinkleEditorPanel::BeginTransientSelectedRidgeEdit()
{
    if (TransientEditedProceduralRidgeStroke.IsSet())
    {
        return TransientEditedProceduralRidgeStroke->StrokeGuid == SelectedStrokeGuid
            ? &TransientEditedProceduralRidgeStroke.GetValue()
            : nullptr;
    }

    const FWetProceduralRidgeStroke* AuthoredStroke = FindProceduralRidgeStroke(SelectedStrokeGuid);
    if (AuthoredStroke == nullptr)
    {
        return nullptr;
    }

    TransientEditedProceduralRidgeStroke = *AuthoredStroke;
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->SetEditingProceduralStrokeGuid(AuthoredStroke->StrokeGuid);
    }
    return &TransientEditedProceduralRidgeStroke.GetValue();
}

bool SWetWrinkleEditorPanel::CommitTransientSelectedRidgeEdit(const bool bRefreshPreview)
{
    if (!TransientEditedProceduralRidgeStroke.IsSet())
    {
        return false;
    }

    const FGuid StrokeGuid = TransientEditedProceduralRidgeStroke->StrokeGuid;
    const int32 MaterialSlotIndex = TransientEditedProceduralRidgeStroke->MaterialSlotIndex;
    if (FindProceduralRidgeStroke(StrokeGuid) == nullptr)
    {
        return DiscardTransientSelectedRidgeEdit(bRefreshPreview);
    }

    FWetProceduralRidgeStroke EditedStroke = MoveTemp(TransientEditedProceduralRidgeStroke.GetValue());
    TransientEditedProceduralRidgeStroke.Reset();
    if (!EditWrinkleData(
            LOCTEXT("EditProceduralRidgeTransaction", "Edit Procedural Ridge"),
            EDWCEditorAuthoringImpact::AssetDirty |
                EDWCEditorAuthoringImpact::ElementList |
                EDWCEditorAuthoringImpact::Preview |
                EDWCEditorAuthoringImpact::WrinkleBake,
            MaterialSlotIndex,
            StrokeGuid,
            [StrokeGuid, EditedStroke = MoveTemp(EditedStroke)](FWetClothingWrinkleData& WrinkleData) mutable
            {
                FWetProceduralRidgeStroke* AuthoredStroke =
                    WrinkleData.EditableProceduralRidgeStrokes.FindByPredicate(
                        [StrokeGuid](const FWetProceduralRidgeStroke& Candidate)
                        {
                            return Candidate.StrokeGuid == StrokeGuid;
                        });
                if (AuthoredStroke == nullptr) return false;
                *AuthoredStroke = MoveTemp(EditedStroke);
                return true;
            }))
    {
        if (PreviewViewport.IsValid())
        {
            PreviewViewport->ClearTransientProceduralStroke(false);
            PreviewViewport->SetEditingProceduralStrokeGuid(FGuid(), false);
        }
        return false;
    }

    if (PreviewViewport.IsValid())
    {
        PreviewViewport->ClearTransientProceduralStroke(false);
        PreviewViewport->SetEditingProceduralStrokeGuid(FGuid(), false);
    }
    if (bRefreshPreview)
    {
        RefreshStrokeOverlay(true);
    }
    if (ElementListPanel.IsValid())
    {
        ElementListPanel->RequestRefresh();
    }
    return true;
}

bool SWetWrinkleEditorPanel::DiscardTransientSelectedRidgeEdit(const bool bRefreshPreview)
{
    if (!TransientEditedProceduralRidgeStroke.IsSet())
    {
        return false;
    }

    TransientEditedProceduralRidgeStroke.Reset();
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->ClearTransientProceduralStroke(false);
        PreviewViewport->SetEditingProceduralStrokeGuid(FGuid(), false);
    }
    if (bRefreshPreview)
    {
        RefreshStrokeOverlay(true);
    }
    return true;
}

void SWetWrinkleEditorPanel::CommitRidgePropertyEdit()
{
    if (!bRidgePropertyEditActive)
    {
        return;
    }

    CommitTransientSelectedRidgeEdit(true);
    bRidgePropertyEditActive = false;
}

void SWetWrinkleEditorPanel::HandleRidgePropertySliderBegin()
{
    if (!bRidgePropertyEditActive &&
        BrushSettings.ToolMode == EWetWrinkleToolMode::ProceduralRidgeStroke &&
        BrushSettings.RidgeEditMode == EWetProceduralRidgeEditMode::Edit)
    {
        bRidgePropertyEditActive = true;
    }
}

void SWetWrinkleEditorPanel::HandleRidgePropertySliderEnd(float NewValue)
{
    CommitRidgePropertyEdit();
}

void SWetWrinkleEditorPanel::HandleRidgePropertyCommitted(float NewValue, ETextCommit::Type CommitType)
{
    CommitRidgePropertyEdit();
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

    const FGuid StrokeGuid = Stroke->StrokeGuid;
    if (!EditWrinkleData(
            LOCTEXT("EditProceduralRidgeEndpointTransaction", "Edit Procedural Ridge Endpoint"),
            EDWCEditorAuthoringImpact::AssetDirty | EDWCEditorAuthoringImpact::Preview |
                EDWCEditorAuthoringImpact::WrinkleBake,
            Stroke->MaterialSlotIndex,
            StrokeGuid,
            [StrokeGuid, bStartEndpoint, NewMode](FWetClothingWrinkleData& WrinkleData)
            {
                FWetProceduralRidgeStroke* MutableStroke = WrinkleData.EditableProceduralRidgeStrokes.FindByPredicate(
                    [StrokeGuid](const FWetProceduralRidgeStroke& Candidate) { return Candidate.StrokeGuid == StrokeGuid; });
                if (MutableStroke == nullptr) return false;
                FWetProceduralRidgeEndpoint& MutableEndpoint =
                    bStartEndpoint ? MutableStroke->StartEndpoint : MutableStroke->EndEndpoint;
                MutableEndpoint.Mode = NewMode;
                MutableEndpoint.ResetConnection();
                return true;
            })) return;
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

    const FGuid StrokeGuid = Stroke->StrokeGuid;
    if (!EditWrinkleData(
            LOCTEXT("EditProceduralRidgeFlaredEndpointTransaction", "Edit Flared Ridge Endpoint"),
            EDWCEditorAuthoringImpact::AssetDirty | EDWCEditorAuthoringImpact::Preview |
                EDWCEditorAuthoringImpact::WrinkleBake,
            Stroke->MaterialSlotIndex,
            StrokeGuid,
            [StrokeGuid, bStartEndpoint, NewMode](FWetClothingWrinkleData& WrinkleData)
            {
                FWetProceduralRidgeStroke* MutableStroke = WrinkleData.EditableProceduralRidgeStrokes.FindByPredicate(
                    [StrokeGuid](const FWetProceduralRidgeStroke& Candidate) { return Candidate.StrokeGuid == StrokeGuid; });
                if (MutableStroke == nullptr) return false;
                FWetProceduralRidgeEndpoint& MutableEndpoint =
                    bStartEndpoint ? MutableStroke->StartEndpoint : MutableStroke->EndEndpoint;
                MutableEndpoint.Mode = NewMode;
                MutableEndpoint.ResetConnection();
                return true;
            })) return;
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

    const FGuid StrokeGuid = Stroke->StrokeGuid;
    const int32 PointIndex = SelectedProceduralRidgePointIndex;
    if (!EditWrinkleData(
            LOCTEXT("DeleteProceduralRidgePointTransaction", "Delete Procedural Ridge Point"),
            EDWCEditorAuthoringImpact::AssetDirty |
                EDWCEditorAuthoringImpact::ElementList |
                EDWCEditorAuthoringImpact::Preview |
                EDWCEditorAuthoringImpact::WrinkleBake,
            Stroke->MaterialSlotIndex,
            StrokeGuid,
            [StrokeGuid, PointIndex](FWetClothingWrinkleData& WrinkleData)
            {
                FWetProceduralRidgeStroke* MutableStroke = WrinkleData.EditableProceduralRidgeStrokes.FindByPredicate(
                    [StrokeGuid](const FWetProceduralRidgeStroke& Candidate) { return Candidate.StrokeGuid == StrokeGuid; });
                if (MutableStroke == nullptr || MutableStroke->Points.Num() <= 2 || !MutableStroke->Points.IsValidIndex(PointIndex)) return false;
                const bool bRemovedStart = PointIndex == 0;
                const bool bRemovedEnd = PointIndex == MutableStroke->Points.Num() - 1;
                MutableStroke->Points.RemoveAt(PointIndex);
                if (bRemovedStart)
                {
                    MutableStroke->StartEndpoint.Mode = EWetProceduralRidgeEndpointMode::Pointed;
                    MutableStroke->StartEndpoint.ResetConnection();
                }
                if (bRemovedEnd)
                {
                    MutableStroke->EndEndpoint.Mode = EWetProceduralRidgeEndpointMode::Pointed;
                    MutableStroke->EndEndpoint.ResetConnection();
                }
                return true;
            })) return FReply::Handled();
    SelectedProceduralRidgePointIndex = INDEX_NONE;
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
        for (FWetProceduralRidgeStroke& Stroke : Asset->Authored.WrinkleData.EditableProceduralRidgeStrokes)
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
    PushBrushPreviewSettingsToViewport();
    RefreshWrinkleUVView();
}

ECheckBoxState SWetWrinkleEditorPanel::GetPreviewToggleState() const
{
    return BrushSettings.bShowPreview ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

FWetWrinklePatchPlacement* SWetWrinkleEditorPanel::FindMutablePatch(const FGuid& PatchGuid) const
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || !PatchGuid.IsValid())
    {
        return nullptr;
    }

    return Asset->Authored.WrinkleData.EditablePatches.FindByPredicate(
        [PatchGuid](const FWetWrinklePatchPlacement& Patch)
        {
            return Patch.PatchGuid == PatchGuid;
        });
}

const FWetWrinklePatchPlacement* SWetWrinkleEditorPanel::FindPatch(const FGuid& PatchGuid) const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || !PatchGuid.IsValid())
    {
        return nullptr;
    }

    return Asset->Authored.WrinkleData.EditablePatches.FindByPredicate(
        [PatchGuid](const FWetWrinklePatchPlacement& Patch)
        {
            return Patch.PatchGuid == PatchGuid;
        });
}

const FWetWrinklePatchPlacement* SWetWrinkleEditorPanel::ResolvePatchListItem(
    const FStrokeListItemPtr& Item) const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr ||
        !Item.IsValid() ||
        Item->ElementType != EWetWrinkleElementType::Patch)
    {
        return nullptr;
    }

    const TArray<FWetWrinklePatchPlacement>& Patches = Asset->Authored.WrinkleData.EditablePatches;
    if (Patches.IsValidIndex(Item->SourceIndex) &&
        Patches[Item->SourceIndex].PatchGuid == Item->StrokeGuid)
    {
        return &Patches[Item->SourceIndex];
    }

    return FindPatch(Item->StrokeGuid);
}

FWetProceduralRidgeStroke* SWetWrinkleEditorPanel::FindMutableProceduralRidgeStroke(const FGuid& StrokeGuid) const
{
    UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr || !StrokeGuid.IsValid())
    {
        return nullptr;
    }

    return Asset->Authored.WrinkleData.EditableProceduralRidgeStrokes.FindByPredicate(
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

    return Asset->Authored.WrinkleData.EditableProceduralRidgeStrokes.FindByPredicate(
        [StrokeGuid](const FWetProceduralRidgeStroke& Stroke)
        {
            return Stroke.StrokeGuid == StrokeGuid;
        });
}

const FWetProceduralRidgeStroke* SWetWrinkleEditorPanel::ResolveProceduralRidgeListItem(
    const FStrokeListItemPtr& Item) const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    if (Asset == nullptr ||
        !Item.IsValid() ||
        Item->ElementType != EWetWrinkleElementType::ProceduralRidgeStroke)
    {
        return nullptr;
    }

    const TArray<FWetProceduralRidgeStroke>& Strokes =
        Asset->Authored.WrinkleData.EditableProceduralRidgeStrokes;
    if (Strokes.IsValidIndex(Item->SourceIndex) &&
        Strokes[Item->SourceIndex].StrokeGuid == Item->StrokeGuid)
    {
        return &Strokes[Item->SourceIndex];
    }

    return FindProceduralRidgeStroke(Item->StrokeGuid);
}

FWetProceduralRidgeStrokePoint SWetWrinkleEditorPanel::MakeProceduralRidgePointFromHit(
    const FWetWrinkleSurfaceHit& SurfaceHit) const
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
    for (const FWetProceduralRidgeStroke& Candidate : Asset->Authored.WrinkleData.EditableProceduralRidgeStrokes)
    {
        if (!Candidate.bEnabled || Candidate.StrokeGuid == ExcludedStrokeGuid || Candidate.Points.Num() < 2 ||
            Candidate.MaterialSlotIndex != SurfaceHit.MaterialSlotIndex)
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
                    SurfaceHit.UVChannelIndex,
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
    const FWetProceduralRidgeStroke* AuthoredStroke =
        SelectedElementType == EWetWrinkleElementType::ProceduralRidgeStroke
            ? FindProceduralRidgeStroke(SelectedStrokeGuid)
            : nullptr;
    if (Asset == nullptr || AuthoredStroke == nullptr || !PreviewViewport.IsValid() || !SurfaceHit.bHit ||
        SurfaceHit.MaterialSlotIndex != AuthoredStroke->MaterialSlotIndex)
    {
        return;
    }

    const bool bInsertPoint = FSlateApplication::Get().GetModifierKeys().IsShiftDown();
    int32 PointIndex = INDEX_NONE;
    if (bInsertPoint)
    {
        float SegmentT = 0.0f;
        const int32 SegmentIndex = FindNearestProceduralRidgeSegment(*AuthoredStroke, SurfaceHit.UV, SegmentT);
        if (SegmentIndex != INDEX_NONE)
        {
            PointIndex = SegmentIndex + 1;
        }
    }
    else
    {
        PointIndex = PreviewViewport->FindNearestProceduralStrokePoint(
            *AuthoredStroke,
            SurfaceHit.WorldPosition,
            FMath::Max(SizeCm * 0.75f, 1.0f));
    }

    if (PointIndex == INDEX_NONE)
    {
        return;
    }

    bRidgePointEditActive = true;

    FWetProceduralRidgeStroke* Stroke = BeginTransientSelectedRidgeEdit();
    if (Stroke == nullptr)
    {
        bRidgePointEditActive = false;
        return;
    }

    if (bInsertPoint)
    {
        Stroke->Points.Insert(MakeProceduralRidgePointFromHit(SurfaceHit), PointIndex);
    }

    EditingProceduralRidgePointIndex = PointIndex;
    SelectedProceduralRidgePointIndex = PointIndex;
    EditingProceduralRidgeUVIslandID = SurfaceHit.UVIslandID;
    bEditingProceduralRidgePoint = true;
    PreviewViewport->PreviewEditedProceduralStroke(*Stroke);
}

void SWetWrinkleEditorPanel::UpdateProceduralRidgePointEdit(const FWetWrinkleSurfaceHit& SurfaceHit)
{
    FWetProceduralRidgeStroke* Stroke =
        TransientEditedProceduralRidgeStroke.IsSet() &&
                TransientEditedProceduralRidgeStroke->StrokeGuid == SelectedStrokeGuid
            ? &TransientEditedProceduralRidgeStroke.GetValue()
            : nullptr;
    if (!bEditingProceduralRidgePoint || Stroke == nullptr || !PreviewViewport.IsValid() || !SurfaceHit.bHit ||
        !Stroke->Points.IsValidIndex(EditingProceduralRidgePointIndex) ||
        SurfaceHit.MaterialSlotIndex != Stroke->MaterialSlotIndex ||
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
}

void SWetWrinkleEditorPanel::EndProceduralRidgePointEdit(
    const bool bCancel,
    const bool bRefreshPreview)
{
    if (!bEditingProceduralRidgePoint)
    {
        return;
    }

    if (bCancel)
    {
        DiscardTransientSelectedRidgeEdit(bRefreshPreview);
    }
    else
    {
        CommitTransientSelectedRidgeEdit(bRefreshPreview);
    }

    bRidgePointEditActive = false;
    EditingProceduralRidgePointIndex = INDEX_NONE;
    EditingProceduralRidgeUVIslandID = INDEX_NONE;
    bEditingProceduralRidgePoint = false;
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
        PushBrushPreviewSettingsToViewport();
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
    SmoothedProceduralRidgeHits.Reset();
    SmoothedProceduralRidgeHits.Add(StartHit);
    LiveProceduralRidgeHit = StartHit;
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

    LiveProceduralRidgeHit = SurfaceHit;
    if (ShouldAddProceduralRidgePoint(SurfaceHit))
    {
        CapturedProceduralRidgeHits.Add(SurfaceHit);
        SmoothedProceduralRidgeHits.Add(SurfaceHit);
        const int32 InteriorPointIndex = CapturedProceduralRidgeHits.Num() - 2;
        if (InteriorPointIndex > 0)
        {
            FWetWrinkleSurfaceHit SmoothedHit;
            if (TrySmoothProceduralRidgeInteriorHit(
                    CapturedProceduralRidgeHits[InteriorPointIndex - 1],
                    CapturedProceduralRidgeHits[InteriorPointIndex],
                    CapturedProceduralRidgeHits[InteriorPointIndex + 1],
                    SmoothedHit))
            {
                SmoothedProceduralRidgeHits[InteriorPointIndex] = SmoothedHit;
            }
        }
    }

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

        FWetProceduralRidgeStroke NewStroke;
        NewStroke.StrokeGuid = FGuid::NewGuid();
        NewStroke.DisplayName = FString::Printf(
            TEXT("Ridge %03d"),
            Asset->Authored.WrinkleData.EditableProceduralRidgeStrokes.Num() + 1);
        NewStroke.MaterialSlotIndex = ActiveProceduralRidgeMaterialSlotIndex;
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

        if (!EditWrinkleData(
                LOCTEXT("CreateProceduralRidgeStrokeTransaction", "Create Procedural Ridge Stroke"),
                EDWCEditorAuthoringImpact::AssetDirty |
                    EDWCEditorAuthoringImpact::ElementList |
                    EDWCEditorAuthoringImpact::Preview |
                    EDWCEditorAuthoringImpact::WrinkleBake,
                NewStroke.MaterialSlotIndex,
                NewStroke.StrokeGuid,
                [&NewStroke](FWetClothingWrinkleData& WrinkleData)
                {
                    WrinkleData.EditableProceduralRidgeStrokes.Add(NewStroke);
                    return true;
                }))
        {
            CancelProceduralRidgeStroke();
            return;
        }
        if (PreviewViewport.IsValid())
        {
            PreviewViewport->AppendAccumulatedPreviewProceduralStroke(NewStroke);
        }
        SelectedStrokeGuid = NewStroke.StrokeGuid;
        SelectedElementType = EWetWrinkleElementType::ProceduralRidgeStroke;
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
    SmoothedProceduralRidgeHits.Reset();
    LiveProceduralRidgeHit = FWetWrinkleSurfaceHit();
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
    TArray<FWetWrinkleSurfaceHit> Result = SmoothedProceduralRidgeHits;
    if (bCapturingProceduralRidgeStroke && LiveProceduralRidgeHit.bHit)
    {
        constexpr double LivePointPositionToleranceSq = 1.0e-8;
        constexpr double LivePointUVToleranceSq = 1.0e-12;
        if (Result.IsEmpty() ||
            FVector::DistSquared(Result.Last().WorldPosition, LiveProceduralRidgeHit.WorldPosition) > LivePointPositionToleranceSq ||
            FVector2D::DistSquared(Result.Last().UV, LiveProceduralRidgeHit.UV) > LivePointUVToleranceSq)
        {
            Result.Add(LiveProceduralRidgeHit);
            const int32 InteriorPointIndex = Result.Num() - 2;
            if (InteriorPointIndex > 0 && CapturedProceduralRidgeHits.IsValidIndex(InteriorPointIndex))
            {
                FWetWrinkleSurfaceHit SmoothedHit;
                if (TrySmoothProceduralRidgeInteriorHit(
                        CapturedProceduralRidgeHits[InteriorPointIndex - 1],
                        CapturedProceduralRidgeHits[InteriorPointIndex],
                        LiveProceduralRidgeHit,
                        SmoothedHit))
                {
                    Result[InteriorPointIndex] = SmoothedHit;
                }
            }
        }
    }

    return Result;
}

bool SWetWrinkleEditorPanel::TrySmoothProceduralRidgeInteriorHit(
    const FWetWrinkleSurfaceHit& Previous,
    const FWetWrinkleSurfaceHit& Current,
    const FWetWrinkleSurfaceHit& Next,
    FWetWrinkleSurfaceHit& OutSmoothedHit) const
{
    if (!PreviewViewport.IsValid())
    {
        return false;
    }

    constexpr double SmoothingAlpha = 0.25;
    const double MaxProjectionDistance = FMath::Max(static_cast<double>(SizeCm * 0.5f), 0.5);
    const FVector2D NeighborMidpoint = (Previous.UV + Next.UV) * 0.5;
    const FVector2D SmoothedUV = FMath::Lerp(Current.UV, NeighborMidpoint, SmoothingAlpha);
    return PreviewViewport->TryBuildSurfaceHitAtUVNearWorldPosition(
               ActiveProceduralRidgeMaterialSlotIndex,
               ActiveProceduralRidgeUVChannelIndex,
               SmoothedUV,
               Current.WorldPosition,
               OutSmoothedHit) &&
           OutSmoothedHit.UVIslandID == ActiveProceduralRidgeUVIslandID &&
           FVector::Distance(OutSmoothedHit.WorldPosition, Current.WorldPosition) <= MaxProjectionDistance;
}

FWetWrinklePatchPlacement SWetWrinkleEditorPanel::MakeStampFromHit(const FWetWrinkleSurfaceHit& SurfaceHit) const
{
    FWetWrinklePatchPlacement Stamp;
    Stamp.PatchGuid = FGuid::NewGuid();
    Stamp.MaterialSlotIndex = SurfaceHit.MaterialSlotIndex;
    Stamp.SourceTexture = ResolveSourceTextureForStamp(SurfaceHit.MaterialSlotIndex);
    Stamp.PositionUV = SurfaceHit.UV;
    Stamp.BrushRadiusUV = BrushSettings.BrushRadiusUV;
    Stamp.RotationRadians = BrushSettings.RotationRadians;
    Stamp.Scale = FVector2D(1.0f, 1.0f);
    Stamp.Strength = BrushSettings.Strength;
    Stamp.Falloff = BrushSettings.Falloff;
    Stamp.WrinkleNormalTexture = BrushSettings.WrinkleNormalTexture;
    return Stamp;
}

UTexture* SWetWrinkleEditorPanel::ResolveSourceTextureForStamp(int32 MaterialSlotIndex) const
{
    return FWetClothingMaterialTextureResolver::ResolveOrSaveTextureSelection(
        const_cast<UWetClothingAsset*>(WetClothingAsset.Get()),
        MaterialSlotIndex);
}

bool SWetWrinkleEditorPanel::IsCurrentWrinkleNormalUsable(FString* OutReason) const
{
    if (BrushSettings.WrinkleNormalTexture == nullptr)
    {
        if (OutReason != nullptr)
        {
            *OutReason = TEXT("Select a wrinkle normal texture before painting wrinkle patches.");
        }
        return false;
    }

    if (OutReason != nullptr)
    {
        OutReason->Reset();
    }
    return true;
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

    return nullptr;
}

const FDWCEditorPreviewSlotState* SWetWrinkleEditorPanel::FindPreviewSlotState(
    const int32 MaterialSlotIndex) const
{
    return PreviewSlotStates.Find(MaterialSlotIndex);
}

FString SWetWrinkleEditorPanel::MakeDefaultPatchName() const
{
    const UWetClothingAsset* Asset = WetClothingAsset.Get();
    const int32 PatchNumber = Asset != nullptr ? Asset->Authored.WrinkleData.EditablePatches.Num() + 1 : 1;
    return FString::Printf(TEXT("Patch %03d"), PatchNumber);
}

bool SWetWrinkleEditorPanel::EditWrinkleData(
    const FText& TransactionText,
    const EDWCEditorAuthoringImpact Impact,
    const int32 MaterialSlotIndex,
    const FGuid& ElementGuid,
    TFunctionRef<bool(FWetClothingWrinkleData&)> Mutation)
{
    if (!AuthoringDocument.IsValid())
    {
        return false;
    }

    FDWCEditorAuthoringChange Change;
    Change.Domain = EDWCEditorAuthoringDomain::Wrinkle;
    Change.Impact = Impact;
    Change.MaterialSlotIndex = MaterialSlotIndex;
    Change.ElementGuid = ElementGuid;
    return AuthoringDocument->Edit(
        TransactionText,
        Change,
        [&Mutation](UWetClothingAsset& Asset)
        {
            return Mutation(Asset.Authored.WrinkleData);
        }).bChanged;
}

void SWetWrinkleEditorPanel::MarkAssetEdited()
{
    if (UWetClothingAsset* Asset = WetClothingAsset.Get())
    {
        Asset->MarkPackageDirty();
    }
}

#undef LOCTEXT_NAMESPACE
