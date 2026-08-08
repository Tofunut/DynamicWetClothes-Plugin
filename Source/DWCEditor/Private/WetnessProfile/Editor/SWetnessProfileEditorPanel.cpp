// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "SWetnessProfileEditorPanel.h"
#include "Utility/DWCLog.h"

#include "AssetRegistry/AssetData.h"
#include "Core/DWCEditorUtils.h"
#include "Core/DWCEditorStyle.h"
#include "DataAssets/WetnessProfile.h"
#include "Engine/SkeletalMesh.h"
#include "IDetailsView.h"
#include "PropertyCustomizationHelpers.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Styling/StyleColors.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "WetnessProfile/Editor/WetnessProfileEditorPolicy.h"
#include "WetnessProfile/Viewport/SWetnessProfileViewport.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "WetnessProfileEditorPanel"

namespace
{
    const FName PreviewDroplet1DetailSizeMetadataKey(TEXT("DWC.Preview.DropletDetailSize"));
    const FName PreviewDroplet2DetailSizeMetadataKey(TEXT("DWC.Preview.Droplet2DetailSize"));

    float ReadFloatMetadata(
        const UWetnessProfile* Profile,
        const FName            Key,
        const float            DefaultValue)
    {
        UPackage* Package = Profile != nullptr ? Profile->GetOutermost() : nullptr;
        if (Package == nullptr)
        {
            return DefaultValue;
        }

        FMetaData&    MetaData = Package->GetMetaData();
        const FString StoredValue = MetaData.GetValue(Profile, Key);
        if (StoredValue.IsEmpty())
        {
            return DefaultValue;
        }

        return FCString::Atof(*StoredValue);
    }

    void WriteFloatMetadata(
        UWetnessProfile* Profile,
        const FName      Key,
        const float      Value)
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
} // namespace

void SWetnessProfileEditorPanel::Construct(const FArguments& InArgs)
{
    WetnessProfile = InArgs._WetnessProfile;
    AbsorbedDetailsView = InArgs._AbsorbedDetailsView;
    SurfaceDetailsView = InArgs._SurfaceDetailsView;
    HasWaterChannelSelectionAttribute = InArgs._HasWaterChannelSelection;
    IsSurfaceWaterSelectedAttribute = InArgs._IsSurfaceWaterSelected;
    LoadPersistedPreviewSettings();
    // Keep the public preview view intentionally small for release. The other
    // internal debug modes remain available in code but are not exposed here.
    PreviewModeItems = {
        MakeShared<SWetnessProfileViewport::EPreviewMode>(SWetnessProfileViewport::EPreviewMode::Lit),
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

    ChildSlot
        [SNew(SVerticalBox)

         + SVerticalBox::Slot()
               .FillHeight(1.0f)
               .Padding(8.0f, 8.0f, 8.0f, 0.0f)
                   [SNew(SBorder)
                        .Padding(0.0f)
                        .BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
                            [SAssignNew(PreviewViewport, SWetnessProfileViewport)
                                 .WetnessProfile(WetnessProfile.Get())]]

         + SVerticalBox::Slot()
               .AutoHeight()
               .Padding(8.0f, 8.0f, 8.0f, 8.0f)
                   [BuildPreviewControlsSection()]];

    ApplyPreviewSettingsToViewport();
    RefreshFromProfile();
}

void SWetnessProfileEditorPanel::RefreshFromProfile()
{
    LoadPersistedPreviewSettings();
    if (PreviewViewport.IsValid())
    {
        ApplyPreviewSettingsToViewport();
        PreviewViewport->RefreshFromProfile();
    }
}

FReply SWetnessProfileEditorPanel::HandleSaveClicked()
{
    TArray<FString> ClampedValues;
    if (FWetnessProfileEditorPolicy::SanitizeProfile(WetnessProfile.Get(), &ClampedValues))
    {
        UE_LOG(
            LogDWC,
            Warning,
            TEXT("Wetness Profile values were clamped before save:\n- %s"),
            *FString::Join(ClampedValues, TEXT("\n- ")));
        RefreshDetailsViews();
        RefreshFromProfile();
    }

    DWCEditorUtils::SaveAsset(WetnessProfile.Get());
#if WITH_EDITORONLY_DATA
    if (UWetnessProfile* Profile = WetnessProfile.Get();
        Profile != nullptr && Profile->GetOutermost() != nullptr && !Profile->GetOutermost()->IsDirty())
    {
        Profile->CaptureEditorSavedParametersSnapshot();
        RefreshDetailsViews();
    }
#endif
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
        .Padding(FMargin(8.0f, 6.0f))
        .BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
            [SNew(SHorizontalBox)

             + SHorizontalBox::Slot()
                   .AutoWidth()
                   .VAlign(VAlign_Center)
                   .Padding(0.0f, 0.0f, 10.0f, 0.0f)
                       [SNew(STextBlock)
                            .Text(LOCTEXT("PreviewHeading", "Preview"))
                            .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 15))]

             + SHorizontalBox::Slot()
                   .FillWidth(1.0f)
                   .VAlign(VAlign_Center)
                   .Padding(0.0f, 0.0f, 8.0f, 0.0f)
                       [SNew(SObjectPropertyEntryBox)
                            .AllowedClass(USkeletalMesh::StaticClass())
                            .AllowClear(true)
                            .ObjectPath(this, &SWetnessProfileEditorPanel::GetCurrentPreviewMeshObjectPath)
                            .OnObjectChanged(this, &SWetnessProfileEditorPanel::HandleCurrentPreviewMeshChanged)]

    ];
}

