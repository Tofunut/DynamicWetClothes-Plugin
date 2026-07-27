#include "WetnessProfile/Editor/WetnessProfileDetailsCustomization.h"

#include "DataAssets/WetnessProfile.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "IDetailGroup.h"
#include "IDetailPropertyRow.h"
#include "IPropertyUtilities.h"
#include "PropertyHandle.h"
#include "Styling/AppStyle.h"
#include "WetnessProfile/Editor/WetnessProfileEditorPolicy.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "WetnessProfileDetailsCustomization"

namespace
{
    constexpr float SurfaceWaterSubchannelNameIndent = 16.0f;

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

    TFunction<float(float)> MakeLinearRawToPercent(const float RawAtHundredPercent)
    {
        return [RawAtHundredPercent](const float RawValue)
        {
            if (RawAtHundredPercent <= KINDA_SMALL_NUMBER)
            {
                return 0.0f;
            }
            return FMath::Clamp(RawValue / RawAtHundredPercent, 0.0f, 1.0f) * 100.0f;
        };
    }

    TFunction<float(float)> MakeLinearPercentToRaw(const float RawAtHundredPercent)
    {
        return [RawAtHundredPercent](const float DisplayValue)
        {
            return FMath::Clamp(DisplayValue, 0.0f, 100.0f) * 0.01f * RawAtHundredPercent;
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

    float ThresholdToVisibilityPercent(const float RawThreshold)
    {
        return (1.0f - FMath::Clamp(RawThreshold, 0.0f, 1.0f)) * 100.0f;
    }

    float VisibilityPercentToThreshold(const float DisplayValue)
    {
        return 1.0f - FMath::Clamp(DisplayValue, 0.0f, 100.0f) * 0.01f;
    }
}

TSharedRef<IDetailCustomization> FWetnessProfileDetailsCustomization::MakeInstance()
{
    return MakeShared<FWetnessProfileDetailsCustomization>();
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
    const TSharedPtr<IPropertyHandle> DropletsEnabled =
        FindPropertyByPath(TEXT("Parameters.SurfaceWater.bEnableDroplets"));
    const TSharedPtr<IPropertyHandle> RivuletsEnabled =
        FindPropertyByPath(TEXT("Parameters.SurfaceWater.bEnableRivulets"));

    const TAttribute<bool> AbsorbedSettingsEnabled = EnabledWhen(AbsorbedEnabled);
    const TAttribute<bool> SurfaceSettingsEnabled = EnabledWhen(SurfaceEnabled);
    const TAttribute<bool> DropletSettingsEnabled = EnabledWhenBoth(SurfaceEnabled, DropletsEnabled);
    const TAttribute<bool> RivuletSettingsEnabled = EnabledWhenBoth(SurfaceEnabled, RivuletsEnabled);

    // ---------------------------------------------------------------------
    // Water Channels
    // ---------------------------------------------------------------------

    IDetailCategoryBuilder& WaterChannelsCategory = DetailBuilder.EditCategory(
        TEXT("DWCWaterChannels"),
        LOCTEXT("WaterChannelsCategory", "Water Channels"),
        ECategoryPriority::Important);
    ConfigurePrimaryCategory(WaterChannelsCategory, 5);

    AddDefaultProperty(
        WaterChannelsCategory,
        AbsorbedEnabled,
        LOCTEXT("EnableAbsorbedWater", "Absorbed Water"),
        LOCTEXT("EnableAbsorbedWaterTooltip", "Enable absorbed water, including spreading, drying, and appearance changes."));
    AddDefaultProperty(
        WaterChannelsCategory,
        SurfaceEnabled,
        LOCTEXT("EnableSurfaceWater", "Surface Water"),
        LOCTEXT("EnableSurfaceWaterTooltip", "Enable water that remains visible on top of the material surface."));
    AddDefaultProperty(
        WaterChannelsCategory,
        DropletsEnabled,
        LOCTEXT("EnableDropletSurfaceWater", "Droplets"),
        LOCTEXT("EnableDropletSurfaceWaterTooltip", "Enable round droplet stamps and droplet normal rendering for this profile."),
        SurfaceSettingsEnabled,
        SurfaceWaterSubchannelNameIndent);
    AddDefaultProperty(
        WaterChannelsCategory,
        RivuletsEnabled,
        LOCTEXT("EnableStreakSurfaceWater", "Streaks"),
        LOCTEXT("EnableStreakSurfaceWaterTooltip", "Enable elongated, flow-aligned streak stamps and streak normal rendering for this profile."),
        SurfaceSettingsEnabled,
        SurfaceWaterSubchannelNameIndent);

    // ---------------------------------------------------------------------
    // Simulation | Absorbed Water
    // ---------------------------------------------------------------------

    IDetailCategoryBuilder& AbsorbedSimulationCategory = DetailBuilder.EditCategory(
        TEXT("DWCSimulationAbsorbedWater"),
        LOCTEXT("AbsorbedSimulationCategory", "Simulation | Absorbed Water"),
        ECategoryPriority::Important);
    ConfigurePrimaryCategory(AbsorbedSimulationCategory, 10);

    AddFloatProperty(
        AbsorbedSimulationCategory,
        FindPropertyByPath(TEXT("Parameters.AbsorbedWetness.AbsorptionFraction")),
        LOCTEXT("Absorption", "Absorption"),
        LOCTEXT("AbsorptionTooltip", "Amount of incoming water routed to absorbed water. Lower values leave more water available for surface effects."),
        0.0f, 1.0f, 0.0f, 1.0f, 0.01f, 100.0f, 1, LOCTEXT("PercentSuffix1", "%"),
        AbsorbedSettingsEnabled);
    AddMappedFloatProperty(
        AbsorbedSimulationCategory,
        FindPropertyByPath(TEXT("Parameters.AbsorbedWetness.SpreadRate")),
        LOCTEXT("SpreadSpeed", "Spread Speed"),
        LOCTEXT("SpreadSpeedTooltip", "How quickly absorbed water spreads across connected surface samples."),
        0.0f, 100.0f, 0.0f, 100.0f, 1.0f, 1, LOCTEXT("PercentSuffixSpread", "%"),
        MakeMidpointRawToPercent(6.5f, 10.0f), MakeMidpointPercentToRaw(6.5f, 10.0f), 0.0f, 10.0f,
        AbsorbedSettingsEnabled);
    AddMappedFloatProperty(
        AbsorbedSimulationCategory,
        FindPropertyByPath(TEXT("Parameters.AbsorbedWetness.DryRate")),
        LOCTEXT("DryingSpeed", "Drying Speed"),
        LOCTEXT("DryingSpeedTooltip", "How quickly absorbed water fades over time."),
        0.0f, 100.0f, 0.0f, 100.0f, 1.0f, 1, LOCTEXT("PercentSuffixDrying", "%"),
        MakeMidpointRawToPercent(20.0f, 40.0f), MakeMidpointPercentToRaw(20.0f, 40.0f), 0.0f, 100.0f,
        AbsorbedSettingsEnabled);

    IDetailGroup& AbsorbedAdvancedGroup = AbsorbedSimulationCategory.AddGroup(
        TEXT("DWCAbsorbedAdvanced"),
        LOCTEXT("AbsorbedAdvancedGroup", "Advanced"),
        false,
        false);
    AddMappedFloatProperty(
        AbsorbedAdvancedGroup,
        FindPropertyByPath(TEXT("Parameters.AbsorbedWetness.GravityFlowStrength")),
        LOCTEXT("GravityInfluence", "Gravity Influence"),
        LOCTEXT("GravityInfluenceTooltip", "How much absorbed water prefers to spread downward instead of evenly in all directions."),
        0.0f, 100.0f, 0.0f, 100.0f, 1.0f, 1, LOCTEXT("PercentSuffixGravity", "%"),
        MakeLinearRawToPercent(2.0f), MakeLinearPercentToRaw(2.0f), 0.0f, 10.0f,
        AbsorbedSettingsEnabled);

    // ---------------------------------------------------------------------
    // Simulation | Surface Water
    // ---------------------------------------------------------------------

    IDetailCategoryBuilder& SurfaceSimulationCategory = DetailBuilder.EditCategory(
        TEXT("DWCSimulationSurfaceWater"),
        LOCTEXT("SurfaceSimulationCategory", "Simulation | Surface Water"),
        ECategoryPriority::Important);
    ConfigurePrimaryCategory(SurfaceSimulationCategory, 20);

    AddFloatProperty(
        SurfaceSimulationCategory,
        FindPropertyByPath(TEXT("Parameters.SurfaceWater.SurfaceRepresentationFraction")),
        LOCTEXT("SurfaceWaterAmount", "Amount"),
        LOCTEXT("SurfaceWaterAmountTooltip", "Amount of non-absorbed water represented as visible surface water."),
        0.0f, 1.0f, 0.0f, 1.0f, 0.01f, 100.0f, 1, LOCTEXT("PercentSuffix2", "%"),
        SurfaceSettingsEnabled);

    IDetailGroup& DropletsGroup = SurfaceSimulationCategory.AddGroup(
        TEXT("DWCDroplets"),
        LOCTEXT("DropletsGroup", "Droplets"),
        false,
        true);
    AddFloatProperty(
        DropletsGroup,
        FindPropertyByPath(TEXT("Parameters.SurfaceWater.DropletSpawnProbability")),
        LOCTEXT("DropletSpawnChance", "Spawn Chance"),
        LOCTEXT("DropletSpawnChanceTooltip", "Chance that eligible surface water produces a droplet stamp."),
        0.0f, 1.0f, 0.0f, 1.0f, 0.01f, 100.0f, 1, LOCTEXT("PercentSuffix3", "%"),
        DropletSettingsEnabled);
    AddMappedFloatProperty(
        DropletsGroup,
        FindPropertyByPath(TEXT("Parameters.SurfaceWater.DropletRadiusPixels")),
        LOCTEXT("DropletSize", "Size"),
        LOCTEXT("DropletSizeTooltip", "Visible droplet size. 0% creates no droplet stamps."),
        0.0f, 100.0f, 0.0f, 100.0f, 1.0f, 1, LOCTEXT("PercentSuffixDropletSize", "%"),
        MakeSquaredRawToPercent(64.0f), MakeSquaredPercentToRaw(64.0f), 0.0f, 256.0f,
        DropletSettingsEnabled);

    IDetailGroup& StreaksGroup = SurfaceSimulationCategory.AddGroup(
        TEXT("DWCStreaks"),
        LOCTEXT("StreaksGroup", "Streaks"),
        false,
        true);
    AddFloatProperty(
        StreaksGroup,
        FindPropertyByPath(TEXT("Parameters.SurfaceWater.RivuletSpawnProbability")),
        LOCTEXT("StreakSpawnChance", "Spawn Chance"),
        LOCTEXT("StreakSpawnChanceTooltip", "Chance that eligible surface water produces a flow-aligned streak stamp."),
        0.0f, 1.0f, 0.0f, 1.0f, 0.01f, 100.0f, 1, LOCTEXT("PercentSuffix4", "%"),
        RivuletSettingsEnabled);
    AddMappedFloatProperty(
        StreaksGroup,
        FindPropertyByPath(TEXT("Parameters.SurfaceWater.RivuletWidthPixels")),
        LOCTEXT("StreakWidth", "Width"),
        LOCTEXT("StreakWidthTooltip", "Visible streak width. 0% creates no streak stamps."),
        0.0f, 100.0f, 0.0f, 100.0f, 1.0f, 1, LOCTEXT("PercentSuffixStreakWidth", "%"),
        MakeSquaredRawToPercent(32.0f), MakeSquaredPercentToRaw(32.0f), 0.0f, 256.0f,
        RivuletSettingsEnabled);
    AddMappedFloatProperty(
        StreaksGroup,
        FindPropertyByPath(TEXT("Parameters.SurfaceWater.RivuletLengthPixels")),
        LOCTEXT("StreakLength", "Length"),
        LOCTEXT("StreakLengthTooltip", "Visible streak length. 0% creates no streak stamps."),
        0.0f, 100.0f, 0.0f, 100.0f, 1.0f, 1, LOCTEXT("PercentSuffixStreakLength", "%"),
        MakeSquaredRawToPercent(192.0f), MakeSquaredPercentToRaw(192.0f), 0.0f, 512.0f,
        RivuletSettingsEnabled);

    IDetailGroup& SurfaceSimulationAdvancedGroup = SurfaceSimulationCategory.AddGroup(
        TEXT("DWCSurfaceSimulationAdvanced"),
        LOCTEXT("SurfaceSimulationAdvancedGroup", "Advanced"),
        false,
        false);
    AddFloatProperty(
        SurfaceSimulationAdvancedGroup,
        FindPropertyByPath(TEXT("Parameters.SurfaceWater.DropletLifetimeSeconds")),
        LOCTEXT("DropletLifetime", "Droplet Lifetime"),
        LOCTEXT("DropletLifetimeTooltip", "Time before a droplet stamp fully fades."),
        0.01f, 120.0f, 0.1f, 30.0f, 0.1f, 1.0f, 2, LOCTEXT("SecondsSuffix1", "s"),
        DropletSettingsEnabled);
    AddFloatProperty(
        SurfaceSimulationAdvancedGroup,
        FindPropertyByPath(TEXT("Parameters.SurfaceWater.RivuletLifetimeSeconds")),
        LOCTEXT("StreakLifetime", "Streak Lifetime"),
        LOCTEXT("StreakLifetimeTooltip", "Time before a streak stamp fully fades."),
        0.01f, 120.0f, 0.1f, 30.0f, 0.1f, 1.0f, 2, LOCTEXT("SecondsSuffix2", "s"),
        RivuletSettingsEnabled);
    AddFloatProperty(
        SurfaceSimulationAdvancedGroup,
        FindPropertyByPath(TEXT("Parameters.SurfaceWater.MinimumRivuletSurfaceAmount")),
        LOCTEXT("FormationThreshold", "Formation Threshold"),
        LOCTEXT("FormationThresholdTooltip", "How much surface water must build up before streaks can appear."),
        0.0f, 1.0f, 0.0f, 1.0f, 0.01f, 100.0f, 1, LOCTEXT("PercentSuffix5", "%"),
        RivuletSettingsEnabled);

    // ---------------------------------------------------------------------
    // Rendering | Absorbed Water
    // ---------------------------------------------------------------------

    IDetailCategoryBuilder& AbsorbedRenderingCategory = DetailBuilder.EditCategory(
        TEXT("DWCRenderingAbsorbedWater"),
        LOCTEXT("AbsorbedRenderingCategory", "Rendering | Absorbed Water"),
        ECategoryPriority::Important);
    ConfigurePrimaryCategory(AbsorbedRenderingCategory, 30);

    AddFloatProperty(
        AbsorbedRenderingCategory,
        FindPropertyByPath(TEXT("Parameters.AbsorbedWetness.AbsorbedDarkeningStrength")),
        LOCTEXT("Darkening", "Darkening"),
        LOCTEXT("DarkeningTooltip", "How strongly absorbed water darkens the base color."),
        0.0f, 1.0f, 0.0f, 1.0f, 0.01f, 100.0f, 1, LOCTEXT("PercentSuffix6", "%"),
        AbsorbedSettingsEnabled);
    AddFloatProperty(
        AbsorbedRenderingCategory,
        FindPropertyByPath(TEXT("Parameters.AbsorbedWetness.AbsorbedGlossinessStrength")),
        LOCTEXT("AbsorbedGlossiness", "Glossiness"),
        LOCTEXT("AbsorbedGlossinessTooltip", "How strongly absorbed water blends roughness toward the wet roughness target."),
        0.0f, 1.0f, 0.0f, 1.0f, 0.01f, 100.0f, 1, LOCTEXT("PercentSuffixAbsorbedGlossiness", "%"),
        AbsorbedSettingsEnabled);

    // ---------------------------------------------------------------------
    // Rendering | Surface Water
    // ---------------------------------------------------------------------

    IDetailCategoryBuilder& SurfaceRenderingCategory = DetailBuilder.EditCategory(
        TEXT("DWCRenderingSurfaceWater"),
        LOCTEXT("SurfaceRenderingCategory", "Rendering | Surface Water"),
        ECategoryPriority::Important);
    ConfigurePrimaryCategory(SurfaceRenderingCategory, 40);

    AddMappedFloatProperty(
        SurfaceRenderingCategory,
        FindPropertyByPath(TEXT("Parameters.SurfaceWater.SurfaceWaterNormalStrength")),
        LOCTEXT("SurfaceNormalStrength", "Normal Strength"),
        LOCTEXT("SurfaceNormalStrengthTooltip", "How strongly droplets and streaks affect the final surface normal."),
        0.0f, 100.0f, 0.0f, 100.0f, 1.0f, 1, LOCTEXT("PercentSuffix7", "%"),
        MakeLinearRawToPercent(2.0f), MakeLinearPercentToRaw(2.0f), 0.0f, 8.0f,
        SurfaceSettingsEnabled);
    AddFloatProperty(
        SurfaceRenderingCategory,
        FindPropertyByPath(TEXT("Parameters.SurfaceWater.SurfaceWaterRoughnessStrength")),
        LOCTEXT("SurfaceRoughnessStrength", "Roughness Strength"),
        LOCTEXT("SurfaceRoughnessStrengthTooltip", "How strongly surface water blends the material toward the wet surface roughness target."),
        0.0f, 1.0f, 0.0f, 1.0f, 0.01f, 100.0f, 1, LOCTEXT("PercentSuffix8", "%"),
        SurfaceSettingsEnabled);

    IDetailGroup& SurfaceRenderingAdvancedGroup = SurfaceRenderingCategory.AddGroup(
        TEXT("DWCSurfaceRenderingAdvanced"),
        LOCTEXT("SurfaceRenderingAdvancedGroup", "Advanced"),
        false,
        false);
    AddMappedFloatProperty(
        SurfaceRenderingAdvancedGroup,
        FindPropertyByPath(TEXT("Parameters.SurfaceWater.SurfaceVisibilityThreshold")),
        LOCTEXT("SurfaceVisibility", "Visibility"),
        LOCTEXT("SurfaceVisibilityTooltip", "How easily surface water becomes visible. Higher values show weaker surface water."),
        0.0f, 100.0f, 0.0f, 100.0f, 1.0f, 1, LOCTEXT("PercentSuffix9", "%"),
        [](const float RawValue) { return ThresholdToVisibilityPercent(RawValue); },
        [](const float DisplayValue) { return VisibilityPercentToThreshold(DisplayValue); },
        0.0f, 1.0f,
        SurfaceSettingsEnabled);
    AddDefaultProperty(
        SurfaceRenderingAdvancedGroup,
        FindPropertyByPath(TEXT("Parameters.SurfaceWater.DropletNormalTexture")),
        LOCTEXT("DropletNormal", "Droplet Normal Texture"),
        LOCTEXT("DropletNormalTooltip", "Normal texture used by droplet rendering. Empty uses the DWC default."),
        DropletSettingsEnabled);
    AddDefaultProperty(
        SurfaceRenderingAdvancedGroup,
        FindPropertyByPath(TEXT("Parameters.SurfaceWater.DropletMaskTexture")),
        LOCTEXT("DropletMask", "Droplet Mask Texture"),
        LOCTEXT("DropletMaskTooltip", "Mask texture used to localize droplet normal detail. Empty uses the unmasked droplet detail."),
        DropletSettingsEnabled);
    AddDefaultProperty(
        SurfaceRenderingAdvancedGroup,
        FindPropertyByPath(TEXT("Parameters.SurfaceWater.RivuletNormalTexture")),
        LOCTEXT("StreakNormal", "Streak Normal Texture"),
        LOCTEXT("StreakNormalTooltip", "Normal texture used by streak rendering. Empty uses the DWC default."),
        RivuletSettingsEnabled);
    AddDefaultProperty(
        SurfaceRenderingAdvancedGroup,
        FindPropertyByPath(TEXT("Parameters.SurfaceWater.RivuletMaskTexture")),
        LOCTEXT("StreakMask", "Streak Mask Texture"),
        LOCTEXT("StreakMaskTooltip", "Mask texture used to localize streak normal detail. Empty uses the unmasked streak detail."),
        RivuletSettingsEnabled);
    AddMappedFloatProperty(
        SurfaceRenderingAdvancedGroup,
        FindPropertyByPath(TEXT("Parameters.SurfaceWater.RivuletUVScrollSpeed")),
        LOCTEXT("DetailMotion", "Detail Motion"),
        LOCTEXT("DetailMotionTooltip", "How quickly the streak detail normal moves along the flow direction. This does not move the streak stamp itself."),
        0.0f, 100.0f, 0.0f, 100.0f, 1.0f, 1, LOCTEXT("PercentSuffixDetailMotion", "%"),
        MakeSquaredRawToPercent(2.0f), MakeSquaredPercentToRaw(2.0f), 0.0f, 10.0f,
        RivuletSettingsEnabled);
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
                    Handle->CreatePropertyNameWidget(DisplayName, Tooltip, true, true, false)
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
            Handle->CreatePropertyNameWidget(DisplayName, Tooltip, true, true, false)
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
            Handle->CreatePropertyNameWidget(DisplayName, Tooltip, true, true, false)
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
            Handle->CreatePropertyNameWidget(DisplayName, Tooltip, true, true, false)
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
            Handle->CreatePropertyNameWidget(DisplayName, Tooltip, true, true, false)
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

    FString Message = TEXT("This profile contains values outside the safe runtime range.\n");
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
