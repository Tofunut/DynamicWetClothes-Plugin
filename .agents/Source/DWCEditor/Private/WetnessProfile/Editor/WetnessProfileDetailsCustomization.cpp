#include "WetnessProfile/Editor/WetnessProfileDetailsCustomization.h"

#include "DataAssets/WetnessProfile.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailGroup.h"
#include "IDetailPropertyRow.h"
#include "IPropertyUtilities.h"
#include "PropertyHandle.h"
#include "ScopedTransaction.h"
#include "UObject/UnrealType.h"
#include "Styling/AppStyle.h"
#include "WetnessProfile/Editor/WetnessProfileEditorPolicy.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "WetnessProfileDetailsCustomization"

namespace
{
#if WITH_EDITORONLY_DATA
    void NotifyDetailsPreviewDisplayFilterChanged(UWetnessProfile& Profile, const FName PropertyName)
    {
        if (FProperty* Property = FindFProperty<FProperty>(UWetnessProfile::StaticClass(), PropertyName))
        {
            FPropertyChangedEvent Event(Property, EPropertyChangeType::ValueSet);
            Profile.PostEditChangeProperty(Event);
        }
    }
#endif

    bool ReadBoolProperty(const TWeakPtr<IPropertyHandle> WeakHandle)
    {
        const TSharedPtr<IPropertyHandle> Handle = WeakHandle.Pin();
        bool bValue = false;
        return Handle.IsValid()
            && Handle->GetValue(bValue) == FPropertyAccess::Success
            && bValue;
    }

    TAttribute<bool> EnabledWhen(const TSharedPtr<IPropertyHandle>& Handle)
    {
        return TAttribute<bool>::CreateLambda(
            [WeakHandle = TWeakPtr<IPropertyHandle>(Handle)]()
            {
                return ReadBoolProperty(WeakHandle);
            });
    }

    TAttribute<bool> EnabledWhenBoth(
        const TSharedPtr<IPropertyHandle>& First,
        const TSharedPtr<IPropertyHandle>& Second)
    {
        return TAttribute<bool>::CreateLambda(
            [FirstWeak = TWeakPtr<IPropertyHandle>(First),
             SecondWeak = TWeakPtr<IPropertyHandle>(Second)]()
            {
                return ReadBoolProperty(FirstWeak) && ReadBoolProperty(SecondWeak);
            });
    }

    void ConfigurePrimaryCategory(IDetailCategoryBuilder& Category, const int32 SortOrder)
    {
        Category.SetSortOrder(SortOrder);
        Category.InitiallyCollapsed(false);
        Category.RestoreExpansionState(true);
    }

    TFunction<float(float)> MakeLinearRawToPercent(
        const float RawAtHundredPercent,
        const float MaxDisplayPercent = 100.0f)
    {
        return [RawAtHundredPercent, MaxDisplayPercent](const float RawValue)
        {
            if (RawAtHundredPercent <= KINDA_SMALL_NUMBER)
            {
                return 0.0f;
            }
            return FMath::Clamp(RawValue / RawAtHundredPercent * 100.0f, 0.0f, MaxDisplayPercent);
        };
    }

    TFunction<float(float)> MakeLinearPercentToRaw(
        const float RawAtHundredPercent,
        const float MaxDisplayPercent = 100.0f)
    {
        return [RawAtHundredPercent, MaxDisplayPercent](const float DisplayValue)
        {
            return FMath::Clamp(DisplayValue, 0.0f, MaxDisplayPercent) * 0.01f * RawAtHundredPercent;
        };
    }

    TFunction<float(float)> MakeSquaredRawToPercent(const float RawAtHundredPercent)
    {
        return [RawAtHundredPercent](const float RawValue)
        {
            if (RawAtHundredPercent <= KINDA_SMALL_NUMBER || RawValue <= 0.0f)
            {
                return 0.0f;
            }
            return FMath::Clamp(FMath::Sqrt(RawValue / RawAtHundredPercent), 0.0f, 1.0f) * 100.0f;
        };
    }

    TFunction<float(float)> MakeSquaredPercentToRaw(const float RawAtHundredPercent)
    {
        return [RawAtHundredPercent](const float DisplayValue)
        {
            const float Percent = FMath::Clamp(DisplayValue, 0.0f, 100.0f) * 0.01f;
            return RawAtHundredPercent * Percent * Percent;
        };
    }

    float CalculateMidpointExponent(const float RawAtFiftyPercent, const float RawAtHundredPercent)
    {
        if (RawAtFiftyPercent <= KINDA_SMALL_NUMBER || RawAtHundredPercent <= RawAtFiftyPercent)
        {
            return 1.0f;
        }
        return FMath::Loge(RawAtFiftyPercent / RawAtHundredPercent) / FMath::Loge(0.5f);
    }

    TFunction<float(float)> MakeMidpointRawToPercent(
        const float RawAtFiftyPercent,
        const float RawAtHundredPercent)
    {
        return [RawAtFiftyPercent, RawAtHundredPercent](const float RawValue)
        {
            if (RawAtHundredPercent <= KINDA_SMALL_NUMBER || RawValue <= 0.0f)
            {
                return 0.0f;
            }

            const float Exponent = CalculateMidpointExponent(RawAtFiftyPercent, RawAtHundredPercent);
            const float Normalized = FMath::Clamp(RawValue / RawAtHundredPercent, 0.0f, 1.0f);
            return FMath::Pow(Normalized, 1.0f / Exponent) * 100.0f;
        };
    }

    TFunction<float(float)> MakeMidpointPercentToRaw(
        const float RawAtFiftyPercent,
        const float RawAtHundredPercent)
    {
        return [RawAtFiftyPercent, RawAtHundredPercent](const float DisplayValue)
        {
            const float Percent = FMath::Clamp(DisplayValue, 0.0f, 100.0f) * 0.01f;
            if (Percent <= 0.0f)
            {
                return 0.0f;
            }

            const float Exponent = CalculateMidpointExponent(RawAtFiftyPercent, RawAtHundredPercent);
            return RawAtHundredPercent * FMath::Pow(Percent, Exponent);
        };
    }

    ECheckBoxState GetBoolCheckState(const TWeakPtr<IPropertyHandle> WeakHandle)
    {
        return ReadBoolProperty(WeakHandle)
            ? ECheckBoxState::Checked
            : ECheckBoxState::Unchecked;
    }

    void SetBoolProperty(const TWeakPtr<IPropertyHandle> WeakHandle, const ECheckBoxState NewState)
    {
        if (const TSharedPtr<IPropertyHandle> Handle = WeakHandle.Pin())
        {
            Handle->SetValue(NewState == ECheckBoxState::Checked);
        }
    }

    void ConfigureSurfaceTypeGroupHeader(
        IDetailGroup& Group,
        const TSharedPtr<IPropertyHandle>& EnabledHandle,
        const FText& Title,
        const FText& Description,
        const TAttribute<bool>& ParentEnabled,
        const FLinearColor& HeaderTint)
    {
        Group.HeaderRow()
            .WholeRowContent()
            [
                SNew(SBorder)
                .Padding(FMargin(8.0f, 5.0f))
                .BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
                .BorderBackgroundColor(HeaderTint)
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot()
                    .FillWidth(1.0f)
                    .VAlign(VAlign_Center)
                    [
                        SNew(SVerticalBox)
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        [
                            SNew(STextBlock)
                            .Text(Title)
                            .Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.BoldFont")))
                        ]
                        + SVerticalBox::Slot()
                        .AutoHeight()
                        .Padding(0.0f, 2.0f, 0.0f, 0.0f)
                        [
                            SNew(STextBlock)
                            .Text(Description)
                            .ColorAndOpacity(FSlateColor::UseSubduedForeground())
                        ]
                    ]
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .VAlign(VAlign_Center)
                    .Padding(12.0f, 0.0f, 0.0f, 0.0f)
                    [
                        SNew(SCheckBox)
                        .IsEnabled(ParentEnabled)
                        .IsChecked_Lambda([WeakHandle = TWeakPtr<IPropertyHandle>(EnabledHandle)]()
                        {
                            return GetBoolCheckState(WeakHandle);
                        })
                        .OnCheckStateChanged_Lambda([WeakHandle = TWeakPtr<IPropertyHandle>(EnabledHandle)](const ECheckBoxState NewState)
                        {
                            SetBoolProperty(WeakHandle, NewState);
                        })
                        [
                            SNew(STextBlock)
                            .Text(LOCTEXT("SurfaceTypeEnabled", "Enabled"))
                        ]
                    ]
                ]
            ];
    }

}

FWetnessProfileDetailsCustomization::FWetnessProfileDetailsCustomization(
    const EWetnessProfileDetailsMode InMode)
    : Mode(InMode)
{
}

TSharedRef<IDetailCustomization> FWetnessProfileDetailsCustomization::MakeInstance()
{
    return MakeShared<FWetnessProfileDetailsCustomization>(EWetnessProfileDetailsMode::Combined);
}

TSharedRef<IDetailCustomization> FWetnessProfileDetailsCustomization::MakeInstance(
    const EWetnessProfileDetailsMode InMode)
{
    return MakeShared<FWetnessProfileDetailsCustomization>(InMode);
}

void FWetnessProfileDetailsCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
    TArray<TWeakObjectPtr<UObject>> Objects;
    DetailBuilder.GetObjectsBeingCustomized(Objects);
    if (Objects.Num() != 1)
    {
        return;
    }

    Profile = Cast<UWetnessProfile>(Objects[0].Get());
    PropertyUtilities = DetailBuilder.GetPropertyUtilities();
    CollectedProperties.Reset();

    const TSharedRef<IPropertyHandle> ParametersHandle = DetailBuilder.GetProperty(
        TEXT("Parameters"),
        UWetnessProfile::StaticClass());
    if (!ParametersHandle->IsValidHandle())
    {
        return;
    }

    // Remove the default nested-struct presentation. Every supported field is
    // re-added below under explicit channel, simulation, and rendering categories.
    DetailBuilder.HideProperty(ParametersHandle);
    DetailBuilder.HideCategory(TEXT("Parameters"));
    DetailBuilder.HideCategory(TEXT("Absorbed Wetness"));
    DetailBuilder.HideCategory(TEXT("AbsorbedWetness"));
    DetailBuilder.HideCategory(TEXT("Surface Water"));
    DetailBuilder.HideCategory(TEXT("SurfaceWater"));

    // These asset-level categories are redundant in the dedicated editor.
    // Channel parameters are rebuilt below, while preview mesh controls live
    // exclusively in the separate Preview tab.
    DetailBuilder.HideCategory(TEXT("Wetness Profile"));
    DetailBuilder.HideCategory(TEXT("WetnessProfile"));
    DetailBuilder.HideCategory(TEXT("Preview"));

    const TSharedPtr<IPropertyHandle> AbsorbedStructHandle =
        ParametersHandle->GetChildHandle(TEXT("AbsorbedWetness"));
    const TSharedPtr<IPropertyHandle> SurfaceStructHandle =
        ParametersHandle->GetChildHandle(TEXT("SurfaceWater"));
    if (AbsorbedStructHandle.IsValid())
    {
        DetailBuilder.HideProperty(AbsorbedStructHandle);
    }
    if (SurfaceStructHandle.IsValid())
    {
        DetailBuilder.HideProperty(SurfaceStructHandle);
    }

    // This is an editor workflow preference, not part of the physical/material
    // profile contract, so keep it out of the dedicated profile editor.
    const TSharedRef<IPropertyHandle> PreferredDirectoryHandle = DetailBuilder.GetProperty(
        TEXT("PreferredSaveDirectory"),
        UWetnessProfile::StaticClass());
    if (PreferredDirectoryHandle->IsValidHandle())
    {
        DetailBuilder.HideProperty(PreferredDirectoryHandle);
    }

    const TSharedRef<IPropertyHandle> PreviewSkeletalMeshHandle = DetailBuilder.GetProperty(
        TEXT("PreviewSkeletalMesh"),
        UWetnessProfile::StaticClass());
    if (PreviewSkeletalMeshHandle->IsValidHandle())
    {
        DetailBuilder.HideProperty(PreviewSkeletalMeshHandle);
    }

    CollectPropertiesRecursive(ParametersHandle, TEXT("Parameters"));
    RefreshValidationIssues();

    IDetailCategoryBuilder& ValidationCategory = DetailBuilder.EditCategory(
        TEXT("DWCValidation"),
        LOCTEXT("ValidationCategory", "Validation"),
        ECategoryPriority::Important);
    ValidationCategory.SetSortOrder(0);
    ValidationCategory.SetCategoryVisibility(!ValidationIssues.IsEmpty());
    ValidationCategory.AddCustomRow(LOCTEXT("InvalidValuesFilter", "Invalid out of range values"))
        .WholeRowContent()
        [
            SNew(SBorder)
            .BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
            .Padding(FMargin(6.0f, 4.0f))
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Top)
                .Padding(0.0f, 1.0f, 8.0f, 0.0f)
                [
                    SNew(SImage)
                    .Image(FAppStyle::GetBrush(TEXT("Icons.WarningWithColor")))
                ]
                + SHorizontalBox::Slot()
                .FillWidth(1.0f)
                .VAlign(VAlign_Center)
                [
                    SNew(STextBlock)
                    .AutoWrapText(true)
                    .Text(this, &FWetnessProfileDetailsCustomization::GetValidationText)
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                .VAlign(VAlign_Center)
                .Padding(10.0f, 0.0f, 0.0f, 0.0f)
                [
                    SNew(SButton)
                    .Text(LOCTEXT("ClampValues", "Fix Values"))
                    .ToolTipText(LOCTEXT(
                        "ClampValuesTooltip",
                        "Clamp invalid values to the safe range before previewing, baking, or saving the profile."))
                    .OnClicked(this, &FWetnessProfileDetailsCustomization::HandleClampValuesClicked)
                ]
            ]
        ];

    const TSharedPtr<IPropertyHandle> AbsorbedEnabled =
        FindPropertyByPath(TEXT("Parameters.AbsorbedWetness.bEnabled"));
    const TSharedPtr<IPropertyHandle> SurfaceEnabled =
        FindPropertyByPath(TEXT("Parameters.SurfaceWater.bEnabled"));
    const TSharedPtr<IPropertyHandle> SecondaryDropletsEnabled =
        FindPropertyByPath(TEXT("Parameters.SurfaceWater.bUseSecondaryDroplets"));

    const TAttribute<bool> AbsorbedSettingsEnabled = EnabledWhen(AbsorbedEnabled);
    const TAttribute<bool> SurfaceSettingsEnabled = EnabledWhen(SurfaceEnabled);

    const bool bShowAbsorbed = Mode != EWetnessProfileDetailsMode::SurfaceWater;
    const bool bShowSurface = Mode != EWetnessProfileDetailsMode::AbsorbedWater;
    const bool bSingleChannel = Mode != EWetnessProfileDetailsMode::Combined;
#if WITH_EDITORONLY_DATA
    const bool bSecondaryDropletsEnabled = Profile.IsValid() &&
        Profile->Parameters.SurfaceWater.bUseSecondaryDroplets;
    if (Profile.IsValid() &&
        !bSecondaryDropletsEnabled &&
        Profile->EditorActiveDropletLayer == 1u)
    {
        Profile->EditorActiveDropletLayer = 0u;
    }
    const uint8 ActiveDropletLayer = Profile.IsValid()
        ? FMath::Min<uint8>(Profile->EditorActiveDropletLayer, static_cast<uint8>(1))
        : 0u;
    const bool bShowDroplet1 = ActiveDropletLayer == 0u || !bSecondaryDropletsEnabled;
    const bool bShowDroplet2 = ActiveDropletLayer == 1u && bSecondaryDropletsEnabled;
#else
    constexpr bool bSecondaryDropletsEnabled = false;
    constexpr bool bShowDroplet1 = true;
    constexpr bool bShowDroplet2 = false;
