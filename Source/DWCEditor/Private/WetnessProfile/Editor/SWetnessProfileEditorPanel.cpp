#include "SWetnessProfileEditorPanel.h"

#include "AssetRegistry/AssetData.h"
#include "Core/DWCEditorUtils.h"
#include "DataAssets/WetnessProfile.h"
#include "Engine/SkeletalMesh.h"
#include "IDetailsView.h"
#include "PropertyCustomizationHelpers.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "WetnessProfile/Editor/WetnessProfileEditorPolicy.h"
#include "WetnessProfile/Viewport/SWetnessProfileViewport.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "WetnessProfileEditorPanel"

namespace
{
    const FName PreviewDroplet1DetailSizeMetadataKey(TEXT("DWC.Preview.DropletDetailSize"));
    const FName PreviewDroplet2DetailSizeMetadataKey(TEXT("DWC.Preview.Droplet2DetailSize"));

    float ReadFloatMetadata(
        const UWetnessProfile* Profile,
        const FName Key,
        const float DefaultValue)
    {
        UPackage* Package = Profile != nullptr ? Profile->GetOutermost() : nullptr;
        if (Package == nullptr)
        {
            return DefaultValue;
        }

        FMetaData& MetaData = Package->GetMetaData();
        const FString StoredValue = MetaData.GetValue(Profile, Key);
        if (StoredValue.IsEmpty())
        {
            return DefaultValue;
        }

        return FCString::Atof(*StoredValue);
    }

    void WriteFloatMetadata(
        UWetnessProfile* Profile,
        const FName Key,
        const float Value)
    {
        UPackage* Package = Profile != nullptr ? Profile->GetOutermost() : nullptr;
        if (Package == nullptr)
        {
            return;
        }

        Package->Modify();
        FMetaData& MetaData = Package->GetMetaData();
        MetaData.SetValue(Profile, Key, *FString::SanitizeFloat(Value));
    }

    FText FormatPreviewFloat(const float Value)
    {
        FNumberFormattingOptions Options;
        Options.MinimumFractionalDigits = 2;
        Options.MaximumFractionalDigits = 2;
        return FText::AsNumber(Value, &Options);
    }

#if WITH_EDITORONLY_DATA
    void NotifyPreviewDisplayFilterChanged(UWetnessProfile& Profile, const FName PropertyName)
    {
        if (FProperty* Property = FindFProperty<FProperty>(UWetnessProfile::StaticClass(), PropertyName))
        {
            FPropertyChangedEvent Event(Property, EPropertyChangeType::ValueSet);
            Profile.PostEditChangeProperty(Event);
        }
    }
#endif
}

void SWetnessProfileEditorPanel::Construct(const FArguments& InArgs)
{
    WetnessProfile = InArgs._WetnessProfile;
    AbsorbedDetailsView = InArgs._AbsorbedDetailsView;
    SurfaceDetailsView = InArgs._SurfaceDetailsView;
    LoadPersistedPreviewSettings();
    PreviewModeItems = {
        MakeShared<SWetnessProfileViewport::EPreviewMode>(SWetnessProfileViewport::EPreviewMode::Lit),
        MakeShared<SWetnessProfileViewport::EPreviewMode>(SWetnessProfileViewport::EPreviewMode::Absorbed),
        MakeShared<SWetnessProfileViewport::EPreviewMode>(SWetnessProfileViewport::EPreviewMode::SurfaceCoverage),
        MakeShared<SWetnessProfileViewport::EPreviewMode>(SWetnessProfileViewport::EPreviewMode::FinalDropletCoverage),
        MakeShared<SWetnessProfileViewport::EPreviewMode>(SWetnessProfileViewport::EPreviewMode::DropletNormal),
        MakeShared<SWetnessProfileViewport::EPreviewMode>(SWetnessProfileViewport::EPreviewMode::DropletStampTest),
    };
    SelectedPreviewModeItem = PreviewModeItems[0];
    PreviewBehaviorItems = {
        MakeShared<SWetnessProfileViewport::EPreviewBehavior>(SWetnessProfileViewport::EPreviewBehavior::Manual),
        MakeShared<SWetnessProfileViewport::EPreviewBehavior>(SWetnessProfileViewport::EPreviewBehavior::Simulation),
    };
    SelectedPreviewBehaviorItem = PreviewBehaviorItems[0];
    PreviewSpeedItems = {
        MakeShared<float>(0.25f),
        MakeShared<float>(0.5f),
        MakeShared<float>(1.0f),
        MakeShared<float>(2.0f),
    };
    SelectedPreviewSpeedItem = PreviewSpeedItems[2];

    const FSlateFontInfo PanelHeadingFont = FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 16);

    ChildSlot
        [SNew(SVerticalBox)

         + SVerticalBox::Slot()
               .AutoHeight()
               .Padding(10.0f, 10.0f, 10.0f, 6.0f)
                   [SNew(SHorizontalBox)

                    + SHorizontalBox::Slot()
                          .FillWidth(1.0f)
                          .VAlign(VAlign_Center)
                              [SNew(STextBlock)
                                   .Text(LOCTEXT("PreviewHeading", "Preview"))
                                   .Font(PanelHeadingFont)]

                    + SHorizontalBox::Slot()
                          .AutoWidth()
                              [SNew(SButton)
                                   .Text(LOCTEXT("SaveButton", "Save"))
                                   .OnClicked(this, &SWetnessProfileEditorPanel::HandleSaveClicked)]]

         + SVerticalBox::Slot()
               .AutoHeight()
               .Padding(10.0f, 0.0f, 10.0f, 8.0f)
                   [BuildPreviewToolbar()]

         + SVerticalBox::Slot()
               .FillHeight(1.0f)
               .Padding(10.0f, 0.0f, 10.0f, 0.0f)
                   [SNew(SBorder)
                        .Padding(0.0f)
                            [SAssignNew(PreviewViewport, SWetnessProfileViewport)
                                 .WetnessProfile(WetnessProfile.Get())]]

         + SVerticalBox::Slot()
               .AutoHeight()
               .Padding(10.0f, 8.0f, 10.0f, 10.0f)
                   [BuildPreviewControlsSection()]];

    ApplyPreviewSettingsToViewport();
    RefreshFromProfile();
}

