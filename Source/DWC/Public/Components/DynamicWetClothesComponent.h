// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "WetInputSystem/WetContactTypes.h"
#include "RuntimeState/Utils/WetInputStage.h"
#include "WetInputSystem/Sampling/WetClothingMeshSampler.h"
#include "RuntimeState/Utils/WetRuntimeDataBuilder.h"
#include "RuntimeState/WetClothingRuntimeData.h"
#include "Core/DWCQualityLODProfile.h"
#include "Core/WetClothingSettings.h"
#include "Core/DWCSimulationMode.h"
#include "GPU/DWCGPUBackend.h"
#include "Async/DWCTaskQueue.h"
#include "DataAssets/WetClothingAsset.h"
#include "RuntimeState/Utils/WetSurfaceContactResolver.h"
#include "WetRendering/WetRenderStage.h"
#include "RuntimeState/Utils/WetSimulationStage.h"
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
class FDWCQualityLODController;
class FDWCQualityLODEvaluator;
class FDWCTaskQueue;
class UDWCRuntimeDataSubsystem;
class UDWCStatsSubsystem;
struct FDWCSkinningTaskResult;
struct FDWCSkinningStaticData;
struct FDWCLODVertexColorTransferResult;
struct FDWCLODVertexStaticData;

struct FDWCWetMeshReceiverRuntime
{
    FName ReceiverId = NAME_None;
    TWeakObjectPtr<USkeletalMeshComponent> MeshComponent;
    TWeakObjectPtr<UWetClothingAsset> WetClothingAsset;

    // Shared CPU data. The TSharedPtr itself is small; the pointed-to payload
    // can be large and must be counted once per unique payload, not once per
    // receiver.
    TSharedPtr<const FWetClothingRuntimeData, ESPMode::ThreadSafe> SharedRuntimeData;
    TSharedPtr<const FDWCSkinningStaticData, ESPMode::ThreadSafe> SkinningStaticData;

    // Per-receiver CPU simulation/render data. These are the main CPU-memory Consumers
    TUniquePtr<FAbsorbedWetnessSimulationState> SimulationState;
    TUniquePtr<FWetClothingMeshSampler> MeshSampler;
    TUniquePtr<FWetRenderStage> RenderStage; //Owns VertexColor

    // Per-receiver CPU/GPU.
    // CPU : owns pending stamp Container
    // GPU : RenderTarget : VRAM Resource
    TMap<int32, TUniquePtr<FSurfaceWaterSimulationState>> SurfaceWaterStatesByMaterialSlot;

    // Per-receiver CPU/GPU simulation backend. 
    TUniquePtr<IDWCGPUBackend> GPUBackend;

    // Small per-slot settings/handles. Count material bindings if needed, but
    // do not treat the UObject memory behind the material pointers as owned
    // receiver memory.
    TMap<int32, FSurfaceWaterProfileParameters> SurfaceWaterProfilesByMaterialSlot;

    // Per-receiver randomness. Keep this state local to the receiver so that
    // adding/reordering other receivers does not change this receiver's
    // deterministic surface-water sequence.
    FRandomStream SurfaceWaterRandomStream = FRandomStream(0x445743);

    // Shared/cache-backed CPU data for LOD vertex-color transfer. Static LOD
    // data and transfer maps can be shared by multiple receivers, so dedupe
    // them by pointed-to object when aggregating globally.
    TMap<int32, TSharedPtr<const FDWCLODVertexStaticData, ESPMode::ThreadSafe>> LODVertexStaticDataByLOD;// LOD Data (Position...)
    TMap<int32, TSharedPtr<const TArray<int32>, ESPMode::ThreadSafe>> LODVertexColorTransferMapsByLOD; // What low LOD Vertex will match in high LOD 

    // Per-receiver LOD color cache. The shared pointer provides lifetime and
    // task hand-off safety; the color array itself is generally receiver-owned
    // and may scale with the target LOD vertex count.
    TMap<int32, TSharedPtr<const TArray<FColor>, ESPMode::ThreadSafe>> LODVertexColorCachesByLOD; // LOD n Vertex 1 -> Color R/G/B 

    // Per-receiver transient CPU work data for LOD vertex-color updates.
    // Usually small, but capacity can grow with the dirty source-vertex count.
    TArray<int32> PendingLODVertexColorDirtySourceVertices;
    int32 LODVertexColorTransferGeneration = 0;
    FDWCQualityLODRuntimeState QualityLODState;

    bool bWetRenderDirty = false;
    bool bCpuSkinningTaskPending = false;
    bool bLODVertexColorTransferPending = false;
    bool bLODVertexColorTransferRequestedAgain = false;
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

    UFUNCTION(BlueprintCallable, Category = "Wetness|Debug")
    void SetWetPartDebugColorsEnabled(bool bEnabled);
    UFUNCTION(BlueprintCallable, Category = "Wetness|LOD")
    void SetDWCQualityLOD(int32 InQualityLOD);
    UFUNCTION(BlueprintCallable, Category = "Wetness|LOD")
    bool SetReceiverDWCQualityLOD(FName ReceiverId, int32 InQualityLOD);
    UFUNCTION(BlueprintPure, Category = "Wetness|LOD")
    int32 GetDWCQualityLOD() const
    {
        return CurrentQualityLOD;
    }
    int32 GetWetSurfaceSampleResolution() const;
    void CommitCpuSkinningTaskResult(FDWCSkinningTaskResult&& Result);
    void CommitLODVertexColorTransferResult(FDWCLODVertexColorTransferResult&& Result);

    EDWCSimulationMode GetActiveSimulationMode() const
    {
        return bSimulationModeLocked ? ActiveSimulationMode : SimulationMode;
    }

