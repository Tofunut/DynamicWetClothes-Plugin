#pragma once

#include "CoreMinimal.h"
#include "GPU/DWCSurfaceWaterSimulationState.h"
#include "UObject/StrongObjectPtr.h"

class UTextureRenderTarget2D;

enum class EDWCCPUSurfaceWaterStampType : uint8
{
    Droplet,
    Rivulet
};

struct FDWCCPUSurfaceWaterStamp
{
    EDWCCPUSurfaceWaterStampType Type = EDWCCPUSurfaceWaterStampType::Droplet;
    FVector2f UV = FVector2f::ZeroVector;
    float Amount = 0.0f;
    FVector2f HalfSizePixels = FVector2f::ZeroVector;
    float LifetimeSeconds = 0.0f;
    float EncodedDataUVFlowAngle = 0.75f;
    float EncodedNormalUVFlowAngle = 0.75f;
};

/**
 * DWCGPU implementation used by the CPU wetness backend for droplet/rivulet
 * presentation. Although the absorbed-wetness simulation is CPU-side, this
 * state owns GPU render targets and a compute-shader stamp pass, so it belongs
 * to the DWCGPU module.
 */
class FDWCSurfaceWaterSimulationState final : public IDWCSurfaceWaterSimulationState
{
public:
    virtual ~FDWCSurfaceWaterSimulationState() override { Release(); }

    virtual bool Initialize(UObject* Outer, int32 InResolution) override;
    virtual void Reset() override;
    virtual void Release() override;

    virtual bool IsValid() const override;
    virtual UTextureRenderTarget2D* GetDropletRenderTarget() const override { return DropletRenderTarget.Get(); }
    virtual UTextureRenderTarget2D* GetRivuletRenderTarget() const override { return RivuletRenderTarget.Get(); }

    virtual void QueueDropletStamp(
        const FVector2f& UV,
        float Amount,
        float RadiusPixels,
        float LifetimeSeconds) override;

    virtual void QueueRivuletStamp(
        const FVector2f& UV,
        float EncodedDataUVFlowAngle,
        float EncodedNormalUVFlowAngle,
        float Amount,
        float WidthPixels,
        float LengthPixels,
        float LifetimeSeconds) override;

    virtual bool FlushStamps(float CurrentSurfaceTimeSeconds) override;

    virtual void SetSimulationPaused(bool bPaused) override { bSimulationPaused = bPaused; }
    virtual bool IsSimulationPaused() const override { return bSimulationPaused; }
    virtual int32 GetResolution() const override { return Resolution; }
    virtual int32 GetPendingStampCount() const override { return PendingStamps.Num(); }
    virtual uint64 GetAllocatedMemoryBytes() const override;
    virtual uint64 GetEstimatedGpuMemoryBytes() const override;

private:
    void QueueStamp(const FDWCCPUSurfaceWaterStamp& Stamp);
    FDWCCPUSurfaceWaterStamp BuildStamp(
        EDWCCPUSurfaceWaterStampType Type,
        const FVector2f& UV,
        float Amount,
        const FVector2f& HalfSizePixels,
        float LifetimeSeconds,
        float EncodedDataUVFlowAngle = 0.75f,
        float EncodedNormalUVFlowAngle = 0.75f) const;

    TStrongObjectPtr<UTextureRenderTarget2D> DropletRenderTarget;
    TStrongObjectPtr<UTextureRenderTarget2D> RivuletRenderTarget;
    TArray<FDWCCPUSurfaceWaterStamp> PendingStamps;
    int32 Resolution = 0;
    bool bSimulationPaused = false;
};