void SWetnessProfileEditorPanel::RefreshFromProfile()
{
    LoadPersistedPreviewSettings();
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->SetPreviewDropletVisibility(
            bPreviewDroplet1Enabled,
            bPreviewDroplet2Enabled);
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
        RefreshDetailsViews();
        RefreshFromProfile();
    }

    DWCEditorUtils::SaveAsset(WetnessProfile.Get());
    return FReply::Handled();
}

void SWetnessProfileEditorPanel::RefreshDetailsViews()
{
    if (AbsorbedDetailsView.IsValid())
    {
        AbsorbedDetailsView->ForceRefresh();
    }
    if (SurfaceDetailsView.IsValid())
    {
        SurfaceDetailsView->ForceRefresh();
    }
}

TSharedRef<SWidget> SWetnessProfileEditorPanel::BuildPreviewToolbar()
{
    return SNew(SBorder)
        .Padding(FMargin(8.0f, 5.0f))
        .BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
            [SNew(SHorizontalBox)

             + SHorizontalBox::Slot()
                   .AutoWidth()
                   .VAlign(VAlign_Center)
                   .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                       [SNew(STextBlock)
                            .Text(LOCTEXT("PreviewMeshLabel", "Mesh:"))]

             + SHorizontalBox::Slot()
                   .FillWidth(1.0f)
                   .VAlign(VAlign_Center)
                   .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                       [SNew(SObjectPropertyEntryBox)
                            .AllowedClass(USkeletalMesh::StaticClass())
                            .AllowClear(true)
                            .ObjectPath(this, &SWetnessProfileEditorPanel::GetCurrentPreviewMeshObjectPath)
                            .OnObjectChanged(this, &SWetnessProfileEditorPanel::HandleCurrentPreviewMeshChanged)]

             + SHorizontalBox::Slot()
                   .AutoWidth()
                   .Padding(0.0f, 0.0f, 4.0f, 0.0f)
                       [SNew(SButton)
                            .Text(LOCTEXT("UseDefaultMeshButton", "Use Default"))
                            .ToolTipText(LOCTEXT("UseDefaultMeshTooltip", "Use the preview mesh saved on this Wetness Profile."))
                            .OnClicked(this, &SWetnessProfileEditorPanel::HandleUseReferenceMeshClicked)]

             + SHorizontalBox::Slot()
                   .AutoWidth()
                   .Padding(0.0f, 0.0f, 4.0f, 0.0f)
                       [SNew(SButton)
                            .Text(LOCTEXT("SaveCurrentMeshAsDefaultButton", "Set Default"))
                            .ToolTipText(LOCTEXT("SaveCurrentMeshAsDefaultTooltip", "Save the currently displayed skeletal mesh as this profile's preview mesh."))
                            .OnClicked(this, &SWetnessProfileEditorPanel::HandleSaveCurrentMeshAsReferenceClicked)]

             + SHorizontalBox::Slot()
                   .AutoWidth()
                       [SNew(SButton)
                            .Text(LOCTEXT("UseSphereMeshButton", "Sphere"))
                            .ToolTipText(LOCTEXT("UseSphereMeshTooltip", "Temporarily preview on the standard sphere."))
                            .OnClicked(this, &SWetnessProfileEditorPanel::HandleUseSphereMeshClicked)]];
}

TSharedRef<SWidget> SWetnessProfileEditorPanel::BuildPreviewControlsSection()
{
    return SNew(SBorder)
        .Padding(10.0f)
        .BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
            [SNew(SVerticalBox)

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                       [SNew(STextBlock)
                            .Text(LOCTEXT("PreviewControlsHeading", "Preview Controls"))
                            .Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.BoldFont")))]

             + SVerticalBox::Slot()
                   .AutoHeight()
                       [BuildPreviewModeSection()]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 9.0f)
                       [SNew(SSeparator)]

             + SVerticalBox::Slot()
                   .AutoHeight()
                       [SNew(SBox)
                            .Visibility(this, &SWetnessProfileEditorPanel::GetManualControlsVisibility)
                            [BuildPreviewWaterSection()]]

             + SVerticalBox::Slot()
                   .AutoHeight()
                       [SNew(SBox)
                            .Visibility(this, &SWetnessProfileEditorPanel::GetSimulationControlsVisibility)
                            [BuildPreviewSimulationSection()]]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 9.0f)
                       [SNew(SSeparator)]

             + SVerticalBox::Slot()
                   .AutoHeight()
                       [BuildPreviewDetailSizeSection()]];
}

