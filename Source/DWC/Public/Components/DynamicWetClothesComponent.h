// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "WetInputSystem/WetContactTypes.h"
#include "RuntimeState/Utils/WetInputStage.h"
#include "RuntimeState/Utils/WetApplicationStage.h"
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
class FDWCLodCoordinator;
class FDWCLODVertexColorTransferCoordinator;
class FDWCTaskQueue;
class UDWCRuntimeDataSubsystem;
class UDWCStatsSubsystem;
struct FWetMeshReceiverInitializerContext;
struct FDWCSkinningTaskResult;
struct FDWCSkinningStaticData;
struct FDWCLODVertexColorTransferResult;
struct FDWCLODVertexStaticData;

struct FDWCWetMeshReceiverRuntime
{
    // Receiver identity and source UObject references.
    FName ReceiverId = NAME_None;
    TWeakObjectPtr<USkeletalMeshComponent> MeshComponent;
    TWeakObjectPtr<UWetClothingAsset> WetClothingAsset;

    // Shared immutable runtime data.
    // The TSharedPtr itself is small; the pointed-to payload can be large and
    // must be counted once per unique payload, not once per receiver.
    TSharedPtr<const FWetClothingRuntimeData, ESPMode::ThreadSafe> SharedRuntimeData;
    TSharedPtr<const FDWCSkinningStaticData, ESPMode::ThreadSafe> SkinningStaticData;

    // Per-receiver simulation and rendering state.
    // These are the main CPU-memory consumers.
    TUniquePtr<FAbsorbedWetnessSimulationState> SimulationState;
    TUniquePtr<FWetClothingMeshSampler> MeshSampler;
    TUniquePtr<FWetRenderStage> RenderStage;


    // Per-receiver GPU simulation backend and GPU-only surface stamp RNG.
    TUniquePtr<IDWCGPUBackend> GPUBackend;
    FRandomStream GPUSurfaceWaterRandomStream = FRandomStream(0x445743);

    // LOD vertex-color transfer data.
    // Static LOD data and transfer maps can be shared by multiple receivers,
    // so global memory accounting must deduplicate the pointed-to objects.
    TMap<int32, TSharedPtr<const FDWCLODVertexStaticData, ESPMode::ThreadSafe>> LODVertexStaticDataByLOD;
    TMap<int32, TSharedPtr<const TArray<int32>, ESPMode::ThreadSafe>> LODVertexColorTransferMapsByLOD;

    // Per-receiver LOD vertex-color cache.
    // The shared pointer provides lifetime and task hand-off safety; the color
    // array may scale with the target LOD vertex count.
    TMap<int32, TSharedPtr<const TArray<FColor>, ESPMode::ThreadSafe>> LODVertexColorCachesByLOD;

    // Per-receiver transient LOD work state.
    TArray<int32> PendingLODVertexColorDirtySourceVertices;
    int32 LODVertexColorTransferGeneration = 0;
    FDWCQualityLODRuntimeState QualityLODState;

    // Per-receiver render invalidation state.
    bool bWetRenderDirty = false;

    // Per-receiver asynchronous task state.
    bool bCpuSkinningTaskPending = false;
    bool bLODVertexColorTransferPending = false;
    bool bLODVertexColorTransferRequestedAgain = false;
};

UCLASS(ClassGroup = (Wetness), DisplayName = "Dynamic Wet Clothes", meta = (BlueprintSpawnableComponent))
class DWC_API UDynamicWetClothesComponent : public UActorComponent
{
    GENERATED_BODY()

  public:
    // Component lifetime.
    UDynamicWetClothesComponent();
    virtual ~UDynamicWetClothesComponent() override;

    // Wetness input API.
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
    UFUNCTION(BlueprintPure, Category = "Wetness|GPU")
    int32 GetDWCReceiverGPUId(FName ReceiverId = NAME_None) const;
    void GetDWCReceiverGPUIds(TArray<int32>& OutReceiverGPUIds) const;

    // Debug and quality LOD API.
    UFUNCTION(BlueprintCallable, Category = "Wetness|Debug")
    void SetWetPartDebugColorsEnabled(bool bEnabled);
    UFUNCTION(BlueprintCallable, Category = "Wetness|Debug")
    void SetSurfaceWaterDebugColorsEnabled(bool bEnabled);
    UFUNCTION(BlueprintCallable, Category = "Wetness|LOD")
    void SetDWCQualityLOD(int32 InQualityLOD);
    UFUNCTION(BlueprintCallable, Category = "Wetness|LOD")
    bool SetReceiverDWCQualityLOD(FName ReceiverId, int32 InQualityLOD);
    UFUNCTION(BlueprintPure, Category = "Wetness|LOD")
    int32 GetDWCQualityLOD() const
    {
        return CurrentQualityLOD;
    }

