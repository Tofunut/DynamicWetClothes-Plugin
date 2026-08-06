#include "SWetnessProfileEditorPanel.h"

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
    HasWaterChannelSelectionAttribute = InArgs._HasWaterChannelSelection;
    IsSurfaceWaterSelectedAttribute = InArgs._IsSurfaceWaterSelected;
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
                   [SNew(STextBlock)
                        .Text(LOCTEXT("PreviewHeading", "Preview"))
                        .Font(PanelHeadingFont)]

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
        ApplyPreviewSettingsToViewport();
        PreviewViewport->RefreshFromProfile();
    }
}

void SWetnessProfileEditorPanel::Tick(
    const FGeometry& AllottedGeometry,
    const double InCurrentTime,
    const float InDeltaTime)
{
    SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

    if (!bAddWaterHeld ||
        !SelectedPreviewBehaviorItem.IsValid() ||
        *SelectedPreviewBehaviorItem != SWetnessProfileViewport::EPreviewBehavior::Simulation)
    {
        AddWaterRepeatAccumulator = 0.0f;
        return;
    }

    constexpr float RepeatIntervalSeconds = 0.12f;
    AddWaterRepeatAccumulator += FMath::Max(InDeltaTime, 0.0f);
    while (AddWaterRepeatAccumulator >= RepeatIntervalSeconds)
    {
        AddWaterRepeatAccumulator -= RepeatIntervalSeconds;
        HandleApplySplashClicked();
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
        .Padding(FMargin(10.0f, 7.0f))
        .BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
        [
            SNew(SHorizontalBox)

            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            .Padding(0.0f, 0.0f, 8.0f, 0.0f)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("PreviewMeshLabel", "Preview Mesh"))
                .Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.BoldFont")))
            ]

            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            .VAlign(VAlign_Center)
            .Padding(0.0f, 0.0f, 8.0f, 0.0f)
            [
                SNew(SObjectPropertyEntryBox)
                .AllowedClass(USkeletalMesh::StaticClass())
                .AllowClear(true)
                .ObjectPath(this, &SWetnessProfileEditorPanel::GetCurrentPreviewMeshObjectPath)
                .OnObjectChanged(this, &SWetnessProfileEditorPanel::HandleCurrentPreviewMeshChanged)
            ]

            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            [
                SNew(SButton)
                .ToolTipText(LOCTEXT("FramePreviewMeshTooltip", "Frame the preview mesh in the viewport."))
                .OnClicked(this, &SWetnessProfileEditorPanel::HandleFramePreviewMeshClicked)
                .ContentPadding(FMargin(10.0f, 4.0f))
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("FramePreviewMeshLabel", "Frame"))
                ]
            ]
        ];
}

TSharedRef<SWidget> SWetnessProfileEditorPanel::BuildPreviewControlsSection()
{
    return SNew(SBox)
        .MinDesiredHeight(138.0f)
        [
            SNew(SBorder)
            .Padding(FMargin(10.0f, 9.0f))
            .BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
            [
                SNew(SVerticalBox)

                + SVerticalBox::Slot()
                .AutoHeight()
                .HAlign(HAlign_Fill)
                [
                    BuildPreviewModeSection()
                ]

                + SVerticalBox::Slot()
                .AutoHeight()
                .Padding(0.0f, 8.0f, 0.0f, 8.0f)
                [
                    SNew(SSeparator)
                ]

                + SVerticalBox::Slot()
                .FillHeight(1.0f)
                [
                    SNew(SWidgetSwitcher)
                    .WidgetIndex_Lambda([this]()
                    {
                        return SelectedPreviewBehaviorItem.IsValid() &&
                            *SelectedPreviewBehaviorItem == SWetnessProfileViewport::EPreviewBehavior::Simulation
                            ? 1
                            : 0;
                    })
                    + SWidgetSwitcher::Slot()
                    [
                        BuildPreviewWaterSection()
                    ]
                    + SWidgetSwitcher::Slot()
                    [
                        BuildPreviewSimulationSection()
                    ]
                ]
            ]
        ];
}

