#pragma once

#include "CoreMinimal.h"
#include "Core/DWCQualityLODProfile.h"

class FDWCQualityLODController
{
  public:
    void SetEnabled(bool bInEnabled);
    void SetProfile(const UDWCQualityLODProfile* InProfile);

    FDWCQualityLODPolicy ResolvePolicy(int32 InQualityLOD) const;
    void SetLOD(FDWCQualityLODRuntimeState& State, int32 InQualityLOD) const;
    void RefreshPolicy(FDWCQualityLODRuntimeState& State) const;

    bool ShouldRunWetness(FDWCQualityLODRuntimeState& State, float BaseInterval) const;
    bool ShouldRunSurfaceWater(FDWCQualityLODRuntimeState& State, float BaseInterval) const;
    bool ShouldRunRendering(FDWCQualityLODRuntimeState& State, float BaseInterval) const;
    bool ShouldUpdateWetnessSimulation(const FDWCQualityLODRuntimeState& State) const;
    bool ShouldUpdateWrinkle(const FDWCQualityLODRuntimeState& State) const;
    bool ShouldUpdateTransparency(const FDWCQualityLODRuntimeState& State) const;

  private:
    bool ShouldRunInterval(float& Accumulator, float BaseInterval, float TargetInterval) const;

    bool bEnabled = true;
    const UDWCQualityLODProfile* Profile = nullptr;
};