TSharedRef<SWidget> SWetnessProfileEditorPanel::BuildPreviewControlsSection()
{
    return SNew(SBox)
        .MinDesiredHeight(176.0f)
            [SNew(SBorder)
                 .Padding(FMargin(8.0f))
                 .BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
                     [SNew(SHorizontalBox)

                      + SHorizontalBox::Slot()
                            .FillWidth(0.60f)
                            .Padding(0.0f, 0.0f, 10.0f, 0.0f)
                                [SNew(SVerticalBox)

                                 + SVerticalBox::Slot()
                                       .AutoHeight()
                                           [BuildPreviewModeSection()]

                                 + SVerticalBox::Slot()
                                       .AutoHeight()
                                       .Padding(0.0f, 8.0f)
                                           [SNew(SSeparator)]

                                 + SVerticalBox::Slot()
                                       .FillHeight(1.0f)
                                           [SNew(SWidgetSwitcher)
                                                .WidgetIndex_Lambda([this]()
                                                                    { return SelectedPreviewBehaviorItem.IsValid() &&
                                                                                     *SelectedPreviewBehaviorItem == SWetnessProfileViewport::EPreviewBehavior::Simulation
                                                                                 ? 1
                                                                                 : 0; })

                                            + SWidgetSwitcher::Slot()
                                                  [BuildPreviewWaterSection()]

                                            + SWidgetSwitcher::Slot()
                                                  [BuildPreviewSimulationSection()]]]

                      + SHorizontalBox::Slot()
                            .AutoWidth()
                            .Padding(0.0f, 0.0f, 10.0f, 0.0f)
                                [SNew(SSeparator)
                                     .Orientation(Orient_Vertical)]

                      + SHorizontalBox::Slot()
                            .FillWidth(0.40f)
                                [BuildPreviewSettingsSection()]]];
}

TSharedRef<SWidget> SWetnessProfileEditorPanel::BuildPreviewWaterSection()
{
    const auto BuildSliderRow = [](
                                    const FText&                Label,
                                    const TAttribute<float>&    Value,
                                    const FOnFloatValueChanged& OnChanged,
                                    const TAttribute<FText>&    ValueText,
                                    const TAttribute<bool>&     IsEnabled,
                                    const float                 MaxValue)
    {
        return SNew(SBox)
            .MinDesiredHeight(34.0f)
            .IsEnabled(IsEnabled)
                [SNew(SHorizontalBox)

                 + SHorizontalBox::Slot()
                       .AutoWidth()
                       .VAlign(VAlign_Center)
                           [SNew(SBox)
                                .WidthOverride(118.0f)
                                    [SNew(STextBlock)
                                         .Text(Label)
                                         .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 10))]]

                 + SHorizontalBox::Slot()
                       .FillWidth(1.0f)
                       .VAlign(VAlign_Center)
                       .Padding(8.0f, 0.0f)
                           [SNew(SSlider)
                                .MinValue(0.0f)
                                .MaxValue(MaxValue)
                                .Value(Value)
                                .OnValueChanged(OnChanged)]

                 + SHorizontalBox::Slot()
                       .AutoWidth()
                       .VAlign(VAlign_Center)
                           [SNew(SBox)
                                .WidthOverride(52.0f)
                                    [SNew(STextBlock)
                                         .Justification(ETextJustify::Right)
                                         .Text(ValueText)
                                         .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 10))]]];
    };

    return SNew(SVerticalBox)

           + SVerticalBox::Slot()
                 .AutoHeight()
                     [BuildSliderRow(
                         LOCTEXT("PreviewAbsorbedAmountLabel", "Absorbed Amount"),
                         TAttribute<float>::Create(TAttribute<float>::FGetter::CreateSP(
                             this, &SWetnessProfileEditorPanel::GetPreviewAbsorbedWaterPercent)),
                         FOnFloatValueChanged::CreateSP(
                             this, &SWetnessProfileEditorPanel::HandlePreviewAbsorbedWaterPercentChanged),
                         TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(
                             this, &SWetnessProfileEditorPanel::GetPreviewAbsorbedWaterPercentText)),
                         TAttribute<bool>::CreateLambda([this]()
                                                        {
                    const UWetnessProfile* Profile = WetnessProfile.Get();
                    return Profile != nullptr && Profile->Parameters.AbsorbedWetness.bEnabled; }),
                         100.0f)]

           + SVerticalBox::Slot()
                 .AutoHeight()
                 .Padding(0.0f, 4.0f, 0.0f, 0.0f)
                     [BuildSliderRow(
                         LOCTEXT("PreviewSurfaceAmountLabel", "Surface Amount"),
                         TAttribute<float>::Create(TAttribute<float>::FGetter::CreateSP(
                             this, &SWetnessProfileEditorPanel::GetPreviewSurfaceWaterPercent)),
                         FOnFloatValueChanged::CreateSP(
                             this, &SWetnessProfileEditorPanel::HandlePreviewSurfaceWaterPercentChanged),
                         TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(
                             this, &SWetnessProfileEditorPanel::GetPreviewSurfaceWaterPercentText)),
                         TAttribute<bool>::CreateLambda([this]()
                                                        {
                    const UWetnessProfile* Profile = WetnessProfile.Get();
                    return Profile != nullptr && Profile->Parameters.SurfaceWater.bEnabled; }),
                         100.0f)];
}

