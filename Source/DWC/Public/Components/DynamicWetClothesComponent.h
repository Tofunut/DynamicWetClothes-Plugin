// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "UObject/WeakObjectPtr.h"
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
#include "RuntimeState/Utils/DWCLodCoordinator.h"
#include "RuntimeState/Utils/DWCLODVertexColorTransferCoordinator.h"
#include "WetSimulation/AbsorbedWetness/AbsorbedWetnessSimulationState.h"
#include "Templates/UniquePtr.h"

#include "DynamicWetClothesComponent.generated.h"

class USkeletalMeshComponent;
class USkeletalMesh;
class FDWCTaskQueue;
class UDWCRuntimeDataSubsystem;
class UDWCStatsSubsystem;
struct FWetMeshReceiverInitializerContext;
struct FDWCSkinningTaskResult;
struct FDWCSkinningStaticData;
struct FDWCLODVertexColorTransferResult;
struct FDWCLODVertexStaticData;
struct FPropertyChangedEvent;

struct FDWCWetMeshReceiverRuntime
{
    // Receiver identity and source UObject references.
    FName                                  ReceiverId = NAME_None;
    TWeakObjectPtr<USkeletalMeshComponent> MeshComponent;
    TWeakObjectPtr<UWetClothingAsset>      WetClothingAsset;

    // Shared immutable runtime data.
    // The TSharedPtr itself is small; the pointed-to payload can be large and
    // must be counted once per unique payload, not once per receiver.
    TSharedPtr<const FWetClothingRuntimeData, ESPMode::ThreadSafe> SharedRuntimeData;
    TSharedPtr<const FDWCSkinningStaticData, ESPMode::ThreadSafe>  SkinningStaticData;

    // Per-receiver simulation and rendering state.
    // These are the main CPU-memory consumers.
    TUniquePtr<FAbsorbedWetnessSimulationState> SimulationState;
    TUniquePtr<FWetClothingMeshSampler>         MeshSampler;
    TUniquePtr<FWetRenderStage>                 RenderStage;

    // Per-receiver GPU simulation backend and GPU-only surface stamp RNG.
    TUniquePtr<IDWCGPUBackend> GPUBackend;
    FRandomStream              GPUSurfaceWaterRandomStream = FRandomStream(0x445743);

    // LOD vertex-color transfer data.
    // Static LOD data and transfer maps can be shared by multiple receivers,
    // so global memory accounting must deduplicate the pointed-to objects.
    TMap<int32, TSharedPtr<const FDWCLODVertexStaticData, ESPMode::ThreadSafe>> LODVertexStaticDataByLOD;
    TMap<int32, TSharedPtr<const TArray<int32>, ESPMode::ThreadSafe>>           LODVertexColorTransferMapsByLOD;

    // Per-receiver LOD vertex-color cache.
    // The shared pointer provides lifetime and task hand-off safety; the color
    // array may scale with the target LOD vertex count.
    TMap<int32, TSharedPtr<const TArray<FColor>, ESPMode::ThreadSafe>> LODVertexColorCachesByLOD;

    // Per-receiver transient LOD work state.
    TArray<int32>              PendingLODVertexColorDirtySourceVertices;
    int32                      LODVertexColorTransferGeneration = 0;
    FDWCQualityLODRuntimeState QualityLODState;

    // Per-receiver render invalidation state.
    bool bWetRenderDirty = false;

    // Per-receiver asynchronous task state.
    bool bCpuSkinningTaskPending = false;
    bool bLODVertexColorTransferPending = false;
    bool bLODVertexColorTransferRequestedAgain = false;
};

UCLASS(ClassGroup = (DWC), DisplayName = "Dynamic Wet Clothes", meta = (BlueprintSpawnableComponent))
class DWC_API UDynamicWetClothesComponent : public UActorComponent
{
    GENERATED_BODY()

  public:
    // Component lifetime.
    UDynamicWetClothesComponent();
    virtual ~UDynamicWetClothesComponent() override;

