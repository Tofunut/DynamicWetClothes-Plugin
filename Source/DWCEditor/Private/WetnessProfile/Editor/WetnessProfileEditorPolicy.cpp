#include "WetnessProfile/Editor/WetnessProfileEditorPolicy.h"

#include "DataAssets/WetnessProfile.h"
#include "UObject/UnrealType.h"

namespace
{
    struct FNumericRule
    {
        const TCHAR* PropertyPath;
        double MinValue;
        double MaxValue;
        double NonFiniteDefault;
    };

    // These are hard data-contract limits, not merely slider limits. The
    // absorbed rendering strengths are packed directly into the render-profile
    // LUT, so out-of-range values would produce invalid material results.
    const FNumericRule NumericRules[] = {
        // Simulation | Absorbed Wetness
        { TEXT("Parameters.AbsorbedWetness.AbsorptionFraction"), 0.0, 1.0, 0.5 },
        { TEXT("Parameters.AbsorbedWetness.AbsorptionRate"), 0.0, 10.0, 1.0 },
        { TEXT("Parameters.AbsorbedWetness.SpreadRate"), 0.0, 10.0, 6.5 },
        { TEXT("Parameters.AbsorbedWetness.DryRate"), 0.0, 100.0, 20.0 },
        { TEXT("Parameters.AbsorbedWetness.GravityFlowStrength"), 0.0, 10.0, 1.0 },

        // Rendering | Absorbed Wetness
        { TEXT("Parameters.AbsorbedWetness.AbsorbedDarkeningStrength"), 0.0, 1.0, 0.5 },
        { TEXT("Parameters.AbsorbedWetness.AbsorbedGlossinessStrength"), 0.0, 1.0, 0.5 },

        // Simulation | Surface Water
        { TEXT("Parameters.SurfaceWater.SurfaceRepresentationFraction"), 0.0, 1.0, 0.5 },
        { TEXT("Parameters.SurfaceWater.DropletSpawnProbability"), 0.0, 1.0, 0.5 },
        { TEXT("Parameters.SurfaceWater.FlowSpawnProbability"), 0.0, 1.0, 0.2 },
        { TEXT("Parameters.SurfaceWater.DropletIntensityMultiplier"), 0.0, 10.0, 1.0 },
        { TEXT("Parameters.SurfaceWater.FlowIntensityMultiplier"), 0.0, 10.0, 1.0 },
        { TEXT("Parameters.SurfaceWater.DropletLifetimeSeconds"), 0.01, 120.0, 5.0 },
        { TEXT("Parameters.SurfaceWater.DropletRadiusPixels"), 0.0, 256.0, 16.0 },
        { TEXT("Parameters.SurfaceWater.FlowLifetimeSeconds"), 0.01, 120.0, 7.0 },
        { TEXT("Parameters.SurfaceWater.MinimumFlowSurfaceAmount"), 0.0, 1.0, 0.2 },
        { TEXT("Parameters.SurfaceWater.FlowWidthPixels"), 0.0, 256.0, 8.0 },
        { TEXT("Parameters.SurfaceWater.FlowLengthPixels"), 0.0, 512.0, 48.0 },

        // Rendering | Surface Water
        { TEXT("Parameters.SurfaceWater.SurfaceWaterNormalStrength"), 0.0, 8.0, 1.0 },
        { TEXT("Parameters.SurfaceWater.SurfaceWaterRoughnessStrength"), 0.0, 1.0, 0.5 },
        { TEXT("Parameters.SurfaceWater.SurfaceVisibilityThreshold"), 0.0, 1.0, 0.2 },
        { TEXT("Parameters.SurfaceWater.RivuletUVScrollSpeed"), 0.0, 10.0, 0.5 },
    };

    const FNumericRule* FindNumericRule(const FString& PropertyPath)
    {
        for (const FNumericRule& Rule : NumericRules)
        {
            if (PropertyPath.Equals(Rule.PropertyPath, ESearchCase::CaseSensitive))
            {
                return &Rule;
            }
        }
        return nullptr;
    }

    FString MakePath(const FString& Prefix, const FName PropertyName)
    {
        return Prefix.IsEmpty()
                   ? PropertyName.ToString()
                   : FString::Printf(TEXT("%s.%s"), *Prefix, *PropertyName.ToString());
    }

    bool SanitizeFloatingProperty(
        FFloatProperty* Property,
        void* Container,
        const FNumericRule& Rule,
        const FString& PropertyPath,
        TArray<FString>* OutChanges)
    {
        const double Original = static_cast<double>(Property->GetPropertyValue_InContainer(Container));
        const double Sanitized = FMath::IsFinite(Original)
                                     ? FMath::Clamp(Original, Rule.MinValue, Rule.MaxValue)
                                     : Rule.NonFiniteDefault;
        if (FMath::IsNearlyEqual(Original, Sanitized, 1.0e-9))
        {
            return false;
        }

        Property->SetPropertyValue_InContainer(Container, static_cast<float>(Sanitized));
        if (OutChanges != nullptr)
        {
            OutChanges->Add(FString::Printf(
                TEXT("%s: %.9g -> %.9g (allowed %.9g..%.9g)"),
                *PropertyPath,
                Original,
                Sanitized,
                Rule.MinValue,
                Rule.MaxValue));
        }
        return true;
    }

