#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WaterVolume.generated.h"

class UBoxComponent;
class UDynamicWetSourceComponent;

UCLASS()
class DYNAMICWETCLOTHES_API AWaterVolume : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	AWaterVolume();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;

private:
	UPROPERTY(VisibleAnywhere, Category = "Water Volume")
	UBoxComponent* VolumeBox;

	UPROPERTY(VisibleAnywhere, Category = "Water Volume")
	UDynamicWetSourceComponent* DynamicWetSource;
};
