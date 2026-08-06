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

    ChildSlot
        [
            SNew(SVerticalBox)

            + SVerticalBox::Slot()
            .FillHeight(1.0f)
            .Padding(8.0f, 8.0f, 8.0f, 0.0f)
            [
                SNew(SBorder)
                .Padding(0.0f)
                .BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
                [
                    SAssignNew(PreviewViewport, SWetnessProfileViewport)
                    .WetnessProfile(WetnessProfile.Get())
                ]
            ]

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(8.0f, 8.0f, 8.0f, 8.0f)
            [
                BuildPreviewControlsSection()
            ]
        ];

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
        .Padding(FMargin(8.0f, 6.0f))
        .BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
        .BorderBackgroundColor(FLinearColor(0.035f, 0.038f, 0.044f, 1.0f))
        [
            SNew(SHorizontalBox)

            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            .Padding(0.0f, 0.0f, 10.0f, 0.0f)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("PreviewHeading", "Preview"))
                .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 15))
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
                .ToolTipText(LOCTEXT(
                    "FramePreviewMeshTooltip",
                    "Frame the preview mesh in the viewport."))
                .OnClicked(this, &SWetnessProfileEditorPanel::HandleFramePreviewMeshClicked)
                .ContentPadding(FMargin(9.0f, 3.0f))
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
        .MinDesiredHeight(176.0f)
        [
            SNew(SBorder)
            .Padding(FMargin(8.0f))
            .BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
            .BorderBackgroundColor(FLinearColor(0.028f, 0.031f, 0.037f, 1.0f))
            [
                SNew(SHorizontalBox)

                + SHorizontalBox::Slot()
                .FillWidth(0.60f)
                .Padding(0.0f, 0.0f, 10.0f, 0.0f)
                [
                    SNew(SVerticalBox)

                    + SVerticalBox::Slot()
                    .AutoHeight()
                    [
                        BuildPreviewModeSection()
                    ]

                    + SVerticalBox::Slot()
                    .AutoHeight()
                    .Padding(0.0f, 8.0f)
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

                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(0.0f, 0.0f, 10.0f, 0.0f)
                [
                    SNew(SSeparator)
                    .Orientation(Orient_Vertical)
                ]

                + SHorizontalBox::Slot()
                .FillWidth(0.40f)
                [
                    BuildPreviewSettingsSection()
                ]
            ]
        ];
}