TSharedRef<SWidget> SWetnessProfileEditorPanel::BuildPreviewModeSection()
{
    const auto SelectBehavior = [this](
                                    const SWetnessProfileViewport::EPreviewBehavior Behavior)
    {
        for (const TSharedPtr<SWetnessProfileViewport::EPreviewBehavior>& Item :
             PreviewBehaviorItems)
        {
            if (Item.IsValid() && *Item == Behavior)
            {
                HandlePreviewBehaviorChanged(Item, ESelectInfo::Direct);
                break;
            }
        }
    };

    const auto BuildModeButton = [this, SelectBehavior](
                                     const SWetnessProfileViewport::EPreviewBehavior Behavior,
                                     const FText&                                    Label,
                                     const FText&                                    Tooltip)
    {
        const FLinearColor SelectedButtonBlue(0.0f, 0.45f, 1.0f, 1.0f);
        return SNew(SButton)
            .ButtonStyle(FAppStyle::Get(), TEXT("NoBorder"))
            .ContentPadding(0.0f)
            .ToolTipText(Tooltip)
            .OnClicked_Lambda([SelectBehavior, Behavior]()
                              {
                SelectBehavior(Behavior);
                return FReply::Handled(); })
                [SNew(SBorder)
                     .BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
                     .BorderBackgroundColor_Lambda([this, Behavior]()
                                                   {
                    const bool bSelected =
                        SelectedPreviewBehaviorItem.IsValid() &&
                        *SelectedPreviewBehaviorItem == Behavior;
                    return bSelected
                        ? FStyleColors::Primary.GetSpecifiedColor()
                        : FStyleColors::Hover.GetSpecifiedColor(); })
                     .Padding(1.0f)
                         [SNew(SBorder)
                              .BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
                              .BorderBackgroundColor_Lambda([this, Behavior, SelectedButtonBlue]()
                                                            {
                        const bool bSelected =
                            SelectedPreviewBehaviorItem.IsValid() &&
                            *SelectedPreviewBehaviorItem == Behavior;
                        return bSelected
                            ? SelectedButtonBlue
                            : FStyleColors::Input.GetSpecifiedColor(); })
                              .Padding(FMargin(12.0f, 0.0f))
                                  [SNew(SBox)
                                       .HeightOverride(38.0f)
                                       .HAlign(HAlign_Center)
                                       .VAlign(VAlign_Center)
                                           [SNew(STextBlock)
                                                .Text(Label)
                                                .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 12))
                                                .ColorAndOpacity_Lambda([this, Behavior]()
                                                                        {
                                const bool bSelected =
                                    SelectedPreviewBehaviorItem.IsValid() &&
                                    *SelectedPreviewBehaviorItem == Behavior;
                                return bSelected
                                    ? FSlateColor(FLinearColor::White)
                                    : FSlateColor::UseForeground(); })
                                                .Justification(ETextJustify::Center)]]]];
    };

    return SNew(SVerticalBox)

           + SVerticalBox::Slot()
                 .AutoHeight()
                     [SNew(SHorizontalBox)

                      + SHorizontalBox::Slot()
                            .FillWidth(1.0f)
                            .Padding(0.0f, 0.0f, 3.0f, 0.0f)
                                [BuildModeButton(
                                    SWetnessProfileViewport::EPreviewBehavior::Manual,
                                    LOCTEXT("PreviewBehaviorStaticSegment", "Static"),
                                    LOCTEXT(
                                        "PreviewBehaviorStaticTooltip",
                                        "Adjust fixed wetness values without advancing the simulation."))]

                      + SHorizontalBox::Slot()
                            .FillWidth(1.0f)
                            .Padding(3.0f, 0.0f, 0.0f, 0.0f)
                                [BuildModeButton(
                                    SWetnessProfileViewport::EPreviewBehavior::Simulation,
                                    LOCTEXT("PreviewBehaviorSimulationSegment", "Simulation"),
                                    LOCTEXT(
                                        "PreviewBehaviorSimulationTooltip",
                                        "Add water and preview spreading and drying."))]

                      + SHorizontalBox::Slot()
                            .AutoWidth()
                            .VAlign(VAlign_Center)
                            .Padding(10.0f, 0.0f, 0.0f, 0.0f)
                                [SNew(SHorizontalBox) + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 6.0f, 0.0f)[SNew(STextBlock).Text(LOCTEXT("PreviewViewLabel", "View")).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 10))] + SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(138.0f)[SNew(SComboBox<TSharedPtr<SWetnessProfileViewport::EPreviewMode>>).OptionsSource(&PreviewModeItems).InitiallySelectedItem(SelectedPreviewModeItem).OnGenerateWidget(this, &SWetnessProfileEditorPanel::GeneratePreviewModeWidget).OnSelectionChanged(this, &SWetnessProfileEditorPanel::HandlePreviewModeChanged)[SNew(STextBlock).Text(this, &SWetnessProfileEditorPanel::GetPreviewModeText)]]] + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.0f, 0.0f, 0.0f, 0.0f)[SNew(SComboButton).ContentPadding(FMargin(8.0f, 3.0f)).ToolTipText(LOCTEXT("PreviewDisplayMenuTooltip", "Preview-only visibility filters. These options do not change runtime Wetness Profile settings.")).OnGetMenuContent(this, &SWetnessProfileEditorPanel::BuildPreviewViewMenu).ButtonContent()[SNew(STextBlock).Text(LOCTEXT("PreviewDisplayMenuLabel", "Display"))]]]];
}

