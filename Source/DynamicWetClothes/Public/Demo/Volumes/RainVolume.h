// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RainVolume.generated.h"

class UBoxComponent;
class UDynamicWetSourceComponent;
class UNiagaraComponent;

UCLASS()
class DYNAMICWETCLOTHES_API ARainVolume : public AActor
{
	GENERATED_BODY()
	
public:
	ARainVolume();

protected:
	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;

private:
	UPROPERTY(VisibleAnywhere)
	UBoxComponent* RainBounds;

	UPROPERTY(VisibleAnywhere)
	UNiagaraComponent* RainNiagara;

	UPROPERTY(VisibleAnywhere)
	UDynamicWetSourceComponent* DynamicWetSource;
};
