// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Templates/UniquePtr.h"
#include "RuntimeState/Utils/WetSurfaceContactResolver.h"

class UObject;
class UDynamicWetClothesComponent;
class USkeletalMeshComponent;
class UWetClothingAsset;
class UMaterialInstanceDynamic;
class UTextureRenderTarget2D;
struct FWetClothingSettings;
struct FWetnessProfileParameters;

/** CPU-side routing request. TriangleID is intentionally resolved from UV through TexelLookup on GPU. */
struct DWC_API FDWCSurfaceStampRequest
{
    FVector2f UV = FVector2f::ZeroVector;
    FVector2f HalfSizePixels = FVector2f::ZeroVector;
    float     Amount = 0.0f;
    int32     MaterialSlotIndex = INDEX_NONE;
    bool      bDroplet2 = false;
};

struct DWC_API FDWCGPUBackendInitArgs
{
    UDynamicWetClothesComponent*                  OwnerComponent = nullptr;
    USkeletalMeshComponent*                       TargetSkeletalMesh = nullptr;
    UWetClothingAsset*                            WetClothingAsset = nullptr;
    const FWetClothingSettings*                   WetnessSettings = nullptr;
    TArray<TObjectPtr<UMaterialInstanceDynamic>>* WetMaterialInstances = nullptr;
    int32                                         LODIndex = 0;
    float                                         SpreadRateScale = 1.0f;
    float                                         DryRateScale = 1.0f;
    float                                         GravityFlowStrengthScale = 1.0f;
    float                                         CapillaryImmediateAbsorptionFraction = 0.65f;
    int32                                         ReceiverGPUId = 0;
    bool                                          bUseEightDirectionDiffusion = true;
};

struct DWC_API FDWCGPUBackendStats
{
    uint32 ActiveMaterialCount = 0;
    uint32 PendingSurfaceStampCount = 0;
    uint64 CPUBytes = 0;
    uint64 GPUBytes = 0;
};

/** DWC-facing interface. It intentionally contains no RHI/RenderCore types. */
class DWC_API IDWCGPUBackend
{
  public:
    IDWCGPUBackend();
    virtual ~IDWCGPUBackend();

    virtual bool                Initialize(const FDWCGPUBackendInitArgs& Args) = 0;
    virtual bool                EnqueueResolvedContacts(const TArray<FDWCResolvedSurfaceContact>& Contacts) = 0;
    virtual bool                EnqueueSurfaceStamps(const TArray<FDWCSurfaceStampRequest>& Stamps) = 0;
    virtual bool                ApplyWetAll(float Amount) = 0;
    virtual void                ClearPendingWetnessMaps() = 0;
    virtual void                Update(float DeltaSeconds) = 0;
    virtual FDWCGPUBackendStats GetStats() const = 0;

    virtual void Shutdown() = 0;
};

/** Lightweight editor-only simulation input that reuses the runtime GPU wetness shaders. */
struct DWC_API FDWCGPUPreviewInitArgs
{
    UObject* WorldContextObject = nullptr;
    int32    Resolution = 512;
    float    MaxWetness = 1.15f;
    float    CapillaryImmediateAbsorptionFraction = 0.65f;
    bool     bUseEightDirectionDiffusion = true;
};

/** Optional DWCGPU-backed Wetness Profile preview. No RHI types cross this interface. */
class DWC_API IDWCGPUPreviewSimulator
{
  public:
    IDWCGPUPreviewSimulator() = default;
    virtual ~IDWCGPUPreviewSimulator() = default;

    virtual bool Initialize(const FDWCGPUPreviewInitArgs& Args) = 0;
    virtual void SetProfileParameters(const FWetnessProfileParameters& Parameters) = 0;
    virtual void SetScenarioSplashUV(FVector2f InSplashUV) = 0;
    virtual void SetInteractionCursorScale(float InScale) {}
    virtual void SetPreviewChannels(
        bool bAbsorbedEnabled,
        bool bSurfaceEnabled,
        bool bDroplet1Enabled,
        bool bDroplet2Enabled) = 0;
    virtual void                    Restart() = 0;
    virtual void                    RequestSplash() = 0;
    virtual void                    Step(float DeltaSeconds, float ScenarioTimeSeconds) = 0;
    virtual bool                    IsReady() const = 0;
    virtual int32                   GetResolution() const = 0;
    virtual UTextureRenderTarget2D* GetWetnessMap() const = 0;
    virtual UTextureRenderTarget2D* GetDroplet1Map() const = 0;
    virtual UTextureRenderTarget2D* GetDroplet2Map() const = 0;
    virtual void                    Shutdown() = 0;
};

/** Implemented by the optional DWCGPU module. */
class DWC_API IDWCGPUModule : public IModuleInterface
{
  public:
    IDWCGPUModule();
    virtual ~IDWCGPUModule();

    virtual TUniquePtr<IDWCGPUBackend>          CreateBackend() = 0;
    virtual TUniquePtr<IDWCGPUPreviewSimulator> CreatePreviewSimulator() = 0;
};