TSharedRef<SWidget> SWetnessProfileEditorPanel::BuildPreviewSimulationSection()
{
    return SNew(SVerticalBox)

           + SVerticalBox::Slot()
                 .AutoHeight()
                     [SNew(SHorizontalBox)

                      + SHorizontalBox::Slot()
                            .AutoWidth()
                            .VAlign(VAlign_Center)
                                [SNew(SButton)
                                     .IsEnabled(this, &SWetnessProfileEditorPanel::IsSelectedWaterChannelEnabled)
                                     .ToolTipText(LOCTEXT(
                                         "ResetWaterTooltip",
                                         "Clear all preview water. Water is never reset automatically."))
                                     .OnClicked(this, &SWetnessProfileEditorPanel::HandleRestartSimulationClicked)
                                     .ContentPadding(FMargin(12.0f, 6.0f))
                                         [SNew(STextBlock)
                                              .Text(LOCTEXT("ResetWaterLabel", "Reset"))
                                              .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 10))]]]

           + SVerticalBox::Slot()
                 .AutoHeight()
                 .Padding(0.0f, 10.0f, 0.0f, 0.0f)
                     [SNew(SWidgetSwitcher)
                          .WidgetIndex_Lambda([this]()
                                              { return IsSurfaceWaterSelectedAttribute.Get(false) ? 1 : 0; })

                      + SWidgetSwitcher::Slot()
                            [SNew(SHorizontalBox)

                             + SHorizontalBox::Slot()
                                   .AutoWidth()
                                   .VAlign(VAlign_Center)
                                       [SNew(SBox)
                                            .WidthOverride(86.0f)
                                                [SNew(STextBlock)
                                                     .Text(LOCTEXT("PreviewCursorSizeLabel", "Cursor Size"))]]

                             + SHorizontalBox::Slot()
                                   .FillWidth(1.0f)
                                   .VAlign(VAlign_Center)
                                   .Padding(6.0f, 0.0f)
                                       [SNew(SSlider)
                                            .MinValue(0.5f)
                                            .MaxValue(3.0f)
                                            .Value(this, &SWetnessProfileEditorPanel::GetPreviewCursorScale)
                                            .OnValueChanged(this, &SWetnessProfileEditorPanel::HandlePreviewCursorScaleChanged)]

                             + SHorizontalBox::Slot()
                                   .AutoWidth()
                                   .VAlign(VAlign_Center)
                                       [SNew(SBox)
                                            .WidthOverride(42.0f)
                                                [SNew(STextBlock)
                                                     .Text(this, &SWetnessProfileEditorPanel::GetPreviewCursorScaleText)
                                                     .Justification(ETextJustify::Right)]]]

                      + SWidgetSwitcher::Slot()
                            [SNew(SBorder)
                                 .Padding(FMargin(8.0f, 6.0f))
                                 .BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
                                     [SNew(SVerticalBox) + SVerticalBox::Slot().AutoHeight()[SNew(STextBlock).Text(LOCTEXT("SurfaceStampRadiusHint", "Spray size follows Stamp Radius. Adjust Stamp Radius in Surface Water settings.")).AutoWrapText(true)] + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f, 0.0f, 0.0f)[SNew(STextBlock).Text_Lambda([this]()
                                                                                                                                                                                                                                                                                                                                                              {
                            const UWetnessProfile* Profile = WetnessProfile.Get();
                            if (Profile == nullptr)
                            {
                                return FText::GetEmpty();
                            }
                            const FSurfaceWaterProfileParameters& Surface = Profile->Parameters.SurfaceWater;
                            const float Radius = IsSecondaryDropletSelected()
                                ? Surface.DropletFlowRadiusPixels
                                : Surface.DropletRadiusPixels;
                            return FText::Format(
                                LOCTEXT("SurfaceStampRadiusValue", "Current Stamp Radius: {0} px"),
                                FText::AsNumber(FMath::RoundToInt(FMath::Max(0.0f, Radius)))); })
                                                                                                                                                                                                                                                                                                                                     .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 9))
                                                                                                                                                                                                                                                                                                                                     .ColorAndOpacity(FSlateColor::UseSubduedForeground())]]]];
}

