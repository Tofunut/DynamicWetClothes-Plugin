#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Templates/UniquePtr.h"
#include "RuntimeState/Utils/WetSurfaceContactResolver.h"

class UDynamicWetClothesComponent;
class USkeletalMeshComponent;
class UWetClothingAsset;
class UMaterialInstanceDynamic;
struct FWetClothingSettings;


enum class EDWCSurfaceStampType : uint8
{
    Droplet,
    Rivulet
};

/** CPU-side routing request. TriangleID is intentionally resolved from UV through TexelLookup on GPU. */
struct DWC_API FDWCSurfaceStampRequest
{
    EDWCSurfaceStampType Type = EDWCSurfaceStampType::Droplet;
    FVector2f UV = FVector2f::ZeroVector;
    FVector2f HalfSizePixels = FVector2f::ZeroVector;
    float Amount = 0.0f;
    float LifetimeSeconds = 0.0f;
    int32 MaterialSlotIndex = INDEX_NONE;
};

struct DWC_API FDWCGPUBackendInitArgs
{
    UDynamicWetClothesComponent* OwnerComponent = nullptr;
    USkeletalMeshComponent* TargetSkeletalMesh = nullptr;
    UWetClothingAsset* WetClothingAsset = nullptr;
    const FWetClothingSettings* WetnessSettings = nullptr;
    TArray<TObjectPtr<UMaterialInstanceDynamic>>* WetMaterialInstances = nullptr;
    int32 LODIndex = 0;
    float SpreadRateScale = 1.0f;
    float DryRateScale = 1.0f;
    float GravityFlowStrengthScale = 1.0f;
    float CapillaryImmediateAbsorptionFraction = 0.65f;
    int32 ReceiverGPUId = 0;
    bool bUseEightDirectionDiffusion = false;
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

    virtual bool Initialize(const FDWCGPUBackendInitArgs& Args) = 0;
    virtual bool EnqueueResolvedContacts(const TArray<FDWCResolvedSurfaceContact>& Contacts) = 0;
    virtual bool EnqueueSurfaceStamps(const TArray<FDWCSurfaceStampRequest>& Stamps) = 0;
    virtual bool ApplyWetAll(float Amount) = 0;
    virtual void Update(float DeltaSeconds) = 0;
    virtual FDWCGPUBackendStats GetStats() const = 0;

    virtual void Shutdown() = 0;
};

/** Implemented by the optional DWCGPU module. */
class DWC_API IDWCGPUModule : public IModuleInterface
{
public:
    IDWCGPUModule();
    virtual ~IDWCGPUModule();

    virtual TUniquePtr<IDWCGPUBackend> CreateBackend() = 0;
};
