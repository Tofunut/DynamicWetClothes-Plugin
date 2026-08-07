//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GPU/DWCGPUBackend.h"
#include "DataAssets/WetnessProfile.h"
#include "UObject/StrongObjectPtr.h"

class FDWCGPUPreviewSimulator final : public IDWCGPUPreviewSimulator
{
public:
    virtual ~FDWCGPUPreviewSimulator() override;
    virtual bool Initialize(const FDWCGPUPreviewInitArgs& Args) override;
    virtual void SetProfileParameters(const FWetnessProfileParameters& Parameters) override;
    virtual void SetScenarioSplashUV(FVector2f InSplashUV) override;
    virtual void SetInteractionCursorScale(float InScale) override;
    virtual void SetPreviewChannels(
        bool bAbsorbedEnabled,
        bool bSurfaceEnabled,
        bool bDroplet1Enabled,
        bool bDroplet2Enabled) override;
    virtual void Restart() override;
    virtual void RequestSplash() override;
    virtual void Step(float DeltaSeconds, float ScenarioTimeSeconds) override;
    virtual bool IsReady() const override { return bInitialized; }
    virtual int32 GetResolution() const override { return Resolution; }
    virtual UTextureRenderTarget2D* GetWetnessMap() const override;
    virtual UTextureRenderTarget2D* GetDroplet1Map() const override;
    virtual UTextureRenderTarget2D* GetDroplet2Map() const override;
    virtual void Shutdown() override;

private:
    struct FRenderState;

    TStrongObjectPtr<UTextureRenderTarget2D> CreateRenderTarget(const FName& Name, bool bBilinear) const;
    void ClearAllRenderTargets();

    TWeakObjectPtr<UObject> WorldContextObject;
    TArray<TStrongObjectPtr<UTextureRenderTarget2D>> WetnessMaps;
    TArray<TStrongObjectPtr<UTextureRenderTarget2D>> PendingWetnessMaps;
    TStrongObjectPtr<UTextureRenderTarget2D> Droplet1Map;
    TStrongObjectPtr<UTextureRenderTarget2D> Droplet2Map;
    TSharedPtr<FRenderState, ESPMode::ThreadSafe> RenderState;
    TUniquePtr<FWetnessProfileParameters> CachedParameters;
    int32 CurrentWetnessIndex = 0;
    int32 CurrentPendingIndex = 0;
    int32 Resolution = 512;
    float MaxWetness = 1.15f;
    float CapillaryImmediateAbsorptionFraction = 0.65f;
    bool bUseEightDirectionDiffusion = true;
    bool bInitialized = false;
    bool bPreviewAbsorbedEnabled = true;
    bool bPreviewSurfaceEnabled = true;
    bool bPreviewDroplet1Enabled = true;
    bool bPreviewDroplet2Enabled = false;
    bool bManualSplashRequested = false;
    float InteractionCursorScale = 1.0f;
    FVector2f ScenarioSplashUV = FVector2f(0.5f, 0.5f);
};
