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
#include "DataAssets/WetClothingAsset.h"
#include "DataAssets/WetnessProfile.h"
#include "WetRendering/WetRenderStage.h"
#include "WetSimulation/WetSimulationStage.h"
#include "WetSimulation/AbsorbedWetness/AbsorbedWetnessSimulationState.h"
#include "WetSimulation/SurfaceWater/SurfaceWaterSimulationState.h"
#include "Templates/UniquePtr.h"

#include "DynamicWetClothesComponent.generated.h"

class USkeletalMeshComponent;
class USkeletalMesh;
class UMaterialInstanceDynamic;
class UWetnessProfile;
class FDWCTaskQueue;
struct FDWCSkinningTaskResult;
struct FDWCSkinningStaticData;

struct FDWCWetMeshReceiverRuntime
{
    FName ReceiverId = NAME_None;
    TWeakObjectPtr<USkeletalMeshComponent> MeshComponent;
    TWeakObjectPtr<UWetClothingAsset> WetClothingAsset;

    TUniquePtr<FWetClothingRuntimeData> RuntimeData;
    TUniquePtr<FWetRuntimeDataBuilder> RuntimeDataBuilder;
    TUniquePtr<FAbsorbedWetnessSimulationState> SimulationState;
    TMap<int32, TUniquePtr<FSurfaceWaterSimulationState>> SurfaceWaterStatesByMaterialSlot;
    TMap<int32, FSurfaceWaterProfileParameters> SurfaceWaterProfilesByMaterialSlot;
    TUniquePtr<FWetSimulationStage> SimulationStage;
    TUniquePtr<FWetInputStage> InputStage;
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
    UFUNCTION(BlueprintCallable, Category = "Wetness|Debug")
    void SetWetPartDebugVertexColorsEnabled(bool bEnabled);
    UFUNCTION(BlueprintCallable, Category = "Wetness|Debug")
    void RefreshWetVertexColors();
    UFUNCTION(BlueprintCallable, Category = "Wetness|Debug")
    bool DebugApplySurfaceWaterAtVertex(int32 VertexIndex, float Amount = 1.0f, float RadiusPixels = 12.0f, bool bFlowStamp = false);
    UFUNCTION(BlueprintCallable, Category = "Wetness|Debug")
    void SetSurfaceWaterDebugView(ESurfaceWaterDebugView DebugView);
    UFUNCTION(BlueprintPure, Category = "Wetness|Debug")
    int64 GetSurfaceWaterEstimatedGpuMemoryBytes() const;
    UFUNCTION(BlueprintCallable, Category = "Wetness|Debug") void SetSurfaceWaterSimulationPaused(bool bPaused);
    UFUNCTION(BlueprintCallable, Category = "Wetness|Debug") void StepSurfaceWaterSimulation();
    UFUNCTION(BlueprintCallable, Category = "Wetness|Debug") void ClearSurfaceWater();
    bool GetWetnessWorldBounds(FBox& OutBounds) const;
    int32 GetWetSurfaceSampleResolution() const;
    void CommitCpuSkinningTaskResult(FDWCSkinningTaskResult&& Result);

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
    FWetSimulationStageArgs  MakeWetSimulationStageArgs(FDWCWetMeshReceiverRuntime& Receiver);
    FWetRenderStageArgs      MakeWetRenderStageArgs(FDWCWetMeshReceiverRuntime& Receiver);
    bool                     InitializeWetRuntime();
    bool                     RebuildWetMeshReceivers();
    bool                     InitializeWetMeshReceiverRuntime(FDWCWetMeshReceiverRuntime& Receiver);
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

    USkeletalMeshComponent* ResolveTargetSkeletalMesh() const;
    UWetClothingAsset*      ResolveWetClothingAssetForMesh(const USkeletalMeshComponent& MeshComponent) const;
    void                    ApplyGeneratedWetMaterialOverrides();
    bool                    ShouldReceiverConsiderContact(const FDWCWetMeshReceiverRuntime& Receiver, const FDWCWetContact& Contact) const;
    bool                    ShouldReceiverConsiderSurface(const FDWCWetMeshReceiverRuntime& Receiver, const FDWCWaterSurfaceData& WaterSurfaceData) const;

  public:
    UPROPERTY(EditAnywhere, Category = "Wetness")
    FName TargetSkeletalMeshName = NAME_None;

    UPROPERTY(Transient)
    TObjectPtr<USkeletalMeshComponent> TargetSkeletalMesh;

    UPROPERTY(EditAnywhere)
    TArray<UWetnessProfile*> WetnessProfiles;

    UPROPERTY(EditAnywhere, Category = "Wetness")
    TObjectPtr<UWetClothingAsset> WetClothingAsset = nullptr;