TSharedRef<SWidget> SWetnessProfileEditorPanel::BuildPreviewWaterSection()
{
    const auto BuildAmountSlider = [](
        const FText& Label,
        const TAttribute<float>& Value,
        const FOnFloatValueChanged& OnChanged,
        const TAttribute<FText>& ValueText,
        const TAttribute<bool>& IsEnabled)
    {
        return SNew(SBox)
            .MinDesiredHeight(28.0f)
            .IsEnabled(IsEnabled)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                [
                    SNew(SBox)
                    .WidthOverride(118.0f)
                    [
                        SNew(STextBlock)
                        .Text(Label)
                    ]
                ]
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                .VAlign(VAlign_Center)
                [
                    SNew(SSlider)
                    .MinValue(0.0f)
                    .MaxValue(100.0f)
                    .Value(Value)
                    .OnValueChanged(OnChanged)
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                .Padding(10.0f, 0.0f, 0.0f, 0.0f)
                [
                    SNew(SBox)
                    .WidthOverride(46.0f)
                    [
                        SNew(STextBlock)
                        .Justification(ETextJustify::Right)
                        .Text(ValueText)
                    ]
                ]
            ];
    };

    const auto BuildDetailSlider = [this]()
    {
        return SNew(SBox)
            .MinDesiredHeight(28.0f)
            .Visibility(this, &SWetnessProfileEditorPanel::GetSelectedPreviewDetailSizeVisibility)
            .IsEnabled_Lambda([this]()
            {
                const UWetnessProfile* Profile = WetnessProfile.Get();
                return Profile != nullptr &&
                    Profile->Parameters.SurfaceWater.bEnabled &&
                    (!IsSecondaryDropletSelected() || Profile->Parameters.SurfaceWater.bUseSecondaryDroplets);
            })
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                [
                    SNew(SBox)
                    .WidthOverride(118.0f)
                    [
                        SNew(STextBlock)
                        .Text(LOCTEXT("PreviewDetailSizeLabel", "Detail Size"))
                        .ToolTipText(LOCTEXT(
                            "PreviewDetailSizeTooltip",
                            "Preview-only Surface Water detail size for the droplet layer selected on the left."))
                    ]
                ]
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                .VAlign(VAlign_Center)
                [
                    SNew(SSlider)
                    .MinValue(0.0f)
                    .MaxValue(4.0f)
                    .Value(this, &SWetnessProfileEditorPanel::GetSelectedPreviewDetailSize)
                    .OnValueChanged(this, &SWetnessProfileEditorPanel::HandleSelectedPreviewDetailSizeChanged)
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                .Padding(10.0f, 0.0f, 0.0f, 0.0f)
                [
                    SNew(SBox)
                    .WidthOverride(46.0f)
                    [
                        SNew(STextBlock)
                        .Justification(ETextJustify::Right)
                        .Text(this, &SWetnessProfileEditorPanel::GetSelectedPreviewDetailSizeText)
                    ]
                ]
            ];
    };

    return SNew(SVerticalBox)
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, 0.0f, 0.0f, 6.0f)
        [
            BuildAmountSlider(
                LOCTEXT("PreviewAbsorbedWaterLabel", "Absorbed Water"),
                TAttribute<float>::Create(TAttribute<float>::FGetter::CreateSP(this, &SWetnessProfileEditorPanel::GetPreviewAbsorbedWaterPercent)),
                FOnFloatValueChanged::CreateSP(this, &SWetnessProfileEditorPanel::HandlePreviewAbsorbedWaterPercentChanged),
                TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &SWetnessProfileEditorPanel::GetPreviewAbsorbedWaterPercentText)),
                TAttribute<bool>::CreateLambda([this]()
                {
                    const UWetnessProfile* Profile = WetnessProfile.Get();
                    return Profile != nullptr && Profile->Parameters.AbsorbedWetness.bEnabled;
                }))
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, 0.0f, 0.0f, 6.0f)
        [
            BuildAmountSlider(
                LOCTEXT("PreviewSurfaceWaterLabel", "Surface Water"),
                TAttribute<float>::Create(TAttribute<float>::FGetter::CreateSP(this, &SWetnessProfileEditorPanel::GetPreviewSurfaceWaterPercent)),
                FOnFloatValueChanged::CreateSP(this, &SWetnessProfileEditorPanel::HandlePreviewSurfaceWaterPercentChanged),
                TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &SWetnessProfileEditorPanel::GetPreviewSurfaceWaterPercentText)),
                TAttribute<bool>::CreateLambda([this]()
                {
                    const UWetnessProfile* Profile = WetnessProfile.Get();
                    return Profile != nullptr && Profile->Parameters.SurfaceWater.bEnabled;
                }))
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            BuildDetailSlider()
        ];
}