TSharedRef<SWidget> SWetnessProfileEditorPanel::BuildPreviewWaterSection()
{
    const auto BuildAmountSlider = [](const FText& Label, const TAttribute<float>& Value, const FOnFloatValueChanged& OnChanged, const TAttribute<FText>& ValueText)
    {
        return SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
                  .AutoWidth()
                  .VAlign(VAlign_Center)
                      [SNew(SBox)
                           .WidthOverride(112.0f)
                               [SNew(STextBlock).Text(Label)]]
            + SHorizontalBox::Slot()
                  .FillWidth(1.0f)
                  .VAlign(VAlign_Center)
                      [SNew(SSlider)
                           .MinValue(0.0f)
                           .MaxValue(100.0f)
                           .Value(Value)
                           .OnValueChanged(OnChanged)]
            + SHorizontalBox::Slot()
                  .AutoWidth()
                  .VAlign(VAlign_Center)
                  .Padding(8.0f, 0.0f, 0.0f, 0.0f)
                      [SNew(SBox)
                           .WidthOverride(42.0f)
                               [SNew(STextBlock)
                                    .Justification(ETextJustify::Right)
                                    .Text(ValueText)]];
    };

    return SNew(SVerticalBox)

        + SVerticalBox::Slot()
              .AutoHeight()
              .Padding(0.0f, 0.0f, 0.0f, 5.0f)
                  [BuildAmountSlider(
                      LOCTEXT("PreviewAbsorbedWaterLabel", "Absorbed Water"),
                      TAttribute<float>::Create(TAttribute<float>::FGetter::CreateSP(this, &SWetnessProfileEditorPanel::GetPreviewAbsorbedWaterPercent)),
                      FOnFloatValueChanged::CreateSP(this, &SWetnessProfileEditorPanel::HandlePreviewAbsorbedWaterPercentChanged),
                      TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &SWetnessProfileEditorPanel::GetPreviewAbsorbedWaterPercentText)))];
}

TSharedRef<SWidget> SWetnessProfileEditorPanel::BuildPreviewModeSection()
{
    const auto BuildLabel = [](const FText& Text)
    {
        return SNew(SBox)
            .WidthOverride(112.0f)
            [SNew(STextBlock).Text(Text)];
    };

    return SNew(SVerticalBox)
        + SVerticalBox::Slot()
              .AutoHeight()
                  [SNew(SHorizontalBox)
                   + SHorizontalBox::Slot()
                         .AutoWidth()
                         .VAlign(VAlign_Center)
                             [BuildLabel(LOCTEXT("PreviewModeLabel", "Preview Mode"))]
                   + SHorizontalBox::Slot()
                         .FillWidth(1.0f)
                         .VAlign(VAlign_Center)
                             [SNew(SComboBox<TSharedPtr<SWetnessProfileViewport::EPreviewMode>>)
                                  .OptionsSource(&PreviewModeItems)
                                  .InitiallySelectedItem(SelectedPreviewModeItem)
                                  .OnGenerateWidget(this, &SWetnessProfileEditorPanel::GeneratePreviewModeWidget)
                                  .OnSelectionChanged(this, &SWetnessProfileEditorPanel::HandlePreviewModeChanged)
                                  [SNew(STextBlock).Text(this, &SWetnessProfileEditorPanel::GetPreviewModeText)]]]
        + SVerticalBox::Slot()
              .AutoHeight()
              .Padding(0.0f, 6.0f, 0.0f, 0.0f)
                  [SNew(SHorizontalBox)
                   + SHorizontalBox::Slot()
                         .AutoWidth()
                         .VAlign(VAlign_Center)
                             [BuildLabel(LOCTEXT("PreviewBehaviorLabel", "Preview Behavior"))]
                   + SHorizontalBox::Slot()
                         .FillWidth(1.0f)
                         .VAlign(VAlign_Center)
                             [SNew(SComboBox<TSharedPtr<SWetnessProfileViewport::EPreviewBehavior>>)
                                  .OptionsSource(&PreviewBehaviorItems)
                                  .InitiallySelectedItem(SelectedPreviewBehaviorItem)
                                  .OnGenerateWidget(this, &SWetnessProfileEditorPanel::GeneratePreviewBehaviorWidget)
                                  .OnSelectionChanged(this, &SWetnessProfileEditorPanel::HandlePreviewBehaviorChanged)
                                  [SNew(STextBlock).Text(this, &SWetnessProfileEditorPanel::GetPreviewBehaviorText)]]];
}