    // Wetness input API.
    UFUNCTION(BlueprintCallable, Category = "Wetness", meta = (ToolTip = "Adds wetness to every wettable vertex on this component. Negative values remove wetness."))
    void ApplyWetAll(float Amount);
    UFUNCTION(BlueprintCallable, Category = "Wetness", meta = (AdvancedDisplay = "bApplyMaterial", ToolTip = "Applies one world-space wet contact. Negative Amount values remove wetness."))
    bool ApplyWetContact(const FDWCWetContact& Contact, bool bApplyMaterial = true);
    UFUNCTION(BlueprintCallable, Category = "Wetness", meta = (AdvancedDisplay = "bApplyMaterial", ToolTip = "Applies multiple world-space wet contacts in one call. Prefer this for Niagara or batched splash input."))
    bool ApplyWetContacts(const TArray<FDWCWetContact>& Contacts, bool bApplyMaterial = true);
    UFUNCTION(BlueprintCallable, Category = "Wetness", meta = (AdvancedDisplay = "bApplyMaterial", ToolTip = "Distributes wetness across sampled receiver vertices, optionally weighted by the incoming direction and surface normals."))
    bool ApplyWetArea(const FDWCWetAreaData& AreaData, bool bApplyMaterial = true);
    UFUNCTION(BlueprintCallable, Category = "Wetness", meta = (AdvancedDisplay = "bApplyMaterial", ToolTip = "Applies wetness to vertices under the supplied water surface height grid. Negative Amount values remove wetness."))
    bool ApplyWetSurface(const FDWCWaterSurfaceData& WaterSurfaceData, float Amount, bool bApplyMaterial = true);
    UFUNCTION(BlueprintCallable, Category = "Wetness")
    void SetDryRateScale(float InDryRateScale);
    UFUNCTION(BlueprintPure, Category = "Wetness")
    float GetDryRateScale() const { return WetnessSettings.DryRateScale; }
    /** Toggles only Droplet1's material contribution. Stamping, simulation, and drying continue. */
    UFUNCTION(BlueprintCallable, Category = "Wetness|Rendering")
    void SetDroplet1RenderingEnabled(bool bEnabled);
    /** Toggles only Droplet2's material contribution. Stamping, simulation, and drying continue. */
    UFUNCTION(BlueprintCallable, Category = "Wetness|Rendering")
    void SetDroplet2RenderingEnabled(bool bEnabled);
    /** Toggles both droplet visuals through the two independent rendering setters. */
    UFUNCTION(BlueprintCallable, Category = "Wetness|Rendering")
    void SetDropletRenderingEnabled(bool bEnabled);
    UFUNCTION(BlueprintCallable, Category = "Wetness|GPU")
    bool ClearGPUPendingWetnessMaps();
    bool GetWetnessWorldBounds(FBox& OutBounds) const;
    UFUNCTION(BlueprintPure, Category = "Wetness|GPU")
    int32 GetDWCReceiverGPUId(FName ReceiverId = NAME_None) const;
    void  GetDWCReceiverGPUIds(TArray<int32>& OutReceiverGPUIds) const;
    // User-facing debug API.
    UFUNCTION(BlueprintCallable, Category = "Debug")
    void SetWetPartDebugColorsEnabled(bool bEnabled);
    UFUNCTION(BlueprintCallable, Category = "Debug")
    void SetSurfaceWaterDebugColorsEnabled(bool bEnabled);
    // Internal DWC quality-LOD support. Not exposed through the public UI or Blueprint API.
    void  SetDWCQualityLOD(int32 InQualityLOD);
    bool  SetReceiverDWCQualityLOD(FName ReceiverId, int32 InQualityLOD);
    int32 GetDWCQualityLOD() const
    {
        return CurrentQualityLOD;
    }

    // Runtime state and asynchronous task callbacks.
    int32 GetWetSurfaceSampleResolution() const;
    void  CommitCpuSkinningTaskResult(FDWCSkinningTaskResult&& Result);
    void  CommitLODVertexColorTransferResult(FDWCLODVertexColorTransferResult&& Result);

    EDWCSimulationMode GetActiveSimulationMode() const
    {
        return bSimulationModeLocked ? ActiveSimulationMode : SimulationMode;
    }

    void GetResolvedWetMeshComponents(TArray<USkeletalMeshComponent*>& OutComponents) const;

    // Internal quality-LOD evaluation queries. These are not part of the current public feature set.
    int32 GetCurrentRenderLODLevel() const;
    float GetMergedReceiverScreenSize() const;

    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
    void         HandleExternalMaterialPropertyChanged(UObject* Object, FPropertyChangedEvent& PropertyChangedEvent);
    void         RebindMaterialsAfterExternalChange(USkeletalMeshComponent* MeshComponent);
#endif

  protected:
    // Called when the game starts
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

  private:
    friend class UDWCStatsSubsystem;

    // Runtime initialization and receiver management.
    bool InitializeWetRuntime();
    void StartWetnessTimers();

    // Stage argument construction.
    FWetMeshReceiverInitializerContext MakeWetMeshReceiverInitializerContext();
    FWetApplicationStageContext        MakeWetApplicationStageContext();
    FWetRuntimeDataBuildArgs           MakeRuntimeDataBuildArgs(FDWCWetMeshReceiverRuntime& Receiver);
    FWetSimulationStageArgs            MakeWetSimulationStageArgs(FDWCWetMeshReceiverRuntime& Receiver);
    FWetRenderStageArgs                MakeWetRenderStageArgs(FDWCWetMeshReceiverRuntime& Receiver);

    // Per-frame simulation and rendering updates.
    void UpdateWetness();
    void UpdateWetRendering();
    bool FlushPendingWetContacts();
    bool ShouldUpdateCPUWetnessRendering(FDWCWetMeshReceiverRuntime& Receiver) const;
    bool ShouldEnableCPUWetnessRendering(const FDWCWetMeshReceiverRuntime& Receiver) const;

    // Wetness input render invalidation.
    void RequestWetRenderingUpdate();
    void RequestWetRenderingUpdate(FDWCWetMeshReceiverRuntime& Receiver);