TSharedRef<SWidget> SWetnessProfileEditorPanel::BuildPreviewWaterSection()
{
    const auto BuildSliderRow = [](
        const FText& Label,
        const TAttribute<float>& Value,
        const FOnFloatValueChanged& OnChanged,
        const TAttribute<FText>& ValueText,
        const TAttribute<bool>& IsEnabled,
        const float MaxValue)
    {
        return SNew(SBox)
            .MinDesiredHeight(34.0f)
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
                        .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 10))
                    ]
                ]

                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                .VAlign(VAlign_Center)
                .Padding(8.0f, 0.0f)
                [
                    SNew(SSlider)
                    .MinValue(0.0f)
                    .MaxValue(MaxValue)
                    .Value(Value)
                    .OnValueChanged(OnChanged)
                ]

                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                [
                    SNew(SBox)
                    .WidthOverride(52.0f)
                    [
                        SNew(STextBlock)
                        .Justification(ETextJustify::Right)
                        .Text(ValueText)
                        .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 10))
                    ]
                ]
            ];
    };

    return SNew(SVerticalBox)

        + SVerticalBox::Slot()
        .AutoHeight()
        [
            BuildSliderRow(
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
                    return Profile != nullptr && Profile->Parameters.AbsorbedWetness.bEnabled;
                }),
                100.0f)
        ]

        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, 4.0f, 0.0f, 0.0f)
        [
            BuildSliderRow(
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
                    return Profile != nullptr && Profile->Parameters.SurfaceWater.bEnabled;
                }),
                100.0f)
        ]

        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, 4.0f, 0.0f, 0.0f)
        [
            SNew(SBox)
            .Visibility(this, &SWetnessProfileEditorPanel::GetSelectedPreviewDetailSizeVisibility)
            [
                BuildSliderRow(
                    LOCTEXT("PreviewDetailSizeLabel", "Detail Size"),
                    TAttribute<float>::Create(TAttribute<float>::FGetter::CreateSP(
                        this, &SWetnessProfileEditorPanel::GetSelectedPreviewDetailSize)),
                    FOnFloatValueChanged::CreateSP(
                        this, &SWetnessProfileEditorPanel::HandleSelectedPreviewDetailSizeChanged),
                    TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(
                        this, &SWetnessProfileEditorPanel::GetSelectedPreviewDetailSizeText)),
                    TAttribute<bool>::CreateLambda([this]()
                    {
                        const UWetnessProfile* Profile = WetnessProfile.Get();
                        return Profile != nullptr &&
                            Profile->Parameters.SurfaceWater.bEnabled &&
                            (!IsSecondaryDropletSelected() ||
                                Profile->Parameters.SurfaceWater.bUseSecondaryDroplets);
                    }),
                    4.0f)
            ]
        ];
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
        const FText& Label,
        const FText& Tooltip)
    {
        return SNew(SBorder)
            .BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
            .BorderBackgroundColor_Lambda([this, Behavior]()
            {
                const bool bSelected =
                    SelectedPreviewBehaviorItem.IsValid() &&
                    *SelectedPreviewBehaviorItem == Behavior;
                return bSelected
                    ? FStyleColors::Primary.GetSpecifiedColor()
                    : FLinearColor(0.19f, 0.20f, 0.23f, 1.0f);
            })
            .Padding(1.0f)
            [
                SNew(SBorder)
                .BorderImage(FCoreStyle::Get().GetBrush(TEXT("WhiteBrush")))
                .BorderBackgroundColor_Lambda([this, Behavior]()
                {
                    const bool bSelected =
                        SelectedPreviewBehaviorItem.IsValid() &&
                        *SelectedPreviewBehaviorItem == Behavior;
                    return bSelected
                        ? FStyleColors::Select.GetSpecifiedColor()
                        : FLinearColor(0.068f, 0.071f, 0.080f, 1.0f);
                })
                [
                    SNew(SBox)
                    .HeightOverride(42.0f)
                    [
                        SNew(SButton)
                        .ButtonStyle(FAppStyle::Get(), TEXT("NoBorder"))
                        .ContentPadding(FMargin(12.0f, 0.0f))
                        .HAlign(HAlign_Center)
                        .VAlign(VAlign_Center)
                        .ToolTipText(Tooltip)
                        .OnClicked_Lambda([SelectBehavior, Behavior]()
                        {
                            SelectBehavior(Behavior);
                            return FReply::Handled();
                        })
                        [
                            SNew(SBox)
                            .HAlign(HAlign_Center)
                            .VAlign(VAlign_Center)
                            [
                                SNew(STextBlock)
                                .Text(Label)
                                .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 12))
                                .ColorAndOpacity(FSlateColor(FLinearColor::White))
                                .Justification(ETextJustify::Center)
                            ]
                        ]
                    ]
                ]
            ];
    };

    return SNew(SHorizontalBox)

        + SHorizontalBox::Slot()
        .FillWidth(1.0f)
        .Padding(0.0f, 0.0f, 3.0f, 0.0f)
        [
            BuildModeButton(
                SWetnessProfileViewport::EPreviewBehavior::Manual,
                LOCTEXT("PreviewBehaviorStaticSegment", "Static"),
                LOCTEXT(
                    "PreviewBehaviorStaticTooltip",
                    "Adjust fixed wetness values without advancing the simulation."))
        ]

        + SHorizontalBox::Slot()
        .FillWidth(1.0f)
        .Padding(3.0f, 0.0f, 0.0f, 0.0f)
        [
            BuildModeButton(
                SWetnessProfileViewport::EPreviewBehavior::Simulation,
                LOCTEXT("PreviewBehaviorSimulationSegment", "Simulation"),
                LOCTEXT(
                    "PreviewBehaviorSimulationTooltip",
                    "Add water and preview spreading and drying over time."))
        ];
}

TSharedRef<SWidget> SWetnessProfileEditorPanel::BuildPreviewSimulationSection()
{
    return SNew(SVerticalBox)

        + SVerticalBox::Slot()
        .AutoHeight()
        [
            SNew(SHorizontalBox)

            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            .Padding(0.0f, 0.0f, 8.0f, 0.0f)
            [
                SNew(SButton)
                .ContentPadding(FMargin(12.0f, 6.0f))
                .ToolTipText(LOCTEXT(
                    "AddWaterTooltip",
                    "Add one water contact at the center cursor."))
                .IsEnabled(this, &SWetnessProfileEditorPanel::IsSelectedWaterChannelEnabled)
                .OnClicked(this, &SWetnessProfileEditorPanel::HandleApplySplashClicked)
                [
                    SNew(SHorizontalBox)

                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .VAlign(VAlign_Center)
                    [
                        SNew(SImage)
                        .Image(FDWCEditorStyle::GetBrush(
                            TEXT("DWCEditor.WetnessProfile.AddWater")))
                    ]

                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .VAlign(VAlign_Center)
                    .Padding(6.0f, 0.0f, 0.0f, 0.0f)
                    [
                        SNew(STextBlock)
                        .Text(LOCTEXT("AddWaterLabel", "Add Water"))
                        .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 10))
                    ]
                ]
            ]

            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            [
                SNew(SButton)
                .IsEnabled(this, &SWetnessProfileEditorPanel::IsSelectedWaterChannelEnabled)
                .ToolTipText(LOCTEXT(
                    "ResetWaterTooltip",
                    "Clear all preview water. Water is never reset automatically."))
                .OnClicked(this, &SWetnessProfileEditorPanel::HandleRestartSimulationClicked)
                .ContentPadding(FMargin(12.0f, 6.0f))
                [
                    SNew(STextBlock)
                    .Text(LOCTEXT("ResetWaterLabel", "Reset"))
                    .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 10))
                ]
            ]
        ]

        + SVerticalBox::Slot()
        .AutoHeight()
        .Padding(0.0f, 10.0f, 0.0f, 0.0f)
        [
            SNew(STextBlock)
            .Text(LOCTEXT(
                "AddWaterSinglePressHint",
                "Press Space or click Add Water to place one contact."))
            .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 10))
            .ColorAndOpacity(FSlateColor::UseSubduedForeground())
        ];
}