TSharedRef<SWidget> SWetnessProfileEditorPanel::BuildPreviewSimulationSection()
{
    const auto BuildFilterCheckBox = [](const FText& Label, const TAttribute<ECheckBoxState>& State, const FOnCheckStateChanged& OnChanged)
    {
        return SNew(SCheckBox)
            .IsChecked(State)
            .OnCheckStateChanged(OnChanged)
            [SNew(STextBlock).Text(Label)];
    };

    return SNew(SVerticalBox)
        + SVerticalBox::Slot()
              .AutoHeight()
              .Padding(0.0f, 0.0f, 0.0f, 5.0f)
                  [SNew(SHorizontalBox)
                   + SHorizontalBox::Slot()
                         .AutoWidth()
                         .VAlign(VAlign_Center)
                         .Padding(0.0f, 0.0f, 10.0f, 0.0f)
                             [SNew(STextBlock)
                                  .Text(LOCTEXT("SimulationLayersHeading", "Simulation Layers"))
                                  .Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.BoldFont")))]
                   + SHorizontalBox::Slot()
                         .AutoWidth()
                         .Padding(0.0f, 0.0f, 12.0f, 0.0f)
                             [BuildFilterCheckBox(
                                 LOCTEXT("PreviewAbsorbedLayer", "Absorbed Water"),
                                 TAttribute<ECheckBoxState>::CreateSP(this, &SWetnessProfileEditorPanel::GetAbsorbedLayerCheckState),
                                 FOnCheckStateChanged::CreateSP(this, &SWetnessProfileEditorPanel::HandleAbsorbedLayerCheckStateChanged))]
                   + SHorizontalBox::Slot()
                         .AutoWidth()
                             [BuildFilterCheckBox(
                                 LOCTEXT("PreviewSurfaceLayer", "Surface Water"),
                                 TAttribute<ECheckBoxState>::CreateSP(this, &SWetnessProfileEditorPanel::GetSurfaceLayerCheckState),
                                 FOnCheckStateChanged::CreateSP(this, &SWetnessProfileEditorPanel::HandleSurfaceLayerCheckStateChanged))]]
        + SVerticalBox::Slot()
              .AutoHeight()
              .Padding(0.0f, 0.0f, 0.0f, 8.0f)
                  [SNew(SHorizontalBox)
                   + SHorizontalBox::Slot()
                         .AutoWidth()
                         .VAlign(VAlign_Center)
                         .Padding(0.0f, 0.0f, 10.0f, 0.0f)
                             [SNew(STextBlock)
                                  .Text(LOCTEXT("SurfaceTypesHeading", "Surface Types"))
                                  .Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.BoldFont")))]
                   + SHorizontalBox::Slot()
                         .AutoWidth()
                         .Padding(0.0f, 0.0f, 12.0f, 0.0f)
                             [BuildFilterCheckBox(
                                 LOCTEXT("PreviewDroplet1", "Droplet 1"),
                                 TAttribute<ECheckBoxState>::CreateSP(this, &SWetnessProfileEditorPanel::GetDroplet1CheckState),
                                 FOnCheckStateChanged::CreateSP(this, &SWetnessProfileEditorPanel::HandleDroplet1CheckStateChanged))]
                   + SHorizontalBox::Slot()
                         .AutoWidth()
                             [BuildFilterCheckBox(
                                 LOCTEXT("PreviewDroplet2", "Droplet 2"),
                                 TAttribute<ECheckBoxState>::CreateSP(this, &SWetnessProfileEditorPanel::GetDroplet2CheckState),
                                 FOnCheckStateChanged::CreateSP(this, &SWetnessProfileEditorPanel::HandleDroplet2CheckStateChanged))]]
        + SVerticalBox::Slot()
              .AutoHeight()
                  [SNew(SHorizontalBox)
                   + SHorizontalBox::Slot()
                         .AutoWidth()
                         .Padding(0.0f, 0.0f, 4.0f, 0.0f)
                             [SNew(SBox)
                                  .WidthOverride(28.0f)
                                  .HeightOverride(26.0f)
                                  [SNew(SButton)
                                       .ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton"))
                                       .ToolTipText(this, &SWetnessProfileEditorPanel::GetPlayPauseToolTip)
                                       .OnClicked(this, &SWetnessProfileEditorPanel::HandlePlayPauseClicked)
                                       .ContentPadding(4.0f)
                                       [SNew(SImage)
                                            .Image(this, &SWetnessProfileEditorPanel::GetPlayPauseBrush)]]]
                   + SHorizontalBox::Slot()
                         .AutoWidth()
                         .Padding(0.0f, 0.0f, 12.0f, 0.0f)
                             [SNew(SBox)
                                  .WidthOverride(28.0f)
                                  .HeightOverride(26.0f)
                                  [SNew(SButton)
                                       .ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton"))
                                       .ToolTipText(LOCTEXT("RestartSimulationTooltip", "Restart Simulation"))
                                       .OnClicked(this, &SWetnessProfileEditorPanel::HandleRestartSimulationClicked)
                                       .ContentPadding(4.0f)
                                       [SNew(SImage)
                                            .Image(FAppStyle::GetBrush(TEXT("Icons.Refresh")))]]]
                   + SHorizontalBox::Slot()
                         .AutoWidth()
                         .VAlign(VAlign_Center)
                         .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                             [SNew(STextBlock).Text(LOCTEXT("SimulationScenarioLabel", "Single Splash"))]
                   + SHorizontalBox::Slot()
                         .FillWidth(1.0f)
                   + SHorizontalBox::Slot()
                         .AutoWidth()
                         .VAlign(VAlign_Center)
                         .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                             [SNew(STextBlock).Text(LOCTEXT("PreviewSpeedLabel", "Speed"))]
                   + SHorizontalBox::Slot()
                         .AutoWidth()
                         .VAlign(VAlign_Center)
                             [SNew(SBox)
                                  .WidthOverride(72.0f)
                                  [SNew(SComboBox<TSharedPtr<float>>)
                                       .OptionsSource(&PreviewSpeedItems)
                                       .InitiallySelectedItem(SelectedPreviewSpeedItem)
                                       .OnGenerateWidget(this, &SWetnessProfileEditorPanel::GeneratePreviewSpeedWidget)
                                       .OnSelectionChanged(this, &SWetnessProfileEditorPanel::HandlePreviewSpeedChanged)
                                       [SNew(STextBlock).Text(this, &SWetnessProfileEditorPanel::GetPreviewSpeedText)]]]]
        + SVerticalBox::Slot()
              .AutoHeight()
              .Padding(0.0f, 6.0f, 0.0f, 0.0f)
                  [SNew(SHorizontalBox)
                   + SHorizontalBox::Slot()
                         .FillWidth(1.0f)
                         .VAlign(VAlign_Center)
                             [SNew(STextBlock).Text(this, &SWetnessProfileEditorPanel::GetSimulationTimeText)]
                   + SHorizontalBox::Slot()
                         .AutoWidth()
                         .VAlign(VAlign_Center)
                             [SNew(SCheckBox)
                                  .IsChecked(this, &SWetnessProfileEditorPanel::GetLoopCheckState)
                                  .OnCheckStateChanged(this, &SWetnessProfileEditorPanel::HandleLoopCheckStateChanged)
                                  [SNew(STextBlock).Text(LOCTEXT("SimulationLoopLabel", "Loop"))]]];
}

TSharedRef<SWidget> SWetnessProfileEditorPanel::BuildPreviewDetailSizeSection()
{
    const auto BuildDetailSlider = [](const FText& Label, const TAttribute<float>& Value, const FOnFloatValueChanged& OnChanged, const TAttribute<FText>& ValueText)
    {
        return SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
                  .AutoWidth()
                  .VAlign(VAlign_Center)
                      [SNew(SBox)
                           .WidthOverride(112.0f)
                               [SNew(STextBlock).Text(Label)]]
            + SHorizontalBox::Slot()
                  .FillWidth(1.0f)
                  .VAlign(VAlign_Center)
                      [SNew(SSlider)
                           .MinValue(0.0f)
                           .MaxValue(4.0f)
                           .Value(Value)
                           .OnValueChanged(OnChanged)]
            + SHorizontalBox::Slot()
                  .AutoWidth()
                  .VAlign(VAlign_Center)
                  .Padding(8.0f, 0.0f, 0.0f, 0.0f)
                      [SNew(SBox)
                           .WidthOverride(42.0f)
                               [SNew(STextBlock)
                                    .Justification(ETextJustify::Right)
                                    .Text(ValueText)]];
    };

    return SNew(SVerticalBox)

        + SVerticalBox::Slot()
              .AutoHeight()
              .Padding(0.0f, 0.0f, 0.0f, 2.0f)
                  [SNew(STextBlock)
                       .Text(LOCTEXT("PreviewDetailSizeHeading", "Droplet Detail Size"))
                       .Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.BoldFont")))]

        + SVerticalBox::Slot()
              .AutoHeight()
              .Padding(0.0f, 0.0f, 0.0f, 6.0f)
                  [SNew(STextBlock)
                       .Text(LOCTEXT("PreviewDetailSizeHint", "Preview metadata only. This does not change the Wetness Profile render settings; it is stored on the asset package to restore this editor preview's droplet normal/mask scale."))
                       .AutoWrapText(true)
                       .ColorAndOpacity(FSlateColor::UseSubduedForeground())]

        + SVerticalBox::Slot()
              .AutoHeight()
                  [SNew(SBox)
                       .Visibility(this, &SWetnessProfileEditorPanel::GetDroplet1ControlsVisibility)
                           [BuildDetailSlider(
                               LOCTEXT("PreviewDroplet1DetailSizeLabel", "Droplet1 Detail Size"),
                               TAttribute<float>::Create(TAttribute<float>::FGetter::CreateSP(this, &SWetnessProfileEditorPanel::GetPreviewDroplet1DetailSize)),
                               FOnFloatValueChanged::CreateSP(this, &SWetnessProfileEditorPanel::HandlePreviewDroplet1DetailSizeChanged),
                               TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &SWetnessProfileEditorPanel::GetPreviewDroplet1DetailSizeText)))]]

        + SVerticalBox::Slot()
              .AutoHeight()
              .Padding(0.0f, 6.0f, 0.0f, 0.0f)
                  [SNew(SBox)
                       .Visibility(this, &SWetnessProfileEditorPanel::GetDroplet2ControlsVisibility)
                           [BuildDetailSlider(
                               LOCTEXT("PreviewDroplet2DetailSizeLabel", "Droplet2 Detail Size"),
                               TAttribute<float>::Create(TAttribute<float>::FGetter::CreateSP(this, &SWetnessProfileEditorPanel::GetPreviewDroplet2DetailSize)),
                               FOnFloatValueChanged::CreateSP(this, &SWetnessProfileEditorPanel::HandlePreviewDroplet2DetailSizeChanged),
                               TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &SWetnessProfileEditorPanel::GetPreviewDroplet2DetailSizeText)))]];
}