    // Runtime state and asynchronous task callbacks.
    int32 GetWetSurfaceSampleResolution() const;
    void CommitCpuSkinningTaskResult(FDWCSkinningTaskResult&& Result);
    void CommitLODVertexColorTransferResult(FDWCLODVertexColorTransferResult&& Result);

    EDWCSimulationMode GetActiveSimulationMode() const
    {
        return bSimulationModeLocked ? ActiveSimulationMode : SimulationMode;
    }

    void GetResolvedWetMeshComponents(TArray<USkeletalMeshComponent*>& OutComponents) const;

    // Current render LOD query API.
    UFUNCTION(BlueprintPure, Category = "Wetness|LOD")
    int32 GetCurrentRenderLODLevel() const;

    UFUNCTION(BlueprintPure, Category = "Wetness|LOD")
    float GetMergedReceiverScreenSize() const;

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

    // Runtime initialization and receiver management.
    bool                     InitializeWetRuntime();
    void                     StartWetnessTimers();

    // Stage argument construction.
    FWetMeshReceiverInitializerContext MakeWetMeshReceiverInitializerContext();
    FWetApplicationStageContext MakeWetApplicationStageContext();
    FWetRuntimeDataBuildArgs MakeRuntimeDataBuildArgs(FDWCWetMeshReceiverRuntime& Receiver);
    FWetSimulationStageArgs  MakeWetSimulationStageArgs(FDWCWetMeshReceiverRuntime& Receiver);
    FWetRenderStageArgs      MakeWetRenderStageArgs(FDWCWetMeshReceiverRuntime& Receiver);

    // Per-frame simulation and rendering updates.
    void                     UpdateWetness();
    void                     UpdateWetRendering();
    bool                     FlushPendingWetContacts();
    bool                     ShouldUpdateCPUWetnessRendering(FDWCWetMeshReceiverRuntime& Receiver) const;
    bool                     ShouldEnableCPUWetnessRendering(const FDWCWetMeshReceiverRuntime& Receiver) const;

    // Wetness input render invalidation.
    void                     RequestWetRenderingUpdate();
    void                     RequestWetRenderingUpdate(FDWCWetMeshReceiverRuntime& Receiver);

    // GPU and material rendering state.
    bool                     InitializeGPUBackend(FDWCWetMeshReceiverRuntime& Receiver);
    void                     ApplyGeneratedWetMaterialOverrides();
    void                     ApplyQualityLODMaterialParameters(FDWCWetMeshReceiverRuntime& Receiver);
    void                     MarkCPUWetnessRenderingDirty(FDWCWetMeshReceiverRuntime& Receiver);

    // Quality LOD and render LOD.
    void                     RefreshResolvedQualityLODPolicies();
    void                     UpdateRenderLOD();

    // Asynchronous skinning and LOD transfer tasks.
    bool                     RequestCpuSkinningTask(FDWCWetMeshReceiverRuntime& Receiver, bool bComputePositions, bool bComputeNormals);
    void                     RequestContinuousCpuSkinningTasks();
    bool                     HasPendingCpuSkinningTasks() const;
    bool                     RequestLODVertexColorTransferTask(FDWCWetMeshReceiverRuntime& Receiver);
    bool                     HasPendingLODVertexColorTransferTasks() const;
    void                     FlushAsyncTaskQueueGameThread();

  public:
    // Input assets and simulation mode.
    /** WCA assets handled by this component. Every matching SkeletalMeshComponent becomes a receiver. */
    UPROPERTY(EditAnywhere, Category = "Wetness")
    TArray<TObjectPtr<UWetClothingAsset>> WetClothingAssets;

    UPROPERTY(EditAnywhere, Category = "Wetness", meta = (ShowOnlyInnerProperties))
    FWetClothingSettings WetnessSettings;

