// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "WetInputSystem/WetContactTypes.h"
#include "WetInputSystem/WetInputStage.h"
#include "WetInputSystem/Sampling/WetClothingMeshSampler.h"
#include "RuntimeState/WetRuntimeDataBuilder.h"
#include "RuntimeState/WetClothingRuntimeData.h"
#include "Core/WetClothingSettings.h"
#include "Core/DWCSimulationMode.h"
#include "GPU/DWCGPUBackend.h"
#include "Async/DWCTaskQueue.h"
#include "DataAssets/WetClothingAsset.h"
#include "WetInputSystem/WetSurfaceContactResolver.h"
#include "WetRendering/WetRenderStage.h"
#include "WetSimulation/WetSimulationStage.h"
#include "WetSimulation/AbsorbedWetness/AbsorbedWetnessSimulationState.h"
#include "WetSimulation/SurfaceWater/SurfaceWaterSimulationState.h"
#include "Templates/UniquePtr.h"

#include "DynamicWetClothesComponent.generated.h"

UENUM(BlueprintType)
enum class EDWCGPUDiffusionNeighborMode : uint8
{
    FourDirections UMETA(DisplayName = "4 Directions"),
    EightDirections UMETA(DisplayName = "8 Directions")
};

class USkeletalMeshComponent;
class USkeletalMesh;
class UMaterialInstanceDynamic;
class FDWCTaskQueue;
struct FDWCSkinningTaskResult;
struct FDWCSkinningStaticData;

struct FDWCWetMeshReceiverRuntime
{
    FName ReceiverId = NAME_None;
    TWeakObjectPtr<USkeletalMeshComponent> MeshComponent;
    TWeakObjectPtr<UWetClothingAsset> WetClothingAsset;

    TSharedPtr<const FWetClothingRuntimeData, ESPMode::ThreadSafe> SharedRuntimeData;
    TUniquePtr<FWetRuntimeDataBuilder> RuntimeDataBuilder;
    TUniquePtr<FAbsorbedWetnessSimulationState> SimulationState;
    TMap<int32, TUniquePtr<FSurfaceWaterSimulationState>> SurfaceWaterStatesByMaterialSlot;
    TMap<int32, FSurfaceWaterProfileParameters> SurfaceWaterProfilesByMaterialSlot;
    TUniquePtr<FWetSimulationStage> SimulationStage;
    TUniquePtr<FWetInputStage> InputStage;
    TUniquePtr<FWetSurfaceContactResolver> SurfaceContactResolver;
    TUniquePtr<IDWCGPUBackend> GPUBackend;
    TUniquePtr<FWetClothingMeshSampler> MeshSampler;
    TUniquePtr<FWetRenderStage> RenderStage;
    TArray<TObjectPtr<UMaterialInstanceDynamic>> WetMaterialInstances;
    TSharedPtr<const FDWCSkinningStaticData, ESPMode::ThreadSafe> SkinningStaticData;

    bool bWetRenderDirty = false;
    bool bCpuSkinningTaskPending = false;
    bool bCpuSkinningTaskRequestedAgain = false;
    bool bCpuSkinningTaskNeedsNormals = false;
};

UCLASS(ClassGroup = (Wetness), DisplayName = "Dynamic Wet Clothes", meta = (BlueprintSpawnableComponent))
class DWC_API UDynamicWetClothesComponent : public UActorComponent
{
    GENERATED_BODY()

  public:
    // Sets default values for this component's properties
    UDynamicWetClothesComponent();
    virtual ~UDynamicWetClothesComponent() override;

    // Apply Wetness
    void ApplyWetAll(float Amount);
    UFUNCTION(BlueprintCallable, Category = "Wetness")
    bool ApplyWetContact(const FDWCWetContact& Contact, bool bApplyMaterial = true);
    UFUNCTION(BlueprintCallable, Category = "Wetness")
    bool ApplyWetContacts(const TArray<FDWCWetContact>& Contacts, bool bApplyMaterial = true);
    UFUNCTION(BlueprintCallable, Category = "Wetness")
    bool ApplyWetArea(const FDWCWetAreaData& AreaData, bool bApplyMaterial = true);
    UFUNCTION(BlueprintCallable, Category = "Wetness")
    bool ApplyWetSurface(const FDWCWaterSurfaceData& WaterSurfaceData, float Amount, bool bApplyMaterial = true);
    bool GetWetnessWorldBounds(FBox& OutBounds) const;

    UFUNCTION(BlueprintCallable, Category = "DWC|Debug")
    void SetWetPartDebugColorsEnabled(bool bEnabled);
    int32 GetWetSurfaceSampleResolution() const;
    void CommitCpuSkinningTaskResult(FDWCSkinningTaskResult&& Result);

    EDWCSimulationMode GetActiveSimulationMode() const
    {
        return bSimulationModeLocked ? ActiveSimulationMode : SimulationMode;
    }

    void GetResolvedWetMeshComponents(TArray<USkeletalMeshComponent*>& OutComponents) const;

    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

  protected:
    // Called when the game starts
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

