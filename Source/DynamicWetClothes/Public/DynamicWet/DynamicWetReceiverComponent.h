// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
>>>> ORIGINAL //depot/Tofunut_EPIC/Plugins/DynamicWetClothes/Source/DynamicWetClothes/Public/DynamicWet/DynamicWetReceiverComponent.h#3
#include "DynamicWetSourceTypes.h"
==== THEIRS //depot/Tofunut_EPIC/Plugins/DynamicWetClothes/Source/DynamicWetClothes/Public/DynamicWet/DynamicWetReceiverComponent.h#4
#include "DynamicWetSourceTypes.h"
#include "WetnessProfile.h"
==== YOURS //DotCho_WorkSpace/Tofunut_EPIC/Plugins/DynamicWetClothes/Source/DynamicWetClothes/Public/DynamicWet/DynamicWetReceiverComponent.h
#include "DynamicWetContactTypes.h"
<<<<

#include "DynamicWetReceiverComponent.generated.h"

class USkeletalMeshComponent;
class UMaterialInstanceDynamic;
class UWetClothingProfile;
class UWetnessProfile;

class FSkeletalMeshLODRenderData;

USTRUCT()
struct FVertexNeighbors
{
	GENERATED_BODY()
	UPROPERTY()
	TArray<int32> Neighbors;
};

UCLASS(ClassGroup=(Wetness), DisplayName="Dynamic Wet Receiver", meta=(BlueprintSpawnableComponent))
class DYNAMICWETCLOTHES_API UDynamicWetReceiverComponent : public UActorComponent
{
	GENERATED_BODY()

public:	
	// Sets default values for this component's properties
	UDynamicWetReceiverComponent();
	
	//Apply Wetness
	void ApplyWetnessGlobal(float Amount);
	void ApplyWetnessBelowHeight(float WaterSurfaceZ, float Amount);
	UFUNCTION(BlueprintCallable, Category = "Wetness")
	bool ApplyWetContact(const FDWCWetContact& Contact, bool bApplyMaterial = true);
	UFUNCTION(BlueprintCallable, Category = "Wetness")
	bool ApplyWetContacts(const TArray<FDWCWetContact>& Contacts, bool bApplyMaterial = true);
	UFUNCTION(BlueprintCallable, Category = "Wetness")
	bool ApplyWetSurface(const FDWCWetSurfaceData& SurfaceData, float Amount, bool bApplyMaterial = true);
	bool GetWetnessWorldBounds(FBox& OutBounds) const;
	void DryOutWetness(bool& bDirty, float EffectiveDryRatePerSecond);
	void ProcessPendingWetness(bool& bDirty, float EffectiveSpreadRatePerSecond);

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	// Called when the game starts
	virtual void BeginPlay() override;

private:	
	void InitializeWetnessData();
	void InitializeWetPartVertexData();
	void BuildNeighborGraph();
	void InitializeWetMaterialInstance();
	void ApplyWetMaterialParameters();
	void AddNeighbor(int32 VertexIndex, int32 NeighborIndex);
	const UWetnessProfile* GetActiveMaterialProfile() const;
	float GetAbsorptionMultiplier() const;
	float GetDryRatePerSecond() const;
	float GetSpreadRatePerSecond() const;
	float GetGravityFlowStrength() const;
>>>> ORIGINAL //depot/Tofunut_EPIC/Plugins/DynamicWetClothes/Source/DynamicWetClothes/Public/DynamicWet/DynamicWetReceiverComponent.h#3
	bool NormalizeWetSourceData(UObject* SourceId, const FDWCWetSourceData& SourceData, FDWCWetSourceData& OutSourceData) const;
==== THEIRS //depot/Tofunut_EPIC/Plugins/DynamicWetClothes/Source/DynamicWetClothes/Public/DynamicWet/DynamicWetReceiverComponent.h#4
	float GetAbsorptionMultiplierForVertex(int32 VertexIndex) const;
	float GetDryRatePerSecondForVertex(int32 VertexIndex) const;
	float GetSpreadRatePerSecondForVertex(int32 VertexIndex) const;
	float GetGravityFlowStrengthForVertex(int32 VertexIndex) const;
	bool NormalizeWetSourceData(UObject* SourceId, const FDWCWetSourceData& SourceData, FDWCWetSourceData& OutSourceData) const;
==== YOURS //DotCho_WorkSpace/Tofunut_EPIC/Plugins/DynamicWetClothes/Source/DynamicWetClothes/Public/DynamicWet/DynamicWetReceiverComponent.h
<<<<
	void EnsureWetnessBufferSize(int32 VertexCount);
	float AbsorbWetnessAtVertex(int32 VertexIndex, float Amount, bool& bDirty);
	void QueuePendingWetness(int32 VertexIndex, float Amount);
	void RefreshWetnessDryHold(int32 VertexIndex);
	void ClearPendingWetness();
	bool PreparePendingWetnessProcessing(
		float EffectiveSpreadRatePerSecond,
		float& OutSpreadAlpha,
		float& OutGravityFlowStrength,
		bool& bOutUseGravityBias,
		bool& bOutCanSpread
	);
	void SnapshotPendingWetnessForCurrentUpdate();
	int32 ProcessCurrentPendingWetness(
		bool& bDirty,
		float SpreadAlpha,
		float GravityFlowStrength,
		bool bUseGravityBias,
		bool bCanSpread
	);
	void SpreadPendingWetnessToNeighbors(
		int32 VertexIndex,
		float SpreadableWetness,
		float SpreadAlpha,
		float GravityFlowStrength,
		bool bUseGravityBias
	);
	float CalculateNeighborGravityBias(
		int32 SourceVertexIndex,
		int32 NeighborIndex,
		float GravityFlowStrength,
		const FTransform& ComponentTransform,
		const FVector& GravityDirection
	) const;
	void RequeueUnprocessedPendingWetness(int32 QueueReadIndex);