TSharedRef<SWidget> SWetnessProfileEditorPanel::BuildPreviewSettingsSection()
{
    const auto BuildDetailSizeRow = [](
                                        const FText&                Label,
                                        const TAttribute<float>&    Value,
                                        const FOnFloatValueChanged& OnChanged,
                                        const TAttribute<FText>&    ValueText)
    {
        return SNew(SHorizontalBox) + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(128.0f)[SNew(STextBlock).Text(Label).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 10)).ToolTipText(LOCTEXT("PreviewDetailSizeTooltip", "Preview-only droplet texture detail size. This does not change runtime stamp size."))]] + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(8.0f, 0.0f)[SNew(SSlider).MinValue(0.0f).MaxValue(4.0f).Value(Value).OnValueChanged(OnChanged)] + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(SBox).WidthOverride(44.0f)[SNew(STextBlock).Justification(ETextJustify::Right).Text(ValueText).Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 10))]];
    };

    return SNew(SBorder)
        .Padding(FMargin(10.0f, 8.0f))
        .BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
            [SNew(SVerticalBox)

             + SVerticalBox::Slot()
                   .AutoHeight()
                       [SNew(STextBlock)
                            .Text(LOCTEXT("PreviewSettingsHeading", "Preview Settings"))
                            .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 14))]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 6.0f, 0.0f, 0.0f)
                       [SNew(SSeparator)]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 3.0f, 0.0f, 0.0f)
                       [SNew(STextBlock)
                            .Text(LOCTEXT("PreviewSettingsSummary", "Viewport-only mesh, droplet detail, and display options."))
                            .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 9))
                            .ColorAndOpacity(FSlateColor::UseSubduedForeground())]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 10.0f, 0.0f, 0.0f)
                       [SNew(SBox)
                            .Visibility(this, &SWetnessProfileEditorPanel::GetSelectedPreviewDetailSizeVisibility)
                                [SNew(SVerticalBox)

                                 + SVerticalBox::Slot()
                                       .AutoHeight()
                                           [SNew(STextBlock)
                                                .Text(LOCTEXT("PreviewSurfaceDetailHeading", "Surface Water Detail"))
                                                .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 10))]

                                 + SVerticalBox::Slot()
                                       .AutoHeight()
                                       .Padding(0.0f, 5.0f, 0.0f, 0.0f)
                                           [BuildDetailSizeRow(
                                               LOCTEXT("PreviewPrimaryDetailSizeLabel", "Primary Detail Size"),
                                               TAttribute<float>::Create(TAttribute<float>::FGetter::CreateSP(
                                                   this, &SWetnessProfileEditorPanel::GetPreviewDroplet1DetailSize)),
                                               FOnFloatValueChanged::CreateSP(
                                                   this, &SWetnessProfileEditorPanel::HandlePreviewDroplet1DetailSizeChanged),
                                               TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(
                                                   this, &SWetnessProfileEditorPanel::GetPreviewDroplet1DetailSizeText)))]

                                 + SVerticalBox::Slot()
                                       .AutoHeight()
                                       .Padding(0.0f, 4.0f, 0.0f, 0.0f)
                                           [BuildDetailSizeRow(
                                               LOCTEXT("PreviewSecondaryDetailSizeLabel", "Secondary Detail Size"),
                                               TAttribute<float>::Create(TAttribute<float>::FGetter::CreateSP(
                                                   this, &SWetnessProfileEditorPanel::GetPreviewDroplet2DetailSize)),
                                               FOnFloatValueChanged::CreateSP(
                                                   this, &SWetnessProfileEditorPanel::HandlePreviewDroplet2DetailSizeChanged),
                                               TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(
                                                   this, &SWetnessProfileEditorPanel::GetPreviewDroplet2DetailSizeText)))]]]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 10.0f, 0.0f, 0.0f)
                       [SNew(SSeparator)]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 9.0f, 0.0f, 0.0f)
                       [SNew(STextBlock)
                            .Text(LOCTEXT("PreviewMeshSettingLabel", "Mesh"))
                            .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 10))]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 3.0f, 0.0f, 0.0f)
                       [SNew(SObjectPropertyEntryBox)
                            .AllowedClass(USkeletalMesh::StaticClass())
                            .AllowClear(true)
                            .ObjectPath(this, &SWetnessProfileEditorPanel::GetCurrentPreviewMeshObjectPath)
                            .OnObjectChanged(this, &SWetnessProfileEditorPanel::HandleCurrentPreviewMeshChanged)]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 6.0f, 0.0f, 0.0f)
                       [SNew(SHorizontalBox)

                        + SHorizontalBox::Slot()
                              .AutoWidth()
                                  [SNew(SCheckBox)
                                       .Style(FAppStyle::Get(), TEXT("RadioButton"))
                                       .IsChecked(this, &SWetnessProfileEditorPanel::GetReferencedMeshSourceState)
                                       .OnCheckStateChanged(this, &SWetnessProfileEditorPanel::HandleReferencedMeshSourceChanged)
                                       .ToolTipText(LOCTEXT(
                                           "ReferencedPreviewMeshSourceTooltip",
                                           "Edit the preview mesh reference stored on this Wetness Profile asset."))
                                           [SNew(STextBlock)
                                                .Text(LOCTEXT("ReferencedPreviewMeshSource", "Asset Reference"))]]

                        + SHorizontalBox::Slot()
                              .AutoWidth()
                              .Padding(12.0f, 0.0f, 0.0f, 0.0f)
                                  [SNew(SCheckBox)
                                       .Style(FAppStyle::Get(), TEXT("RadioButton"))
                                       .IsChecked(this, &SWetnessProfileEditorPanel::GetTemporaryMeshSourceState)
                                       .OnCheckStateChanged(this, &SWetnessProfileEditorPanel::HandleTemporaryMeshSourceChanged)
                                       .ToolTipText(LOCTEXT(
                                           "TemporaryPreviewMeshSourceTooltip",
                                           "Use a preview-only mesh override. This selection is not saved to the Wetness Profile asset."))
                                           [SNew(STextBlock)
                                                .Text(LOCTEXT("TemporaryPreviewMeshSource", "Session Override"))]]]

             + SVerticalBox::Slot()
                   .AutoHeight()
                   .Padding(0.0f, 3.0f, 0.0f, 0.0f)
                       [SNew(STextBlock)
                            .Text_Lambda([this]()
                                         { return bUseTemporaryPreviewMesh
                                                      ? LOCTEXT("TemporaryPreviewMeshHelp", "Session only. The asset is unchanged.")
                                                      : LOCTEXT("ReferencedPreviewMeshHelp", "Saved to this Wetness Profile."); })
                            .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 9))
                            .ColorAndOpacity(FSlateColor::UseSubduedForeground())]

    ];
}

TSharedRef<SWidget> SWetnessProfileEditorPanel::BuildPreviewViewMenu()
{
    const auto BuildLayerToggle = [](const FText&                      Label,
                                     const TAttribute<ECheckBoxState>& State,
                                     const FOnCheckStateChanged&       OnChanged,
                                     const TAttribute<bool>&           IsEnabled,
                                     const TAttribute<FText>&          Tooltip)
    {
        return SNew(SCheckBox)
            .IsEnabled(IsEnabled)
            .IsChecked(State)
            .OnCheckStateChanged(OnChanged)
            .ToolTipText(Tooltip)
                [SNew(STextBlock).Text(Label)];
    };

    return SNew(SBorder)
        .Padding(10.0f)
        .BorderImage(FAppStyle::GetBrush(TEXT("Menu.Background")))
            [SNew(SVerticalBox) + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 5.0f)[SNew(STextBlock).Text(LOCTEXT("PreviewViewDisplayHeading", "Display")).Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.BoldFont")))] + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)[BuildLayerToggle(LOCTEXT("PreviewAbsorbedLayer", "Absorbed Water"), TAttribute<ECheckBoxState>::Create(TAttribute<ECheckBoxState>::FGetter::CreateSP(this, &SWetnessProfileEditorPanel::GetAbsorbedLayerCheckState)), FOnCheckStateChanged::CreateSP(this, &SWetnessProfileEditorPanel::HandleAbsorbedLayerCheckStateChanged), TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateSP(this, &SWetnessProfileEditorPanel::IsAbsorbedLayerToggleEnabled)), TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &SWetnessProfileEditorPanel::GetAbsorbedLayerTooltip)))] + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)[BuildLayerToggle(LOCTEXT("PreviewSurfaceLayer", "Surface Water"), TAttribute<ECheckBoxState>::Create(TAttribute<ECheckBoxState>::FGetter::CreateSP(this, &SWetnessProfileEditorPanel::GetSurfaceLayerCheckState)), FOnCheckStateChanged::CreateSP(this, &SWetnessProfileEditorPanel::HandleSurfaceLayerCheckStateChanged), TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateSP(this, &SWetnessProfileEditorPanel::IsSurfaceLayerToggleEnabled)), TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &SWetnessProfileEditorPanel::GetSurfaceLayerTooltip)))]];
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
        return LOCTEXT("PreviewModeDropletStampTest", "Stamp Footprint");
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
        if (*InBehavior == SWetnessProfileViewport::EPreviewBehavior::Simulation)
        {
            PreviewViewport->SetPreviewAnimationEnabled(true);
            PreviewViewport->SetPreviewLoopEnabled(false);
            FSlateApplication::Get().SetKeyboardFocus(PreviewViewport, EFocusCause::SetDirectly);
        }
    }
    ApplyPreviewLayerSettingsToViewport();
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
               : LOCTEXT("PreviewBehaviorStatic", "Static");
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

FReply SWetnessProfileEditorPanel::HandleApplySplashClicked()
{
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->ApplyPreviewSplash();
    }
    return FReply::Handled();
}