TSharedRef<SWidget> SWetnessProfileEditorPanel::BuildPreviewModeSection()
{
    const auto SelectBehavior = [this](const SWetnessProfileViewport::EPreviewBehavior Behavior)
    {
        for (const TSharedPtr<SWetnessProfileViewport::EPreviewBehavior>& Item : PreviewBehaviorItems)
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
        const FText& Label,
        const FText& Tooltip)
    {
        return SNew(SBorder)
            .BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
            .BorderBackgroundColor_Lambda([this, Behavior]()
            {
                const bool bSelected = SelectedPreviewBehaviorItem.IsValid() &&
                    *SelectedPreviewBehaviorItem == Behavior;
                return bSelected
                    ? FLinearColor(0.05f, 0.42f, 0.80f, 1.0f)
                    : FLinearColor(0.20f, 0.21f, 0.24f, 1.0f);
            })
            .Padding(2.0f)
            [SNew(SBorder)
                 .BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
                 .BorderBackgroundColor_Lambda([this, Behavior]()
                 {
                     const bool bSelected = SelectedPreviewBehaviorItem.IsValid() &&
                         *SelectedPreviewBehaviorItem == Behavior;
                     return bSelected
                         ? FLinearColor(0.025f, 0.22f, 0.42f, 1.0f)
                         : FLinearColor(0.075f, 0.078f, 0.086f, 1.0f);
                 })
             [SNew(SButton)
                  .ButtonStyle(FAppStyle::Get(), TEXT("NoBorder"))
                  .ContentPadding(FMargin(12.0f, 9.0f))
                  .HAlign(HAlign_Center)
                  .ToolTipText(Tooltip)
                  .OnClicked_Lambda([SelectBehavior, Behavior]()
                  {
                      SelectBehavior(Behavior);
                      return FReply::Handled();
                  })
              [SNew(STextBlock)
                   .Text(Label)
                   .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 13))
                   .ColorAndOpacity(FSlateColor(FLinearColor::White))
                   .Justification(ETextJustify::Center)]]];
    };

    return SNew(SVerticalBox)
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(1.0f, 0.0f, 0.0f, 7.0f)
        [
            SNew(STextBlock)
            .Text(LOCTEXT("PreviewModeHeading", "PREVIEW MODE"))
            .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 10))
            .ColorAndOpacity(FSlateColor(FLinearColor(0.63f, 0.68f, 0.76f, 1.0f)))
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            .Padding(0.0f, 0.0f, 3.0f, 0.0f)
            [
                BuildModeButton(
                    SWetnessProfileViewport::EPreviewBehavior::Manual,
                    LOCTEXT("PreviewBehaviorStaticSegment", "Static"),
                    LOCTEXT("PreviewBehaviorStaticTooltip", "Adjust fixed wetness values without advancing the simulation."))
            ]
            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            .Padding(3.0f, 0.0f, 0.0f, 0.0f)
            [
                BuildModeButton(
                    SWetnessProfileViewport::EPreviewBehavior::Simulation,
                    LOCTEXT("PreviewBehaviorSimulationSegment", "Simulation"),
                    LOCTEXT("PreviewBehaviorSimulationTooltip", "Add water and preview spreading and drying over time."))
            ]
        ];
}

