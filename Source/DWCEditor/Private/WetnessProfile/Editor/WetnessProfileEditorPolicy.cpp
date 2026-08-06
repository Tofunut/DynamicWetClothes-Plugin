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
    // absorbed rendering strengths are packed directly into the floating-point
    // render-profile LUT, and the material clamps their final response.
    const FNumericRule NumericRules[] = {
        // Simulation | Absorbed Wetness
        { TEXT("Parameters.AbsorbedWetness.AbsorptionFraction"), 0.0, 1.0, 0.5 },
        { TEXT("Parameters.AbsorbedWetness.AbsorptionRate"), 0.0, 10.0, 1.0 },
        { TEXT("Parameters.AbsorbedWetness.MaxPendingWaterPerPixel"),
          0.0,
          static_cast<double>(TNumericLimits<float>::Max()),
          0.0 },
        { TEXT("Parameters.AbsorbedWetness.SpreadRate"), 0.0, 10.0, 6.5 },
        { TEXT("Parameters.AbsorbedWetness.DryRate"), 0.0, 100.0, 20.0 },
        { TEXT("Parameters.AbsorbedWetness.GravityFlowStrength"), 0.0, 10.0, 1.0 },

        // Rendering | Absorbed Wetness
        { TEXT("Parameters.AbsorbedWetness.AbsorbedDarkeningStrength"), 0.0, 3.0, 0.5 },
        { TEXT("Parameters.AbsorbedWetness.AbsorbedGlossinessStrength"), 0.0, 3.0, 0.5 },

        // Simulation | Surface Water
        { TEXT("Parameters.SurfaceWater.DropletDryRate"), 0.0, 100.0, 20.0 },
        { TEXT("Parameters.SurfaceWater.DropletSpawnProbability"), 0.0, 1.0, 0.5 },
        { TEXT("Parameters.SurfaceWater.DropletRadiusPixels"), 0.0, 256.0, 16.0 },
        { TEXT("Parameters.SurfaceWater.DropletHeightPixels"), 0.0, 256.0, 16.0 },
        { TEXT("Parameters.SurfaceWater.DropletFlowSpawnProbability"), 0.0, 1.0, 0.5 },
        { TEXT("Parameters.SurfaceWater.DropletFlowRadiusPixels"), 0.0, 256.0, 16.0 },
        { TEXT("Parameters.SurfaceWater.DropletFlowHeightPixels"), 0.0, 256.0, 32.0 },
        { TEXT("Parameters.SurfaceWater.DropletFlowSpawnPositionSpread"), 0.0, 1.0, 0.35 },

        // Rendering | Surface Water
        { TEXT("Parameters.SurfaceWater.SurfaceWaterTotalStrength"), 0.0, 1.0, 0.5 },
        { TEXT("Parameters.SurfaceWater.SurfaceWaterColorBlend"), 0.0, 1.0, 1.0 },
        { TEXT("Parameters.SurfaceWater.SurfaceWaterTargetRoughness"), 0.0, 1.0, 0.02 },
        { TEXT("Parameters.SurfaceWater.SurfaceWaterNormalStrength"), 0.0, 3.0, 3.0 },
        { TEXT("Parameters.SurfaceWater.SurfaceWaterRoughnessBlend"), 0.0, 1.0, 0.85 },
        { TEXT("Parameters.SurfaceWater.SurfaceWaterSpecular"), 0.0, 1.0, 0.5 },
        { TEXT("Parameters.SurfaceWater.DropletFlowTotalStrength"), 0.0, 1.0, 0.5 },
        { TEXT("Parameters.SurfaceWater.DropletFlowColorBlend"), 0.0, 1.0, 1.0 },
        { TEXT("Parameters.SurfaceWater.DropletFlowNormalStrength"), 0.0, 3.0, 3.0 },
        { TEXT("Parameters.SurfaceWater.DropletFlowTargetRoughness"), 0.0, 1.0, 0.02 },
        { TEXT("Parameters.SurfaceWater.DropletFlowRoughnessBlend"), 0.0, 1.0, 0.85 },
        { TEXT("Parameters.SurfaceWater.DropletFlowSpecular"), 0.0, 1.0, 0.5 },
    };

    constexpr float MinRenderableRejectedWaterFraction = 0.05f;
    constexpr float MinRenderableDropletSpawnProbability = 0.05f;
    constexpr float MinRenderableDropletRadiusPixels = 1.0f;

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

    bool SanitizeIntegerProperty(
        FIntProperty* Property,
        void* Container,
        const FNumericRule& Rule,
        const FString& PropertyPath,
        TArray<FString>* OutChanges)
    {
        const int32 Original = Property->GetPropertyValue_InContainer(Container);
        const int32 Sanitized = FMath::Clamp(
            Original,
            static_cast<int32>(Rule.MinValue),
            static_cast<int32>(Rule.MaxValue));
        if (Original == Sanitized)
        {
            return false;
        }

        Property->SetPropertyValue_InContainer(Container, Sanitized);
        if (OutChanges != nullptr)
        {
            OutChanges->Add(FString::Printf(
                TEXT("%s: %d -> %d (allowed %.0f..%.0f)"),
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
            else if (FIntProperty* IntProperty = CastField<FIntProperty>(Property))
            {
                bChanged |= SanitizeIntegerProperty(IntProperty, StructMemory, *Rule, PropertyPath, OutChanges);
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

    void FindIntegerIssue(
        const FIntProperty* Property,
        const void* Container,
        const FNumericRule& Rule,
        const FString& PropertyPath,
        TArray<FString>& OutIssues)
    {
        const int32 Value = Property->GetPropertyValue_InContainer(Container);
        if (Value < Rule.MinValue || Value > Rule.MaxValue)
        {
            OutIssues.Add(FString::Printf(
                TEXT("%s is %d; allowed range is %.0f..%.0f."),
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
            else if (const FIntProperty* IntProperty = CastField<FIntProperty>(Property))
            {
                FindIntegerIssue(IntProperty, StructMemory, *Rule, PropertyPath, OutIssues);
            }
        }
    }

    const FStructProperty* FindParametersProperty(const UWetnessProfile* Profile)
    {
        return Profile != nullptr
                   ? FindFProperty<FStructProperty>(Profile->GetClass(), TEXT("Parameters"))
                   : nullptr;
    }

    bool ClampRenderableFloat(
        float& Value,
        const float MinValue,
        const TCHAR* Label,
        TArray<FString>* OutChanges)
    {
        if (Value >= MinValue)
        {
            return false;
        }

        const float Original = Value;
        Value = MinValue;
        if (OutChanges != nullptr)
        {
            OutChanges->Add(FString::Printf(
                TEXT("%s: %.3f -> %.3f"),
                Label,
                Original,
                Value));
        }
        return true;
    }

    bool ApplySurfaceWaterRenderableMinimums(
        FWetnessProfileParameters& Parameters,
        TArray<FString>* OutChanges)
    {
        FSurfaceWaterProfileParameters& Surface = Parameters.SurfaceWater;
        if (!Surface.bEnabled)
        {
            return false;
        }

        bool bChanged = false;
        bChanged |= ClampRenderableFloat(
            Surface.DropletSpawnProbability,
            MinRenderableDropletSpawnProbability,
            TEXT("Droplet spawn chance"),
            OutChanges);
        bChanged |= ClampRenderableFloat(
            Surface.DropletRadiusPixels,
            MinRenderableDropletRadiusPixels,
            TEXT("Droplet1 width"),
            OutChanges);
        bChanged |= ClampRenderableFloat(
            Surface.DropletHeightPixels,
            MinRenderableDropletRadiusPixels,
            TEXT("Droplet1 height"),
            OutChanges);
        bChanged |= ClampRenderableFloat(
            Surface.DropletFlowSpawnProbability,
            MinRenderableDropletSpawnProbability,
            TEXT("Droplet2 spawn chance"),
            OutChanges);
        bChanged |= ClampRenderableFloat(
            Surface.DropletFlowRadiusPixels,
            MinRenderableDropletRadiusPixels,
            TEXT("Droplet2 width"),
            OutChanges);
        bChanged |= ClampRenderableFloat(
            Surface.DropletFlowHeightPixels,
            MinRenderableDropletRadiusPixels,
            TEXT("Droplet2 height"),
            OutChanges);

        if (Parameters.AbsorbedWetness.bEnabled)
        {
            const float RejectedWaterFraction =
                FMath::Clamp(Parameters.GetRejectedWaterFraction(), 0.0f, 1.0f);
            if (RejectedWaterFraction < MinRenderableRejectedWaterFraction)
            {
                const float Original = Parameters.AbsorbedWetness.AbsorptionFraction;
                Parameters.AbsorbedWetness.AbsorptionFraction = 1.0f - MinRenderableRejectedWaterFraction;
                bChanged = true;
                if (OutChanges != nullptr)
                {
                    OutChanges->Add(FString::Printf(
                        TEXT("Absorption: %.3f -> %.3f"),
                        Original,
                        Parameters.AbsorbedWetness.AbsorptionFraction));
                }
            }
        }

        return bChanged;
    }

    void FindSurfaceWaterRenderableIssues(
        const FWetnessProfileParameters& Parameters,
        TArray<FString>& OutIssues)
    {
        const FSurfaceWaterProfileParameters& Surface = Parameters.SurfaceWater;
        if (!Surface.bEnabled)
        {
            return;
        }

        const float RejectedWaterFraction =
            FMath::Clamp(Parameters.GetRejectedWaterFraction(), 0.0f, 1.0f);
        if (RejectedWaterFraction < MinRenderableRejectedWaterFraction)
        {
            OutIssues.Add(FString::Printf(
                TEXT("Absorption leaves only %.1f%% rejected water; Surface Water needs at least %.1f%%."),
                RejectedWaterFraction * 100.0f,
                MinRenderableRejectedWaterFraction * 100.0f));
        }
        if (Surface.DropletSpawnProbability < MinRenderableDropletSpawnProbability)
        {
            OutIssues.Add(FString::Printf(
                TEXT("Droplet spawn chance is %.1f%%; values below %.1f%% can prevent stamps from spawning."),
                Surface.DropletSpawnProbability * 100.0f,
                MinRenderableDropletSpawnProbability * 100.0f));
        }
        if (Surface.DropletRadiusPixels < MinRenderableDropletRadiusPixels)
        {
            OutIssues.Add(FString::Printf(
                TEXT("Droplet1 width is %.2f RT pixel(s); values below %.2f cannot produce a stable stamp."),
                Surface.DropletRadiusPixels,
                MinRenderableDropletRadiusPixels));
        }
        if (Surface.DropletHeightPixels < MinRenderableDropletRadiusPixels)
        {
            OutIssues.Add(FString::Printf(
                TEXT("Droplet1 height is %.2f RT pixel(s); values below %.2f cannot produce a stable stamp."),
                Surface.DropletHeightPixels,
                MinRenderableDropletRadiusPixels));
        }
        if (Surface.DropletFlowSpawnProbability < MinRenderableDropletSpawnProbability)
        {
            OutIssues.Add(FString::Printf(
                TEXT("Droplet2 spawn chance is %.1f%%; values below %.1f%% can prevent stamps from spawning."),
                Surface.DropletFlowSpawnProbability * 100.0f,
                MinRenderableDropletSpawnProbability * 100.0f));
        }
        if (Surface.DropletFlowRadiusPixels < MinRenderableDropletRadiusPixels)
        {
            OutIssues.Add(FString::Printf(
                TEXT("Droplet2 width is %.2f RT pixel(s); values below %.2f cannot produce a stable stamp."),
                Surface.DropletFlowRadiusPixels,
                MinRenderableDropletRadiusPixels));
        }
        if (Surface.DropletFlowHeightPixels < MinRenderableDropletRadiusPixels)
        {
            OutIssues.Add(FString::Printf(
                TEXT("Droplet2 height is %.2f RT pixel(s); values below %.2f cannot produce a stable stamp."),
                Surface.DropletFlowHeightPixels,
                MinRenderableDropletRadiusPixels));
        }
    }
} // namespace

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
    bool bChanged = SanitizeStructRecursive(
        &Parameters,
        FWetnessProfileParameters::StaticStruct(),
        TEXT("Parameters"),
        OutChanges);
    bChanged |= ApplySurfaceWaterRenderableMinimums(Parameters, OutChanges);
    return bChanged;
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
    FindSurfaceWaterRenderableIssues(Profile->GetParameters(), OutIssues);
}
