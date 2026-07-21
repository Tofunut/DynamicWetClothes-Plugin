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

    // Per-receiver CPU simulation/render data. These are the main CPU-memory
    // consumers and generally scale with the active vertex count.
    TUniquePtr<FAbsorbedWetnessSimulationState> SimulationState;
    TUniquePtr<FWetClothingMeshSampler> MeshSampler;
    TUniquePtr<FWetRenderStage> RenderStage;

    // Per-receiver CPU/GPU surface-water state. The CPU side owns pending
    // stamps and container overhead; each valid state also owns two GPU
    // render targets, so GPU memory is estimated through the state API.
    TMap<int32, TUniquePtr<FSurfaceWaterSimulationState>> SurfaceWaterStatesByMaterialSlot;

    // Per-receiver GPU simulation backend. The interface object is small, but
    // its implementation may own GPU render targets and CPU staging data;
    // query it through a backend stats API instead of sizeof(pointer).
    TUniquePtr<IDWCGPUBackend> GPUBackend;

    // Small per-slot settings/handles. Count material bindings if needed, but
    // do not treat the UObject memory behind the material pointers as owned
    // receiver memory.
    TMap<int32, FSurfaceWaterProfileParameters> SurfaceWaterProfilesByMaterialSlot;
    TArray<TObjectPtr<UMaterialInstanceDynamic>> WetMaterialInstances;

    // Lightweight helper/state objects. Their pointees currently contain no
    // large persistent arrays, so they are normally negligible in memory
    // stats (apart from their own object size).
    TUniquePtr<FWetRuntimeDataBuilder> RuntimeDataBuilder;
    TUniquePtr<FWetSimulationStage> SimulationStage;
    TUniquePtr<FWetSurfaceContactResolver> SurfaceContactResolver;
    TUniquePtr<FWetInputStage> InputStage;

    // Shared/cache-backed CPU data for LOD vertex-color transfer. Static LOD
    // data and transfer maps can be shared by multiple receivers, so dedupe
    // them by pointed-to object when aggregating globally.
    TMap<int32, TSharedPtr<const FDWCLODVertexStaticData, ESPMode::ThreadSafe>> LODVertexStaticDataByLOD;
    TMap<int32, TSharedPtr<const TArray<int32>, ESPMode::ThreadSafe>> LODVertexColorTransferMapsByLOD;

    // Per-receiver LOD color cache. The shared pointer provides lifetime and
    // task hand-off safety; the color array itself is generally receiver-owned
    // and may scale with the target LOD vertex count.
    TMap<int32, TSharedPtr<const TArray<FColor>, ESPMode::ThreadSafe>> LODVertexColorCachesByLOD;

    // Per-receiver transient CPU work data for LOD vertex-color updates.
    // Usually small, but capacity can grow with the dirty source-vertex count.
    TArray<int32> PendingLODVertexColorDirtySourceVertices;
    int32 LODVertexColorTransferGeneration = 0;

    bool bWetRenderDirty = false;
    bool bCpuSkinningTaskPending = false;
    bool bCpuSkinningTaskRequestedAgain = false;
    bool bCpuSkinningTaskNeedsNormals = false;
    bool bLODVertexColorTransferPending = false;
    bool bLODVertexColorTransferRequestedAgain = false;
};

/** One component-wide LOD threshold for the merged DWC receiver bounds. */
USTRUCT(BlueprintType)
struct DWC_API FDWCRenderLODRatioLevel
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Rendering LOD", meta = (ClampMin = "0"))
    int32 LODLevel = 0;

    /** This LOD becomes active when the merged receiver bounds occupy at least this screen-size ratio. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Rendering LOD", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float MinScreenSize = 0.0f;
};

/** Component-wide rendering LOD state derived from the merged bounds of all receivers. */
struct FDWCRenderLODRuntimeState
{
    FBoxSphereBounds MergedBounds;
    float ScreenSize = 0.0f;
    bool bHasValidScreenSize = false;
    int32 ActiveLODLevel = INDEX_NONE;
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
    void CommitLODVertexColorTransferResult(FDWCLODVertexColorTransferResult&& Result);

    EDWCSimulationMode GetActiveSimulationMode() const
    {
        return bSimulationModeLocked ? ActiveSimulationMode : SimulationMode;
    }

    void GetResolvedWetMeshComponents(TArray<USkeletalMeshComponent*>& OutComponents) const;

    UFUNCTION(BlueprintPure, Category = "Wetness|Rendering LOD")
    int32 GetCurrentRenderLODLevel() const { return RenderLODState.ActiveLODLevel; }

    UFUNCTION(BlueprintPure, Category = "Wetness|Rendering LOD")
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

    /** Component-wide LOD number and merged-bounds screen-size ratio pairs. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Rendering LOD", meta = (TitleProperty = "LODLevel"))
    TArray<FDWCRenderLODRatioLevel> RenderLODRatioLevels;

    /** How often the merged receiver bounds are evaluated for component rendering LOD selection. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Rendering LOD", meta = (ClampMin = "0.01", AdvancedDisplay))
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

  private:
    UPROPERTY(Transient)
    EDWCSimulationMode ActiveSimulationMode = EDWCSimulationMode::VertexCPU;

    UPROPERTY(Transient)
    bool bSimulationModeLocked = false;

    TUniquePtr<FDWCTaskQueue> AsyncTaskQueue;
    TArray<TUniquePtr<FDWCWetMeshReceiverRuntime>> Receivers;
    FDWCRenderLODRuntimeState RenderLODState;

    FTimerHandle           WetnessSimulationTimer;
    FTimerHandle           SurfaceWaterSimulationTimer;
    FTimerHandle           WetnessRenderTimer;
    FTimerHandle           RenderLODEvaluationTimer;
    TArray<FDWCWetContact> PendingWetContacts;
    bool                   bPendingWetContactsApplyMaterial = false;
    bool                   bWetRenderDirty = false;
};