TSharedRef<SWidget> SWetnessProfileEditorPanel::BuildPreviewSimulationSection()
{
    return SNew(SVerticalBox)
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, 0.0f, 0.0f, 7.0f)
        [
            SNew(STextBlock)
            .Text(this, &SWetnessProfileEditorPanel::GetSimulationTargetText)
            .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 10))
            .ColorAndOpacity(FSlateColor(FLinearColor(0.70f, 0.76f, 0.86f, 0.95f)))
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, 0.0f, 0.0f, 8.0f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(0.0f, 0.0f, 5.0f, 0.0f)
            [
                SNew(SButton)
                .IsEnabled(this, &SWetnessProfileEditorPanel::IsSelectedWaterChannelEnabled)
                .ToolTipText(this, &SWetnessProfileEditorPanel::GetPlayPauseToolTip)
                .OnClicked(this, &SWetnessProfileEditorPanel::HandlePlayPauseClicked)
                .ContentPadding(FMargin(10.0f, 5.0f))
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .VAlign(VAlign_Center)
                    [
                        SNew(SImage)
                        .Image(this, &SWetnessProfileEditorPanel::GetPlayPauseBrush)
                    ]
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .VAlign(VAlign_Center)
                    .Padding(6.0f, 0.0f, 0.0f, 0.0f)
                    [
                        SNew(STextBlock)
                        .Text_Lambda([this]()
                        {
                            return PreviewViewport.IsValid() && PreviewViewport->IsPreviewAnimationEnabled()
                                ? LOCTEXT("PauseSimulationLabel", "Pause")
                                : LOCTEXT("PlaySimulationLabel", "Play");
                        })
                    ]
                ]
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .Padding(0.0f, 0.0f, 12.0f, 0.0f)
            [
                SNew(SButton)
                .IsEnabled(this, &SWetnessProfileEditorPanel::IsSelectedWaterChannelEnabled)
                .ToolTipText(LOCTEXT("ClearWaterTooltip", "Clear all preview water and return the simulation to a dry state."))
                .OnClicked(this, &SWetnessProfileEditorPanel::HandleRestartSimulationClicked)
                .ContentPadding(FMargin(10.0f, 5.0f))
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .VAlign(VAlign_Center)
                    [
                        SNew(SImage)
                        .Image(FAppStyle::GetBrush(TEXT("Icons.Refresh")))
                    ]
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .VAlign(VAlign_Center)
                    .Padding(5.0f, 0.0f, 0.0f, 0.0f)
                    [
                        SNew(STextBlock)
                        .Text(LOCTEXT("ClearWaterLabel", "Clear Water"))
                    ]
                ]
            ]
            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            [
                SNew(SSpacer)
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            .Padding(0.0f, 0.0f, 6.0f, 0.0f)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("PreviewSpeedLabel", "Speed"))
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            [
                SNew(SBox)
                .WidthOverride(76.0f)
                [
                    SNew(SComboBox<TSharedPtr<float>>)
                    .IsEnabled(this, &SWetnessProfileEditorPanel::IsSelectedWaterChannelEnabled)
                    .OptionsSource(&PreviewSpeedItems)
                    .InitiallySelectedItem(SelectedPreviewSpeedItem)
                    .OnGenerateWidget(this, &SWetnessProfileEditorPanel::GeneratePreviewSpeedWidget)
                    .OnSelectionChanged(this, &SWetnessProfileEditorPanel::HandlePreviewSpeedChanged)
                    [
                        SNew(STextBlock)
                        .Text(this, &SWetnessProfileEditorPanel::GetPreviewSpeedText)
                    ]
                ]
            ]
        ]
        + SVerticalBox::Slot()
        .AutoHeight()
        [
            SNew(SButton)
            .HAlign(HAlign_Center)
            .ContentPadding(FMargin(12.0f, 9.0f))
            .ToolTipText(LOCTEXT(
                "AddWaterTooltip",
                "Add water where the center of the viewport is aimed. Hold the button or Space to keep adding water."))
            .IsEnabled(this, &SWetnessProfileEditorPanel::IsSelectedWaterChannelEnabled)
            .OnPressed(this, &SWetnessProfileEditorPanel::HandleAddWaterPressed)
            .OnReleased(this, &SWetnessProfileEditorPanel::HandleAddWaterReleased)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                [
                    SNew(SImage)
                    .Image(FDWCEditorStyle::GetBrush(TEXT("DWCEditor.WetnessProfile.AddWater")))
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                .Padding(7.0f, 0.0f, 0.0f, 0.0f)
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("AddWaterLabel", "Add Water"))
                    .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 12))
                ]
            ]
        ];
}

