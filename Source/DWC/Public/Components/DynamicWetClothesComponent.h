// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "WetInputSystem/WetContactTypes.h"
#include "WetInputSystem/WetInputStage.h"
#include "WetInputSystem/Sampling/WetClothingMeshSampler.h"
#include "RuntimeData/WetRuntimeDataBuilder.h"
#include "RuntimeData/WetClothingRuntimeData.h"
#include "Core/WetClothingSettings.h"
#include "DataAssets/WetClothingAsset.h"
#include "DataAssets/WetnessProfile.h"
#include "WetRendering/WetRenderStage.h"
#include "WetSimulation/WetSimulationStage.h"
#include "WetSimulation/AbsorbedWetness/AbsorbedWetnessSimulationState.h"
#include "Templates/UniquePtr.h"

#include "DynamicWetClothesComponent.generated.h"

class USkeletalMeshComponent;
class UMaterialInstanceDynamic;
class UWetnessProfile;

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
    bool GetWetnessWorldBounds(FBox& OutBounds) const;
    int32 GetWetSurfaceSampleResolution() const;

    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

  protected:
    // Called when the game starts
    virtual void BeginPlay() override;

  private:
    FWetRuntimeDataBuildArgs MakeRuntimeDataBuildArgs();
    FWetInputStageArgs       MakeWetInputStageArgs();
    FWetSimulationStageArgs  MakeWetSimulationStageArgs();
    FWetRenderStageArgs      MakeWetRenderStageArgs();
    bool                     InitializeWetRuntime();
    void                     StartWetnessTimer();
    void                     UpdateWetness();
    bool                     FlushPendingWetContacts();

    USkeletalMeshComponent* ResolveTargetSkeletalMesh() const;
    void                    ApplyWetMaterialOverrides();

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

    UPROPERTY(Transient)
    TArray<TObjectPtr<UMaterialInstanceDynamic>> WetMaterialInstances;

  private:
    TUniquePtr<FWetClothingRuntimeData>         RuntimeData;
    TUniquePtr<FWetRuntimeDataBuilder>          RuntimeDataBuilder;
    TUniquePtr<FAbsorbedWetnessSimulationState> SimulationState;
    TUniquePtr<FWetSimulationStage>             SimulationStage;
    TUniquePtr<FWetInputStage>                  InputStage;
    TUniquePtr<FWetClothingMeshSampler>         MeshSampler;
    TUniquePtr<FWetRenderStage>                 RenderStage;

    FTimerHandle           WetnessUpdateTimer;
    TArray<FDWCWetContact> PendingWetContacts;
    bool                   bPendingWetContactsApplyMaterial = false;
};