TSharedRef<SWidget> SWetnessProfileEditorPanel::BuildPreviewSettingsSection()
{
    return SNew(SBorder)
        .Padding(FMargin(10.0f, 8.0f))
        .BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
        .BorderBackgroundColor(FLinearColor(0.040f, 0.044f, 0.052f, 1.0f))
        [
            SNew(SVerticalBox)

            + SVerticalBox::Slot()
            .AutoHeight()
            [
                SNew(STextBlock)
                .Text(LOCTEXT("PreviewSettingsHeading", "Preview Settings"))
                .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Bold"), 12))
            ]

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 7.0f, 0.0f, 0.0f)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("PreviewMeshSettingLabel", "Preview Mesh"))
                .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 10))
            ]

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 3.0f, 0.0f, 0.0f)
            [
                SNew(SObjectPropertyEntryBox)
                .AllowedClass(USkeletalMesh::StaticClass())
                .AllowClear(true)
                .ObjectPath(this, &SWetnessProfileEditorPanel::GetCurrentPreviewMeshObjectPath)
                .OnObjectChanged(this, &SWetnessProfileEditorPanel::HandleCurrentPreviewMeshChanged)
            ]

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 6.0f, 0.0f, 0.0f)
            [
                SNew(SHorizontalBox)

                + SHorizontalBox::Slot()
                .AutoWidth()
                [
                    SNew(SCheckBox)
                    .Style(FAppStyle::Get(), TEXT("RadioButton"))
                    .IsChecked(this, &SWetnessProfileEditorPanel::GetReferencedMeshSourceState)
                    .OnCheckStateChanged(this, &SWetnessProfileEditorPanel::HandleReferencedMeshSourceChanged)
                    .ToolTipText(LOCTEXT(
                        "ReferencedPreviewMeshSourceTooltip",
                        "Edit the preview mesh reference stored on this Wetness Profile asset."))
                    [
                        SNew(STextBlock)
                        .Text(LOCTEXT("ReferencedPreviewMeshSource", "Referenced"))
                    ]
                ]

                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(12.0f, 0.0f, 0.0f, 0.0f)
                [
                    SNew(SCheckBox)
                    .Style(FAppStyle::Get(), TEXT("RadioButton"))
                    .IsChecked(this, &SWetnessProfileEditorPanel::GetTemporaryMeshSourceState)
                    .OnCheckStateChanged(this, &SWetnessProfileEditorPanel::HandleTemporaryMeshSourceChanged)
                    .ToolTipText(LOCTEXT(
                        "TemporaryPreviewMeshSourceTooltip",
                        "Use a preview-only mesh override. This selection is not saved to the Wetness Profile asset."))
                    [
                        SNew(STextBlock)
                        .Text(LOCTEXT("TemporaryPreviewMeshSource", "Temporary Override"))
                    ]
                ]
            ]

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 3.0f, 0.0f, 0.0f)
            [
                SNew(STextBlock)
                .Text_Lambda([this]()
                {
                    return bUseTemporaryPreviewMesh
                        ? LOCTEXT("TemporaryPreviewMeshHelp", "Preview only — not saved with the asset.")
                        : LOCTEXT("ReferencedPreviewMeshHelp", "Saved as this asset's preview mesh.");
                })
                .Font(FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), 9))
                .ColorAndOpacity(FSlateColor::UseSubduedForeground())
            ]

            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 8.0f, 0.0f, 0.0f)
            [
                SNew(SHorizontalBox)

                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                [
                    SNew(SBox)
                    .WidthOverride(86.0f)
                    [
                        SNew(STextBlock)
                        .Text(LOCTEXT("PreviewCursorSizeLabel", "Cursor Size"))
                    ]
                ]

                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                .VAlign(VAlign_Center)
                .Padding(6.0f, 0.0f)
                [
                    SNew(SSlider)
                    .MinValue(0.5f)
                    .MaxValue(2.5f)
                    .Value(this, &SWetnessProfileEditorPanel::GetPreviewCursorScale)
                    .OnValueChanged(this, &SWetnessProfileEditorPanel::HandlePreviewCursorScaleChanged)
                ]

                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                [
                    SNew(SBox)
                    .WidthOverride(42.0f)
                    [
                        SNew(STextBlock)
                        .Text(this, &SWetnessProfileEditorPanel::GetPreviewCursorScaleText)
                        .Justification(ETextJustify::Right)
                    ]
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
    PreviewCursorScale = FMath::Clamp(InValue, 0.5f, 2.5f);
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
    PreviewViewport->SetInteractionCursorScale(PreviewCursorScale);
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