TSharedRef<SWidget> SWetnessProfileEditorPanel::BuildPreviewDetailSizeSection()
{
    const auto BuildDetailSlider = [](const FText& Label, const TAttribute<float>& Value, const FOnFloatValueChanged& OnChanged, const TAttribute<FText>& ValueText)
    {
        return SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
                  [SNew(SBox).WidthOverride(150.0f)[SNew(STextBlock).Text(Label)]]
            + SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
                  [SNew(SSlider).MinValue(0.0f).MaxValue(4.0f).Value(Value).OnValueChanged(OnChanged)]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.0f, 0.0f)
                  [SNew(SBox).WidthOverride(42.0f)
                       [SNew(STextBlock).Justification(ETextJustify::Right).Text(ValueText)]];
    };

    return SNew(SExpandableArea)
        .InitiallyCollapsed(true)
        .AreaTitle(LOCTEXT("PreviewDisplaySettingsHeading", "Preview Display Settings"))
        .ToolTipText(LOCTEXT("PreviewDisplaySettingsTooltip", "Preview-only display controls. These values do not change runtime Wetness Profile parameters."))
        .BodyContent()
        [SNew(SVerticalBox)
         + SVerticalBox::Slot().AutoHeight()
               [SNew(SBox).Visibility(this, &SWetnessProfileEditorPanel::GetDroplet1ControlsVisibility)
                    [BuildDetailSlider(
                        LOCTEXT("PreviewPrimaryDropletScale", "Primary Droplet Detail Scale"),
                        TAttribute<float>::Create(TAttribute<float>::FGetter::CreateSP(this, &SWetnessProfileEditorPanel::GetPreviewDroplet1DetailSize)),
                        FOnFloatValueChanged::CreateSP(this, &SWetnessProfileEditorPanel::HandlePreviewDroplet1DetailSizeChanged),
                        TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &SWetnessProfileEditorPanel::GetPreviewDroplet1DetailSizeText)))]]
         + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 6.0f, 0.0f, 0.0f)
               [SNew(SBox).Visibility(this, &SWetnessProfileEditorPanel::GetDroplet2ControlsVisibility)
                    [BuildDetailSlider(
                        LOCTEXT("PreviewSecondaryDropletScale", "Secondary Droplet Detail Scale"),
                        TAttribute<float>::Create(TAttribute<float>::FGetter::CreateSP(this, &SWetnessProfileEditorPanel::GetPreviewDroplet2DetailSize)),
                        FOnFloatValueChanged::CreateSP(this, &SWetnessProfileEditorPanel::HandlePreviewDroplet2DetailSizeChanged),
                        TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &SWetnessProfileEditorPanel::GetPreviewDroplet2DetailSizeText)))]]];
}

