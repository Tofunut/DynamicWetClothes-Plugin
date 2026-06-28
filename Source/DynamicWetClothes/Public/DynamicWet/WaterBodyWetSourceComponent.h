#pragma once

#include "CoreMinimal.h"
#include "DynamicWetSourceComponent.h"
#include "WaterBodyWetSourceComponent.generated.h"

class UBoxComponent;
class UWaterBodyComponent;

UCLASS(ClassGroup = (Wetness), meta = (BlueprintSpawnableComponent))
class DYNAMICWETCLOTHES_API UWaterBodyWetSourceComponent : public UDynamicWetSourceComponent
{
	GENERATED_BODY()

public:
	UWaterBodyWetSourceComponent();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual bool BuildWetSourceData(FDWCWetSourceData& OutSourceData) const override;
	virtual bool QueryWetSurfaceZ(const FVector& WorldPosition, float& OutSurfaceZ) const override;

private:
	void CreateWetnessOverlapProxy();
	void DestroyWetnessOverlapProxy();
	bool GetWaterBodyProxyBounds(FBox& OutBounds) const;
	UPROPERTY(EditAnywhere, Category = "Wetness")
	float WetAmountPerSecond = 0.5f;

	UPROPERTY(EditAnywhere, Category = "Wetness|Overlap Proxy")
	bool bCreateWetnessOverlapProxy = true;

	UPROPERTY(Transient)
	TObjectPtr<UWaterBodyComponent> WaterBodyComponent;

	UPROPERTY(Transient)
	TObjectPtr<UBoxComponent> WetnessOverlapProxy;
};

