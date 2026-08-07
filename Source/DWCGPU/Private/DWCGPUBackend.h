//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GPU/DWCGPUBackend.h"
#include "UObject/StrongObjectPtr.h"

class UMaterialInstanceDynamic;
class UTextureRenderTarget2D;
struct FDWCGPUStaticSlotResources;

class FDWCGPUBackend final : public IDWCGPUBackend
{
public:
    virtual bool Initialize(const FDWCGPUBackendInitArgs& Args) override;
    virtual bool EnqueueResolvedContacts(const TArray<FDWCResolvedSurfaceContact>& Contacts) override;
    virtual bool EnqueueSurfaceStamps(const TArray<FDWCSurfaceStampRequest>& Stamps) override;
    virtual bool ApplyWetAll(float Amount) override;
    virtual void ClearPendingWetnessMaps() override;
    virtual void Update(float DeltaSeconds) override;
    virtual FDWCGPUBackendStats GetStats() const override;
    virtual void Shutdown() override;

private:
    struct FStaticSimulationData;
    struct FRenderState;

    struct FMaterialSlotRuntime
    {
        int32 MaterialSlotIndex = INDEX_NONE;
        int32 StaticSlotIndex = INDEX_NONE;
        int32 Resolution = 0;
        int32 SurfaceWaterResolution = 0;
        bool bUsesSurfaceWater = false;
        TArray<TStrongObjectPtr<UTextureRenderTarget2D>> WetnessMaps;
        TArray<TStrongObjectPtr<UTextureRenderTarget2D>> PendingWetnessMaps;
        TStrongObjectPtr<UTextureRenderTarget2D> SurfaceDroplet1RT;
        TStrongObjectPtr<UTextureRenderTarget2D> SurfaceDroplet2RT;
        int32 CurrentTextureIndex = 0;
        int32 CurrentPendingTextureIndex = 0;
        TWeakObjectPtr<UMaterialInstanceDynamic> MaterialInstance;

        UTextureRenderTarget2D* GetCurrentMap() const;
        UTextureRenderTarget2D* GetNextMap() const;
        UTextureRenderTarget2D* GetCurrentPendingMap() const;
        UTextureRenderTarget2D* GetNextPendingMap() const;
        void SwapMaps();
        void SwapPendingMaps();
    };

    bool BuildStaticSimulationData();
    bool AcquireSharedStaticResources();
    bool BuildDebugVertexLookup();
    bool CreateSlotResources();
    bool BindMaterialSlot(FMaterialSlotRuntime& Slot);
    void DispatchSimulation(
        TArray<FDWCResolvedSurfaceContact>&& Contacts,
        TArray<FDWCSurfaceStampRequest>&& SurfaceStamps,
        float WetAllAmount,
        float DeltaSeconds);

    TWeakObjectPtr<UDynamicWetClothesComponent> OwnerComponent;
    TWeakObjectPtr<USkeletalMeshComponent> TargetSkeletalMesh;
    TWeakObjectPtr<UWetClothingAsset> WetClothingAsset;
    TArray<TObjectPtr<UMaterialInstanceDynamic>>* WetMaterialInstances = nullptr;
    FName WetnessMapParameterName;

    TSharedPtr<const FStaticSimulationData, ESPMode::ThreadSafe> StaticSimulationData;
    TSharedPtr<FRenderState, ESPMode::ThreadSafe> RenderState;
    TArray<TSharedPtr<FDWCGPUStaticSlotResources, ESPMode::ThreadSafe>> SharedStaticSlotResources;
    TArray<FMaterialSlotRuntime> MaterialSlots;
    TArray<FDWCResolvedSurfaceContact> PendingContacts;
    TArray<FDWCSurfaceStampRequest> PendingSurfaceStamps;
    TArray<FVector2f> DebugVertexDataUVs;
    TArray<int32> DebugVertexMaterialSlots;
    float PendingWetAllAmount = 0.0f;
    float MaxWetness = 1.0f;
    float SpreadRateScale = 1.0f;
    float DryRateScale = 1.0f;
    float GravityFlowStrengthScale = 1.0f;
    float CapillaryImmediateAbsorptionFraction = 0.65f;
    int32 ReceiverGPUId = 0;
    int32 LODIndex = 0;
    int32 DebugDispatchLogCount = 0;
    bool bUseEightDirectionDiffusion = true;
    bool bInitialized = false;
};
