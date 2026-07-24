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
    // re-added below under the four explicit Simulation/Rendering categories.
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
    // Simulation | Absorbed Water
    // ---------------------------------------------------------------------

    IDetailCategoryBuilder& AbsorbedSimulationCategory = DetailBuilder.EditCategory(
        TEXT("DWCSimulationAbsorbedWater"),
        LOCTEXT("AbsorbedSimulationCategory", "Simulation | Absorbed Water"),
        ECategoryPriority::Important);
    ConfigurePrimaryCategory(AbsorbedSimulationCategory, 10);

    IDetailGroup& AbsorptionGroup = AbsorbedSimulationCategory.AddGroup(
        TEXT("DWCAbsorption"),
        LOCTEXT("AbsorptionGroup", "Absorption"),
        false,
        true);
    AddDefaultProperty(
        AbsorptionGroup,
        AbsorbedEnabled,
        LOCTEXT("EnableAbsorbedWater", "Enable Absorbed Water"),
        LOCTEXT("EnableAbsorbedWaterTooltip", "Enable water absorption, spreading, and drying for this profile."));
    AddFloatProperty(
        AbsorptionGroup,
        FindPropertyByPath(TEXT("Parameters.AbsorbedWetness.AbsorptionFraction")),
        LOCTEXT("AbsorptionAmount", "Absorption Amount"),
        LOCTEXT("AbsorptionAmountTooltip", "Percentage of incoming water routed into absorbed water. The remainder can feed surface water."),
        0.0f, 1.0f, 0.0f, 1.0f, 0.01f, 100.0f, 1, LOCTEXT("PercentSuffix1", "%"),
        AbsorbedSettingsEnabled);
    AddFloatProperty(
        AbsorptionGroup,
        FindPropertyByPath(TEXT("Parameters.AbsorbedWetness.AbsorptionRate")),
        LOCTEXT("AbsorptionResponse", "Absorption Response"),
        LOCTEXT("AbsorptionResponseTooltip", "Response multiplier applied to absorbed-water input. This is a multiplier, not a per-second rate."),
        0.0f, 10.0f, 0.0f, 3.0f, 0.01f, 1.0f, 3, LOCTEXT("MultiplierSuffix1", "x"),
        AbsorbedSettingsEnabled);

    IDetailGroup& DistributionGroup = AbsorbedSimulationCategory.AddGroup(
        TEXT("DWCAbsorbedDistribution"),
        LOCTEXT("AbsorbedDistributionGroup", "Distribution"),
        false,
        true);
    AddFloatProperty(
        DistributionGroup,
        FindPropertyByPath(TEXT("Parameters.AbsorbedWetness.SpreadRate")),
        LOCTEXT("SpreadSpeed", "Spread Speed"),
        LOCTEXT("SpreadSpeedTooltip", "Rate at which absorbed water spreads across connected surface samples."),
        0.0f, 10.0f, 0.0f, 2.0f, 0.01f, 1.0f, 3, LOCTEXT("PerSecondSuffix1", "/s"),
        AbsorbedSettingsEnabled);
    AddFloatProperty(
        DistributionGroup,
        FindPropertyByPath(TEXT("Parameters.AbsorbedWetness.GravityFlowStrength")),
        LOCTEXT("GravityFlowBias", "Gravity Flow Bias"),
        LOCTEXT("GravityFlowBiasTooltip", "Strength of gravity-biased absorbed-water spreading."),
        0.0f, 10.0f, 0.0f, 3.0f, 0.01f, 1.0f, 3, LOCTEXT("MultiplierSuffix2", "x"),
        AbsorbedSettingsEnabled);

    IDetailGroup& DryingGroup = AbsorbedSimulationCategory.AddGroup(
        TEXT("DWCDrying"),
        LOCTEXT("DryingGroup", "Drying"),
        false,
        true);
    AddFloatProperty(
        DryingGroup,
        FindPropertyByPath(TEXT("Parameters.AbsorbedWetness.DryRate")),
        LOCTEXT("DryingSpeed", "Drying Speed"),
        LOCTEXT("DryingSpeedTooltip", "Percentage of remaining absorbed water removed per second."),
        0.0f, 100.0f, 0.0f, 100.0f, 1.0f, 1.0f, 1, LOCTEXT("PercentPerSecondSuffix", "%/s"),
        AbsorbedSettingsEnabled);

    // ---------------------------------------------------------------------
    // Simulation | Surface Water
    // ---------------------------------------------------------------------

    IDetailCategoryBuilder& SurfaceSimulationCategory = DetailBuilder.EditCategory(
        TEXT("DWCSimulationSurfaceWater"),
        LOCTEXT("SurfaceSimulationCategory", "Simulation | Surface Water"),
        ECategoryPriority::Important);
    ConfigurePrimaryCategory(SurfaceSimulationCategory, 20);

    IDetailGroup& SurfaceGeneralGroup = SurfaceSimulationCategory.AddGroup(
        TEXT("DWCSurfaceGeneral"),
        LOCTEXT("SurfaceGeneralGroup", "General"),
        false,
        true);
    AddDefaultProperty(
        SurfaceGeneralGroup,
        SurfaceEnabled,
        LOCTEXT("EnableSurfaceWater", "Enable Surface Water"),
        LOCTEXT("EnableSurfaceWaterTooltip", "Enable water that remains on top of the material surface."));
    AddFloatProperty(
        SurfaceGeneralGroup,
        FindPropertyByPath(TEXT("Parameters.SurfaceWater.SurfaceRepresentationFraction")),
        LOCTEXT("SurfaceRepresentation", "Surface Representation"),
        LOCTEXT("SurfaceRepresentationTooltip", "Percentage of water rejected by absorption that is represented as visible surface water."),
        0.0f, 1.0f, 0.0f, 1.0f, 0.01f, 100.0f, 1, LOCTEXT("PercentSuffix2", "%"),
        SurfaceSettingsEnabled);

    IDetailGroup& DropletsGroup = SurfaceSimulationCategory.AddGroup(
        TEXT("DWCDroplets"),
        LOCTEXT("DropletsGroup", "Droplets"),
        false,
        true);
    AddDefaultProperty(
        DropletsGroup,
        DropletsEnabled,
        LOCTEXT("EnableDroplets", "Enable Droplets"),
        LOCTEXT("EnableDropletsTooltip", "Allow droplet stamps for this profile."),
        SurfaceSettingsEnabled);
    AddFloatProperty(
        DropletsGroup,
        FindPropertyByPath(TEXT("Parameters.SurfaceWater.DropletSpawnProbability")),
        LOCTEXT("DropletSpawnChance", "Spawn Chance"),
        LOCTEXT("DropletSpawnChanceTooltip", "Chance that eligible surface water produces a droplet stamp."),
        0.0f, 1.0f, 0.0f, 1.0f, 0.01f, 100.0f, 1, LOCTEXT("PercentSuffix3", "%"),
        DropletSettingsEnabled);
    AddFloatProperty(
        DropletsGroup,
        FindPropertyByPath(TEXT("Parameters.SurfaceWater.DropletIntensityMultiplier")),
        LOCTEXT("DropletIntensity", "Intensity"),
        LOCTEXT("DropletIntensityTooltip", "Multiplier applied to droplet stamp amount."),
        0.0f, 10.0f, 0.0f, 3.0f, 0.01f, 1.0f, 3, LOCTEXT("MultiplierSuffix3", "x"),
        DropletSettingsEnabled);
    AddFloatProperty(
        DropletsGroup,
        FindPropertyByPath(TEXT("Parameters.SurfaceWater.DropletLifetimeSeconds")),
        LOCTEXT("DropletLifetime", "Lifetime"),
        LOCTEXT("DropletLifetimeTooltip", "Time before a droplet stamp fully fades."),
        0.01f, 120.0f, 0.1f, 30.0f, 0.1f, 1.0f, 2, LOCTEXT("SecondsSuffix1", "s"),
        DropletSettingsEnabled);
    AddFloatProperty(
        DropletsGroup,
        FindPropertyByPath(TEXT("Parameters.SurfaceWater.DropletRadiusPixels")),
        LOCTEXT("DropletRadius", "Radius"),
        LOCTEXT("DropletRadiusTooltip", "Base droplet radius in Surface Water render-target pixels."),
        0.5f, 256.0f, 0.5f, 64.0f, 0.5f, 1.0f, 1, LOCTEXT("PixelsSuffix1", "px"),
        DropletSettingsEnabled);

    IDetailGroup& RivuletsGroup = SurfaceSimulationCategory.AddGroup(
        TEXT("DWCRivulets"),
        LOCTEXT("RivuletsGroup", "Rivulets"),
        false,
        true);
    AddDefaultProperty(
        RivuletsGroup,
        RivuletsEnabled,
        LOCTEXT("EnableRivulets", "Enable Rivulets"),
        LOCTEXT("EnableRivuletsTooltip", "Allow flowing rivulet stamps for this profile."),
        SurfaceSettingsEnabled);
    AddFloatProperty(
        RivuletsGroup,
        FindPropertyByPath(TEXT("Parameters.SurfaceWater.FlowSpawnProbability")),
        LOCTEXT("RivuletSpawnChance", "Spawn Chance"),
        LOCTEXT("RivuletSpawnChanceTooltip", "Chance that eligible surface water produces a rivulet stamp."),
        0.0f, 1.0f, 0.0f, 1.0f, 0.01f, 100.0f, 1, LOCTEXT("PercentSuffix4", "%"),
        RivuletSettingsEnabled);
    AddFloatProperty(
        RivuletsGroup,
        FindPropertyByPath(TEXT("Parameters.SurfaceWater.FlowIntensityMultiplier")),
        LOCTEXT("RivuletIntensity", "Intensity"),
        LOCTEXT("RivuletIntensityTooltip", "Multiplier applied to rivulet stamp amount."),
        0.0f, 10.0f, 0.0f, 3.0f, 0.01f, 1.0f, 3, LOCTEXT("MultiplierSuffix4", "x"),
        RivuletSettingsEnabled);
    AddFloatProperty(
        RivuletsGroup,
        FindPropertyByPath(TEXT("Parameters.SurfaceWater.FlowLifetimeSeconds")),
        LOCTEXT("RivuletLifetime", "Lifetime"),
        LOCTEXT("RivuletLifetimeTooltip", "Time before a rivulet stamp fully fades."),
        0.01f, 120.0f, 0.1f, 30.0f, 0.1f, 1.0f, 2, LOCTEXT("SecondsSuffix2", "s"),
        RivuletSettingsEnabled);
    AddFloatProperty(
        RivuletsGroup,
        FindPropertyByPath(TEXT("Parameters.SurfaceWater.MinimumFlowSurfaceAmount")),
        LOCTEXT("MinimumRivuletAmount", "Minimum Surface Amount"),
        LOCTEXT("MinimumRivuletAmountTooltip", "Minimum normalized surface-water amount required to create a rivulet."),
        0.0f, 1.0f, 0.0f, 1.0f, 0.01f, 100.0f, 1, LOCTEXT("PercentSuffix5", "%"),
        RivuletSettingsEnabled);
    AddFloatProperty(
        RivuletsGroup,
        FindPropertyByPath(TEXT("Parameters.SurfaceWater.FlowWidthPixels")),
        LOCTEXT("RivuletWidth", "Width"),
        LOCTEXT("RivuletWidthTooltip", "Base rivulet width in Surface Water render-target pixels."),
        0.5f, 256.0f, 0.5f, 64.0f, 0.5f, 1.0f, 1, LOCTEXT("PixelsSuffix2", "px"),
        RivuletSettingsEnabled);
    AddFloatProperty(
        RivuletsGroup,
        FindPropertyByPath(TEXT("Parameters.SurfaceWater.FlowLengthPixels")),
        LOCTEXT("RivuletLength", "Length"),
        LOCTEXT("RivuletLengthTooltip", "Base rivulet length in Surface Water render-target pixels."),
        1.0f, 512.0f, 1.0f, 128.0f, 1.0f, 1.0f, 0, LOCTEXT("PixelsSuffix3", "px"),
        RivuletSettingsEnabled);

    // ---------------------------------------------------------------------
    // Rendering | Absorbed Water
    // ---------------------------------------------------------------------

    IDetailCategoryBuilder& AbsorbedRenderingCategory = DetailBuilder.EditCategory(
        TEXT("DWCRenderingAbsorbedWater"),
        LOCTEXT("AbsorbedRenderingCategory", "Rendering | Absorbed Water"),
        ECategoryPriority::Important);
    ConfigurePrimaryCategory(AbsorbedRenderingCategory, 30);

    IDetailGroup& WetAppearanceGroup = AbsorbedRenderingCategory.AddGroup(
        TEXT("DWCWetAppearance"),
        LOCTEXT("WetAppearanceGroup", "Wet Appearance"),
        false,
        true);
    AddFloatProperty(
        WetAppearanceGroup,
        FindPropertyByPath(TEXT("Parameters.AbsorbedWetness.WetVisualStrength")),
        LOCTEXT("WetAppearanceStrength", "Wet Appearance Strength"),
        LOCTEXT(
            "WetAppearanceStrengthTooltip",
            "Visual darkening contribution of absorbed water. Stored as 0..1 and shown as 0..100%. Values above 100% are rejected because they can force Base Color to full black."),
        0.0f, 1.0f, 0.0f, 1.0f, 0.01f, 100.0f, 1, LOCTEXT("PercentSuffix6", "%"),
        AbsorbedSettingsEnabled);

    // ---------------------------------------------------------------------
    // Rendering | Surface Water
    // ---------------------------------------------------------------------

    IDetailCategoryBuilder& SurfaceRenderingCategory = DetailBuilder.EditCategory(
        TEXT("DWCRenderingSurfaceWater"),
        LOCTEXT("SurfaceRenderingCategory", "Rendering | Surface Water"),
        ECategoryPriority::Important);
    ConfigurePrimaryCategory(SurfaceRenderingCategory, 40);

    IDetailGroup& SharedAppearanceGroup = SurfaceRenderingCategory.AddGroup(
        TEXT("DWCSurfaceSharedAppearance"),
        LOCTEXT("SurfaceSharedAppearanceGroup", "Shared Appearance"),
        false,
        true);
    AddFloatProperty(
        SharedAppearanceGroup,
        FindPropertyByPath(TEXT("Parameters.SurfaceWater.SurfaceWaterNormalStrength")),
        LOCTEXT("SurfaceNormalStrength", "Normal Strength"),
        LOCTEXT("SurfaceNormalStrengthTooltip", "Shared strength applied to droplet and rivulet detail normals."),
        0.0f, 8.0f, 0.0f, 2.0f, 0.01f, 100.0f, 1, LOCTEXT("PercentSuffix7", "%"),
        SurfaceSettingsEnabled);
    AddFloatProperty(
        SharedAppearanceGroup,
        FindPropertyByPath(TEXT("Parameters.SurfaceWater.SurfaceWaterRoughnessStrength")),
        LOCTEXT("SurfaceRoughnessStrength", "Roughness Strength"),
        LOCTEXT("SurfaceRoughnessStrengthTooltip", "Blend strength toward the material-wide Surface Water target roughness."),
        0.0f, 1.0f, 0.0f, 1.0f, 0.01f, 100.0f, 1, LOCTEXT("PercentSuffix8", "%"),
        SurfaceSettingsEnabled);
    AddFloatProperty(
        SharedAppearanceGroup,
        FindPropertyByPath(TEXT("Parameters.SurfaceWater.SurfaceVisibilityThreshold")),
        LOCTEXT("SurfaceVisibilityThreshold", "Visibility Threshold"),
        LOCTEXT("SurfaceVisibilityThresholdTooltip", "Minimum normalized RT amount before Surface Water detail becomes visible."),
        0.0f, 1.0f, 0.0f, 1.0f, 0.01f, 100.0f, 1, LOCTEXT("PercentSuffix9", "%"),
        SurfaceSettingsEnabled);

    IDetailGroup& DropletNormalGroup = SurfaceRenderingCategory.AddGroup(
        TEXT("DWCDropletNormal"),
        LOCTEXT("DropletNormalGroup", "Droplet Normal"),
        false,
        true);
    AddDefaultProperty(
        DropletNormalGroup,
        FindPropertyByPath(TEXT("Parameters.SurfaceWater.DropletNormalTexture")),
        LOCTEXT("DropletNormal", "Normal Texture"),
        LOCTEXT("DropletNormalTooltip", "Tangent-space normal texture inserted into the shared droplet Texture2DArray. Null uses the flat-normal slice."),
        DropletSettingsEnabled);

    IDetailGroup& RivuletNormalGroup = SurfaceRenderingCategory.AddGroup(
        TEXT("DWCRivuletNormal"),
        LOCTEXT("RivuletNormalGroup", "Rivulet Normal"),
        false,
        true);
    AddDefaultProperty(
        RivuletNormalGroup,
        FindPropertyByPath(TEXT("Parameters.SurfaceWater.RivuletNormalTexture")),
        LOCTEXT("RivuletNormal", "Normal Texture"),
        LOCTEXT("RivuletNormalTooltip", "Tangent-space normal texture inserted into the shared rivulet Texture2DArray. Null uses the flat-normal slice."),
        RivuletSettingsEnabled);
    AddFloatProperty(
        RivuletNormalGroup,
        FindPropertyByPath(TEXT("Parameters.SurfaceWater.RivuletUVScrollSpeed")),
        LOCTEXT("RivuletScrollSpeed", "Flow Speed"),
        LOCTEXT("RivuletScrollSpeedTooltip", "UV scroll speed along the encoded rivulet flow direction."),
        0.0f, 10.0f, 0.0f, 3.0f, 0.01f, 1.0f, 3, LOCTEXT("UVPerSecondSuffix", "UV/s"),
        RivuletSettingsEnabled);

    IDetailGroup& RuntimeUpdateGroup = SurfaceRenderingCategory.AddGroup(
        TEXT("DWCSurfaceRuntimeUpdate"),
        LOCTEXT("SurfaceRuntimeUpdateGroup", "Runtime Update"),
        false,
        true);
    AddFloatProperty(
        RuntimeUpdateGroup,
        FindPropertyByPath(TEXT("Parameters.SurfaceWater.MaterialTimeUpdateInterval")),
        LOCTEXT("MaterialUpdateInterval", "Material Update Interval"),
        LOCTEXT("MaterialUpdateIntervalTooltip", "How often time-dependent Surface Water material parameters are refreshed. Stored in seconds and shown in milliseconds."),
        0.001f, 1.0f, 1.0f / 120.0f, 0.1f, 0.001f, 1000.0f, 1, LOCTEXT("MillisecondsSuffix", "ms"),
        SurfaceSettingsEnabled);
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
