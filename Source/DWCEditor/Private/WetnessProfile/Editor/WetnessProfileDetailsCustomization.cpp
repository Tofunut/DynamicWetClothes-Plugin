#include "WetnessProfile/Editor/WetnessProfileDetailsCustomization.h"

#include "DataAssets/WetnessProfile.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailGroup.h"
#include "IDetailPropertyRow.h"
#include "IPropertyUtilities.h"
#include "PropertyHandle.h"
#include "UObject/UnrealType.h"
#include "Styling/AppStyle.h"
#include "WetnessProfile/Editor/WetnessProfileEditorPolicy.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "WetnessProfileDetailsCustomization"

namespace
{
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

    const TAttribute<bool> AbsorbedSettingsEnabled = EnabledWhen(AbsorbedEnabled);
    const TAttribute<bool> SurfaceSettingsEnabled = EnabledWhen(SurfaceEnabled);

    const bool bShowAbsorbed = Mode != EWetnessProfileDetailsMode::SurfaceWater;
    const bool bShowSurface = Mode != EWetnessProfileDetailsMode::AbsorbedWater;
    const bool bSingleChannel = Mode != EWetnessProfileDetailsMode::Combined;
#if WITH_EDITORONLY_DATA
    const bool bShowDroplet1 = !Profile.IsValid() || Profile->bEditorShowDroplet1;
    const bool bShowDroplet2 = Profile.IsValid() && Profile->bEditorShowDroplet2;
#else
    constexpr bool bShowDroplet1 = true;
    constexpr bool bShowDroplet2 = true;
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
        GeneralCategory.AddCustomRow(LOCTEXT("SurfaceTypeVisibilityFilter", "Droplet1 Droplet2 Show"))
            .NameContent()
            [
                SNew(STextBlock)
                .Text(LOCTEXT("SurfaceTypeVisibilityLabel", "Show"))
                .ToolTipText(LOCTEXT(
                    "SurfaceTypeVisibilityTooltip",
                    "Editor-only display filters. They control which Droplet sections and preview layers are shown; runtime behavior is unchanged."))
                .Font(IDetailLayoutBuilder::GetDetailFont())
            ]
            .ValueContent()
            .MinDesiredWidth(260.0f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(0.0f, 0.0f, 14.0f, 0.0f)
                [
                    SNew(SCheckBox)
                    .IsChecked(bShowDroplet1 ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
                    .OnCheckStateChanged_Lambda(
                        [WeakProfile = Profile, WeakUtilities = PropertyUtilities](const ECheckBoxState NewState)
                        {
                            if (UWetnessProfile* MutableProfile = WeakProfile.Get())
                            {
                                MutableProfile->bEditorShowDroplet1 = NewState == ECheckBoxState::Checked;
                                NotifyDetailsPreviewDisplayFilterChanged(
                                    *MutableProfile,
                                    GET_MEMBER_NAME_CHECKED(UWetnessProfile, bEditorShowDroplet1));
                            }
                            if (const TSharedPtr<IPropertyUtilities> Utilities = WeakUtilities.Pin())
                            {
                                Utilities->ForceRefresh();
                            }
                        })
                    [
                        SNew(STextBlock)
                        .Text(LOCTEXT("ShowDroplet1", "Droplet 1"))
                    ]
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                [
                    SNew(SCheckBox)
                    .IsChecked(bShowDroplet2 ? ECheckBoxState::Checked : ECheckBoxState::Unchecked)
                    .OnCheckStateChanged_Lambda(
                        [WeakProfile = Profile, WeakUtilities = PropertyUtilities](const ECheckBoxState NewState)
                        {
                            if (UWetnessProfile* MutableProfile = WeakProfile.Get())
                            {
                                MutableProfile->bEditorShowDroplet2 = NewState == ECheckBoxState::Checked;
                                NotifyDetailsPreviewDisplayFilterChanged(
                                    *MutableProfile,
                                    GET_MEMBER_NAME_CHECKED(UWetnessProfile, bEditorShowDroplet2));
                            }
                            if (const TSharedPtr<IPropertyUtilities> Utilities = WeakUtilities.Pin())
                            {
                                Utilities->ForceRefresh();
                            }
                        })
                    [
                        SNew(STextBlock)
                        .Text(LOCTEXT("ShowDroplet2", "Droplet 2"))
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
            LOCTEXT("DropletDryRate", "Droplet Dry Rate"),
            LOCTEXT("DropletDryRateTooltip", "Shared fade-out rate for wetness in both the Droplet1 and Droplet2 render targets."),
            0.0f, 100.0f, 0.0f, 100.0f, 1.0f, 1, LOCTEXT("PercentSuffixDropletDryRate", "%"),
            MakeMidpointRawToPercent(20.0f, 40.0f), MakeMidpointPercentToRaw(20.0f, 40.0f), 0.0f, 100.0f,
            SurfaceSettingsEnabled);

        if (bShowDroplet1)
        {
        IDetailGroup& Droplet1Group = SimulationCategory.AddGroup(
            TEXT("DWCDroplet1"),
            LOCTEXT("Droplet1Group", "Droplet1"),
            false,
            true);
        AddFloatProperty(
            Droplet1Group,
            FindPropertyByPath(TEXT("Parameters.SurfaceWater.DropletSpawnProbability")),
            LOCTEXT("Droplet1SpawnChance", "Spawn Chance"),
            LOCTEXT("Droplet1SpawnChanceTooltip", "Chance that eligible surface water produces a Droplet1 stamp."),
            0.0f, 1.0f, 0.0f, 1.0f, 0.01f, 100.0f, 1, LOCTEXT("PercentSuffixDroplet1Spawn", "%"),
            SurfaceSettingsEnabled);
        AddMappedFloatProperty(
            Droplet1Group,
            FindPropertyByPath(TEXT("Parameters.SurfaceWater.DropletRadiusPixels")),
            LOCTEXT("Droplet1StampWidth", "Stamp Width"),
            LOCTEXT("Droplet1StampWidthTooltip", "Horizontal half-size of Droplet1 stamps. Wet Parts can apply a local scale."),
            0.0f, 100.0f, 0.0f, 100.0f, 1.0f, 1, LOCTEXT("PercentSuffixDroplet1Width", "%"),
            MakeSquaredRawToPercent(64.0f), MakeSquaredPercentToRaw(64.0f), 0.0f, 256.0f,
            SurfaceSettingsEnabled);
        AddMappedFloatProperty(
            Droplet1Group,
            FindPropertyByPath(TEXT("Parameters.SurfaceWater.DropletHeightPixels")),
            LOCTEXT("Droplet1StampHeight", "Stamp Height"),
            LOCTEXT("Droplet1StampHeightTooltip", "Vertical half-size of Droplet1 stamps. Wet Parts can apply a local scale."),
            0.0f, 100.0f, 0.0f, 100.0f, 1.0f, 1, LOCTEXT("PercentSuffixDroplet1Height", "%"),
            MakeSquaredRawToPercent(64.0f), MakeSquaredPercentToRaw(64.0f), 0.0f, 256.0f,
            SurfaceSettingsEnabled);

        }

        if (bShowDroplet2)
        {
        IDetailGroup& Droplet2Group = SimulationCategory.AddGroup(
            TEXT("DWCDroplet2"),
            LOCTEXT("Droplet2Group", "Droplet2"),
            false,
            true);
        AddFloatProperty(
            Droplet2Group,
            FindPropertyByPath(TEXT("Parameters.SurfaceWater.DropletFlowSpawnProbability")),
            LOCTEXT("Droplet2SpawnChance", "Spawn Chance"),
            LOCTEXT("Droplet2SpawnChanceTooltip", "Independent chance that eligible surface water produces a Droplet2 stamp."),
            0.0f, 1.0f, 0.0f, 1.0f, 0.01f, 100.0f, 1, LOCTEXT("PercentSuffixDroplet2Spawn", "%"),
            SurfaceSettingsEnabled);
        AddMappedFloatProperty(
            Droplet2Group,
            FindPropertyByPath(TEXT("Parameters.SurfaceWater.DropletFlowRadiusPixels")),
            LOCTEXT("Droplet2StampWidth", "Stamp Width"),
            LOCTEXT("Droplet2StampWidthTooltip", "Horizontal half-size of stamps written to the Droplet2 RT."),
            0.0f, 100.0f, 0.0f, 100.0f, 1.0f, 1, LOCTEXT("PercentSuffixDroplet2Width", "%"),
            MakeSquaredRawToPercent(64.0f), MakeSquaredPercentToRaw(64.0f), 0.0f, 256.0f,
            SurfaceSettingsEnabled);
        AddMappedFloatProperty(
            Droplet2Group,
            FindPropertyByPath(TEXT("Parameters.SurfaceWater.DropletFlowHeightPixels")),
            LOCTEXT("Droplet2StampHeight", "Stamp Height"),
            LOCTEXT("Droplet2StampHeightTooltip", "Vertical half-size of stamps written to the Droplet2 RT."),
            0.0f, 100.0f, 0.0f, 100.0f, 1.0f, 1, LOCTEXT("PercentSuffixDroplet2Height", "%"),
            MakeSquaredRawToPercent(64.0f), MakeSquaredPercentToRaw(64.0f), 0.0f, 256.0f,
            SurfaceSettingsEnabled);
        AddFloatProperty(
            Droplet2Group,
            FindPropertyByPath(TEXT("Parameters.SurfaceWater.DropletFlowSpawnPositionSpread")),
            LOCTEXT("Droplet2SpawnSpreadRate", "Spawn Spread Rate"),
            LOCTEXT("Droplet2SpawnSpreadRateTooltip", "Spreads Droplet2 spawn positions within the same eligible UV triangle."),
            0.0f, 1.0f, 0.0f, 1.0f, 0.01f, 100.0f, 1, LOCTEXT("PercentSuffixDroplet2Spread", "%"),
            SurfaceSettingsEnabled);

        }

        IDetailCategoryBuilder& RenderingCategory = DetailBuilder.EditCategory(
            TEXT("DWCSurfaceRendering"),
            bSingleChannel
                ? LOCTEXT("SurfaceRenderingCategory", "Rendering")
                : LOCTEXT("CombinedSurfaceRenderingCategory", "Surface Water | Rendering"),
            ECategoryPriority::Important);
        ConfigurePrimaryCategory(RenderingCategory, BaseSortOrder + 10);
        if (bShowDroplet1)
        {
        IDetailGroup& StaticRenderingGroup = RenderingCategory.AddGroup(
            TEXT("DWCDroplet1Rendering"),
            LOCTEXT("Droplet1RenderingGroup", "Droplet1"),
            false,
            true);

        AddFloatProperty(
            StaticRenderingGroup,
            FindPropertyByPath(TEXT("Parameters.SurfaceWater.SurfaceWaterTotalStrength")),
            LOCTEXT("SurfaceTotalStrength", "Total Strength"),
            LOCTEXT("SurfaceTotalStrengthTooltip", "Overall Surface Water rendering strength after final droplet coverage is resolved. This does not change the preview Surface Water amount."),
            0.0f, 1.0f, 0.0f, 1.0f, 0.01f, 100.0f, 1, LOCTEXT("PercentSuffixSurfaceTotalStrength", "%"),
            SurfaceSettingsEnabled);
        AddFloatProperty(
            StaticRenderingGroup,
            FindPropertyByPath(TEXT("Parameters.SurfaceWater.SurfaceWaterColorBlend")),
            LOCTEXT("SurfaceColorBlend", "Color Blend"),
            LOCTEXT("SurfaceColorBlendTooltip", "How strongly Droplet1 coverage modifies the underlying Base Color. This does not affect normal, roughness, or specular."),
            0.0f, 1.0f, 0.0f, 1.0f, 0.01f, 100.0f, 1, LOCTEXT("PercentSuffixSurfaceColorBlend", "%"),
            SurfaceSettingsEnabled);
        AddMappedFloatProperty(
            StaticRenderingGroup,
            FindPropertyByPath(TEXT("Parameters.SurfaceWater.SurfaceWaterNormalStrength")),
            LOCTEXT("SurfaceNormalStrength", "Water Normal Strength"),
            LOCTEXT("SurfaceNormalStrengthTooltip", "Droplet1 normal-map strength. 100% is the authored normal texture strength."),
            0.0f, 300.0f, 0.0f, 300.0f, 1.0f, 1, LOCTEXT("PercentSuffix7", "%"),
            MakeLinearRawToPercent(1.0f, 300.0f), MakeLinearPercentToRaw(1.0f, 300.0f), 0.0f, 3.0f,
            SurfaceSettingsEnabled);
        AddFloatProperty(
            StaticRenderingGroup,
            FindPropertyByPath(TEXT("Parameters.SurfaceWater.SurfaceWaterTargetRoughness")),
            LOCTEXT("WaterRoughness", "Water Roughness"),
            LOCTEXT("WaterRoughnessTooltip", "Target roughness reached inside final droplet coverage."),
            0.0f, 1.0f, 0.0f, 1.0f, 0.01f, 100.0f, 1, LOCTEXT("PercentSuffixWetSurfaceRoughness", "%"),
            SurfaceSettingsEnabled);
        AddFloatProperty(
            StaticRenderingGroup,
            FindPropertyByPath(TEXT("Parameters.SurfaceWater.SurfaceWaterRoughnessBlend")),
            LOCTEXT("WetRoughnessBlend", "Roughness Blend"),
            LOCTEXT("WetRoughnessBlendTooltip", "How strongly final droplet coverage blends from source roughness to Water Roughness."),
            0.0f, 1.0f, 0.0f, 1.0f, 0.01f, 100.0f, 1, LOCTEXT("PercentSuffix8", "%"),
            SurfaceSettingsEnabled);
        AddFloatProperty(
            StaticRenderingGroup,
            FindPropertyByPath(TEXT("Parameters.SurfaceWater.SurfaceWaterSpecular")),
            LOCTEXT("WaterSpecular", "Water Specular"),
            LOCTEXT("WaterSpecularTooltip", "Target specular reached inside final droplet coverage."),
            0.0f, 1.0f, 0.0f, 1.0f, 0.01f, 100.0f, 1, LOCTEXT("PercentSuffixWaterSpecular", "%"),
            SurfaceSettingsEnabled);

        }

        if (bShowDroplet2)
        {
        IDetailGroup& FlowRenderingGroup = RenderingCategory.AddGroup(
            TEXT("DWCDroplet2Rendering"),
            LOCTEXT("Droplet2RenderingGroup", "Droplet2"),
            false,
            true);
        AddFloatProperty(
            FlowRenderingGroup,
            FindPropertyByPath(TEXT("Parameters.SurfaceWater.DropletFlowTotalStrength")),
            LOCTEXT("DropletFlowTotalStrength", "Total Strength"),
            LOCTEXT("DropletFlowTotalStrengthTooltip", "Overall Droplet2 rendering strength after its coverage is resolved."),
            0.0f, 1.0f, 0.0f, 1.0f, 0.01f, 100.0f, 1, LOCTEXT("PercentSuffixFlowTotalStrength", "%"),
            SurfaceSettingsEnabled);
        AddFloatProperty(
            FlowRenderingGroup,
            FindPropertyByPath(TEXT("Parameters.SurfaceWater.DropletFlowColorBlend")),
            LOCTEXT("DropletFlowColorBlend", "Color Blend"),
            LOCTEXT("DropletFlowColorBlendTooltip", "How strongly Droplet2 coverage modifies the underlying Base Color. This does not affect normal, roughness, or specular."),
            0.0f, 1.0f, 0.0f, 1.0f, 0.01f, 100.0f, 1, LOCTEXT("PercentSuffixFlowColorBlend", "%"),
            SurfaceSettingsEnabled);
        AddMappedFloatProperty(
            FlowRenderingGroup,
            FindPropertyByPath(TEXT("Parameters.SurfaceWater.DropletFlowNormalStrength")),
            LOCTEXT("DropletFlowNormalStrength", "Water Normal Strength"),
            LOCTEXT("DropletFlowNormalStrengthTooltip", "Droplet2 normal-map strength. 100% is the authored normal texture strength."),
            0.0f, 300.0f, 0.0f, 300.0f, 1.0f, 1, LOCTEXT("PercentSuffixFlowNormalStrength", "%"),
            MakeLinearRawToPercent(1.0f, 300.0f), MakeLinearPercentToRaw(1.0f, 300.0f), 0.0f, 3.0f,
            SurfaceSettingsEnabled);
        AddFloatProperty(
            FlowRenderingGroup,
            FindPropertyByPath(TEXT("Parameters.SurfaceWater.DropletFlowTargetRoughness")),
            LOCTEXT("DropletFlowWaterRoughness", "Water Roughness"),
            LOCTEXT("DropletFlowWaterRoughnessTooltip", "Target roughness reached inside Droplet2 coverage."),
            0.0f, 1.0f, 0.0f, 1.0f, 0.01f, 100.0f, 1, LOCTEXT("PercentSuffixFlowRoughness", "%"),
            SurfaceSettingsEnabled);
        AddFloatProperty(
            FlowRenderingGroup,
            FindPropertyByPath(TEXT("Parameters.SurfaceWater.DropletFlowRoughnessBlend")),
            LOCTEXT("DropletFlowRoughnessBlend", "Roughness Blend"),
            LOCTEXT("DropletFlowRoughnessBlendTooltip", "How strongly Droplet2 coverage blends from source roughness to its Water Roughness."),
            0.0f, 1.0f, 0.0f, 1.0f, 0.01f, 100.0f, 1, LOCTEXT("PercentSuffixFlowRoughnessBlend", "%"),
            SurfaceSettingsEnabled);
        AddFloatProperty(
            FlowRenderingGroup,
            FindPropertyByPath(TEXT("Parameters.SurfaceWater.DropletFlowSpecular")),
            LOCTEXT("DropletFlowWaterSpecular", "Water Specular"),
            LOCTEXT("DropletFlowWaterSpecularTooltip", "Target specular reached inside Droplet2 coverage."),
            0.0f, 1.0f, 0.0f, 1.0f, 0.01f, 100.0f, 1, LOCTEXT("PercentSuffixFlowSpecular", "%"),
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
        IDetailGroup& DropletTexturesGroup = DetailTexturesCategory.AddGroup(
            TEXT("DWCDroplet1Textures"),
            LOCTEXT("Droplet1TexturesGroup", "Droplet1"),
            false,
            true);
        AddDefaultProperty(
            DropletTexturesGroup,
            FindPropertyByPath(TEXT("Parameters.SurfaceWater.DropletNormalTexture")),
            LOCTEXT("DropletNormal", "Normal Texture"),
            LOCTEXT("DropletNormalTooltip", "Normal texture used by droplet rendering. Empty uses the DWC default."),
            SurfaceSettingsEnabled);
        AddDefaultProperty(
            DropletTexturesGroup,
            FindPropertyByPath(TEXT("Parameters.SurfaceWater.DropletMaskTexture")),
            LOCTEXT("DropletMask", "Mask Texture"),
            LOCTEXT("DropletMaskTooltip", "Optional mask used to localize visible Surface Water coverage and droplet detail. Empty means unmasked coverage."),
            SurfaceSettingsEnabled);

        }

        if (bShowDroplet2)
        {
        IDetailGroup& DropletFlowTexturesGroup = DetailTexturesCategory.AddGroup(
            TEXT("DWCDroplet2Textures"),
            LOCTEXT("Droplet2TexturesGroup", "Droplet2"),
            false,
            true);
        AddDefaultProperty(
            DropletFlowTexturesGroup,
            FindPropertyByPath(TEXT("Parameters.SurfaceWater.DropletFlowNormalTexture")),
            LOCTEXT("DropletFlowNormal", "Normal Texture"),
            LOCTEXT("DropletFlowNormalTooltip", "Optional normal texture used by Droplet2. Empty uses the neutral flat-normal slice and never copies Droplet1."),
            SurfaceSettingsEnabled);
        AddDefaultProperty(
            DropletFlowTexturesGroup,
            FindPropertyByPath(TEXT("Parameters.SurfaceWater.DropletFlowMaskTexture")),
            LOCTEXT("DropletFlowMask", "Mask Texture"),
            LOCTEXT("DropletFlowMaskTooltip", "Optional mask texture used by Droplet2. Empty uses the neutral unmasked slice and never copies Droplet1."),
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

    if (NameIndent > 0.0f)
    {
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
            [
                Handle->CreatePropertyValueWidget()
            ];
        return;
    }

    Category.AddProperty(Handle.ToSharedRef())
        .DisplayName(DisplayName)
        .ToolTip(Tooltip)
        .IsEnabled(IsEnabled);
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

    Group.AddPropertyRow(Handle.ToSharedRef())
        .DisplayName(DisplayName)
        .ToolTip(Tooltip)
        .IsEnabled(IsEnabled);
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