TSharedRef<SWidget> SWetnessProfileEditorPanel::GeneratePreviewModeWidget(
    TSharedPtr<SWetnessProfileViewport::EPreviewMode> InMode) const
{
    return SNew(STextBlock)
        .Text(InMode.IsValid() ? GetPreviewModeText(*InMode) : FText::GetEmpty());
}

void SWetnessProfileEditorPanel::HandlePreviewModeChanged(
    TSharedPtr<SWetnessProfileViewport::EPreviewMode> InMode,
    ESelectInfo::Type /*SelectInfo*/)
{
    if (!InMode.IsValid())
    {
        return;
    }

    SelectedPreviewModeItem = InMode;
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->SetPreviewMode(*InMode);
    }
}

FText SWetnessProfileEditorPanel::GetPreviewModeText() const
{
    return SelectedPreviewModeItem.IsValid()
               ? GetPreviewModeText(*SelectedPreviewModeItem)
               : FText::GetEmpty();
}

FText SWetnessProfileEditorPanel::GetPreviewModeText(const SWetnessProfileViewport::EPreviewMode InMode) const
{
    switch (InMode)
    {
    case SWetnessProfileViewport::EPreviewMode::Lit:
        return LOCTEXT("PreviewModeLit", "Lit");
    case SWetnessProfileViewport::EPreviewMode::Absorbed:
        return LOCTEXT("PreviewModeAbsorbed", "Absorbed");
    case SWetnessProfileViewport::EPreviewMode::SurfaceCoverage:
        return LOCTEXT("PreviewModeSurfaceCoverage", "Surface Coverage");
    case SWetnessProfileViewport::EPreviewMode::FinalDropletCoverage:
        return LOCTEXT("PreviewModeFinalDropletCoverage", "Final Droplet Coverage");
    case SWetnessProfileViewport::EPreviewMode::DropletNormal:
        return LOCTEXT("PreviewModeDropletNormal", "Droplet Normal");
    case SWetnessProfileViewport::EPreviewMode::DropletStampTest:
        return LOCTEXT("PreviewModeDropletStampTest", "Droplet Stamp Test");
    default:
        return FText::GetEmpty();
    }
}