    /** Selected per component instance and locked when BeginPlay starts. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Wetness|Simulation")
    EDWCSimulationMode SimulationMode = EDWCSimulationMode::VertexCPU;

    // GPU simulation configuration.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Simulation|GPU", meta = (EditCondition = "SimulationMode == EDWCSimulationMode::WetnessMapGPU", ClampMin = "3", ClampMax = "64", AdvancedDisplay))
    int32 GPUContactNearestSeedVertexCount = 12;

    /** Number of neighboring texels used by GPU wetness diffusion. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Simulation|GPU", meta = (EditCondition = "SimulationMode == EDWCSimulationMode::WetnessMapGPU"))
    EDWCGPUDiffusionNeighborMode GPUDiffusionNeighborMode = EDWCGPUDiffusionNeighborMode::FourDirections;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Simulation|GPU|Tuning", meta = (EditCondition = "SimulationMode == EDWCSimulationMode::WetnessMapGPU", ClampMin = "0.0", AdvancedDisplay))
    float GPUSpreadRateScale = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Simulation|GPU|Tuning", meta = (EditCondition = "SimulationMode == EDWCSimulationMode::WetnessMapGPU", ClampMin = "0.0", AdvancedDisplay))
    float GPUGravityFlowStrengthScale = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Simulation|GPU|Tuning", meta = (EditCondition = "SimulationMode == EDWCSimulationMode::WetnessMapGPU", ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0", AdvancedDisplay))
    float GPUImmediateAbsorptionFraction = 0.35f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Simulation|GPU|Tuning", meta = (EditCondition = "SimulationMode == EDWCSimulationMode::WetnessMapGPU", ClampMin = "0.0", AdvancedDisplay))
    float GPUDryRateScale = 1.0f;

    // Contact and surface-water configuration.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Contact", meta = (AllowPrivateAccess = "true"))
    bool bBatchWetContactsPerFrame = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Contact", meta = (ClampMin = "1", AllowPrivateAccess = "true", AdvancedDisplay))
    int32 MaxBatchedWetContactsPerFrame = 64;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Surface", meta = (ClampMin = "2", ClampMax = "64", AllowPrivateAccess = "true", AdvancedDisplay))
    int32 WetSurfaceSampleResolution = 8;

    // Component-level quality LOD policy.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|LOD")
    bool bEnableDWCQualityLOD = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|LOD", meta = (EditCondition = "bEnableDWCQualityLOD"))
    TObjectPtr<UDWCQualityLODProfile> QualityLODProfile = nullptr;

    /** Component-wide quality LOD thresholds. The array index is the LOD level. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|LOD", meta = (TitleProperty = "LODLevel"))
    TArray<FDWCQualityLODScreenSizeThreshold> QualityLODScreenSizeThresholds;

    /** How often the merged receiver bounds are evaluated for component rendering LOD selection. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|LOD", meta = (ClampMin = "0.01", AdvancedDisplay))
    float RenderLODEvaluationInterval = 0.1f;

    // Visual appearance and optional rendering features.
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

    /** Displays GPU surface-water droplets as a debug color over the wet part debug overlay. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Debug", meta = (EditCondition = "SimulationMode == EDWCSimulationMode::WetnessMapGPU"))
    bool bShowSurfaceWaterDebugColors = false;

  private:
    // Runtime mode state.
    UPROPERTY(Transient)
    EDWCSimulationMode ActiveSimulationMode = EDWCSimulationMode::VertexCPU;

    UPROPERTY(Transient)
    bool bSimulationModeLocked = false;

    // Receiver collection and shared coordinators.
    TArray<TUniquePtr<FDWCWetMeshReceiverRuntime>> Receivers;
    TUniquePtr<FDWCTaskQueue> AsyncTaskQueue;
    TUniquePtr<FDWCLodCoordinator> LODCoordinator;
    TUniquePtr<FDWCLODVertexColorTransferCoordinator> LODVertexColorTransferCoordinator;

    // Timers and pending frame work.
    FTimerHandle           WetnessSimulationTimer;
    FTimerHandle           WetnessRenderTimer;
    FTimerHandle           RenderLODEvaluationTimer;
    TArray<FDWCWetContact> PendingWetContacts;
    bool                   bPendingWetContactsApplyMaterial = false;
    bool                   bWetRenderDirty = false;

    // Debug-only runtime values exposed to the editor.
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Wetness|LOD", meta = (DisplayName = "Current Quality LOD", AllowPrivateAccess = "true"))
    int32 CurrentQualityLOD = 0;

    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category = "Wetness|LOD", meta = (DisplayName = "Current Screen Size", AllowPrivateAccess = "true"))
    float CurrentRenderLODScreenSize = 0.0f;
};
