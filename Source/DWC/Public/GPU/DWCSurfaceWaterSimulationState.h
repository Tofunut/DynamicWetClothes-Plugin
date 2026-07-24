#pragma once

#include "CoreMinimal.h"

class UTextureRenderTarget2D;

/**
 * DWC-facing interface for surface-water render-target simulation.
 *
 * The concrete implementation, render targets, compute shader and render-thread
 * dispatch live in the optional DWCGPU module. DWC owns only this module-neutral
 * interface so the runtime module does not acquire a dependency on DWCGPU.
 */
class DWC_API IDWCSurfaceWaterSimulationState
{
public:
    IDWCSurfaceWaterSimulationState();
    virtual ~IDWCSurfaceWaterSimulationState();

    virtual bool Initialize(UObject* Outer, int32 Resolution) = 0;
    virtual void Reset() = 0;
    virtual void Release() = 0;

    virtual bool IsValid() const = 0;
    virtual UTextureRenderTarget2D* GetDropletRenderTarget() const = 0;
    virtual UTextureRenderTarget2D* GetRivuletRenderTarget() const = 0;

    virtual void QueueDropletStamp(
        const FVector2f& UV,
        float Amount,
        float RadiusPixels,
        float LifetimeSeconds) = 0;

    /**
     * Queues a rivulet in DWCDataUV space.
     * EncodedDataUVFlowAngle rotates the RT stamp geometry, while
     * EncodedNormalUVFlowAngle is stored in RivuletRT.A for material normal sampling.
     */
    virtual void QueueRivuletStamp(
        const FVector2f& UV,
        float EncodedDataUVFlowAngle,
        float EncodedNormalUVFlowAngle,
        float Amount,
        float WidthPixels,
        float LengthPixels,
        float LifetimeSeconds) = 0;

    virtual bool FlushStamps(float CurrentSurfaceTimeSeconds) = 0;

    virtual void SetSimulationPaused(bool bPaused) = 0;
    virtual bool IsSimulationPaused() const = 0;
    virtual int32 GetResolution() const = 0;
    virtual int32 GetPendingStampCount() const = 0;
    virtual uint64 GetAllocatedMemoryBytes() const = 0;
    virtual uint64 GetEstimatedGpuMemoryBytes() const = 0;
};