    UPROPERTY(EditAnywhere, Category = "Wetness", meta = (ShowOnlyInnerProperties))
    FWetClothingSettings WetnessSettings;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Contact", meta = (AllowPrivateAccess = "true"))
    bool bBatchWetContactsPerFrame = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Contact", meta = (ClampMin = "1", AllowPrivateAccess = "true"))
    int32 MaxBatchedWetContactsPerFrame = 64;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Surface", meta = (ClampMin = "2", ClampMax = "64", AllowPrivateAccess = "true"))
    int32 WetSurfaceSampleResolution = 8;

    UPROPERTY(EditAnywhere, Category = "Wetness|Visual")
    FLinearColor FallbackUnderColor = FLinearColor(0.8f, 0.55f, 0.42f, 1.0f);

    UPROPERTY(EditAnywhere, Category = "Wetness|Visual", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float WetUnderColorBlendStrength = 0.3f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Visual")
    FName UnderColorParameterName = TEXT("DWC_UnderColor");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Visual")
    FName UnderColorBlendStrengthParameterName = TEXT("DWC_UnderColorBlendStrength");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Surface Water")
    FName SurfaceWaterRTParameterName = TEXT("DWC_SurfaceWaterRT");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Surface Water")
    FName SurfaceDropletRTParameterName = TEXT("DWC_SurfaceDropletRT");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Surface Water")
    FName SurfaceFlowRTParameterName = TEXT("DWC_SurfaceFlowRT");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Surface Water")
    FName SurfaceWaterTimeParameterName = TEXT("DWC_SurfaceWaterTime");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Surface Water")
    FName SurfaceWaterTexelSizeParameterName = TEXT("DWC_SurfaceWaterTexelSize");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Debug")
    ESurfaceWaterDebugView SurfaceWaterDebugView = ESurfaceWaterDebugView::None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Debug")
    bool bEnableWetPartDebugVertexColors = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Debug")
    bool bWetPartDebugUseWetnessMask = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Debug")
    FLinearColor UnassignedWetPartDebugColor = FLinearColor(0.25f, 0.25f, 0.25f, 1.0f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Debug")
    FName WetPartDebugStrengthParameterName = TEXT("DWC_WetPartDebugStrength");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Debug")
    FName WetPartDebugUseWetnessMaskParameterName = TEXT("DWC_WetPartDebugUseWetnessMask");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Wetness Profile Map")
    FName WetnessProfileMap0ParameterName = TEXT("DWC_WetnessProfileMap0");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Wetness Profile Map")
    FName UseWetnessProfileMap0ParameterName = TEXT("DWC_UseWetnessProfileMap0");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Wrinkle", meta = (ClampMin = "0.0"))
    float WrinkleStrength = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Wrinkle", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float WrinkleWetnessMin = 0.25f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Wrinkle", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float WrinkleWetnessMax = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Wrinkle")
    FName WrinkleNormalMapParameterName = TEXT("DWC_WrinkleNormalMap");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Wrinkle")
    FName UseWrinkleNormalMapParameterName = TEXT("DWC_UseWrinkleNormalMap");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Wrinkle")
    FName WrinkleStrengthParameterName = TEXT("DWC_WrinkleStrength");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Wrinkle")
    FName WrinkleWetnessMinParameterName = TEXT("DWC_WrinkleWetnessMin");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Wrinkle")
    FName WrinkleWetnessMaxParameterName = TEXT("DWC_WrinkleWetnessMax");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Transparency", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float TransparencyWetnessMin = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Transparency", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float TransparencyWetnessMax = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Transparency")
    FName TransparencyMapParameterName = TEXT("DWC_TransparencyMap");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Transparency")
    FName UseTransparencyMapParameterName = TEXT("DWC_UseTransparencyMap");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Transparency")
    FName TransparencyStrengthParameterName = TEXT("DWC_TransparencyStrength");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Transparency")
    FName TransparencyWetnessMinParameterName = TEXT("DWC_TransparencyWetnessMin");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Transparency")
    FName TransparencyWetnessMaxParameterName = TEXT("DWC_TransparencyWetnessMax");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Transparency")
    FName TransparencyUVChannelParameterName = TEXT("DWC_TransparencyUVChannel");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Transparency")
    FName WrinkleSuppressionStrengthParameterName = TEXT("DWC_WrinkleSuppressionStrength");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Debug")
    bool bLogWrinkleRuntimeBindings = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness|Debug")
    bool bLogTransparencyRuntimeBindings = false;

  private:
    TUniquePtr<FDWCTaskQueue> AsyncTaskQueue;
    TArray<TUniquePtr<FDWCWetMeshReceiverRuntime>> Receivers;

    FTimerHandle           WetnessSimulationTimer;
    FTimerHandle           SurfaceWaterSimulationTimer;
    FTimerHandle           WetnessRenderTimer;
    TArray<FDWCWetContact> PendingWetContacts;
    bool                   bPendingWetContactsApplyMaterial = false;
    bool                   bWetRenderDirty = false;
};