  private:
    FWetRuntimeDataBuildArgs MakeRuntimeDataBuildArgs(FDWCWetMeshReceiverRuntime& Receiver);
    FWetInputStageArgs       MakeWetInputStageArgs(FDWCWetMeshReceiverRuntime& Receiver);
    FWetSurfaceContactResolverArgs MakeWetSurfaceContactResolverArgs(FDWCWetMeshReceiverRuntime& Receiver);
    FWetSimulationStageArgs  MakeWetSimulationStageArgs(FDWCWetMeshReceiverRuntime& Receiver);
    FWetRenderStageArgs      MakeWetRenderStageArgs(FDWCWetMeshReceiverRuntime& Receiver);
    bool                     InitializeWetRuntime();
    bool                     RebuildWetMeshReceivers();
    bool                     InitializeWetMeshReceiverRuntime(FDWCWetMeshReceiverRuntime& Receiver);
    bool                     InitializeGPUBackend(FDWCWetMeshReceiverRuntime& Receiver);
    void                     StartWetnessTimers();
    void                     UpdateWetness();
    void                     UpdateSurfaceWater();
    void                     UpdateWetRendering();
    void                     RequestWetRenderingUpdate();
    void                     RequestWetRenderingUpdate(FDWCWetMeshReceiverRuntime& Receiver);
    bool                     RequestCpuSkinningTask(FDWCWetMeshReceiverRuntime& Receiver, bool bComputePositions, bool bComputeNormals);
    void                     RequestContinuousCpuSkinningTasks();
    bool                     HasPendingCpuSkinningTasks() const;
    void                     FlushAsyncTaskQueueGameThread();
    bool                     FlushPendingWetContacts();

    void                    ApplyGeneratedWetMaterialOverrides();
    bool                    ShouldReceiverConsiderContact(const FDWCWetMeshReceiverRuntime& Receiver, const FDWCWetContact& Contact) const;
    bool                    ShouldReceiverConsiderSurface(const FDWCWetMeshReceiverRuntime& Receiver, const FDWCWaterSurfaceData& WaterSurfaceData) const;

  public:
    /** WCA assets handled by this component. Every matching SkeletalMeshComponent becomes a receiver. */
    UPROPERTY(EditAnywhere, Category = "Wetness")
    TArray<TObjectPtr<UWetClothingAsset>> WetClothingAssets;

    /** Selected per component instance and locked when BeginPlay starts. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wetness|Simulation")
    EDWCSimulationMode SimulationMode = EDWCSimulationMode::VertexCPU;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Simulation|GPU", meta = (EditCondition = "SimulationMode == EDWCSimulationMode::WetnessMapGPU", ClampMin = "3", ClampMax = "64", AdvancedDisplay))
    int32 GPUContactNearestSeedVertexCount = 12;

    /** Number of neighboring texels used by GPU wetness diffusion. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Simulation|GPU", meta = (EditCondition = "SimulationMode == EDWCSimulationMode::WetnessMapGPU"))
    EDWCGPUDiffusionNeighborMode GPUDiffusionNeighborMode = EDWCGPUDiffusionNeighborMode::FourDirections;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Simulation|GPU|Tuning", meta = (EditCondition = "SimulationMode == EDWCSimulationMode::WetnessMapGPU", ClampMin = "0.0", AdvancedDisplay))
    float GPUSpreadRateScale = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Simulation|GPU|Tuning", meta = (EditCondition = "SimulationMode == EDWCSimulationMode::WetnessMapGPU", ClampMin = "0.0", AdvancedDisplay))
    float GPUGravityFlowStrengthScale = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Simulation|GPU|Tuning", meta = (EditCondition = "SimulationMode == EDWCSimulationMode::WetnessMapGPU", ClampMin = "0.0", AdvancedDisplay))
    float GPUDryRateScale = 1.0f;

    UPROPERTY(EditAnywhere, Category = "Wetness", meta = (ShowOnlyInnerProperties))
    FWetClothingSettings WetnessSettings;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Contact", meta = (AllowPrivateAccess = "true"))
    bool bBatchWetContactsPerFrame = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Contact", meta = (ClampMin = "1", AllowPrivateAccess = "true", AdvancedDisplay))
    int32 MaxBatchedWetContactsPerFrame = 64;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Surface", meta = (ClampMin = "2", ClampMax = "64", AllowPrivateAccess = "true", AdvancedDisplay))
    int32 WetSurfaceSampleResolution = 8;

    UPROPERTY(EditAnywhere, Category = "Wetness|Visual")
    FLinearColor FallbackUnderColor = FLinearColor(0.8f, 0.55f, 0.42f, 1.0f);

    UPROPERTY(EditAnywhere, Category = "Wetness|Visual", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float WetUnderColorBlendStrength = 0.3f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Wrinkle", meta = (ClampMin = "0.0"))
    float WrinkleStrength = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Wrinkle", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float WrinkleWetnessMin = 0.25f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Wrinkle", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float WrinkleWetnessMax = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Transparency", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float TransparencyWetnessMin = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Transparency", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float TransparencyWetnessMax = 1.0f;

    /**
     * Keeps the normal material while dry and displays each Wet Part's configured color while wet.
     * CPU wetness uses VertexColor.R; GPU wetness stays in the existing wetness texture path.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Debug")
    bool bShowWetPartDebugColors = false;

  private:
    UPROPERTY(Transient)
    EDWCSimulationMode ActiveSimulationMode = EDWCSimulationMode::VertexCPU;

    UPROPERTY(Transient)
    bool bSimulationModeLocked = false;

    TUniquePtr<FDWCTaskQueue> AsyncTaskQueue;
    TArray<TUniquePtr<FDWCWetMeshReceiverRuntime>> Receivers;

    FTimerHandle           WetnessSimulationTimer;
    FTimerHandle           SurfaceWaterSimulationTimer;
    FTimerHandle           WetnessRenderTimer;
    TArray<FDWCWetContact> PendingWetContacts;
    bool                   bPendingWetContactsApplyMaterial = false;
    bool                   bWetRenderDirty = false;
};