TSharedRef<SWidget> SWetnessProfileEditorPanel::GeneratePreviewBehaviorWidget(
    TSharedPtr<SWetnessProfileViewport::EPreviewBehavior> InBehavior) const
{
    return SNew(STextBlock)
        .Text(InBehavior.IsValid() ? GetPreviewBehaviorText(*InBehavior) : FText::GetEmpty());
}

void SWetnessProfileEditorPanel::HandlePreviewBehaviorChanged(
    TSharedPtr<SWetnessProfileViewport::EPreviewBehavior> InBehavior,
    ESelectInfo::Type /*SelectInfo*/)
{
    if (!InBehavior.IsValid())
    {
        return;
    }
    SelectedPreviewBehaviorItem = InBehavior;
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->SetPreviewBehavior(*InBehavior);
    }
}

FText SWetnessProfileEditorPanel::GetPreviewBehaviorText() const
{
    return SelectedPreviewBehaviorItem.IsValid()
        ? GetPreviewBehaviorText(*SelectedPreviewBehaviorItem)
        : FText::GetEmpty();
}

FText SWetnessProfileEditorPanel::GetPreviewBehaviorText(
    const SWetnessProfileViewport::EPreviewBehavior InBehavior) const
{
    return InBehavior == SWetnessProfileViewport::EPreviewBehavior::Simulation
        ? LOCTEXT("PreviewBehaviorSimulation", "Simulation")
        : LOCTEXT("PreviewBehaviorManual", "Manual");
}

EVisibility SWetnessProfileEditorPanel::GetManualControlsVisibility() const
{
    return SelectedPreviewBehaviorItem.IsValid() &&
        *SelectedPreviewBehaviorItem == SWetnessProfileViewport::EPreviewBehavior::Manual
        ? EVisibility::Visible
        : EVisibility::Collapsed;
}

EVisibility SWetnessProfileEditorPanel::GetSimulationControlsVisibility() const
{
    return SelectedPreviewBehaviorItem.IsValid() &&
        *SelectedPreviewBehaviorItem == SWetnessProfileViewport::EPreviewBehavior::Simulation
        ? EVisibility::Visible
        : EVisibility::Collapsed;
}

FReply SWetnessProfileEditorPanel::HandlePlayPauseClicked()
{
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->SetPreviewAnimationEnabled(!PreviewViewport->IsPreviewAnimationEnabled());
    }
    return FReply::Handled();
}

FReply SWetnessProfileEditorPanel::HandleRestartSimulationClicked()
{
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->RestartPreviewSimulation();
    }
    return FReply::Handled();
}

const FSlateBrush* SWetnessProfileEditorPanel::GetPlayPauseBrush() const
{
    return FAppStyle::GetBrush(
        PreviewViewport.IsValid() && PreviewViewport->IsPreviewAnimationEnabled()
            ? TEXT("Icons.Pause")
            : TEXT("Icons.Play"));
}

FText SWetnessProfileEditorPanel::GetPlayPauseToolTip() const
{
    return PreviewViewport.IsValid() && PreviewViewport->IsPreviewAnimationEnabled()
        ? LOCTEXT("PauseSimulationTooltip", "Pause Simulation")
        : LOCTEXT("PlaySimulationTooltip", "Play Simulation");
}

ECheckBoxState SWetnessProfileEditorPanel::GetAbsorbedLayerCheckState() const
{
    return bPreviewAbsorbedLayerEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SWetnessProfileEditorPanel::HandleAbsorbedLayerCheckStateChanged(const ECheckBoxState NewState)
{
    bPreviewAbsorbedLayerEnabled = NewState == ECheckBoxState::Checked;
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->SetPreviewSimulationLayers(
            bPreviewAbsorbedLayerEnabled,
            bPreviewSurfaceLayerEnabled);
    }
}

ECheckBoxState SWetnessProfileEditorPanel::GetSurfaceLayerCheckState() const
{
    return bPreviewSurfaceLayerEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SWetnessProfileEditorPanel::HandleSurfaceLayerCheckStateChanged(const ECheckBoxState NewState)
{
    bPreviewSurfaceLayerEnabled = NewState == ECheckBoxState::Checked;
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->SetPreviewSimulationLayers(
            bPreviewAbsorbedLayerEnabled,
            bPreviewSurfaceLayerEnabled);
    }
}