    void GetResolvedWetMeshComponents(TArray<USkeletalMeshComponent*>& OutComponents) const;

    UFUNCTION(BlueprintPure, Category = "Wetness|LOD")
    int32 GetCurrentRenderLODLevel() const { return RenderLODState.ActiveLODLevel; }

    UFUNCTION(BlueprintPure, Category = "Wetness|LOD")
    float GetMergedReceiverScreenSize() const { return RenderLODState.ScreenSize; }

    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

  protected:
    // Called when the game starts
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

  private:
    friend class UDWCStatsSubsystem;

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
    void                     UpdateRenderLOD();
    void                     UpdateWetness();
    void                     UpdateSurfaceWater();
    void                     UpdateWetRendering();
    void                     RequestWetRenderingUpdate();
    void                     RequestWetRenderingUpdate(FDWCWetMeshReceiverRuntime& Receiver);
    void                     ApplyQualityLODMaterialParameters(FDWCWetMeshReceiverRuntime& Receiver);
    void                     ConfigureQualityLODController();
    void                     SetReceiverQualityLOD(FDWCWetMeshReceiverRuntime& Receiver, int32 InQualityLOD);
    void                     RefreshResolvedQualityLODPolicies();
    bool                     ShouldUpdateGPUWetness(FDWCWetMeshReceiverRuntime& Receiver) const;
    bool                     ShouldUpdateCPUWetness(FDWCWetMeshReceiverRuntime& Receiver) const;
    bool                     ShouldUpdateSurfaceWater(FDWCWetMeshReceiverRuntime& Receiver) const;
    bool                     ShouldUpdateWetRendering(FDWCWetMeshReceiverRuntime& Receiver) const;
    bool                     InitializeLODVertexColorTransfer(FDWCWetMeshReceiverRuntime& Receiver, UDWCRuntimeDataSubsystem& RuntimeDataSubsystem, int32 RuntimeLODIndex);
    bool                     RequestCpuSkinningTask(FDWCWetMeshReceiverRuntime& Receiver, bool bComputePositions, bool bComputeNormals);
    void                     RequestContinuousCpuSkinningTasks();
    bool                     HasPendingCpuSkinningTasks() const;
    bool                     RequestLODVertexColorTransferTask(FDWCWetMeshReceiverRuntime& Receiver);
    bool                     HasPendingLODVertexColorTransferTasks() const;
    void                     FlushAsyncTaskQueueGameThread();
    bool                     FlushPendingWetContacts();
    bool                     HasAnyRenderLODSettings() const;
    bool                     CalculateRenderLODScreenSize(float& OutScreenSize, FBoxSphereBounds& OutBounds) const;
    bool                     FindRenderLODLevel(float ScreenSize, int32& OutLODLevel) const;
    void                     ResetRenderLODState();

    void                    ApplyGeneratedWetMaterialOverrides();
    bool                    ShouldReceiverConsiderContact(const FDWCWetMeshReceiverRuntime& Receiver, const FDWCWetContact& Contact) const;
    bool                    ShouldReceiverConsiderSurface(const FDWCWetMeshReceiverRuntime& Receiver, const FDWCWaterSurfaceData& WaterSurfaceData) const;

  public:  //Detail Fields
    /** WCA assets handled by this component. Every matching SkeletalMeshComponent becomes a receiver. */
    UPROPERTY(EditAnywhere, Category = "Wetness")
    TArray<TObjectPtr<UWetClothingAsset>> WetClothingAssets;

    /** Selected per component instance and locked when BeginPlay starts. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wetness|Simulation")
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

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|LOD")
    bool bEnableDWCQualityLOD = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|LOD", meta = (EditCondition = "bEnableDWCQualityLOD"))
    TObjectPtr<UDWCQualityLODProfile> QualityLODProfile = nullptr;

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

    /** Component-wide quality LOD thresholds. The array index is the LOD level. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|LOD", meta = (TitleProperty = "LODLevel"))
    TArray<FDWCQualityLODScreenSizeThreshold> QualityLODScreenSizeThresholds;

    /** How often the merged receiver bounds are evaluated for component rendering LOD selection. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|LOD", meta = (ClampMin = "0.01", AdvancedDisplay))
    float RenderLODEvaluationInterval = 0.1f;

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

//Runtime    
  private:
    UPROPERTY(Transient)
    EDWCSimulationMode ActiveSimulationMode = EDWCSimulationMode::VertexCPU;

    UPROPERTY(Transient)
    bool bSimulationModeLocked = false;

    //Receivers
    TArray<TUniquePtr<FDWCWetMeshReceiverRuntime>> Receivers;

    TUniquePtr<FDWCTaskQueue> AsyncTaskQueue;
    TUniquePtr<FDWCQualityLODController> QualityLODController;
    TUniquePtr<FDWCQualityLODEvaluator> QualityLODEvaluator;
    FDWCQualityLODScreenSizeRuntimeState RenderLODState;

    FTimerHandle           WetnessSimulationTimer;
    FTimerHandle           SurfaceWaterSimulationTimer;
    FTimerHandle           WetnessRenderTimer;
    FTimerHandle           RenderLODEvaluationTimer;
    float                  SurfaceWaterTimerInterval = 1.0f / 30.0f;
    TArray<FDWCWetContact> PendingWetContacts;
    bool                   bPendingWetContactsApplyMaterial = false;
    bool                   bWetRenderDirty = false;
    
private: //For Debug
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Wetness|LOD", meta = (DisplayName = "Current Quality LOD", AllowPrivateAccess = "true"))
    int32 CurrentQualityLOD = 0;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Wetness|LOD", meta = (DisplayName = "Current Screen Size", AllowPrivateAccess = "true"))
    float CurrentRenderLODScreenSize = 0.0f;
};
