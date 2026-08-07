//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "Core/DWCQualityLODProfile.h"

namespace
{
    constexpr int32 DefaultQualityLODCount = 5;
    constexpr float DefaultCPUWetnessRenderUpdateInterval = 0.1f;

    FDWCQualityLODPolicyEntry MakeDefaultQualityLODPolicyEntry(const int32 InQualityLOD)
    {
        FDWCQualityLODPolicyEntry Entry;
        Entry.LODLevel = InQualityLOD;
        Entry.Policy = UDWCQualityLODProfile::MakeDefaultPolicyForLOD(InQualityLOD);
        return Entry;
    }
}

UDWCQualityLODProfile::UDWCQualityLODProfile()
{
    if (Policies.IsEmpty())
    {
        Policies.Reserve(DefaultQualityLODCount);
        for (int32 QualityLOD = 0; QualityLOD < DefaultQualityLODCount; ++QualityLOD)
        {
            Policies.Add(MakeDefaultQualityLODPolicyEntry(QualityLOD));
        }
    }
}

FDWCQualityLODPolicy UDWCQualityLODProfile::MakeDefaultPolicyForLOD(const int32 InQualityLOD)
{
    const int32 SafeLOD = FMath::Max(0, InQualityLOD);

    FDWCQualityLODPolicy DefaultPolicy;
    const float IntervalMultiplier = SafeLOD == 0 ? 1.0f : FMath::Pow(2.0f, static_cast<float>(FMath::Min(SafeLOD, 4)));
    DefaultPolicy.RenderUpdateInterval = DefaultCPUWetnessRenderUpdateInterval * IntervalMultiplier;

    if (SafeLOD == 2)
    {
        DefaultPolicy.bUpdateSurfaceWater = false;
    }
    else if (SafeLOD == 3)
    {
        DefaultPolicy.bUpdateSurfaceWater = false;
        DefaultPolicy.bUpdateWrinkle = false;
    }
    else if (SafeLOD >= 4)
    {
        DefaultPolicy.bUpdateSurfaceWater = false;
        DefaultPolicy.bUpdateWetRendering = false;
        DefaultPolicy.bUpdateWrinkle = false;
        DefaultPolicy.bUpdateTransparency = false;
    }

    return DefaultPolicy;
}

void UDWCQualityLODProfile::NormalizeLODLevels()
{
    for (int32 Index = 0; Index < Policies.Num(); ++Index)
    {
        Policies[Index].LODLevel = Index;
    }
}

#if WITH_EDITOR
void UDWCQualityLODProfile::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);
    NormalizeLODLevels();
}
#endif