    bool SanitizeFloatingProperty(
        FDoubleProperty* Property,
        void* Container,
        const FNumericRule& Rule,
        const FString& PropertyPath,
        TArray<FString>* OutChanges)
    {
        const double Original = Property->GetPropertyValue_InContainer(Container);
        const double Sanitized = FMath::IsFinite(Original)
                                     ? FMath::Clamp(Original, Rule.MinValue, Rule.MaxValue)
                                     : Rule.NonFiniteDefault;
        if (FMath::IsNearlyEqual(Original, Sanitized, 1.0e-9))
        {
            return false;
        }

        Property->SetPropertyValue_InContainer(Container, Sanitized);
        if (OutChanges != nullptr)
        {
            OutChanges->Add(FString::Printf(
                TEXT("%s: %.9g -> %.9g (allowed %.9g..%.9g)"),
                *PropertyPath,
                Original,
                Sanitized,
                Rule.MinValue,
                Rule.MaxValue));
        }
        return true;
    }

    bool SanitizeStructRecursive(
        void* StructMemory,
        UStruct* StructType,
        const FString& Prefix,
        TArray<FString>* OutChanges)
    {
        if (StructMemory == nullptr || StructType == nullptr)
        {
            return false;
        }

        bool bChanged = false;
        for (TFieldIterator<FProperty> It(StructType); It; ++It)
        {
            FProperty* Property = *It;
            if (Property == nullptr)
            {
                continue;
            }

            const FString PropertyPath = MakePath(Prefix, Property->GetFName());
            if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
            {
                void* ChildMemory = StructProperty->ContainerPtrToValuePtr<void>(StructMemory);
                bChanged |= SanitizeStructRecursive(ChildMemory, StructProperty->Struct, PropertyPath, OutChanges);
                continue;
            }

            const FNumericRule* Rule = FindNumericRule(PropertyPath);
            if (Rule == nullptr)
            {
                continue;
            }

            if (FFloatProperty* FloatProperty = CastField<FFloatProperty>(Property))
            {
                bChanged |= SanitizeFloatingProperty(FloatProperty, StructMemory, *Rule, PropertyPath, OutChanges);
            }
            else if (FDoubleProperty* DoubleProperty = CastField<FDoubleProperty>(Property))
            {
                bChanged |= SanitizeFloatingProperty(DoubleProperty, StructMemory, *Rule, PropertyPath, OutChanges);
            }
        }
        return bChanged;
    }

    void FindFloatingIssue(
        const FFloatProperty* Property,
        const void* Container,
        const FNumericRule& Rule,
        const FString& PropertyPath,
        TArray<FString>& OutIssues)
    {
        const double Value = static_cast<double>(Property->GetPropertyValue_InContainer(Container));
        if (!FMath::IsFinite(Value) || Value < Rule.MinValue || Value > Rule.MaxValue)
        {
            OutIssues.Add(FString::Printf(
                TEXT("%s is %.9g; allowed range is %.9g..%.9g."),
                *PropertyPath,
                Value,
                Rule.MinValue,
                Rule.MaxValue));
        }
    }

    void FindFloatingIssue(
        const FDoubleProperty* Property,
        const void* Container,
        const FNumericRule& Rule,
        const FString& PropertyPath,
        TArray<FString>& OutIssues)
    {
        const double Value = Property->GetPropertyValue_InContainer(Container);
        if (!FMath::IsFinite(Value) || Value < Rule.MinValue || Value > Rule.MaxValue)
        {
            OutIssues.Add(FString::Printf(
                TEXT("%s is %.9g; allowed range is %.9g..%.9g."),
                *PropertyPath,
                Value,
                Rule.MinValue,
                Rule.MaxValue));
        }
    }

    void FindIssuesRecursive(
        const void* StructMemory,
        UStruct* StructType,
        const FString& Prefix,
        TArray<FString>& OutIssues)
    {
        if (StructMemory == nullptr || StructType == nullptr)
        {
            return;
        }

        for (TFieldIterator<FProperty> It(StructType); It; ++It)
        {
            const FProperty* Property = *It;
            if (Property == nullptr)
            {
                continue;
            }

            const FString PropertyPath = MakePath(Prefix, Property->GetFName());
            if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
            {
                const void* ChildMemory = StructProperty->ContainerPtrToValuePtr<void>(StructMemory);
                FindIssuesRecursive(ChildMemory, StructProperty->Struct, PropertyPath, OutIssues);
                continue;
            }

            const FNumericRule* Rule = FindNumericRule(PropertyPath);
            if (Rule == nullptr)
            {
                continue;
            }

            if (const FFloatProperty* FloatProperty = CastField<FFloatProperty>(Property))
            {
                FindFloatingIssue(FloatProperty, StructMemory, *Rule, PropertyPath, OutIssues);
            }
            else if (const FDoubleProperty* DoubleProperty = CastField<FDoubleProperty>(Property))
            {
                FindFloatingIssue(DoubleProperty, StructMemory, *Rule, PropertyPath, OutIssues);
            }
        }
    }

