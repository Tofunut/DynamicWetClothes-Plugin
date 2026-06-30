// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DynamicWetContactTypes.h"
#include "DynamicWetReceiverContext.h"
#include "DynamicWetReceiverInputApplicator.h"
#include "DynamicWetReceiverMeshSampler.h"
#include "DynamicWetReceiverRenderApplier.h"
#include "DynamicWetReceiverRuntimeData.h"
#include "DynamicWetReceiverSettings.h"
#include "DynamicWetReceiverSimulationSolver.h"
#include "DynamicWetReceiverSimulationState.h"
#include "Templates/UniquePtr.h"
#include "WetClothingAsset.h"
#include "WetnessProfile.h"

#include "DynamicWetReceiverComponent.generated.h"

class USkeletalMeshComponent;
class UMaterialInstanceDynamic;
class UWetnessProfile;


UCLASS(ClassGroup=(Wetness), DisplayName="Dynamic Wet Receiver", meta=(BlueprintSpawnableComponent))
class DYNAMICWETCLOTHES_API UDynamicWetReceiverComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	// Sets default values for this component's properties
	UDynamicWetReceiverComponent();
	virtual ~UDynamicWetReceiverComponent() override;
	
	//Apply Wetness
	void ApplyWetnessGlobal(float Amount);
	void ApplyWetnessBelowHeight(float WaterSurfaceZ, float Amount);
	UFUNCTION(BlueprintCallable, Category = "Wetness")
	bool ApplyWetContact(const FDWCWetContact& Contact, bool bApplyMaterial = true);
	UFUNCTION(BlueprintCallable, Category = "Wetness")
	bool ApplyWetContacts(const TArray<FDWCWetContact>& Contacts, bool bApplyMaterial = true);
	UFUNCTION(BlueprintCallable, Category = "Wetness")
	bool ApplyWetSurface(const FDWCWetSurfaceData& SurfaceData, float Amount, bool bApplyMaterial = true);
	UFUNCTION(BlueprintCallable, Category = "Wetness|Debug")
	void SetWetPartDebugVertexColorsEnabled(bool bEnabled);
	UFUNCTION(BlueprintCallable, Category = "Wetness|Debug")
	void RefreshWetVertexColors();
	bool GetWetnessWorldBounds(FBox& OutBounds) const;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

private:	
	FDynamicWetReceiverContext MakeContext();
	bool InitializeReceiverRuntime();
	void StartWetnessTimer();
	void UpdateWetness();

	USkeletalMeshComponent* ResolveTargetSkeletalMesh() const;

public:
	UPROPERTY(EditAnywhere, Category = "Wetness")
	FName TargetSkeletalMeshName = NAME_None;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> TargetSkeletalMesh;

	UPROPERTY(EditAnywhere)
	TArray<UWetnessProfile*> MaterialProfiles;

	UPROPERTY(EditAnywhere, Category = "Wetness")
	TObjectPtr<UWetClothingAsset> WetClothingProfile = nullptr;

	UPROPERTY(EditAnywhere, Category = "Wetness", meta = (ShowOnlyInnerProperties))
	FDynamicWetReceiverSettings WetnessSettings;

	UPROPERTY(EditAnywhere, Category = "Wetness|Visual")
	FLinearColor FallbackUnderColor = FLinearColor(0.8f, 0.55f, 0.42f, 1.0f);

	UPROPERTY(EditAnywhere, Category = "Wetness|Visual", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WetUnderColorBlendStrength  = 0.3f;

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

	
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> WetMaterialInstances;
private:
	TUniquePtr<FDynamicWetReceiverRuntimeData> RuntimeData;
	TUniquePtr<FDynamicWetReceiverRuntimeDataBuilder> RuntimeDataBuilder;
	TUniquePtr<FDynamicWetReceiverSimulationState> SimulationState;
	TUniquePtr<FDynamicWetReceiverSimulationSolver> SimulationSolver;
	TUniquePtr<FDynamicWetReceiverInputApplicator> InputApplicator;
	TUniquePtr<FDynamicWetReceiverMeshSampler> MeshSampler;
	TUniquePtr<FDynamicWetReceiverRenderApplier> RenderApplier;
	
	

	FTimerHandle WetnessUpdateTimer;
};
