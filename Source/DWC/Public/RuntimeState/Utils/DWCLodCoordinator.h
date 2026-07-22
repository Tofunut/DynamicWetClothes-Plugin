#pragma once

#include "CoreMinimal.h"
#include "Core/DWCQualityLODProfile.h"
#include "Templates/UniquePtr.h"

class UWorld;
struct FDWCWetMeshReceiverRuntime;
class FDWCQualityLODController;
class FDWCQualityLODEvaluator;

/** Component-owned coordinator for Quality LOD policy and render LOD selection. */
class FDWCLodCoordinator
{
  public:
    FDWCLodCoordinator();
    ~FDWCLodCoordinator();

    void NormalizeScreenSizeThresholds(TArray<FDWCQualityLODScreenSizeThreshold>& Thresholds) const;
    void ConfigureQualityLOD(bool bEnabled, const UDWCQualityLODProfile* Profile);
    void SetReceiverQualityLOD(FDWCWetMeshReceiverRuntime& Receiver, int32 InQualityLOD) const;
    void RefreshReceiverQualityLODPolicy(FDWCWetMeshReceiverRuntime& Receiver) const;

    bool ShouldRunSurfaceWater(FDWCQualityLODRuntimeState& State, float BaseInterval);
    bool ShouldRunCPUWetnessRendering(FDWCQualityLODRuntimeState& State, float BaseInterval);
    bool ShouldEnableCPUWetnessRendering(const FDWCQualityLODRuntimeState& State) const;
    bool ShouldUpdateWrinkle(const FDWCQualityLODRuntimeState& State) const;
    bool ShouldUpdateTransparency(const FDWCQualityLODRuntimeState& State) const;

    bool HasAnyRenderLODSettings(const TArray<FDWCQualityLODScreenSizeThreshold>& Thresholds) const;
    bool UpdateRenderLOD(
        UWorld* World,
        const UObject* OwnerForLogs,
        const TArray<TUniquePtr<FDWCWetMeshReceiverRuntime>>& Receivers,
        const TArray<FDWCQualityLODScreenSizeThreshold>& Thresholds,
        int32& OutLODLevel);
    void ResetRenderLODState();

    int32 GetCurrentRenderLODLevel() const;
    float GetMergedReceiverScreenSize() const;

  private:
    bool CalculateRenderLODScreenSize(
        UWorld* World,
        const TArray<TUniquePtr<FDWCWetMeshReceiverRuntime>>& Receivers,
        float& OutScreenSize,
        FBoxSphereBounds& OutBounds,
        bool& bOutInViewFrustum) const;
    bool FindRenderLODLevel(
        const TArray<FDWCQualityLODScreenSizeThreshold>& Thresholds,
        float ScreenSize,
        int32& OutLODLevel) const;

    TUniquePtr<FDWCQualityLODController> QualityLODController;
    TUniquePtr<FDWCQualityLODEvaluator> QualityLODEvaluator;
    FDWCQualityLODScreenSizeRuntimeState RenderLODState;
};