ECheckBoxState SWetnessProfileEditorPanel::GetDroplet1CheckState() const
{
    return bPreviewDroplet1Enabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SWetnessProfileEditorPanel::HandleDroplet1CheckStateChanged(const ECheckBoxState NewState)
{
    bPreviewDroplet1Enabled = NewState == ECheckBoxState::Checked;
#if WITH_EDITORONLY_DATA
    if (UWetnessProfile* Profile = WetnessProfile.Get())
    {
        Profile->bEditorShowDroplet1 = bPreviewDroplet1Enabled;
        NotifyPreviewDisplayFilterChanged(
            *Profile,
            GET_MEMBER_NAME_CHECKED(UWetnessProfile, bEditorShowDroplet1));
    }
#endif
    RefreshDetailsViews();
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->SetPreviewDropletVisibility(
            bPreviewDroplet1Enabled,
            bPreviewDroplet2Enabled);
    }
}

ECheckBoxState SWetnessProfileEditorPanel::GetDroplet2CheckState() const
{
    return bPreviewDroplet2Enabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SWetnessProfileEditorPanel::HandleDroplet2CheckStateChanged(const ECheckBoxState NewState)
{
    bPreviewDroplet2Enabled = NewState == ECheckBoxState::Checked;
#if WITH_EDITORONLY_DATA
    if (UWetnessProfile* Profile = WetnessProfile.Get())
    {
        Profile->bEditorShowDroplet2 = bPreviewDroplet2Enabled;
        NotifyPreviewDisplayFilterChanged(
            *Profile,
            GET_MEMBER_NAME_CHECKED(UWetnessProfile, bEditorShowDroplet2));
    }
#endif
    RefreshDetailsViews();
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->SetPreviewDropletVisibility(
            bPreviewDroplet1Enabled,
            bPreviewDroplet2Enabled);
    }
}

EVisibility SWetnessProfileEditorPanel::GetDroplet1ControlsVisibility() const
{
    return bPreviewDroplet1Enabled ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SWetnessProfileEditorPanel::GetDroplet2ControlsVisibility() const
{
    return bPreviewDroplet2Enabled ? EVisibility::Visible : EVisibility::Collapsed;
}

FText SWetnessProfileEditorPanel::GetSimulationTimeText() const
{
    const float Time = PreviewViewport.IsValid() ? PreviewViewport->GetPreviewAnimationTime() : 0.0f;
    FNumberFormattingOptions Options;
    Options.MinimumFractionalDigits = 1;
    Options.MaximumFractionalDigits = 1;
    return FText::Format(
        LOCTEXT("SimulationTimeFormat", "Simulation Time  {0} s / {1} s"),
        FText::AsNumber(Time, &Options),
        FText::AsNumber(SWetnessProfileViewport::GetPreviewLoopDuration(), &Options));
}

TSharedRef<SWidget> SWetnessProfileEditorPanel::GeneratePreviewSpeedWidget(TSharedPtr<float> InSpeed) const
{
    const float Speed = InSpeed.IsValid() ? *InSpeed : 1.0f;
    return SNew(STextBlock).Text(FText::Format(LOCTEXT("PreviewSpeedItemFormat", "{0}x"), FText::AsNumber(Speed)));
}

void SWetnessProfileEditorPanel::HandlePreviewSpeedChanged(
    TSharedPtr<float> InSpeed,
    ESelectInfo::Type /*SelectInfo*/)
{
    if (!InSpeed.IsValid())
    {
        return;
    }
    SelectedPreviewSpeedItem = InSpeed;
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->SetPreviewAnimationSpeed(*InSpeed);
    }
}

FText SWetnessProfileEditorPanel::GetPreviewSpeedText() const
{
    const float Speed = SelectedPreviewSpeedItem.IsValid() ? *SelectedPreviewSpeedItem : 1.0f;
    return FText::Format(LOCTEXT("PreviewSpeedValueFormat", "{0}x"), FText::AsNumber(Speed));
}

ECheckBoxState SWetnessProfileEditorPanel::GetLoopCheckState() const
{
    return PreviewViewport.IsValid() && PreviewViewport->IsPreviewLoopEnabled()
        ? ECheckBoxState::Checked
        : ECheckBoxState::Unchecked;
}

void SWetnessProfileEditorPanel::HandleLoopCheckStateChanged(const ECheckBoxState NewState)
{
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->SetPreviewLoopEnabled(NewState == ECheckBoxState::Checked);
    }
}

FString SWetnessProfileEditorPanel::GetCurrentPreviewMeshObjectPath() const
{
    USkeletalMesh* Mesh = PreviewViewport.IsValid()
                              ? PreviewViewport->GetDisplayedPreviewSkeletalMesh()
                              : nullptr;
    return Mesh != nullptr ? FSoftObjectPath(Mesh).ToString() : FString();
}

void SWetnessProfileEditorPanel::HandleCurrentPreviewMeshChanged(const FAssetData& AssetData)
{
    if (!PreviewViewport.IsValid())
    {
        return;
    }

    PreviewViewport->SetPreviewSkeletalMeshOverride(Cast<USkeletalMesh>(AssetData.GetAsset()));
}

FReply SWetnessProfileEditorPanel::HandleUseReferenceMeshClicked()
{
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->ClearPreviewSkeletalMeshOverride();
    }
    return FReply::Handled();
}

