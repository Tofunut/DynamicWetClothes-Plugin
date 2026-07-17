#pragma once

#include "CoreMinimal.h"
#include "GPU/DWCGPUBackend.h"
#include "UObject/StrongObjectPtr.h"

class UMaterialInstanceDynamic;
class UTextureRenderTarget2D;

class FDWCGPUBackend final : public IDWCGPUBackend
{
public:
    virtual bool Initialize(const FDWCGPUBackendInitArgs& Args) override;
    virtual bool EnqueueResolvedContacts(const TArray<FDWCResolvedSurfaceContact>& Contacts) override;
    virtual bool ApplyWetAll(float Amount) override;
    virtual void Update(float DeltaSeconds) override;
    virtual void Shutdown() override;

private:
    struct FStaticSimulationData;
    struct FRenderState;

    struct FMaterialSlotRuntime
    {
        int32 MaterialSlotIndex = INDEX_NONE;
        int32 StaticSlotIndex = INDEX_NONE;
        int32 Resolution = 0;
        TArray<TStrongObjectPtr<UTextureRenderTarget2D>> WetnessMaps;
        int32 CurrentTextureIndex = 0;
        TWeakObjectPtr<UMaterialInstanceDynamic> MaterialInstance;

        UTextureRenderTarget2D* GetCurrentMap() const;
        UTextureRenderTarget2D* GetNextMap() const;
        void SwapMaps();
    };

    bool BuildStaticSimulationData();
    bool CreateSlotResources();
    void DispatchSimulation(TArray<FDWCResolvedSurfaceContact>&& Contacts, float WetAllAmount, float DeltaSeconds);

    TWeakObjectPtr<UDynamicWetClothesComponent> OwnerComponent;
    TWeakObjectPtr<USkeletalMeshComponent> TargetSkeletalMesh;
    TWeakObjectPtr<UWetClothingAsset> WetClothingAsset;
    TArray<TObjectPtr<UMaterialInstanceDynamic>>* WetMaterialInstances = nullptr;
    FName WetnessMapParameterName = TEXT("DWC_WetnessMap");

    TSharedPtr<const FStaticSimulationData, ESPMode::ThreadSafe> StaticSimulationData;
    TSharedPtr<FRenderState, ESPMode::ThreadSafe> RenderState;
    TArray<FMaterialSlotRuntime> MaterialSlots;
    TArray<FDWCResolvedSurfaceContact> PendingContacts;
    float PendingWetAllAmount = 0.0f;
    float MaxWetness = 1.0f;
    float SpreadRateScale = 1.0f;
    float DryRateScale = 1.0f;
    float GravityFlowStrengthScale = 1.0f;
    int32 LODIndex = 0;
    int32 DebugDispatchLogCount = 0;
    bool bLogGPUWetnessRuntimeBindings = false;
    bool bInitialized = false;
};