    const FStructProperty* FindParametersProperty(const UWetnessProfile* Profile)
    {
        return Profile != nullptr
                   ? FindFProperty<FStructProperty>(Profile->GetClass(), TEXT("Parameters"))
                   : nullptr;
    }
}

bool FWetnessProfileEditorPolicy::SanitizeProfile(UWetnessProfile* Profile, TArray<FString>* OutChanges)
{
    if (Profile == nullptr)
    {
        return false;
    }

    const FStructProperty* ParametersProperty = FindParametersProperty(Profile);
    if (ParametersProperty == nullptr)
    {
        return false;
    }

    void* ParametersMemory = ParametersProperty->ContainerPtrToValuePtr<void>(Profile);
    TArray<FString> PendingChanges;
    TArray<FString>* ChangeTarget = OutChanges != nullptr ? OutChanges : &PendingChanges;

    // First inspect through a temporary copy so Modify() is called only when a
    // transaction-worthy mutation is actually required.
    FWetnessProfileParameters SanitizedCopy = Profile->GetParameters();
    if (!SanitizeParameters(SanitizedCopy, ChangeTarget))
    {
        return false;
    }

    Profile->Modify();
    ParametersProperty->CopyCompleteValue(ParametersMemory, &SanitizedCopy);
    Profile->MarkPackageDirty();

    FPropertyChangedEvent ChangedEvent(
        const_cast<FStructProperty*>(ParametersProperty),
        EPropertyChangeType::ValueSet);
    Profile->PostEditChangeProperty(ChangedEvent);
    return true;
}

bool FWetnessProfileEditorPolicy::SanitizeParameters(
    FWetnessProfileParameters& Parameters,
    TArray<FString>* OutChanges)
{
    return SanitizeStructRecursive(
        &Parameters,
        FWetnessProfileParameters::StaticStruct(),
        TEXT("Parameters"),
        OutChanges);
}

void FWetnessProfileEditorPolicy::FindProfileIssues(
    const UWetnessProfile* Profile,
    TArray<FString>& OutIssues)
{
    OutIssues.Reset();
    const FStructProperty* ParametersProperty = FindParametersProperty(Profile);
    if (Profile == nullptr || ParametersProperty == nullptr)
    {
        return;
    }

    const void* ParametersMemory = ParametersProperty->ContainerPtrToValuePtr<void>(Profile);
    FindIssuesRecursive(
        ParametersMemory,
        ParametersProperty->Struct,
        TEXT("Parameters"),
        OutIssues);
}

bool FWetnessProfileEditorPolicy::IsObsoleteEditorPropertyPath(const FString& PropertyPath)
{
    static const TSet<FString> ObsoletePropertyPaths = {
        // FWetnessProfileParameters compatibility fields.
        TEXT("Parameters.Absorption"),
        TEXT("Parameters.SpreadRate"),
        TEXT("Parameters.DryRate"),
        TEXT("Parameters.GravityFlowStrength"),
        TEXT("Parameters.WetVisualStrength"),
        TEXT("Parameters.AbsorbedWetness.WetVisualStrength"),

        // FSurfaceWaterProfileParameters legacy mask/rendering fields.
        TEXT("Parameters.SurfaceWater.FlowMaskMin"),
        TEXT("Parameters.SurfaceWater.FlowMaskMax"),
        TEXT("Parameters.SurfaceWater.FlowMaskTexture"),
        TEXT("Parameters.SurfaceWater.DropletMaskMin"),
        TEXT("Parameters.SurfaceWater.DropletMaskMax"),
        TEXT("Parameters.SurfaceWater.DropletMaskTexture"),
        TEXT("Parameters.SurfaceWater.NormalStrength"),
        TEXT("Parameters.SurfaceWater.SurfaceRoughness"),
        TEXT("Parameters.SurfaceWater.FlowTiling"),
        TEXT("Parameters.SurfaceWater.FlowPanningX"),
        TEXT("Parameters.SurfaceWater.FlowPanningY"),
        TEXT("Parameters.SurfaceWater.FlowNormalStrength"),
        TEXT("Parameters.SurfaceWater.FlowRoughness"),
        TEXT("Parameters.SurfaceWater.FlowNormalTexture"),
        TEXT("Parameters.SurfaceWater.DropletTiling"),
        TEXT("Parameters.SurfaceWater.SurfaceAmountThresholdMin"),
        TEXT("Parameters.SurfaceWater.SurfaceAmountThresholdMax"),
    };
    return ObsoletePropertyPaths.Contains(PropertyPath);
}