	void UpdateWetness();
	void ApplyWetnessToMaterial();
	bool ApplyRainWetness(const FVector& RainDirection, float Amount, bool bApplyMaterial = true);
	bool QueryWetSurfaceData(const FDWCWetSurfaceData& SurfaceData, const FVector& WorldPosition, float& OutSurfaceZ) const;
	bool DoesVertexMatchBoneName(int32 VertexIndex, FName BoneName) const;

	USkeletalMeshComponent* ResolveTargetSkeletalMesh() const;
	bool UpdateSkinnedPositions();
	bool UpdateSkinnedNormals();
	bool GetLODRenderData(int32 LODIndex, FSkeletalMeshLODRenderData*& OutLODData) const;

public:
	UPROPERTY(EditAnywhere, Category = "Wetness")
	FName TargetSkeletalMeshName = NAME_None;

	UPROPERTY(Transient)
	TObjectPtr<USkeletalMeshComponent> TargetSkeletalMesh;

	UPROPERTY(EditAnywhere)
	TArray<UWetnessProfile*> MaterialProfiles;

	UPROPERTY(EditAnywhere, Category = "Wetness")
	TObjectPtr<UWetClothingProfile> WetClothingProfile = nullptr;

	UPROPERTY(EditAnywhere, Category = "Wetness|Visual")
	FLinearColor FallbackUnderColor = FLinearColor(0.8f, 0.55f, 0.42f, 1.0f);

	UPROPERTY(EditAnywhere, Category = "Wetness|Visual", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float WetUnderColorBlendStrength  = 0.3f;

	
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> WetMaterialInstances;
private:
	UPROPERTY(VisibleAnywhere, Category = "Wetness")
	TArray<float> WetnessPerVertex;
	TArray<int32> VertexWetPartIDs;
	TArray<FWetnessProfileParameters> VertexWetnessProfileParameters;
	TArray<float> Updating_Pending_Wetness_Amounts;
	TArray<float> WetnessDryHoldTimePerVertex;
	
	
	TArray<int32> Updating_Pending_Wetness_Vertex_IndexQueue;
	TArray<int32> Current_Pending_Wetness_Vertex_IndexQueue;
	TArray<float> Current_Pending_Wetness_Amounts;
	TArray<bool> bPendingWetnessQueued;

	TArray<FLinearColor> CachedWetVertexColors;
	TArray<int32> DirtyWetVertexIndices;

	UPROPERTY(VisibleAnywhere, Category = "Wetness")
	TArray<FVertexNeighbors> NeighborGraph;

	UPROPERTY(EditAnywhere, Category = "Wetness")
	float WetnessUpdateInterval = 0.1f; // 10Hz

	UPROPERTY(EditAnywhere, Category = "Wetness", meta = (ClampMin = "0.0"))
	float MaxStoredWetness = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Wetness|Visual", meta = (ClampMin = "0.001"))
	float VisualSaturationWetness = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Wetness", meta = (ClampMin = "0.0"))
	float WetnessDryHoldDuration = 0.3f;

	UPROPERTY(EditAnywhere, Category = "Wetness|Capillary", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CapillaryImmediateAbsorptionFraction = 0.65f;

	UPROPERTY(EditAnywhere, Category = "Wetness|Capillary", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CrossWetPartSpreadScale = 1.0f;

	UPROPERTY(EditAnywhere, Category = "Wetness|Rain", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float RainExposureMin = 0.5f;

	UPROPERTY(EditAnywhere, Category = "Wetness|Rain", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
	float RainExposureMax = 0.9f;

	UPROPERTY(EditAnywhere, Category = "Wetness|Performance", meta = (ClampMin = "1"))
	int32 MaxPendingWetnessVerticesPerUpdate = 4096;

	UPROPERTY(EditAnywhere, Category = "Wetness|Performance", meta = (ClampMin = "0.0"))
	float MinPendingWetnessAmount = 0.0001f;
	
	

	FTimerHandle WetnessUpdateTimer;

	//Skinning cache for vertex positions
	TArray<FVector3f> CachedSkinnedPositions;
	TArray<FVector3f> CachedSkinnedNormals;
	TArray<FMatrix44f> CachedRefToLocalMatrices;
};
