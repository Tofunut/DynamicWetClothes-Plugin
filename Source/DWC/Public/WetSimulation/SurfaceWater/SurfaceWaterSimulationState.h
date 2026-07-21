#pragma once

#include "CoreMinimal.h"
#include "Engine/TextureRenderTarget2D.h"
#include "UObject/StrongObjectPtr.h"

class UTexture2D;
class UTextureRenderTarget2D;

enum class ESurfaceWaterStampType : uint8
{
    Droplet,
    Flow
};

struct DWC_API FSurfaceWaterStamp
{
    ESurfaceWaterStampType Type = ESurfaceWaterStampType::Droplet;
    FVector2f UV = FVector2f::ZeroVector;
    float Amount = 0.0f;
    FVector2f HalfSizePixels = FVector2f::ZeroVector;
    float LifetimeSeconds = 0.0f;
};

/**
 * Independent surface-water presentation state.
 *
 * Each RT stores surface amount, spawn time, and lifetime. Nothing in this state reads
 * absorbed wetness, and no per-frame advection or destructive fade is needed.
 */
class DWC_API FSurfaceWaterSimulationState
{
  public:
    ~FSurfaceWaterSimulationState() { Release(); }

    bool Initialize(UObject* Outer, int32 InResolution);
    void Reset();
    void Release();

    bool IsValid() const;
    UTextureRenderTarget2D* GetDropletRenderTarget() const { return DropletRenderTarget.Get(); }
    UTextureRenderTarget2D* GetFlowRenderTarget() const { return FlowRenderTarget.Get(); }
    UTextureRenderTarget2D* GetRenderTarget() const { return GetDropletRenderTarget(); }

    void QueueDropletStamp(
        const FVector2f& UV,
        float Amount,
        float RadiusPixels,
        float LifetimeSeconds);
    void QueueFlowStamp(
        const FVector2f& UV,
        float Amount,
        float WidthPixels,
        float LengthPixels,
        float LifetimeSeconds);
    bool FlushStamps(UTexture2D* FlowMap, float CurrentSurfaceTimeSeconds);

    void SetSimulationPaused(bool bPaused) { bSimulationPaused = bPaused; }
    bool IsSimulationPaused() const { return bSimulationPaused; }
    int32 GetResolution() const { return Resolution; }
    int32 GetPendingStampCount() const { return PendingStamps.Num(); }
    ETextureRenderTargetFormat GetFormat() const { return Format; }
    uint64 GetAllocatedMemoryBytes() const;
    uint64 GetEstimatedGpuMemoryBytes() const;

  private:
    void QueueStamp(const FSurfaceWaterStamp& Stamp);
    FSurfaceWaterStamp BuildStamp(
        ESurfaceWaterStampType Type,
        const FVector2f& UV,
        float Amount,
        const FVector2f& HalfSizePixels,
        float LifetimeSeconds) const;

    TStrongObjectPtr<UTextureRenderTarget2D> DropletRenderTarget;
    TStrongObjectPtr<UTextureRenderTarget2D> FlowRenderTarget;
    TArray<FSurfaceWaterStamp> PendingStamps;
    int32 Resolution = 0;
    bool bSimulationPaused = false;
    ETextureRenderTargetFormat Format = RTF_RGBA32f;
};