const FSlateBrush* SWetnessProfileEditorPanel::GetPlayPauseBrush() const
{
    return FDWCEditorStyle::GetBrush(
        PreviewViewport.IsValid() && PreviewViewport->IsPreviewAnimationEnabled()
            ? TEXT("DWCEditor.WetnessProfile.Pause")
            : TEXT("DWCEditor.WetnessProfile.Play"));
}

FText SWetnessProfileEditorPanel::GetPlayPauseToolTip() const
{
    return PreviewViewport.IsValid() && PreviewViewport->IsPreviewAnimationEnabled()
               ? LOCTEXT("PauseSimulationTooltip", "Pause Simulation")
               : LOCTEXT("PlaySimulationTooltip", "Play Simulation");
}

bool SWetnessProfileEditorPanel::IsSelectedWaterChannelEnabled() const
{
    const UWetnessProfile* Profile = WetnessProfile.Get();
    if (Profile == nullptr || !HasWaterChannelSelectionAttribute.Get(false))
    {
        return false;
    }

    return IsSurfaceWaterSelectedAttribute.Get(false)
               ? Profile->Parameters.SurfaceWater.bEnabled
               : Profile->Parameters.AbsorbedWetness.bEnabled;
}

ECheckBoxState SWetnessProfileEditorPanel::GetAbsorbedLayerCheckState() const
{
    return IsAbsorbedLayerToggleEnabled() && bPreviewAbsorbedLayerEnabled
               ? ECheckBoxState::Checked
               : ECheckBoxState::Unchecked;
}

void SWetnessProfileEditorPanel::HandleAbsorbedLayerCheckStateChanged(const ECheckBoxState NewState)
{
    if (!IsAbsorbedLayerToggleEnabled())
    {
        return;
    }
    bPreviewAbsorbedLayerEnabled = NewState == ECheckBoxState::Checked;
    ApplyPreviewLayerSettingsToViewport();
}

bool SWetnessProfileEditorPanel::IsAbsorbedLayerToggleEnabled() const
{
    const UWetnessProfile* Profile = WetnessProfile.Get();
    return Profile != nullptr && Profile->Parameters.AbsorbedWetness.bEnabled;
}

FText SWetnessProfileEditorPanel::GetAbsorbedLayerTooltip() const
{
    return IsAbsorbedLayerToggleEnabled()
               ? LOCTEXT("PreviewAbsorbedLayerTooltip", "Show or hide Absorbed Water in the preview.")
               : LOCTEXT("PreviewAbsorbedLayerDisabledTooltip", "Absorbed Water is disabled in this Wetness Profile. Enable it in the channel header to simulate or display spreading.");
}

ECheckBoxState SWetnessProfileEditorPanel::GetSurfaceLayerCheckState() const
{
    return IsSurfaceLayerToggleEnabled() && bPreviewSurfaceLayerEnabled
               ? ECheckBoxState::Checked
               : ECheckBoxState::Unchecked;
}

void SWetnessProfileEditorPanel::HandleSurfaceLayerCheckStateChanged(const ECheckBoxState NewState)
{
    if (!IsSurfaceLayerToggleEnabled())
    {
        return;
    }
    bPreviewSurfaceLayerEnabled = NewState == ECheckBoxState::Checked;
    ApplyPreviewLayerSettingsToViewport();
}

bool SWetnessProfileEditorPanel::IsSurfaceLayerToggleEnabled() const
{
    const UWetnessProfile* Profile = WetnessProfile.Get();
    return Profile != nullptr && Profile->Parameters.SurfaceWater.bEnabled;
}

FText SWetnessProfileEditorPanel::GetSurfaceLayerTooltip() const
{
    return IsSurfaceLayerToggleEnabled()
               ? LOCTEXT("PreviewSurfaceLayerTooltip", "Show or hide Surface Water in the preview.")
               : LOCTEXT("PreviewSurfaceLayerDisabledTooltip", "Surface Water is disabled in this Wetness Profile. Enable it in the channel header before previewing droplets.");
}

ECheckBoxState SWetnessProfileEditorPanel::GetDroplet1CheckState() const
{
    return IsSurfaceLayerToggleEnabled() && bPreviewSurfaceLayerEnabled && bPreviewDroplet1Enabled
               ? ECheckBoxState::Checked
               : ECheckBoxState::Unchecked;
}

void SWetnessProfileEditorPanel::HandleDroplet1CheckStateChanged(const ECheckBoxState NewState)
{
    if (!IsSurfaceLayerToggleEnabled())
    {
        return;
    }
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
    ApplyPreviewLayerSettingsToViewport();
}

ECheckBoxState SWetnessProfileEditorPanel::GetDroplet2CheckState() const
{
    return IsSurfaceLayerToggleEnabled() && bPreviewSurfaceLayerEnabled && bPreviewDroplet2Enabled
               ? ECheckBoxState::Checked
               : ECheckBoxState::Unchecked;
}