#endif

    if (bShowAbsorbed)
    {
        if (!bSingleChannel)
        {
            IDetailCategoryBuilder& GeneralCategory = DetailBuilder.EditCategory(
                TEXT("DWCAbsorbedGeneral"),
                LOCTEXT("AbsorbedWaterCategory", "Absorbed Water"),
                ECategoryPriority::Important);
            ConfigurePrimaryCategory(GeneralCategory, 5);
            AddDefaultProperty(
                GeneralCategory,
                AbsorbedEnabled,
                LOCTEXT("EnableAbsorbedWater", "Enabled"),
                LOCTEXT("EnableAbsorbedWaterTooltip", "Enable absorbed water, including spreading, drying, and appearance changes."));
        }

        IDetailCategoryBuilder& SimulationCategory = DetailBuilder.EditCategory(
            TEXT("DWCAbsorbedSimulation"),
            bSingleChannel
                ? LOCTEXT("AbsorbedSimulationCategory", "Simulation")
                : LOCTEXT("CombinedAbsorbedSimulationCategory", "Absorbed Water | Simulation"),
            ECategoryPriority::Important);
        ConfigurePrimaryCategory(SimulationCategory, bSingleChannel ? 5 : 10);

        AddFloatProperty(
            SimulationCategory,
            FindPropertyByPath(TEXT("Parameters.AbsorbedWetness.AbsorptionFraction")),
            LOCTEXT("Absorption", "Absorption"),
            LOCTEXT("AbsorptionTooltip", "Amount of incoming water routed to absorbed water. Lower values leave more water available for surface effects."),
            0.0f, 1.0f, 0.0f, 1.0f, 0.01f, 100.0f, 1, LOCTEXT("PercentSuffix1", "%"),
            AbsorbedSettingsEnabled);
        AddMappedFloatProperty(
            SimulationCategory,
            FindPropertyByPath(TEXT("Parameters.AbsorbedWetness.SpreadRate")),
            LOCTEXT("SpreadSpeed", "Spread Speed"),
            LOCTEXT("SpreadSpeedTooltip", "How quickly absorbed water spreads across connected surface samples."),
            0.0f, 100.0f, 0.0f, 100.0f, 1.0f, 1, LOCTEXT("PercentSuffixSpread", "%"),
            MakeMidpointRawToPercent(6.5f, 10.0f), MakeMidpointPercentToRaw(6.5f, 10.0f), 0.0f, 10.0f,
            AbsorbedSettingsEnabled);
        AddMappedFloatProperty(
            SimulationCategory,
            FindPropertyByPath(TEXT("Parameters.AbsorbedWetness.DryRate")),
            LOCTEXT("DryingSpeed", "Drying Speed"),
            LOCTEXT("DryingSpeedTooltip", "How quickly absorbed water fades over time."),
            0.0f, 100.0f, 0.0f, 100.0f, 1.0f, 1, LOCTEXT("PercentSuffixDrying", "%"),
            MakeMidpointRawToPercent(20.0f, 40.0f), MakeMidpointPercentToRaw(20.0f, 40.0f), 0.0f, 100.0f,
            AbsorbedSettingsEnabled);
        AddMappedFloatProperty(
            SimulationCategory,
            FindPropertyByPath(TEXT("Parameters.AbsorbedWetness.GravityFlowStrength")),
            LOCTEXT("GravityInfluence", "Gravity Influence"),
            LOCTEXT("GravityInfluenceTooltip", "How much absorbed water prefers to spread downward instead of evenly in all directions."),
            0.0f, 100.0f, 0.0f, 100.0f, 1.0f, 1, LOCTEXT("PercentSuffixGravity", "%"),
            MakeLinearRawToPercent(2.0f), MakeLinearPercentToRaw(2.0f), 0.0f, 10.0f,
            AbsorbedSettingsEnabled);

        IDetailCategoryBuilder& RenderingCategory = DetailBuilder.EditCategory(
            TEXT("DWCAbsorbedRendering"),
            bSingleChannel
                ? LOCTEXT("AbsorbedRenderingCategory", "Rendering")
                : LOCTEXT("CombinedAbsorbedRenderingCategory", "Absorbed Water | Rendering"),
            ECategoryPriority::Important);
        ConfigurePrimaryCategory(RenderingCategory, bSingleChannel ? 10 : 20);

        AddFloatProperty(
            RenderingCategory,
            FindPropertyByPath(TEXT("Parameters.AbsorbedWetness.AbsorbedDarkeningStrength")),
            LOCTEXT("Darkening", "Darkening"),
            LOCTEXT("DarkeningTooltip", "How strongly absorbed water darkens the base color."),
            0.0f, 3.0f, 0.0f, 3.0f, 0.01f, 100.0f, 1, LOCTEXT("PercentSuffix6", "%"),
            AbsorbedSettingsEnabled);
        AddFloatProperty(
            RenderingCategory,
            FindPropertyByPath(TEXT("Parameters.AbsorbedWetness.AbsorbedGlossinessStrength")),
            LOCTEXT("AbsorbedGlossiness", "Glossiness"),
            LOCTEXT("AbsorbedGlossinessTooltip", "How strongly absorbed water blends roughness toward the wet roughness target."),
            0.0f, 3.0f, 0.0f, 3.0f, 0.01f, 100.0f, 1, LOCTEXT("PercentSuffixAbsorbedGlossiness", "%"),
            AbsorbedSettingsEnabled);
    }

    if (bShowSurface)
    {
        const int32 BaseSortOrder = bSingleChannel ? 5 : 30;

        IDetailCategoryBuilder& GeneralCategory = DetailBuilder.EditCategory(
            TEXT("DWCSurfaceAmount"),
            bSingleChannel
                ? LOCTEXT("SurfaceAmountCategory", "Surface Water")
                : LOCTEXT("SurfaceWaterCategory", "Surface Water"),
            ECategoryPriority::Important);
        ConfigurePrimaryCategory(GeneralCategory, BaseSortOrder);

        if (!bSingleChannel)
        {
            AddDefaultProperty(
                GeneralCategory,
                SurfaceEnabled,
                LOCTEXT("EnableSurfaceWater", "Enabled"),
                LOCTEXT("EnableSurfaceWaterTooltip", "Enable water that remains visible on top of the material surface."));
        }

#if WITH_EDITORONLY_DATA
        GeneralCategory.AddCustomRow(LOCTEXT("DropletLayerSelectorFilter", "Primary Secondary Use Secondary Droplet Layer"))
            .WholeRowContent()
            [
                SNew(SBorder)
                .Padding(FMargin(8.0f, 6.0f))
                .BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
                [
                    SNew(SHorizontalBox)
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .VAlign(VAlign_Center)
                    .Padding(0.0f, 0.0f, 12.0f, 0.0f)
                    [
                        SNew(STextBlock)
                        .Text(LOCTEXT("DropletLayersLabel", "Droplet Layers"))
                        .Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.BoldFont")))
                        .ToolTipText(LOCTEXT(
                            "DropletLayersTooltip",
                            "Choose the Surface Water droplet layer edited below. Secondary is optional."))
                    ]
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    [
                        SNew(SCheckBox)
                        .Style(FAppStyle::Get(), TEXT("ToggleButtonCheckbox"))
                        .Padding(FMargin(12.0f, 4.0f))
                        .IsChecked(bShowDroplet1 ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
                        .OnCheckStateChanged_Lambda(
                            [WeakProfile = Profile, WeakUtilities = PropertyUtilities](const ECheckBoxState NewState)
                            {
                                if (NewState != ECheckBoxState::Checked)
                                {
                                    return;
                                }
                                if (UWetnessProfile* MutableProfile = WeakProfile.Get())
                                {
                                    MutableProfile->EditorActiveDropletLayer = 0u;
                                    NotifyDetailsPreviewDisplayFilterChanged(
                                        *MutableProfile,
                                        GET_MEMBER_NAME_CHECKED(UWetnessProfile, EditorActiveDropletLayer));
                                }
                                if (const TSharedPtr<IPropertyUtilities> Utilities = WeakUtilities.Pin())
                                {
                                    Utilities->ForceRefresh();
                                }
                            })
                        [
                            SNew(STextBlock)
                            .Text(LOCTEXT("PrimaryDropletLayer", "Primary"))
                        ]
                    ]
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .Padding(2.0f, 0.0f, 0.0f, 0.0f)
                    [
                        SNew(SCheckBox)
                        .Style(FAppStyle::Get(), TEXT("ToggleButtonCheckbox"))
                        .Padding(FMargin(12.0f, 4.0f))
                        .Visibility_Lambda([WeakProfile = Profile]()
                        {
                            const UWetnessProfile* CurrentProfile = WeakProfile.Get();
                            return CurrentProfile != nullptr &&
                                CurrentProfile->Parameters.SurfaceWater.bUseSecondaryDroplets
                                ? EVisibility::Visible
                                : EVisibility::Collapsed;
                        })
                        .IsEnabled_Lambda([WeakProfile = Profile]()
                        {
                            const UWetnessProfile* CurrentProfile = WeakProfile.Get();
                            return CurrentProfile != nullptr &&
                                CurrentProfile->Parameters.SurfaceWater.bEnabled &&
                                CurrentProfile->Parameters.SurfaceWater.bUseSecondaryDroplets;
                        })
                        .IsChecked(bShowDroplet2 ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
                        .OnCheckStateChanged_Lambda(
                            [WeakProfile = Profile, WeakUtilities = PropertyUtilities](const ECheckBoxState NewState)
                            {
                                if (NewState != ECheckBoxState::Checked)
                                {
                                    return;
                                }
                                if (UWetnessProfile* MutableProfile = WeakProfile.Get())
                                {
                                    if (!MutableProfile->Parameters.SurfaceWater.bUseSecondaryDroplets)
                                    {
                                        return;
                                    }
                                    MutableProfile->EditorActiveDropletLayer = 1u;
                                    NotifyDetailsPreviewDisplayFilterChanged(
                                        *MutableProfile,
                                        GET_MEMBER_NAME_CHECKED(UWetnessProfile, EditorActiveDropletLayer));
                                }
                                if (const TSharedPtr<IPropertyUtilities> Utilities = WeakUtilities.Pin())
                                {
                                    Utilities->ForceRefresh();
                                }
                            })
                        [
                            SNew(STextBlock)
                            .Text(LOCTEXT("SecondaryDropletLayer", "Secondary"))
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
                    [
                        SNew(SButton)
                        .IsEnabled(SurfaceSettingsEnabled)
                        .ContentPadding(FMargin(10.0f, 4.0f))
                        .ButtonColorAndOpacity_Lambda([WeakHandle = TWeakPtr<IPropertyHandle>(SecondaryDropletsEnabled)]()
                        {
                            return ReadBoolProperty(WeakHandle)
                                ? FLinearColor(0.08f, 0.48f, 0.26f, 1.0f)
                                : FLinearColor(0.22f, 0.23f, 0.26f, 1.0f);
                        })
                        .OnClicked_Lambda(
                            [WeakHandle = TWeakPtr<IPropertyHandle>(SecondaryDropletsEnabled),
                             WeakProfile = Profile,
                             WeakUtilities = PropertyUtilities]()
                            {
                                const TSharedPtr<IPropertyHandle> Handle = WeakHandle.Pin();
                                bool bCurrentValue = false;
                                if (!Handle.IsValid() ||
                                    Handle->GetValue(bCurrentValue) != FPropertyAccess::Success)
                                {
                                    return FReply::Handled();
                                }

                                const bool bNewValue = !bCurrentValue;
                                Handle->SetValue(bNewValue);
                                if (!bNewValue)
                                {
                                    if (UWetnessProfile* MutableProfile = WeakProfile.Get())
                                    {
                                        MutableProfile->EditorActiveDropletLayer = 0u;
                                        MutableProfile->bEditorShowDroplet2 = false;
                                        NotifyDetailsPreviewDisplayFilterChanged(
                                            *MutableProfile,
                                            GET_MEMBER_NAME_CHECKED(UWetnessProfile, EditorActiveDropletLayer));
                                    }
                                }
                                if (const TSharedPtr<IPropertyUtilities> Utilities = WeakUtilities.Pin())
                                {
                                    Utilities->ForceRefresh();
                                }
                                return FReply::Handled();
                            })
                        .ToolTipText(LOCTEXT(
                            "UseSecondaryDropletsTooltip",
                            "Enable the optional Secondary Droplet layer. Turning it off preserves its authored values."))
                        [
                            SNew(STextBlock)
                            .Text_Lambda([WeakHandle = TWeakPtr<IPropertyHandle>(SecondaryDropletsEnabled)]()
                            {
                                return ReadBoolProperty(WeakHandle)
                                    ? LOCTEXT("UseSecondaryDropletsOn", "Secondary · On")
                                    : LOCTEXT("UseSecondaryDropletsOff", "Secondary · Off");
                            })
                            .Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.BoldFont")))
                        ]
                    ]
                    + SHorizontalBox::Slot()
                    .AutoWidth()
                    .VAlign(VAlign_Center)
                    .Padding(4.0f, 0.0f, 0.0f, 0.0f)
                    [
                        BuildRevertButton(SecondaryDropletsEnabled)
                    ]
                ]
            ];
#endif

        IDetailCategoryBuilder& SimulationCategory = DetailBuilder.EditCategory(
            TEXT("DWCSurfaceSimulation"),
            bSingleChannel
                ? LOCTEXT("SurfaceSimulationCategory", "Simulation")
                : LOCTEXT("CombinedSurfaceSimulationCategory", "Surface Water | Simulation"),
            ECategoryPriority::Important);
        ConfigurePrimaryCategory(SimulationCategory, BaseSortOrder + 5);

        AddMappedFloatProperty(
            SimulationCategory,
            FindPropertyByPath(TEXT("Parameters.SurfaceWater.DropletDryRate")),
            LOCTEXT("DropletDryRate", "Surface Water Drying Speed"),
            LOCTEXT("DropletDryRateTooltip", "Shared fade-out speed for both Primary and Secondary Surface Water droplet layers."),
            0.0f, 100.0f, 0.0f, 100.0f, 1.0f, 1, LOCTEXT("PercentSuffixDropletDryRate", "%"),
            MakeMidpointRawToPercent(20.0f, 40.0f), MakeMidpointPercentToRaw(20.0f, 40.0f), 0.0f, 100.0f,
            SurfaceSettingsEnabled);

        if (bShowDroplet1)
        {
            AddFloatProperty(
                SimulationCategory,
                FindPropertyByPath(TEXT("Parameters.SurfaceWater.DropletSpawnProbability")),
                LOCTEXT("PrimaryDropletSpawnChance", "Spawn Chance"),
                LOCTEXT("PrimaryDropletSpawnChanceTooltip", "Chance that eligible surface water produces a Primary Droplet stamp."),
                0.0f, 1.0f, 0.0f, 1.0f, 0.01f, 100.0f, 1, LOCTEXT("PercentSuffixPrimaryDropletSpawn", "%"),
                SurfaceSettingsEnabled);
            AddStampSizeProperty(
                SimulationCategory,
                FindPropertyByPath(TEXT("Parameters.SurfaceWater.DropletRadiusPixels")),
                FindPropertyByPath(TEXT("Parameters.SurfaceWater.DropletHeightPixels")),
                LOCTEXT("PrimaryDropletStampSizeTooltip", "Primary Droplet stamp half-size in wetness-map texels. Lock Ratio updates width and height together."),
                SurfaceSettingsEnabled);
        }
        else
        {
            AddFloatProperty(
                SimulationCategory,
                FindPropertyByPath(TEXT("Parameters.SurfaceWater.DropletFlowSpawnProbability")),
                LOCTEXT("SecondaryDropletSpawnChance", "Spawn Chance"),
                LOCTEXT("SecondaryDropletSpawnChanceTooltip", "Independent chance that eligible surface water produces a Secondary Droplet stamp."),
                0.0f, 1.0f, 0.0f, 1.0f, 0.01f, 100.0f, 1, LOCTEXT("PercentSuffixSecondaryDropletSpawn", "%"),
                SurfaceSettingsEnabled);
            AddStampSizeProperty(
                SimulationCategory,
                FindPropertyByPath(TEXT("Parameters.SurfaceWater.DropletFlowRadiusPixels")),
                FindPropertyByPath(TEXT("Parameters.SurfaceWater.DropletFlowHeightPixels")),
                LOCTEXT("SecondaryDropletStampSizeTooltip", "Secondary Droplet stamp half-size in wetness-map texels. Lock Ratio updates width and height together."),
                SurfaceSettingsEnabled);
            AddFloatProperty(
                SimulationCategory,
                FindPropertyByPath(TEXT("Parameters.SurfaceWater.DropletFlowSpawnPositionSpread")),
                LOCTEXT("SecondarySpawnPositionVariation", "Spawn Position Variation"),
                LOCTEXT("SecondarySpawnPositionVariationTooltip", "Offsets the Secondary Droplet's initial spawn position inside the eligible UV triangle. It does not spread the droplet over time."),
                0.0f, 1.0f, 0.0f, 1.0f, 0.01f, 100.0f, 1, LOCTEXT("PercentSuffixSecondaryPositionVariation", "%"),
                SurfaceSettingsEnabled);
        }

        IDetailCategoryBuilder& AppearanceCategory = DetailBuilder.EditCategory(
            TEXT("DWCSurfaceRendering"),
            bSingleChannel
                ? LOCTEXT("SurfaceAppearanceCategory", "Appearance")
                : LOCTEXT("CombinedSurfaceAppearanceCategory", "Surface Water | Appearance"),
            ECategoryPriority::Important);
        ConfigurePrimaryCategory(AppearanceCategory, BaseSortOrder + 10);

        if (bShowDroplet1)
        {
            AddFloatProperty(
                AppearanceCategory,
                FindPropertyByPath(TEXT("Parameters.SurfaceWater.SurfaceWaterTotalStrength")),
                LOCTEXT("SurfaceTotalStrength", "Total Strength"),
                LOCTEXT("SurfaceTotalStrengthTooltip", "Overall Primary Droplet rendering strength after final coverage is resolved."),
                0.0f, 1.0f, 0.0f, 1.0f, 0.01f, 100.0f, 1, LOCTEXT("PercentSuffixSurfaceTotalStrength", "%"),
                SurfaceSettingsEnabled);
            AddFloatProperty(
                AppearanceCategory,
                FindPropertyByPath(TEXT("Parameters.SurfaceWater.SurfaceWaterColorBlend")),
                LOCTEXT("SurfaceColorBlend", "Color Blend"),
                LOCTEXT("SurfaceColorBlendTooltip", "How strongly Primary Droplet coverage modifies the underlying Base Color."),
                0.0f, 1.0f, 0.0f, 1.0f, 0.01f, 100.0f, 1, LOCTEXT("PercentSuffixSurfaceColorBlend", "%"),
                SurfaceSettingsEnabled);
            AddMappedFloatProperty(
                AppearanceCategory,
                FindPropertyByPath(TEXT("Parameters.SurfaceWater.SurfaceWaterNormalStrength")),
                LOCTEXT("SurfaceNormalStrength", "Water Normal Strength"),
                LOCTEXT("SurfaceNormalStrengthTooltip", "Primary Droplet normal-map strength. 100% is the authored normal texture strength."),
                0.0f, 300.0f, 0.0f, 300.0f, 1.0f, 1, LOCTEXT("PercentSuffix7", "%"),
                MakeLinearRawToPercent(1.0f, 300.0f), MakeLinearPercentToRaw(1.0f, 300.0f), 0.0f, 3.0f,
                SurfaceSettingsEnabled);
            AddFloatProperty(
                AppearanceCategory,
                FindPropertyByPath(TEXT("Parameters.SurfaceWater.SurfaceWaterTargetRoughness")),
                LOCTEXT("WaterRoughness", "Water Roughness"),
                LOCTEXT("WaterRoughnessTooltip", "Target roughness reached inside Primary Droplet coverage."),
                0.0f, 1.0f, 0.0f, 1.0f, 0.01f, 100.0f, 1, LOCTEXT("PercentSuffixWetSurfaceRoughness", "%"),
                SurfaceSettingsEnabled);
            AddFloatProperty(
                AppearanceCategory,
                FindPropertyByPath(TEXT("Parameters.SurfaceWater.SurfaceWaterRoughnessBlend")),
                LOCTEXT("WetRoughnessBlend", "Roughness Blend"),
                LOCTEXT("WetRoughnessBlendTooltip", "How strongly Primary Droplet coverage blends from source roughness to Water Roughness."),
                0.0f, 1.0f, 0.0f, 1.0f, 0.01f, 100.0f, 1, LOCTEXT("PercentSuffix8", "%"),
                SurfaceSettingsEnabled);
            AddFloatProperty(
                AppearanceCategory,
                FindPropertyByPath(TEXT("Parameters.SurfaceWater.SurfaceWaterSpecular")),
                LOCTEXT("WaterSpecular", "Water Specular"),
                LOCTEXT("WaterSpecularTooltip", "Target specular reached inside Primary Droplet coverage."),
                0.0f, 1.0f, 0.0f, 1.0f, 0.01f, 100.0f, 1, LOCTEXT("PercentSuffixWaterSpecular", "%"),
                SurfaceSettingsEnabled);
        }
        else
        {
            AddFloatProperty(
                AppearanceCategory,
                FindPropertyByPath(TEXT("Parameters.SurfaceWater.DropletFlowTotalStrength")),
                LOCTEXT("SecondaryTotalStrength", "Total Strength"),
                LOCTEXT("SecondaryTotalStrengthTooltip", "Overall Secondary Droplet rendering strength after final coverage is resolved."),
                0.0f, 1.0f, 0.0f, 1.0f, 0.01f, 100.0f, 1, LOCTEXT("PercentSuffixSecondaryTotalStrength", "%"),
                SurfaceSettingsEnabled);
            AddFloatProperty(
                AppearanceCategory,
                FindPropertyByPath(TEXT("Parameters.SurfaceWater.DropletFlowColorBlend")),
                LOCTEXT("SecondaryColorBlend", "Color Blend"),
                LOCTEXT("SecondaryColorBlendTooltip", "How strongly Secondary Droplet coverage modifies the underlying Base Color."),
                0.0f, 1.0f, 0.0f, 1.0f, 0.01f, 100.0f, 1, LOCTEXT("PercentSuffixSecondaryColorBlend", "%"),
                SurfaceSettingsEnabled);
            AddMappedFloatProperty(
                AppearanceCategory,
                FindPropertyByPath(TEXT("Parameters.SurfaceWater.DropletFlowNormalStrength")),
                LOCTEXT("SecondaryNormalStrength", "Water Normal Strength"),
                LOCTEXT("SecondaryNormalStrengthTooltip", "Secondary Droplet normal-map strength. 100% is the authored normal texture strength."),
                0.0f, 300.0f, 0.0f, 300.0f, 1.0f, 1, LOCTEXT("PercentSuffixSecondaryNormalStrength", "%"),
                MakeLinearRawToPercent(1.0f, 300.0f), MakeLinearPercentToRaw(1.0f, 300.0f), 0.0f, 3.0f,
                SurfaceSettingsEnabled);
            AddFloatProperty(
                AppearanceCategory,
                FindPropertyByPath(TEXT("Parameters.SurfaceWater.DropletFlowTargetRoughness")),
                LOCTEXT("SecondaryWaterRoughness", "Water Roughness"),
                LOCTEXT("SecondaryWaterRoughnessTooltip", "Target roughness reached inside Secondary Droplet coverage."),
                0.0f, 1.0f, 0.0f, 1.0f, 0.01f, 100.0f, 1, LOCTEXT("PercentSuffixSecondaryRoughness", "%"),
                SurfaceSettingsEnabled);
            AddFloatProperty(
                AppearanceCategory,
                FindPropertyByPath(TEXT("Parameters.SurfaceWater.DropletFlowRoughnessBlend")),
                LOCTEXT("SecondaryRoughnessBlend", "Roughness Blend"),
                LOCTEXT("SecondaryRoughnessBlendTooltip", "How strongly Secondary Droplet coverage blends from source roughness to Water Roughness."),
                0.0f, 1.0f, 0.0f, 1.0f, 0.01f, 100.0f, 1, LOCTEXT("PercentSuffixSecondaryRoughnessBlend", "%"),
                SurfaceSettingsEnabled);
            AddFloatProperty(
                AppearanceCategory,
                FindPropertyByPath(TEXT("Parameters.SurfaceWater.DropletFlowSpecular")),
                LOCTEXT("SecondaryWaterSpecular", "Water Specular"),
                LOCTEXT("SecondaryWaterSpecularTooltip", "Target specular reached inside Secondary Droplet coverage."),
                0.0f, 1.0f, 0.0f, 1.0f, 0.01f, 100.0f, 1, LOCTEXT("PercentSuffixSecondaryWaterSpecular", "%"),
                SurfaceSettingsEnabled);
        }

        IDetailCategoryBuilder& DetailTexturesCategory = DetailBuilder.EditCategory(
            TEXT("DWCSurfaceDetailTextures"),
            bSingleChannel
                ? LOCTEXT("DetailTexturesCategory", "Textures")
                : LOCTEXT("CombinedDetailTexturesCategory", "Surface Water | Textures"),
            ECategoryPriority::Important);
        ConfigurePrimaryCategory(DetailTexturesCategory, BaseSortOrder + 15);

        if (bShowDroplet1)
        {
            AddDefaultProperty(
                DetailTexturesCategory,
                FindPropertyByPath(TEXT("Parameters.SurfaceWater.DropletNormalTexture")),
                LOCTEXT("PrimaryDropletNormal", "Normal Texture"),
                LOCTEXT("PrimaryDropletNormalTooltip", "Normal texture used by Primary Droplet rendering. Empty uses the DWC default."),
                SurfaceSettingsEnabled);
            AddDefaultProperty(
                DetailTexturesCategory,
                FindPropertyByPath(TEXT("Parameters.SurfaceWater.DropletMaskTexture")),
                LOCTEXT("PrimaryDropletMask", "Mask Texture"),
                LOCTEXT("PrimaryDropletMaskTooltip", "Optional mask used to localize Primary Droplet coverage and detail."),
                SurfaceSettingsEnabled);
        }
        else
        {
            AddDefaultProperty(
                DetailTexturesCategory,
                FindPropertyByPath(TEXT("Parameters.SurfaceWater.DropletFlowNormalTexture")),
                LOCTEXT("SecondaryDropletNormal", "Normal Texture"),
                LOCTEXT("SecondaryDropletNormalTooltip", "Optional normal texture used by Secondary Droplets. Empty uses the neutral flat-normal slice and never copies the Primary texture."),
                SurfaceSettingsEnabled);
            AddDefaultProperty(
                DetailTexturesCategory,
                FindPropertyByPath(TEXT("Parameters.SurfaceWater.DropletFlowMaskTexture")),
                LOCTEXT("SecondaryDropletMask", "Mask Texture"),
                LOCTEXT("SecondaryDropletMaskTooltip", "Optional mask texture used by Secondary Droplets. Empty uses the neutral unmasked slice and never copies the Primary texture."),
                SurfaceSettingsEnabled);
        }

    }
}

void FWetnessProfileDetailsCustomization::CollectPropertiesRecursive(
    const TSharedPtr<IPropertyHandle>& Parent,
    const FString& ParentPath)
{
    if (!Parent.IsValid())
    {
        return;
    }

    uint32 ChildCount = 0;
    Parent->GetNumChildren(ChildCount);
    for (uint32 ChildIndex = 0; ChildIndex < ChildCount; ++ChildIndex)
    {
        const TSharedPtr<IPropertyHandle> Child = Parent->GetChildHandle(ChildIndex);
        if (!Child.IsValid() || !Child->IsValidHandle() || Child->GetProperty() == nullptr)
        {
            continue;
        }

        const FString ChildPath = FString::Printf(
            TEXT("%s.%s"),
            *ParentPath,
            *Child->GetProperty()->GetName());

        uint32 GrandChildCount = 0;
        Child->GetNumChildren(GrandChildCount);
        if (GrandChildCount > 0)
        {
            CollectPropertiesRecursive(Child, ChildPath);
        }
        else
        {
            CollectedProperties.Add({ Child, ChildPath });
        }
    }
}

TSharedPtr<IPropertyHandle> FWetnessProfileDetailsCustomization::FindPropertyByPath(
    const TCHAR* PropertyPath) const
{
    for (const FCollectedProperty& Collected : CollectedProperties)
    {
        if (Collected.Handle.IsValid() && Collected.Path.Equals(PropertyPath, ESearchCase::CaseSensitive))
        {
            return Collected.Handle;
        }
    }
    return nullptr;
}

bool FWetnessProfileDetailsCustomization::ResolveSavedPropertyValue(
    const TWeakPtr<IPropertyHandle> WeakHandle,
    FProperty*& OutProperty,
    void*& OutCurrentValue,
    const void*& OutSavedValue) const
{
    OutProperty = nullptr;
    OutCurrentValue = nullptr;
    OutSavedValue = nullptr;

#if WITH_EDITORONLY_DATA
    const TSharedPtr<IPropertyHandle> Handle = WeakHandle.Pin();
    const UWetnessProfile* CurrentProfile = Profile.Get();
    if (!Handle.IsValid() || !Handle->IsValidHandle() ||
        CurrentProfile == nullptr || !CurrentProfile->HasEditorSavedParametersSnapshot())
    {
        return false;
    }

    const FCollectedProperty* Collected = CollectedProperties.FindByPredicate(
        [&Handle](const FCollectedProperty& Candidate)
        {
            return Candidate.Handle == Handle;
        });
    if (Collected == nullptr)
    {
        return false;
    }

    static const FString AbsorbedPrefix(TEXT("Parameters.AbsorbedWetness."));
    static const FString SurfacePrefix(TEXT("Parameters.SurfaceWater."));

    const void* SavedContainer = nullptr;
    UScriptStruct* SavedStruct = nullptr;
    FString LeafName;
    if (Collected->Path.StartsWith(AbsorbedPrefix, ESearchCase::CaseSensitive))
    {
        SavedContainer = &CurrentProfile->GetEditorSavedParametersSnapshot().AbsorbedWetness;
        SavedStruct = FAbsorbedWetnessProfileParameters::StaticStruct();
        LeafName = Collected->Path.RightChop(AbsorbedPrefix.Len());
    }
    else if (Collected->Path.StartsWith(SurfacePrefix, ESearchCase::CaseSensitive))
    {
        SavedContainer = &CurrentProfile->GetEditorSavedParametersSnapshot().SurfaceWater;
        SavedStruct = FSurfaceWaterProfileParameters::StaticStruct();
        LeafName = Collected->Path.RightChop(SurfacePrefix.Len());
    }
    else
    {
        return false;
    }

    if (SavedStruct == nullptr || LeafName.IsEmpty() || LeafName.Contains(TEXT(".")))
    {
        return false;
    }

    OutProperty = SavedStruct->FindPropertyByName(FName(*LeafName));
    if (OutProperty == nullptr || Handle->GetValueData(OutCurrentValue) != FPropertyAccess::Success)
    {
        OutProperty = nullptr;
        OutCurrentValue = nullptr;
        return false;
    }

    OutSavedValue = OutProperty->ContainerPtrToValuePtr<void>(SavedContainer);
    return OutSavedValue != nullptr;
#else
    return false;
#endif
}

bool FWetnessProfileDetailsCustomization::IsPropertyDifferentFromSaved(
    const TWeakPtr<IPropertyHandle> WeakHandle) const
{
    FProperty* Property = nullptr;
    void* CurrentValue = nullptr;
    const void* SavedValue = nullptr;
    return ResolveSavedPropertyValue(WeakHandle, Property, CurrentValue, SavedValue) &&
        !Property->Identical(CurrentValue, SavedValue, PPF_None);
}

bool FWetnessProfileDetailsCustomization::AreStampSizePropertiesDifferentFromSaved(
    const TWeakPtr<IPropertyHandle> WeakWidth,
    const TWeakPtr<IPropertyHandle> WeakHeight) const
{
    return IsPropertyDifferentFromSaved(WeakWidth) || IsPropertyDifferentFromSaved(WeakHeight);
}

FReply FWetnessProfileDetailsCustomization::RevertPropertyToSaved(
    const TWeakPtr<IPropertyHandle> WeakHandle)
{
    const TSharedPtr<IPropertyHandle> Handle = WeakHandle.Pin();
    FProperty* Property = nullptr;
    void* CurrentValue = nullptr;
    const void* SavedValue = nullptr;
    if (!Handle.IsValid() ||
        !ResolveSavedPropertyValue(WeakHandle, Property, CurrentValue, SavedValue))
    {
        return FReply::Handled();
    }

    const FScopedTransaction Transaction(LOCTEXT("RevertWetnessProperty", "Revert Wetness Profile Property"));
    if (UWetnessProfile* MutableProfile = Profile.Get())
    {
        MutableProfile->Modify();
    }
    Handle->NotifyPreChange();
    Property->CopyCompleteValue(CurrentValue, SavedValue);
    Handle->NotifyPostChange(EPropertyChangeType::ValueSet);
    Handle->NotifyFinishedChangingProperties();
    if (UWetnessProfile* MutableProfile = Profile.Get())
    {
        MutableProfile->MarkPackageDirty();
    }
    RefreshValidationIssues();
    if (const TSharedPtr<IPropertyUtilities> Utilities = PropertyUtilities.Pin())
    {
        Utilities->ForceRefresh();
    }
    return FReply::Handled();
}

FReply FWetnessProfileDetailsCustomization::RevertStampSizeToSaved(
    const TWeakPtr<IPropertyHandle> WeakWidth,
    const TWeakPtr<IPropertyHandle> WeakHeight)
{
    const FScopedTransaction Transaction(LOCTEXT("RevertWetnessStampSize", "Revert Wetness Profile Stamp Size"));
    if (UWetnessProfile* MutableProfile = Profile.Get())
    {
        MutableProfile->Modify();
    }

    const auto RevertOne = [this](const TWeakPtr<IPropertyHandle>& WeakHandle)
    {
        const TSharedPtr<IPropertyHandle> Handle = WeakHandle.Pin();
        FProperty* Property = nullptr;
        void* CurrentValue = nullptr;
        const void* SavedValue = nullptr;
        if (!Handle.IsValid() ||
            !ResolveSavedPropertyValue(WeakHandle, Property, CurrentValue, SavedValue))
        {
            return;
        }

        Handle->NotifyPreChange();
        Property->CopyCompleteValue(CurrentValue, SavedValue);
        Handle->NotifyPostChange(EPropertyChangeType::ValueSet);
        Handle->NotifyFinishedChangingProperties();
    };

    RevertOne(WeakWidth);
    RevertOne(WeakHeight);
    if (UWetnessProfile* MutableProfile = Profile.Get())
    {
        MutableProfile->MarkPackageDirty();
    }
    RefreshValidationIssues();
    if (const TSharedPtr<IPropertyUtilities> Utilities = PropertyUtilities.Pin())
    {
        Utilities->ForceRefresh();
    }
    return FReply::Handled();
}

TSharedRef<SWidget> FWetnessProfileDetailsCustomization::BuildRevertButton(
    const TSharedPtr<IPropertyHandle>& Handle)
{
    const TWeakPtr<IPropertyHandle> WeakHandle = Handle;
    return SNew(SButton)
        .Visibility_Lambda([this, WeakHandle]()
        {
            return IsPropertyDifferentFromSaved(WeakHandle)
                ? EVisibility::Visible
                : EVisibility::Collapsed;
        })
        .ButtonStyle(FAppStyle::Get(), TEXT("NoBorder"))
        .ContentPadding(FMargin(4.0f))
        .ToolTipText(LOCTEXT("RevertToSavedValueTooltip", "Revert to Saved Value"))
        .OnClicked_Lambda([this, WeakHandle]()
        {
            return RevertPropertyToSaved(WeakHandle);
        })
        [
            SNew(SImage)
            .Image(FAppStyle::GetBrush(TEXT("PropertyWindow.DiffersFromDefault")))
        ];
}

TSharedRef<SWidget> FWetnessProfileDetailsCustomization::BuildStampSizeRevertButton(
    const TSharedPtr<IPropertyHandle>& WidthHandle,
    const TSharedPtr<IPropertyHandle>& HeightHandle)
{
    const TWeakPtr<IPropertyHandle> WeakWidth = WidthHandle;
    const TWeakPtr<IPropertyHandle> WeakHeight = HeightHandle;
    return SNew(SButton)
        .Visibility_Lambda([this, WeakWidth, WeakHeight]()
        {
            return AreStampSizePropertiesDifferentFromSaved(WeakWidth, WeakHeight)
                ? EVisibility::Visible
                : EVisibility::Collapsed;
        })
        .ButtonStyle(FAppStyle::Get(), TEXT("NoBorder"))
        .ContentPadding(FMargin(4.0f))
        .ToolTipText(LOCTEXT("RevertStampSizeToSavedTooltip", "Revert Stamp Size to Saved Values"))
        .OnClicked_Lambda([this, WeakWidth, WeakHeight]()
        {
            return RevertStampSizeToSaved(WeakWidth, WeakHeight);
        })
        [
            SNew(SImage)
            .Image(FAppStyle::GetBrush(TEXT("PropertyWindow.DiffersFromDefault")))
        ];
}

void FWetnessProfileDetailsCustomization::AddDefaultProperty(
    IDetailCategoryBuilder& Category,
    const TSharedPtr<IPropertyHandle>& Handle,
    const FText& DisplayName,
    const FText& Tooltip,
    const TAttribute<bool> IsEnabled,
    const float NameIndent)
{
    if (!Handle.IsValid() || !Handle->IsValidHandle())
    {
        return;
    }

    Category.AddCustomRow(DisplayName)
        .FilterString(DisplayName)
        .IsEnabled(IsEnabled)
        .NameContent()
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            .Padding(NameIndent, 0.0f, 0.0f, 0.0f)
            [
                Handle->CreatePropertyNameWidget(DisplayName, Tooltip)
            ]
        ]
        .ValueContent()
        .MinDesiredWidth(190.0f)
        .MaxDesiredWidth(460.0f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            .VAlign(VAlign_Center)
            [
                Handle->CreatePropertyValueWidget(false)
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            .Padding(5.0f, 0.0f, 0.0f, 0.0f)
            [
                BuildRevertButton(Handle)
            ]
        ];
}

void FWetnessProfileDetailsCustomization::AddDefaultProperty(
    IDetailGroup& Group,
    const TSharedPtr<IPropertyHandle>& Handle,
    const FText& DisplayName,
    const FText& Tooltip,
    const TAttribute<bool> IsEnabled)
{
    if (!Handle.IsValid() || !Handle->IsValidHandle())
    {
        return;
    }

    Group.AddWidgetRow()
        .FilterString(DisplayName)
        .IsEnabled(IsEnabled)
        .NameContent()
        [
            Handle->CreatePropertyNameWidget(DisplayName, Tooltip)
        ]
        .ValueContent()
        .MinDesiredWidth(190.0f)
        .MaxDesiredWidth(460.0f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            .VAlign(VAlign_Center)
            [
                Handle->CreatePropertyValueWidget(false)
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            .Padding(5.0f, 0.0f, 0.0f, 0.0f)
            [
                BuildRevertButton(Handle)
            ]
        ];
}

void FWetnessProfileDetailsCustomization::AddFloatProperty(
    IDetailCategoryBuilder& Category,
    const TSharedPtr<IPropertyHandle>& Handle,
    const FText& DisplayName,
    const FText& Tooltip,
    const float HardMin,
    const float HardMax,
    const float SliderMin,
    const float SliderMax,
    const float Delta,
    const float DisplayScale,
    const int32 MaxFractionalDigits,
    const FText& Suffix,
    const TAttribute<bool> IsEnabled)
{
    if (!Handle.IsValid() || !Handle->IsValidHandle() || DisplayScale <= 0.0f)
    {
        return;
    }

    Category.AddCustomRow(DisplayName)
        .FilterString(DisplayName)
        .IsEnabled(IsEnabled)
        .NameContent()
        [
            Handle->CreatePropertyNameWidget(DisplayName, Tooltip)
        ]
        .ValueContent()
        .MinDesiredWidth(190.0f)
        .MaxDesiredWidth(420.0f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            [
                SNew(SNumericEntryBox<float>)
                .MinValue(HardMin * DisplayScale)
                .MaxValue(HardMax * DisplayScale)
                .MinSliderValue(SliderMin * DisplayScale)
                .MaxSliderValue(SliderMax * DisplayScale)
                .Delta(Delta * DisplayScale)
                .MinFractionalDigits(0)
                .MaxFractionalDigits(MaxFractionalDigits)
                .AllowSpin(true)
                .Value_Lambda([this, WeakHandle = TWeakPtr<IPropertyHandle>(Handle), DisplayScale]()
                {
                    return GetDisplayedFloatValue(WeakHandle, DisplayScale);
                })
                .OnValueChanged_Lambda([this, WeakHandle = TWeakPtr<IPropertyHandle>(Handle), HardMin, HardMax, DisplayScale](float NewValue)
                {
                    SetDisplayedFloatValue(NewValue, WeakHandle, HardMin, HardMax, DisplayScale);
                })
                .OnValueCommitted_Lambda([this, WeakHandle = TWeakPtr<IPropertyHandle>(Handle), HardMin, HardMax, DisplayScale](float NewValue, ETextCommit::Type)
                {
                    SetDisplayedFloatValue(NewValue, WeakHandle, HardMin, HardMax, DisplayScale);
                })
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            .Padding(6.0f, 0.0f, 0.0f, 0.0f)
            [
                SNew(STextBlock)
                .Text(Suffix)
                .ColorAndOpacity(FSlateColor::UseSubduedForeground())
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            .Padding(5.0f, 0.0f, 0.0f, 0.0f)
            [
                BuildRevertButton(Handle)
            ]
        ];
}

void FWetnessProfileDetailsCustomization::AddFloatProperty(
    IDetailGroup& Group,
    const TSharedPtr<IPropertyHandle>& Handle,
    const FText& DisplayName,
    const FText& Tooltip,
    const float HardMin,
    const float HardMax,
    const float SliderMin,
    const float SliderMax,
    const float Delta,
    const float DisplayScale,
    const int32 MaxFractionalDigits,
    const FText& Suffix,
    const TAttribute<bool> IsEnabled)
{
    if (!Handle.IsValid() || !Handle->IsValidHandle() || DisplayScale <= 0.0f)
    {
        return;
    }

    Group.AddWidgetRow()
        .FilterString(DisplayName)
        .IsEnabled(IsEnabled)
        .NameContent()
        [
            Handle->CreatePropertyNameWidget(DisplayName, Tooltip)
        ]
        .ValueContent()
        .MinDesiredWidth(190.0f)
        .MaxDesiredWidth(420.0f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            [
                SNew(SNumericEntryBox<float>)
                .MinValue(HardMin * DisplayScale)
                .MaxValue(HardMax * DisplayScale)
                .MinSliderValue(SliderMin * DisplayScale)
                .MaxSliderValue(SliderMax * DisplayScale)
                .Delta(Delta * DisplayScale)
                .MinFractionalDigits(0)
                .MaxFractionalDigits(MaxFractionalDigits)
                .AllowSpin(true)
                .Value_Lambda([this, WeakHandle = TWeakPtr<IPropertyHandle>(Handle), DisplayScale]()
                {
                    return GetDisplayedFloatValue(WeakHandle, DisplayScale);
                })
                .OnValueChanged_Lambda([this, WeakHandle = TWeakPtr<IPropertyHandle>(Handle), HardMin, HardMax, DisplayScale](float NewValue)
                {
                    SetDisplayedFloatValue(NewValue, WeakHandle, HardMin, HardMax, DisplayScale);
                })
                .OnValueCommitted_Lambda([this, WeakHandle = TWeakPtr<IPropertyHandle>(Handle), HardMin, HardMax, DisplayScale](float NewValue, ETextCommit::Type)
                {
                    SetDisplayedFloatValue(NewValue, WeakHandle, HardMin, HardMax, DisplayScale);
                })
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            .Padding(6.0f, 0.0f, 0.0f, 0.0f)
            [
                SNew(STextBlock)
                .Text(Suffix)
                .ColorAndOpacity(FSlateColor::UseSubduedForeground())
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            .Padding(5.0f, 0.0f, 0.0f, 0.0f)
            [
                BuildRevertButton(Handle)
            ]
        ];
}

void FWetnessProfileDetailsCustomization::AddMappedFloatProperty(
    IDetailCategoryBuilder& Category,
    const TSharedPtr<IPropertyHandle>& Handle,
    const FText& DisplayName,
    const FText& Tooltip,
    const float HardDisplayMin,
    const float HardDisplayMax,
    const float SliderDisplayMin,
    const float SliderDisplayMax,
    const float DisplayDelta,
    const int32 MaxFractionalDigits,
    const FText& Suffix,
    TFunction<float(float)> RawToDisplay,
    TFunction<float(float)> DisplayToRaw,
    const float RawHardMin,
    const float RawHardMax,
    const TAttribute<bool> IsEnabled)
{
    if (!Handle.IsValid() || !Handle->IsValidHandle() || !RawToDisplay || !DisplayToRaw)
    {
        return;
    }

    Category.AddCustomRow(DisplayName)
        .FilterString(DisplayName)
        .IsEnabled(IsEnabled)
        .NameContent()
        [
            Handle->CreatePropertyNameWidget(DisplayName, Tooltip)
        ]
        .ValueContent()
        .MinDesiredWidth(190.0f)
        .MaxDesiredWidth(420.0f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            [
                SNew(SNumericEntryBox<float>)
                .MinValue(HardDisplayMin)
                .MaxValue(HardDisplayMax)
                .MinSliderValue(SliderDisplayMin)
                .MaxSliderValue(SliderDisplayMax)
                .Delta(DisplayDelta)
                .MinFractionalDigits(0)
                .MaxFractionalDigits(MaxFractionalDigits)
                .AllowSpin(true)
                .Value_Lambda([this, WeakHandle = TWeakPtr<IPropertyHandle>(Handle), RawToDisplay]()
                {
                    return GetMappedDisplayedFloatValue(WeakHandle, RawToDisplay);
                })
                .OnValueChanged_Lambda(
                    [this, WeakHandle = TWeakPtr<IPropertyHandle>(Handle), DisplayToRaw, RawHardMin, RawHardMax](
                        float NewValue)
                    {
                        SetMappedDisplayedFloatValue(NewValue, WeakHandle, DisplayToRaw, RawHardMin, RawHardMax);
                    })
                .OnValueCommitted_Lambda(
                    [this, WeakHandle = TWeakPtr<IPropertyHandle>(Handle), DisplayToRaw, RawHardMin, RawHardMax](
                        float NewValue,
                        ETextCommit::Type)
                    {
                        SetMappedDisplayedFloatValue(NewValue, WeakHandle, DisplayToRaw, RawHardMin, RawHardMax);
                    })
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            .Padding(6.0f, 0.0f, 0.0f, 0.0f)
            [
                SNew(STextBlock)
                .Text(Suffix)
                .ColorAndOpacity(FSlateColor::UseSubduedForeground())
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            .Padding(5.0f, 0.0f, 0.0f, 0.0f)
            [
                BuildRevertButton(Handle)
            ]
        ];
}

void FWetnessProfileDetailsCustomization::AddMappedFloatProperty(
    IDetailGroup& Group,
    const TSharedPtr<IPropertyHandle>& Handle,
    const FText& DisplayName,
    const FText& Tooltip,
    const float HardDisplayMin,
    const float HardDisplayMax,
    const float SliderDisplayMin,
    const float SliderDisplayMax,
    const float DisplayDelta,
    const int32 MaxFractionalDigits,
    const FText& Suffix,
    TFunction<float(float)> RawToDisplay,
    TFunction<float(float)> DisplayToRaw,
    const float RawHardMin,
    const float RawHardMax,
    const TAttribute<bool> IsEnabled)
{
    if (!Handle.IsValid() || !Handle->IsValidHandle() || !RawToDisplay || !DisplayToRaw)
    {
        return;
    }

    Group.AddWidgetRow()
        .FilterString(DisplayName)
        .IsEnabled(IsEnabled)
        .NameContent()
        [
            Handle->CreatePropertyNameWidget(DisplayName, Tooltip)
        ]
        .ValueContent()
        .MinDesiredWidth(190.0f)
        .MaxDesiredWidth(420.0f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .FillWidth(1.0f)
            [
                SNew(SNumericEntryBox<float>)
                .MinValue(HardDisplayMin)
                .MaxValue(HardDisplayMax)
                .MinSliderValue(SliderDisplayMin)
                .MaxSliderValue(SliderDisplayMax)
                .Delta(DisplayDelta)
                .MinFractionalDigits(0)
                .MaxFractionalDigits(MaxFractionalDigits)
                .AllowSpin(true)
                .Value_Lambda([this, WeakHandle = TWeakPtr<IPropertyHandle>(Handle), RawToDisplay]()
                {
                    return GetMappedDisplayedFloatValue(WeakHandle, RawToDisplay);
                })
                .OnValueChanged_Lambda(
                    [this, WeakHandle = TWeakPtr<IPropertyHandle>(Handle), DisplayToRaw, RawHardMin, RawHardMax](
                        float NewValue)
                    {
                        SetMappedDisplayedFloatValue(NewValue, WeakHandle, DisplayToRaw, RawHardMin, RawHardMax);
                    })
                .OnValueCommitted_Lambda(
                    [this, WeakHandle = TWeakPtr<IPropertyHandle>(Handle), DisplayToRaw, RawHardMin, RawHardMax](
                        float NewValue,
                        ETextCommit::Type)
                    {
                        SetMappedDisplayedFloatValue(NewValue, WeakHandle, DisplayToRaw, RawHardMin, RawHardMax);
                    })
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            .Padding(6.0f, 0.0f, 0.0f, 0.0f)
            [
                SNew(STextBlock)
                .Text(Suffix)
                .ColorAndOpacity(FSlateColor::UseSubduedForeground())
            ]
            + SHorizontalBox::Slot()
            .AutoWidth()
            .VAlign(VAlign_Center)
            .Padding(5.0f, 0.0f, 0.0f, 0.0f)
            [
                BuildRevertButton(Handle)
            ]
        ];
}

void FWetnessProfileDetailsCustomization::AddStampSizeProperty(
    IDetailCategoryBuilder& Category,
    const TSharedPtr<IPropertyHandle>& WidthHandle,
    const TSharedPtr<IPropertyHandle>& HeightHandle,
    const FText& Tooltip,
    const TAttribute<bool> IsEnabled)
{
    if (!WidthHandle.IsValid() || !HeightHandle.IsValid() ||
        !WidthHandle->IsValidHandle() || !HeightHandle->IsValidHandle())
    {
        return;
    }

    const TWeakPtr<IPropertyHandle> WeakWidth = WidthHandle;
    const TWeakPtr<IPropertyHandle> WeakHeight = HeightHandle;
    const auto SetDimension = [this, WeakWidth, WeakHeight](const bool bWidth, const float NewValue)
    {
        const TWeakPtr<IPropertyHandle> Target = bWidth ? WeakWidth : WeakHeight;
        SetDisplayedFloatValue(NewValue, Target, 0.0f, 256.0f, 1.0f);
        if (bLockStampAspectRatio)
        {
            SetDisplayedFloatValue(NewValue, bWidth ? WeakHeight : WeakWidth, 0.0f, 256.0f, 1.0f);
        }
    };

    Category.AddCustomRow(LOCTEXT("StampSizeFilter", "Stamp Size Width Height"))
        .FilterString(LOCTEXT("StampSizeFilter", "Stamp Size Width Height"))
        .IsEnabled(IsEnabled)
        .NameContent()
        [
            SNew(STextBlock)
            .Text(LOCTEXT("StampSize", "Stamp Size"))
            .ToolTipText(Tooltip)
            .Font(IDetailLayoutBuilder::GetDetailFont())
        ]
        .ValueContent()
        .MinDesiredWidth(300.0f)
        .MaxDesiredWidth(460.0f)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 4.0f, 0.0f)
            [
                SNew(STextBlock).Text(LOCTEXT("StampWidthShort", "W"))
            ]
            + SHorizontalBox::Slot().FillWidth(1.0f)
            [
                SNew(SNumericEntryBox<float>)
                .MinValue(0.0f).MaxValue(256.0f)
                .MinSliderValue(0.0f).MaxSliderValue(128.0f)
                .Delta(1.0f).MinFractionalDigits(0).MaxFractionalDigits(1).AllowSpin(true)
                .Value_Lambda([this, WeakWidth]() { return GetDisplayedFloatValue(WeakWidth, 1.0f); })
                .OnValueChanged_Lambda([SetDimension](const float Value) { SetDimension(true, Value); })
                .OnValueCommitted_Lambda([SetDimension](const float Value, ETextCommit::Type) { SetDimension(true, Value); })
            ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.0f, 0.0f)
            [
                SNew(STextBlock).Text(LOCTEXT("StampHeightShort", "H"))
            ]
            + SHorizontalBox::Slot().FillWidth(1.0f)
            [
                SNew(SNumericEntryBox<float>)
                .MinValue(0.0f).MaxValue(256.0f)
                .MinSliderValue(0.0f).MaxSliderValue(128.0f)
                .Delta(1.0f).MinFractionalDigits(0).MaxFractionalDigits(1).AllowSpin(true)
                .Value_Lambda([this, WeakHeight]() { return GetDisplayedFloatValue(WeakHeight, 1.0f); })
                .OnValueChanged_Lambda([SetDimension](const float Value) { SetDimension(false, Value); })
                .OnValueCommitted_Lambda([SetDimension](const float Value, ETextCommit::Type) { SetDimension(false, Value); })
            ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(5.0f, 0.0f)
            [
                SNew(STextBlock)
                .Text(LOCTEXT("PixelsSuffix", "px"))
                .ColorAndOpacity(FSlateColor::UseSubduedForeground())
            ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.0f, 0.0f, 0.0f, 0.0f)
            [
                SNew(SCheckBox)
                .ToolTipText(LOCTEXT("LockStampAspectTooltip", "Keep width and height equal while editing."))
                .IsChecked_Lambda([this]() { return bLockStampAspectRatio ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
                .OnCheckStateChanged_Lambda([this](const ECheckBoxState State) { bLockStampAspectRatio = State == ECheckBoxState::Checked; })
                [
                    SNew(STextBlock).Text(LOCTEXT("LockStampAspect", "Lock"))
                ]
            ]
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(5.0f, 0.0f, 0.0f, 0.0f)
            [
                BuildStampSizeRevertButton(WidthHandle, HeightHandle)
            ]
        ];
}

TOptional<float> FWetnessProfileDetailsCustomization::GetDisplayedFloatValue(
    const TWeakPtr<IPropertyHandle> WeakHandle,
    const float DisplayScale) const
{
    const TSharedPtr<IPropertyHandle> Handle = WeakHandle.Pin();
    if (!Handle.IsValid())
    {
        return TOptional<float>();
    }

    float RawValue = 0.0f;
    if (Handle->GetValue(RawValue) != FPropertyAccess::Success)
    {
        return TOptional<float>();
    }
    return RawValue * DisplayScale;
}

void FWetnessProfileDetailsCustomization::SetDisplayedFloatValue(
    const float DisplayValue,
    const TWeakPtr<IPropertyHandle> WeakHandle,
    const float HardMin,
    const float HardMax,
    const float DisplayScale)
{
    const TSharedPtr<IPropertyHandle> Handle = WeakHandle.Pin();
    if (!Handle.IsValid() || DisplayScale <= 0.0f)
    {
        return;
    }

    const float RawValue = DisplayValue / DisplayScale;
    Handle->SetValue(FMath::Clamp(RawValue, HardMin, HardMax));
    RefreshValidationIssues();
}

TOptional<float> FWetnessProfileDetailsCustomization::GetMappedDisplayedFloatValue(
    const TWeakPtr<IPropertyHandle> WeakHandle,
    const TFunction<float(float)>& RawToDisplay) const
{
    const TSharedPtr<IPropertyHandle> Handle = WeakHandle.Pin();
    if (!Handle.IsValid() || !RawToDisplay)
    {
        return TOptional<float>();
    }

    float RawValue = 0.0f;
    if (Handle->GetValue(RawValue) != FPropertyAccess::Success)
    {
        return TOptional<float>();
    }
    return RawToDisplay(RawValue);
}

void FWetnessProfileDetailsCustomization::SetMappedDisplayedFloatValue(
    const float DisplayValue,
    const TWeakPtr<IPropertyHandle> WeakHandle,
    const TFunction<float(float)>& DisplayToRaw,
    const float RawHardMin,
    const float RawHardMax)
{
    const TSharedPtr<IPropertyHandle> Handle = WeakHandle.Pin();
    if (!Handle.IsValid() || !DisplayToRaw)
    {
        return;
    }

    Handle->SetValue(FMath::Clamp(DisplayToRaw(DisplayValue), RawHardMin, RawHardMax));
    RefreshValidationIssues();
}

FText FWetnessProfileDetailsCustomization::GetValidationText() const
{
    if (ValidationIssues.IsEmpty())
    {
        return FText::GetEmpty();
    }

    FString Message = TEXT("This profile contains legacy values that should be repaired before previewing, baking, or saving.\n");
    const int32 MaxDisplayedIssues = 5;
    for (int32 IssueIndex = 0; IssueIndex < FMath::Min(ValidationIssues.Num(), MaxDisplayedIssues); ++IssueIndex)
    {
        Message += FString::Printf(TEXT("- %s\n"), *ValidationIssues[IssueIndex]);
    }
    if (ValidationIssues.Num() > MaxDisplayedIssues)
    {
        Message += FString::Printf(TEXT("- ...and %d more."), ValidationIssues.Num() - MaxDisplayedIssues);
    }
    else
    {
        Message.RemoveFromEnd(TEXT("\n"));
    }
    return FText::FromString(Message);
}

FReply FWetnessProfileDetailsCustomization::HandleClampValuesClicked()
{
    TArray<FString> Changes;
    if (FWetnessProfileEditorPolicy::SanitizeProfile(Profile.Get(), &Changes))
    {
        if (const TSharedPtr<IPropertyUtilities> Utilities = PropertyUtilities.Pin())
        {
            Utilities->ForceRefresh();
        }
    }
    RefreshValidationIssues();
    return FReply::Handled();
}

void FWetnessProfileDetailsCustomization::RefreshValidationIssues()
{
    FWetnessProfileEditorPolicy::FindProfileIssues(Profile.Get(), ValidationIssues);
}

#undef LOCTEXT_NAMESPACE