TSharedRef<SWidget> SWetnessProfileEditorPanel::BuildPreviewViewMenu()
{
    const auto BuildLayerToggle = [](const FText& Label,
                                     const TAttribute<ECheckBoxState>& State,
                                     const FOnCheckStateChanged& OnChanged,
                                     const TAttribute<bool>& IsEnabled,
                                     const TAttribute<FText>& Tooltip)
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
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 5.0f)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("PreviewViewDisplayHeading", "Display"))
                .Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.BoldFont")))
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
            [
                BuildLayerToggle(
                    LOCTEXT("PreviewAbsorbedLayer", "Absorbed Water"),
                    TAttribute<ECheckBoxState>::Create(TAttribute<ECheckBoxState>::FGetter::CreateSP(this, &SWetnessProfileEditorPanel::GetAbsorbedLayerCheckState)),
                    FOnCheckStateChanged::CreateSP(this, &SWetnessProfileEditorPanel::HandleAbsorbedLayerCheckStateChanged),
                    TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateSP(this, &SWetnessProfileEditorPanel::IsAbsorbedLayerToggleEnabled)),
                    TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &SWetnessProfileEditorPanel::GetAbsorbedLayerTooltip)))
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
            [
                BuildLayerToggle(
                    LOCTEXT("PreviewSurfaceLayer", "Surface Water"),
                    TAttribute<ECheckBoxState>::Create(TAttribute<ECheckBoxState>::FGetter::CreateSP(this, &SWetnessProfileEditorPanel::GetSurfaceLayerCheckState)),
                    FOnCheckStateChanged::CreateSP(this, &SWetnessProfileEditorPanel::HandleSurfaceLayerCheckStateChanged),
                    TAttribute<bool>::Create(TAttribute<bool>::FGetter::CreateSP(this, &SWetnessProfileEditorPanel::IsSurfaceLayerToggleEnabled)),
                    TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &SWetnessProfileEditorPanel::GetSurfaceLayerTooltip)))
            ]
            + SVerticalBox::Slot().AutoHeight()
            [
                SNew(SBox)
                .Visibility(this, &SWetnessProfileEditorPanel::GetSurfaceDetailsVisibility)
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(14.0f, 5.0f, 0.0f, 1.0f)
                    [
                        SNew(SCheckBox)
                        .IsChecked(this, &SWetnessProfileEditorPanel::GetDroplet1CheckState)
                        .OnCheckStateChanged(this, &SWetnessProfileEditorPanel::HandleDroplet1CheckStateChanged)
                        [SNew(STextBlock).Text(LOCTEXT("PreviewPrimaryDroplets", "Primary Droplets"))]
                    ]
                    + SVerticalBox::Slot().AutoHeight().Padding(14.0f, 1.0f, 0.0f, 3.0f)
                    [
                        SNew(SBox)
                        .Visibility(this, &SWetnessProfileEditorPanel::GetSecondaryDropletDisplayVisibility)
                        [
                            SNew(SCheckBox)
                            .IsChecked(this, &SWetnessProfileEditorPanel::GetDroplet2CheckState)
                            .OnCheckStateChanged(this, &SWetnessProfileEditorPanel::HandleDroplet2CheckStateChanged)
                            [SNew(STextBlock).Text(LOCTEXT("PreviewSecondaryDroplets", "Secondary Droplets"))]
                        ]
                    ]
                ]
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 7.0f, 0.0f, 0.0f)
            [
                SNew(SSeparator)
            ]
            + SVerticalBox::Slot().AutoHeight().Padding(0.0f, 7.0f, 0.0f, 0.0f)
            [
                BuildPreviewDetailSizeSection()
            ]
        ];
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
    bAddWaterHeld = false;
    AddWaterRepeatAccumulator = 0.0f;
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->SetPreviewBehavior(*InBehavior);
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

void SWetnessProfileEditorPanel::HandleAddWaterPressed()
{
    bAddWaterHeld = true;
    AddWaterRepeatAccumulator = 0.0f;
    HandleApplySplashClicked();
}