void SWetnessProfileEditorPanel::HandleDroplet2CheckStateChanged(const ECheckBoxState NewState)
{
    if (!IsSurfaceLayerToggleEnabled())
    {
        return;
    }
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
    ApplyPreviewLayerSettingsToViewport();
}

EVisibility SWetnessProfileEditorPanel::GetSurfaceDetailsVisibility() const
{
    return IsSurfaceLayerToggleEnabled() && bPreviewSurfaceLayerEnabled
               ? EVisibility::Visible
               : EVisibility::Collapsed;
}

EVisibility SWetnessProfileEditorPanel::GetSecondaryDropletDisplayVisibility() const
{
    const UWetnessProfile* Profile = WetnessProfile.Get();
    if (Profile == nullptr)
    {
        return EVisibility::Collapsed;
    }
    return Profile->Parameters.SurfaceWater.bUseSecondaryDroplets
               ? EVisibility::Visible
               : EVisibility::Collapsed;
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
    USkeletalMesh* Mesh = nullptr;
#if WITH_EDITORONLY_DATA
    if (!bUseTemporaryPreviewMesh)
    {
        const UWetnessProfile* Profile = WetnessProfile.Get();
        Mesh = Profile != nullptr ? Profile->PreviewSkeletalMesh : nullptr;
    }
    else
#endif
    {
        Mesh = TemporaryPreviewMesh.Get();
    }
    return Mesh != nullptr ? FSoftObjectPath(Mesh).ToString() : FString();
}

void SWetnessProfileEditorPanel::HandleCurrentPreviewMeshChanged(const FAssetData& AssetData)
{
    UWetnessProfile* Profile = WetnessProfile.Get();
    if (Profile == nullptr || !PreviewViewport.IsValid())
    {
        return;
    }

    USkeletalMesh* NewPreviewMesh = Cast<USkeletalMesh>(AssetData.GetAsset());
    if (bUseTemporaryPreviewMesh)
    {
        TemporaryPreviewMesh = NewPreviewMesh;
        PreviewViewport->SetPreviewSkeletalMeshOverride(NewPreviewMesh);
        return;
    }

#if WITH_EDITORONLY_DATA
    if (Profile->PreviewSkeletalMesh == NewPreviewMesh)
    {
        return;
    }

    const FScopedTransaction Transaction(LOCTEXT(
        "ChangeWetnessProfilePreviewMesh",
        "Change Wetness Profile Preview Mesh"));
    Profile->Modify();
    Profile->PreviewSkeletalMesh = NewPreviewMesh;
    Profile->MarkPackageDirty();
    PreviewViewport->ClearPreviewSkeletalMeshOverride();
#endif
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
    USkeletalMesh*   CurrentMesh = PreviewViewport.IsValid()
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

ECheckBoxState SWetnessProfileEditorPanel::GetReferencedMeshSourceState() const
{
    return bUseTemporaryPreviewMesh
               ? ECheckBoxState::Unchecked
               : ECheckBoxState::Checked;
}

ECheckBoxState SWetnessProfileEditorPanel::GetTemporaryMeshSourceState() const
{
    return bUseTemporaryPreviewMesh
               ? ECheckBoxState::Checked
               : ECheckBoxState::Unchecked;
}

void SWetnessProfileEditorPanel::HandleReferencedMeshSourceChanged(const ECheckBoxState NewState)
{
    if (NewState != ECheckBoxState::Checked)
    {
        return;
    }
    bUseTemporaryPreviewMesh = false;
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->ClearPreviewSkeletalMeshOverride();
    }
}

void SWetnessProfileEditorPanel::HandleTemporaryMeshSourceChanged(const ECheckBoxState NewState)
{
    if (NewState != ECheckBoxState::Checked)
    {
        return;
    }
    bUseTemporaryPreviewMesh = true;
#if WITH_EDITORONLY_DATA
    if (!TemporaryPreviewMesh.IsValid())
    {
        const UWetnessProfile* Profile = WetnessProfile.Get();
        TemporaryPreviewMesh = Profile != nullptr ? Profile->PreviewSkeletalMesh : nullptr;
    }
#endif
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->SetPreviewSkeletalMeshOverride(TemporaryPreviewMesh.Get());
    }
}

float SWetnessProfileEditorPanel::GetPreviewCursorScale() const
{
    return PreviewCursorScale;
}

void SWetnessProfileEditorPanel::HandlePreviewCursorScaleChanged(const float InValue)
{
    PreviewCursorScale = FMath::Clamp(InValue, 0.5f, 3.0f);
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->SetInteractionCursorScale(PreviewCursorScale);
    }
}

FText SWetnessProfileEditorPanel::GetPreviewCursorScaleText() const
{
    FNumberFormattingOptions Options;
    Options.MinimumFractionalDigits = 2;
    Options.MaximumFractionalDigits = 2;
    return FText::AsNumber(PreviewCursorScale, &Options);
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

float SWetnessProfileEditorPanel::GetPreviewSurfaceWaterPercent() const
{
    return PreviewViewport.IsValid()
               ? PreviewViewport->GetPreviewSurfaceWater() * 100.0f
               : 100.0f;
}

void SWetnessProfileEditorPanel::HandlePreviewSurfaceWaterPercentChanged(const float InPercent)
{
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->SetPreviewSurfaceWater(InPercent * 0.01f);
    }
}

FText SWetnessProfileEditorPanel::GetPreviewSurfaceWaterPercentText() const
{
    return FText::Format(
        LOCTEXT("PreviewSurfacePercentFormat", "{0}%"),
        FText::AsNumber(FMath::RoundToInt(GetPreviewSurfaceWaterPercent())));
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
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->SetPreviewDropletDetailSizes(
            PreviewDroplet1DetailSize,
            PreviewDroplet2DetailSize);
    }
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
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->SetPreviewDropletDetailSizes(
            PreviewDroplet1DetailSize,
            PreviewDroplet2DetailSize);
    }
}