    // GPU and material rendering state.
    bool InitializeGPUBackend(FDWCWetMeshReceiverRuntime& Receiver);
    void ApplyGeneratedWetMaterialOverrides();
    void ApplyQualityLODMaterialParameters(FDWCWetMeshReceiverRuntime& Receiver);
    void MarkCPUWetnessRenderingDirty(FDWCWetMeshReceiverRuntime& Receiver);
    // Internal quality-LOD implementation. Not exposed through the public API.
    void RefreshResolvedQualityLODPolicies();
    void UpdateRenderLOD();

    // Asynchronous skinning and LOD transfer tasks.
    bool RequestCpuSkinningTask(FDWCWetMeshReceiverRuntime& Receiver, bool bComputePositions, bool bComputeNormals);
    void RequestContinuousCpuSkinningTasks();
    bool HasPendingCpuSkinningTasks() const;
    bool RequestLODVertexColorTransferTask(FDWCWetMeshReceiverRuntime& Receiver);
    bool HasPendingLODVertexColorTransferTasks() const;
    void FlushAsyncTaskQueueGameThread();

  public:
    // Input asset and simulation mode.
    /** The single Wet Clothing Asset handled by this component. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Setup", meta = (DisplayName = "Wet Clothing Asset"))
    TObjectPtr<UWetClothingAsset> WetClothingAsset = nullptr;

    // User-facing rows are explicitly arranged by the Details customization.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation")
    FWetClothingSettings WetnessSettings;

    /** Selected per component instance and locked when BeginPlay starts. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Simulation")
    EDWCSimulationMode SimulationMode = EDWCSimulationMode::WetnessMapGPU;

    // GPU implementation tuning is fixed internally and intentionally not exposed to Details or Blueprint.

    // Contact processing configuration. Contacts are always batched internally.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Performance", meta = (DisplayName = "Max Contacts Per Frame", ClampMin = "1", AdvancedDisplay))
    int32 MaxWetContactsPerFrame = 64;

    // Internal sampling resolution retained for runtime integration; intentionally not user-configurable.
    int32 WetSurfaceSampleResolution = 8;

    // Internal DWC quality-LOD support. Disabled by default and not exposed through the public UI or Blueprint API.
    bool bEnableDWCQualityLOD = false;

    UPROPERTY()
    TObjectPtr<UDWCQualityLODProfile> QualityLODProfile = nullptr;

    TArray<FDWCQualityLODScreenSizeThreshold> QualityLODScreenSizeThresholds;
    float                                     RenderLODEvaluationInterval = 0.1f;

    // Legacy component-level appearance overrides are intentionally not exposed.
    // Rendering appearance is authored by WCA/Wetness Profile data. These defaults preserve
    // the existing runtime behavior without duplicating authoring controls on the component.
    FLinearColor FallbackUnderColor = FLinearColor(0.8f, 0.55f, 0.42f, 1.0f);
    float        WetUnderColorBlendStrength = 0.3f;
    float        WrinkleStrength = 1.0f;
    float        WrinkleWetnessMin = 0.25f;
    float        WrinkleWetnessMax = 1.0f;
    float        TransparencyWetnessMin = 0.0f;
    float        TransparencyWetnessMax = 1.0f;

    /**
     * Keeps the normal material while dry and displays each Wet Part's configured color while wet.
     * CPU wetness uses VertexColor.R; GPU wetness stays in the existing wetness texture path.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
    bool bShowWetPartDebugColors = false;

    /** Displays GPU surface-water droplets as a debug color over the wet part debug overlay. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug", meta = (EditCondition = "SimulationMode == EDWCSimulationMode::WetnessMapGPU"))
    bool bShowSurfaceWaterDebugColors = false;

  private:
    // Runtime mode state.
    bool bDroplet1RenderingEnabled = true;
    bool bDroplet2RenderingEnabled = true;

    UPROPERTY(Transient)
    EDWCSimulationMode ActiveSimulationMode = EDWCSimulationMode::WetnessMapGPU;

    UPROPERTY(Transient)
    bool bSimulationModeLocked = false;

    // Receiver collection and shared coordinators.
    TArray<TUniquePtr<FDWCWetMeshReceiverRuntime>>    Receivers;
    TUniquePtr<FDWCTaskQueue>                         AsyncTaskQueue;
    TUniquePtr<FDWCLodCoordinator>                    LODCoordinator;
    TUniquePtr<FDWCLODVertexColorTransferCoordinator> LODVertexColorTransferCoordinator;

    // Timers and pending frame work.
    FTimerHandle WetnessSimulationTimer;
    FTimerHandle WetnessRenderTimer;
    FTimerHandle RenderLODEvaluationTimer;
#if WITH_EDITOR
    FDelegateHandle ExternalMaterialPropertyChangedHandle;
    bool            bRebindingExternalMaterials = false;
#endif
    TArray<FDWCWetContact> PendingWetContacts;
    bool                   bPendingWetContactsApplyMaterial = false;
    bool                   bWetRenderDirty = false;
    // Internal runtime state for DWC quality-LOD support. Not exposed through the public API.
    int32 CurrentQualityLOD = 0;
    float CurrentRenderLODScreenSize = 0.0f;
};