FReply SWetnessProfileEditorPanel::HandleSaveCurrentMeshAsReferenceClicked()
{
#if WITH_EDITORONLY_DATA
    UWetnessProfile* Profile = WetnessProfile.Get();
    USkeletalMesh* CurrentMesh = PreviewViewport.IsValid()
                                     ? PreviewViewport->GetDisplayedPreviewSkeletalMesh()
                                     : nullptr;
    if (Profile != nullptr)
    {
        const FScopedTransaction Transaction(LOCTEXT("SetCurrentPreviewMeshAsDefault", "Set Temporary Preview Mesh as Default"));
        Profile->Modify();
        Profile->PreviewSkeletalMesh = CurrentMesh;
        PersistPreviewDetailSizes();
        Profile->MarkPackageDirty();
        RefreshDetailsViews();
    }
#endif
    return FReply::Handled();
}

FReply SWetnessProfileEditorPanel::HandleUseSphereMeshClicked()
{
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->UseSpherePreview();
    }
    return FReply::Handled();
}

float SWetnessProfileEditorPanel::GetPreviewAbsorbedWaterPercent() const
{
    return PreviewViewport.IsValid()
               ? PreviewViewport->GetPreviewAbsorbedWater() * 100.0f
               : 50.0f;
}

void SWetnessProfileEditorPanel::HandlePreviewAbsorbedWaterPercentChanged(const float InPercent)
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

float SWetnessProfileEditorPanel::GetPreviewDroplet1DetailSize() const
{
    return PreviewDroplet1DetailSize;
}

void SWetnessProfileEditorPanel::HandlePreviewDroplet1DetailSizeChanged(const float InValue)
{
    const float NewValue = FMath::Clamp(InValue, 0.0f, 4.0f);
    if (FMath::IsNearlyEqual(NewValue, PreviewDroplet1DetailSize))
    {
        return;
    }

    PreviewDroplet1DetailSize = NewValue;
    PersistPreviewDetailSizes();
    ApplyPreviewSettingsToViewport();
}

FText SWetnessProfileEditorPanel::GetPreviewDroplet1DetailSizeText() const
{
    return FormatPreviewFloat(PreviewDroplet1DetailSize);
}

float SWetnessProfileEditorPanel::GetPreviewDroplet2DetailSize() const
{
    return PreviewDroplet2DetailSize;
}

void SWetnessProfileEditorPanel::HandlePreviewDroplet2DetailSizeChanged(const float InValue)
{
    const float NewValue = FMath::Clamp(InValue, 0.0f, 4.0f);
    if (FMath::IsNearlyEqual(NewValue, PreviewDroplet2DetailSize))
    {
        return;
    }

    PreviewDroplet2DetailSize = NewValue;
    PersistPreviewDetailSizes();
    ApplyPreviewSettingsToViewport();
}

FText SWetnessProfileEditorPanel::GetPreviewDroplet2DetailSizeText() const
{
    return FormatPreviewFloat(PreviewDroplet2DetailSize);
}

void SWetnessProfileEditorPanel::LoadPersistedPreviewSettings()
{
    const UWetnessProfile* Profile = WetnessProfile.Get();
    PreviewDroplet1DetailSize = FMath::Clamp(
        ReadFloatMetadata(Profile, PreviewDroplet1DetailSizeMetadataKey, 1.0f),
        0.0f,
        4.0f);
    PreviewDroplet2DetailSize = FMath::Clamp(
        ReadFloatMetadata(Profile, PreviewDroplet2DetailSizeMetadataKey, 1.0f),
        0.0f,
        4.0f);
#if WITH_EDITORONLY_DATA
    if (Profile != nullptr)
    {
        bPreviewDroplet1Enabled = Profile->bEditorShowDroplet1;
        bPreviewDroplet2Enabled = Profile->bEditorShowDroplet2;
    }
#endif
}

void SWetnessProfileEditorPanel::PersistPreviewDetailSizes()
{
    UWetnessProfile* Profile = WetnessProfile.Get();
    if (Profile == nullptr)
    {
        return;
    }

    Profile->Modify();
    WriteFloatMetadata(Profile, PreviewDroplet1DetailSizeMetadataKey, PreviewDroplet1DetailSize);
    WriteFloatMetadata(Profile, PreviewDroplet2DetailSizeMetadataKey, PreviewDroplet2DetailSize);
    Profile->MarkPackageDirty();
}

void SWetnessProfileEditorPanel::ApplyPreviewSettingsToViewport()
{
    if (!PreviewViewport.IsValid())
    {
        return;
    }

    PreviewViewport->SetPreviewDropletDetailSizes(PreviewDroplet1DetailSize, PreviewDroplet2DetailSize);
    PreviewViewport->SetPreviewSimulationLayers(
        bPreviewAbsorbedLayerEnabled,
        bPreviewSurfaceLayerEnabled);
    PreviewViewport->SetPreviewDropletVisibility(
        bPreviewDroplet1Enabled,
        bPreviewDroplet2Enabled);
    PreviewViewport->SetPreviewSurfaceWater(1.0f);
    if (SelectedPreviewModeItem.IsValid())
    {
        PreviewViewport->SetPreviewMode(*SelectedPreviewModeItem);
    }
    if (SelectedPreviewBehaviorItem.IsValid())
    {
        PreviewViewport->SetPreviewBehavior(*SelectedPreviewBehaviorItem);
    }
    PreviewViewport->SetPreviewAnimationEnabled(true);
    PreviewViewport->SetPreviewAnimationSpeed(
        SelectedPreviewSpeedItem.IsValid() ? *SelectedPreviewSpeedItem : 1.0f);
    PreviewViewport->SetPreviewLoopEnabled(true);
}

#undef LOCTEXT_NAMESPACE