FText SWetnessProfileEditorPanel::GetPreviewDroplet2DetailSizeText() const
{
    return FormatPreviewFloat(PreviewDroplet2DetailSize);
}

float SWetnessProfileEditorPanel::GetSelectedPreviewDetailSize() const
{
    return IsSecondaryDropletSelected()
               ? PreviewDroplet2DetailSize
               : PreviewDroplet1DetailSize;
}

void SWetnessProfileEditorPanel::HandleSelectedPreviewDetailSizeChanged(const float InValue)
{
    if (IsSecondaryDropletSelected())
    {
        HandlePreviewDroplet2DetailSizeChanged(InValue);
    }
    else
    {
        HandlePreviewDroplet1DetailSizeChanged(InValue);
    }
}

FText SWetnessProfileEditorPanel::GetSelectedPreviewDetailSizeText() const
{
    return FormatPreviewFloat(GetSelectedPreviewDetailSize());
}

EVisibility SWetnessProfileEditorPanel::GetSelectedPreviewDetailSizeVisibility() const
{
    return HasWaterChannelSelectionAttribute.Get(false) && IsSurfaceWaterSelectedAttribute.Get(false)
               ? EVisibility::Visible
               : EVisibility::Collapsed;
}

bool SWetnessProfileEditorPanel::IsSecondaryDropletSelected() const
{
#if WITH_EDITORONLY_DATA
    const UWetnessProfile* Profile = WetnessProfile.Get();
    return Profile != nullptr && Profile->EditorActiveDropletLayer == 1u;
#else
    return false;
#endif
}

void SWetnessProfileEditorPanel::LoadPersistedPreviewSettings()
{
    UWetnessProfile* Profile = WetnessProfile.Get();
    PreviewDroplet1DetailSize = FMath::Clamp(
        ReadFloatMetadata(Profile, PreviewDroplet1DetailSizeMetadataKey, 1.0f),
        0.0f,
        4.0f);
    PreviewDroplet2DetailSize = FMath::Clamp(
        ReadFloatMetadata(Profile, PreviewDroplet2DetailSizeMetadataKey, 1.0f),
        0.0f,
        4.0f);
    // Surface Water preview is intentionally composited: Primary and Secondary
    // are shown together. Layer selection controls editing/cursor feedback only.
    bPreviewDroplet1Enabled = true;
    bPreviewDroplet2Enabled = true;
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

void SWetnessProfileEditorPanel::ApplyPreviewLayerSettingsToViewport()
{
    if (!PreviewViewport.IsValid())
    {
        return;
    }

    const UWetnessProfile* Profile = WetnessProfile.Get();
    const bool             bHasSelection = HasWaterChannelSelectionAttribute.Get(false);
    const bool             bSurfaceSelected = bHasSelection && IsSurfaceWaterSelectedAttribute.Get(false);
    const bool             bSecondarySelected = bSurfaceSelected && IsSecondaryDropletSelected();
    const bool             bSelectedChannelEnabled = bHasSelection && Profile != nullptr &&
                                         (bSurfaceSelected
                                              ? Profile->Parameters.SurfaceWater.bEnabled
                                              : Profile->Parameters.AbsorbedWetness.bEnabled);
    const bool bSimulation = SelectedPreviewBehaviorItem.IsValid() &&
                             *SelectedPreviewBehaviorItem == SWetnessProfileViewport::EPreviewBehavior::Simulation;

    bool bEffectiveAbsorbed = Profile != nullptr &&
                              Profile->Parameters.AbsorbedWetness.bEnabled &&
                              bPreviewAbsorbedLayerEnabled;
    bool bEffectiveSurface = Profile != nullptr &&
                             Profile->Parameters.SurfaceWater.bEnabled &&
                             bPreviewSurfaceLayerEnabled;

    if (bSimulation)
    {
        bEffectiveAbsorbed = bHasSelection && !bSurfaceSelected && bEffectiveAbsorbed;
        bEffectiveSurface = bSurfaceSelected && bEffectiveSurface;
    }

    // Surface Water preview always renders the composed result. Primary/Secondary
    // selection is only an editing target and must not hide the other layer.
    const bool bEffectiveDroplet1 = bEffectiveSurface;
    const bool bEffectiveDroplet2 = bEffectiveSurface;

    PreviewViewport->SetPreviewSimulationTarget(
        bHasSelection,
        bSurfaceSelected,
        bSecondarySelected,
        bSelectedChannelEnabled);
    PreviewViewport->SetPreviewSimulationLayers(bEffectiveAbsorbed, bEffectiveSurface);
    PreviewViewport->SetPreviewDropletVisibility(bEffectiveDroplet1, bEffectiveDroplet2);
    // Surface Water uses the authored Stamp Radius directly. Cursor Size remains
    // a preview-only control for Absorbed Water.
    PreviewViewport->SetInteractionCursorScale(bSurfaceSelected ? 1.0f : PreviewCursorScale);
}

void SWetnessProfileEditorPanel::ApplyPreviewSettingsToViewport()
{
    if (!PreviewViewport.IsValid())
    {
        return;
    }

    PreviewViewport->SetPreviewDropletDetailSizes(PreviewDroplet1DetailSize, PreviewDroplet2DetailSize);
    ApplyPreviewLayerSettingsToViewport();
    if (SelectedPreviewModeItem.IsValid())
    {
        PreviewViewport->SetPreviewMode(*SelectedPreviewModeItem);
    }
    if (SelectedPreviewBehaviorItem.IsValid())
    {
        PreviewViewport->SetPreviewBehavior(*SelectedPreviewBehaviorItem);
    }
    PreviewViewport->SetPreviewAnimationSpeed(1.0f);
    PreviewViewport->SetPreviewLoopEnabled(false);
}

#undef LOCTEXT_NAMESPACE
