#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DynamicWetSourceBindingTypes.h"
#include "DynamicWetSourceTypes.h"
#include "DynamicWetSourceComponent.generated.h"

class UPrimitiveComponent;
class UDynamicWetReceiverComponent;
class UNiagaraComponent;

UCLASS(ClassGroup = (Wetness), meta = (BlueprintSpawnableComponent))
class DYNAMICWETCLOTHES_API UDynamicWetSourceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDynamicWetSourceComponent();

	UFUNCTION(BlueprintCallable, Category = "Dynamic Wet Source")
	void SetOverlapComponent(UPrimitiveComponent* InOverlapComponent);

	void SetExternalSourceBinding(FDWCExternalSourceBinding InBinding);
	void ClearExternalSourceBinding();
	const FDWCWetSourceData& GetManualSourceData() const { return ManualSourceData; }

	UFUNCTION(BlueprintCallable, Category = "Dynamic Wet Source|Water")
	void InitializeWaterVolume(UPrimitiveComponent* InWaterBounds);

	UFUNCTION(BlueprintCallable, Category = "Dynamic Wet Source|Rain")
	void InitializeRainVolume(UPrimitiveComponent* InRainBounds, UNiagaraComponent* InRainNiagara);

	UFUNCTION(BlueprintCallable, Category = "Dynamic Wet Source|Rain")
	void ApplyRainNiagaraParameters() const;

	UFUNCTION(BlueprintCallable, Category = "Dynamic Wet Source|Rain")
	FVector GetWorldRainDirection() const;

	UFUNCTION(BlueprintCallable, Category = "Dynamic Wet Source")
	void RefreshExistingOverlaps();

	UFUNCTION(BlueprintCallable, Category = "Dynamic Wet Source")
	virtual bool BuildWetSourceData(FDWCWetSourceData& OutSourceData) const;

	virtual bool QueryWetSurfaceZ(const FVector& WorldPosition, float& OutSurfaceZ) const;

protected:
	void SetAutoSourceBindingEnabled(bool bEnabled) { bEnableAutoSourceBinding = bEnabled; }

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UPrimitiveComponent* ResolveOverlapComponent() const;
	void ApplyAutoSourceBindings();
	void BindOverlapComponent();
	void UnbindOverlapComponent();
	void ApplyWetSourceToActor(AActor* OtherActor);
	void RemoveWetSourceFromActor(AActor* OtherActor);
	void ClearActiveWetSources();

	UFUNCTION()
	void OnSourceBeginOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult
	);

	UFUNCTION()
	void OnSourceEndOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex
	);

private:
	UPROPERTY(EditAnywhere, Category = "Dynamic Wet Source")
	FDWCWetSourceData ManualSourceData;

	UPROPERTY(EditAnywhere, Category = "Dynamic Wet Source|Overlap")
	TObjectPtr<UPrimitiveComponent> ExplicitOverlapComponent;

	UPROPERTY(EditAnywhere, Category = "Dynamic Wet Source|Overlap")
	bool bAutoUseOwnerPrimitiveOverlap = true;

	UPROPERTY(EditAnywhere, Category = "Dynamic Wet Source|Overlap")
	bool bApplyToExistingOverlapsOnBeginPlay = true;

	UPROPERTY(EditAnywhere, Category = "Dynamic Wet Source|Auto Binding")
	bool bEnableAutoSourceBinding = true;

	UPROPERTY(EditAnywhere, Category = "Dynamic Wet Source|Rain")
	FName RainDirectionParameterName = TEXT("User.RainDirection");

	UPROPERTY(EditAnywhere, Category = "Dynamic Wet Source|Rain")
	FName RainBoundsExtentParameterName = TEXT("User.RainBoundsExtent");

	UPROPERTY(EditAnywhere, Category = "Dynamic Wet Source|Rain")
	FName RainIntensityParameterName = TEXT("User.RainIntensity");

	UPROPERTY(Transient)
	TObjectPtr<UPrimitiveComponent> BoundOverlapComponent;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> SourceNiagara;

	FDWCExternalSourceBinding ExternalSourceBinding;
	bool bApplyingAutoSourceBinding = false;

	TMap<TWeakObjectPtr<UDynamicWetReceiverComponent>, int32> ActiveWetReceiverOverlapCounts;
};
