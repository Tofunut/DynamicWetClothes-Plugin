#include "Core/DWCQualityLODController.h"

void FDWCQualityLODController::SetEnabled(const bool bInEnabled)
{
    bEnabled = bInEnabled;
}

void FDWCQualityLODController::SetProfile(const UDWCQualityLODProfile* InProfile)
{
    Profile = InProfile;
}

FDWCQualityLODPolicy FDWCQualityLODController::ResolvePolicy(const int32 InQualityLOD) const
{
    const int32 SafeLOD = FMath::Max(0, InQualityLOD);

    const FDWCQualityLODPolicy FallbackPolicy = UDWCQualityLODProfile::MakeDefaultPolicyForLOD(SafeLOD);

    const UDWCQualityLODProfile* ResolvedProfile = Profile;
    if (!bEnabled || ResolvedProfile == nullptr || ResolvedProfile->Policies.IsEmpty())
    {
        return bEnabled ? FallbackPolicy : FDWCQualityLODPolicy();
    }

    const int32 PolicyIndex = FMath::Clamp(SafeLOD, 0, ResolvedProfile->Policies.Num() - 1);
    return ResolvedProfile->Policies[PolicyIndex].Policy;
}

void FDWCQualityLODController::SetLOD(FDWCQualityLODRuntimeState& State, const int32 InQualityLOD) const
{
    const int32 SafeLOD = FMath::Max(0, InQualityLOD);
    const bool bLODChanged = State.CurrentQualityLOD != SafeLOD;
    State.CurrentQualityLOD = SafeLOD;
    State.ResolvedPolicy = ResolvePolicy(SafeLOD);
    if (bLODChanged)
    {
        State.RenderUpdateAccumulator = 0.0f;
        State.SurfaceWaterUpdateAccumulator = 0.0f;
    }
}

void FDWCQualityLODController::RefreshPolicy(FDWCQualityLODRuntimeState& State) const
{
    State.ResolvedPolicy = ResolvePolicy(State.CurrentQualityLOD);
}

bool FDWCQualityLODController::ShouldRunSurfaceWater(FDWCQualityLODRuntimeState& State, const float BaseInterval) const
{
    if (bEnabled && !State.ResolvedPolicy.bUpdateSurfaceWater)
    {
        return false;
    }

    return ShouldRunInterval(State.SurfaceWaterUpdateAccumulator, BaseInterval, State.ResolvedPolicy.SurfaceWaterUpdateInterval);
}

bool FDWCQualityLODController::ShouldRunRendering(FDWCQualityLODRuntimeState& State, const float BaseInterval) const
{
    if (bEnabled && !State.ResolvedPolicy.bUpdateWetRendering)
    {
        return false;
    }

    return ShouldRunInterval(State.RenderUpdateAccumulator, BaseInterval, State.ResolvedPolicy.RenderUpdateInterval);
}

bool FDWCQualityLODController::ShouldUpdateWrinkle(const FDWCQualityLODRuntimeState& State) const
{
    return !bEnabled || State.ResolvedPolicy.bUpdateWrinkle;
}

bool FDWCQualityLODController::ShouldUpdateTransparency(const FDWCQualityLODRuntimeState& State) const
{
    return !bEnabled || State.ResolvedPolicy.bUpdateTransparency;
}

bool FDWCQualityLODController::ShouldRunInterval(
    float& Accumulator,
    const float BaseInterval,
    const float TargetInterval) const
{
    if (!bEnabled)
    {
        return true;
    }

    const float SafeBaseInterval = FMath::Max(KINDA_SMALL_NUMBER, BaseInterval);
    const float RequestedTargetInterval = TargetInterval > 0.0f ? TargetInterval : SafeBaseInterval;
    const float SafeTargetInterval = FMath::Max(SafeBaseInterval, RequestedTargetInterval);
    Accumulator += SafeBaseInterval;
    if (Accumulator + KINDA_SMALL_NUMBER < SafeTargetInterval)
    {
        return false;
    }

    Accumulator = FMath::Max(0.0f, Accumulator - SafeTargetInterval);
    return true;
}