void SWetnessProfileEditorPanel::HandleAddWaterReleased()
{
    bAddWaterHeld = false;
    AddWaterRepeatAccumulator = 0.0f;
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

EVisibility SWetnessProfileEditorPanel::GetDroplet1ControlsVisibility() const
{
    return IsSurfaceLayerToggleEnabled() && bPreviewSurfaceLayerEnabled && bPreviewDroplet1Enabled
        ? EVisibility::Visible
        : EVisibility::Collapsed;
}

EVisibility SWetnessProfileEditorPanel::GetDroplet2ControlsVisibility() const
{
    return IsSurfaceLayerToggleEnabled() && bPreviewSurfaceLayerEnabled && bPreviewDroplet2Enabled
        ? EVisibility::Visible
        : EVisibility::Collapsed;
}

FText SWetnessProfileEditorPanel::GetSimulationTargetText() const
{
    if (!HasWaterChannelSelectionAttribute.Get(false))
    {
        return LOCTEXT("SimulationTargetNone", "SELECT A WATER TYPE ON THE LEFT");
    }

    if (!IsSelectedWaterChannelEnabled())
    {
        return IsSurfaceWaterSelectedAttribute.Get(false)
            ? LOCTEXT("SimulationTargetSurfaceDisabled", "SURFACE WATER IS OFF · ENABLE IT TO SIMULATE")
            : LOCTEXT("SimulationTargetAbsorbedDisabled", "ABSORBED WATER IS OFF · ENABLE IT TO SIMULATE");
    }

    if (!IsSurfaceWaterSelectedAttribute.Get(false))
    {
        return LOCTEXT("SimulationTargetAbsorbed", "SIMULATING · ABSORBED WATER");
    }

    return IsSecondaryDropletSelected()
        ? LOCTEXT("SimulationTargetSurfaceSecondary", "SIMULATING · SURFACE WATER · SECONDARY")
        : LOCTEXT("SimulationTargetSurfacePrimary", "SIMULATING · SURFACE WATER · PRIMARY");
}

bool SWetnessProfileEditorPanel::IsSelectedWaterChannelEnabled() const
{
    if (!HasWaterChannelSelectionAttribute.Get(false))
    {
        return false;
    }

    const UWetnessProfile* Profile = WetnessProfile.Get();
    if (Profile == nullptr)
    {
        return false;
    }

    return IsSurfaceWaterSelectedAttribute.Get(false)
        ? Profile->Parameters.SurfaceWater.bEnabled
        : Profile->Parameters.AbsorbedWetness.bEnabled;
}

FText SWetnessProfileEditorPanel::GetSimulationTimeText() const
{
    const float Time = PreviewViewport.IsValid() ? PreviewViewport->GetPreviewAnimationTime() : 0.0f;
    FNumberFormattingOptions Options;
    Options.MinimumFractionalDigits = 1;
    Options.MaximumFractionalDigits = 1;
    return FText::Format(
        LOCTEXT("SimulationTimeFormat", "{0} / {1} s"),
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
#if WITH_EDITORONLY_DATA
    UWetnessProfile* Profile = WetnessProfile.Get();
    if (Profile == nullptr || !PreviewViewport.IsValid())
    {
        return;
    }

    USkeletalMesh* NewPreviewMesh = Cast<USkeletalMesh>(AssetData.GetAsset());
    if (Profile->PreviewSkeletalMesh == NewPreviewMesh)
    {
        return;
    }

    const FScopedTransaction Transaction(LOCTEXT("ChangeWetnessProfilePreviewMesh", "Change Wetness Profile Preview Mesh"));
    Profile->Modify();
    Profile->PreviewSkeletalMesh = NewPreviewMesh;
    Profile->MarkPackageDirty();
    PreviewViewport->ClearPreviewSkeletalMeshOverride();
#else
    (void)AssetData;
#endif
}

FReply SWetnessProfileEditorPanel::HandleFramePreviewMeshClicked()
{
    if (PreviewViewport.IsValid())
    {
        PreviewViewport->FocusOnPreviewMesh();
    }
    return FReply::Handled();
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
    return Profile != nullptr &&
        Profile->Parameters.SurfaceWater.bUseSecondaryDroplets &&
        Profile->EditorActiveDropletLayer == 1u;
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
#if WITH_EDITORONLY_DATA
    if (Profile != nullptr)
    {
        bPreviewDroplet1Enabled = true;
        bPreviewDroplet2Enabled = Profile->Parameters.SurfaceWater.bUseSecondaryDroplets;
        Profile->bEditorShowDroplet1 = true;
        Profile->bEditorShowDroplet2 = bPreviewDroplet2Enabled;
        if (!bPreviewDroplet2Enabled && Profile->EditorActiveDropletLayer == 1u)
        {
            Profile->EditorActiveDropletLayer = 0u;
        }
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

void SWetnessProfileEditorPanel::ApplyPreviewLayerSettingsToViewport()
{
    if (!PreviewViewport.IsValid())
    {
        return;
    }

    const UWetnessProfile* Profile = WetnessProfile.Get();
    const bool bHasSelection = HasWaterChannelSelectionAttribute.Get(false);
    const bool bSurfaceSelected = bHasSelection && IsSurfaceWaterSelectedAttribute.Get(false);
    const bool bSecondarySelected = bSurfaceSelected && IsSecondaryDropletSelected();
    const bool bSelectedChannelEnabled = bHasSelection && Profile != nullptr &&
        (bSurfaceSelected
            ? Profile->Parameters.SurfaceWater.bEnabled
            : Profile->Parameters.AbsorbedWetness.bEnabled);
    const bool bSimulation = SelectedPreviewBehaviorItem.IsValid() &&
        *SelectedPreviewBehaviorItem == SWetnessProfileViewport::EPreviewBehavior::Simulation;

    bool bEffectiveAbsorbed = Profile != nullptr && Profile->Parameters.AbsorbedWetness.bEnabled;
    bool bEffectiveSurface = Profile != nullptr && Profile->Parameters.SurfaceWater.bEnabled;
    bool bEffectiveDroplet1 = bEffectiveSurface;
    bool bEffectiveDroplet2 = bEffectiveSurface &&
        Profile->Parameters.SurfaceWater.bUseSecondaryDroplets;

    if (bSimulation)
    {
        bEffectiveAbsorbed = bHasSelection && !bSurfaceSelected && bEffectiveAbsorbed;
        bEffectiveSurface = bSurfaceSelected && bEffectiveSurface;
        bEffectiveDroplet1 = bEffectiveSurface && !bSecondarySelected;
        bEffectiveDroplet2 = bEffectiveSurface && bSecondarySelected &&
            Profile->Parameters.SurfaceWater.bUseSecondaryDroplets;
    }

    bPreviewAbsorbedLayerEnabled = bEffectiveAbsorbed;
    bPreviewSurfaceLayerEnabled = bEffectiveSurface;
    bPreviewDroplet1Enabled = bEffectiveDroplet1;
    bPreviewDroplet2Enabled = bEffectiveDroplet2;

    PreviewViewport->SetPreviewSimulationTarget(
        bHasSelection,
        bSurfaceSelected,
        bSecondarySelected,
        bSelectedChannelEnabled);
    PreviewViewport->SetPreviewSimulationLayers(bEffectiveAbsorbed, bEffectiveSurface);
    PreviewViewport->SetPreviewDropletVisibility(bEffectiveDroplet1, bEffectiveDroplet2);
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
    PreviewViewport->SetPreviewAnimationSpeed(
        SelectedPreviewSpeedItem.IsValid() ? *SelectedPreviewSpeedItem : 1.0f);
    PreviewViewport->SetPreviewLoopEnabled(true);
}

#undef LOCTEXT_NAMESPACE
