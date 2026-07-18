#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "Templates/UniquePtr.h"
#include "WetInputSystem/WetSurfaceContactResolver.h"

class UDynamicWetClothesComponent;
class USkeletalMeshComponent;
class UWetClothingAsset;
class UMaterialInstanceDynamic;
struct FWetClothingSettings;

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
    bool bUseEightDirectionDiffusion = false;
};

/** DWC-facing interface. It intentionally contains no RHI/RenderCore types. */
class DWC_API IDWCGPUBackend
{
public:
    IDWCGPUBackend();
    virtual ~IDWCGPUBackend();

    virtual bool Initialize(const FDWCGPUBackendInitArgs& Args) = 0;
    virtual bool EnqueueResolvedContacts(const TArray<FDWCResolvedSurfaceContact>& Contacts) = 0;
    virtual bool ApplyWetAll(float Amount) = 0;
    virtual void Update(float DeltaSeconds) = 0;

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
