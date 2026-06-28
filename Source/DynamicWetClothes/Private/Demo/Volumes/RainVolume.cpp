// Fill out your copyright notice in the Description page of Project Settings.

#include "Demo/Volumes/RainVolume.h"

#include "Components/BoxComponent.h"
#include "DynamicWet/DynamicWetSourceComponent.h"
#include "NiagaraComponent.h"

ARainVolume::ARainVolume()
{
	PrimaryActorTick.bCanEverTick = false;

	RainBounds = CreateDefaultSubobject<UBoxComponent>(TEXT("RainBounds"));
	SetRootComponent(RainBounds);

	RainBounds->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	RainBounds->SetCollisionObjectType(ECC_WorldDynamic);
	RainBounds->SetCollisionResponseToAllChannels(ECR_Ignore);
	RainBounds->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	RainBounds->SetGenerateOverlapEvents(true);
	RainBounds->SetBoxExtent(FVector(500.0f, 500.0f, 500.0f));

	RainNiagara = CreateDefaultSubobject<UNiagaraComponent>(TEXT("RainNiagara"));
	RainNiagara->SetupAttachment(RootComponent);

	DynamicWetSource = CreateDefaultSubobject<UDynamicWetSourceComponent>(TEXT("DynamicWetSource"));
}

void ARainVolume::BeginPlay()
{
	Super::BeginPlay();

	if (DynamicWetSource)
	{
		DynamicWetSource->InitializeRainVolume(RainBounds, RainNiagara);
	}
}

void ARainVolume::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	if (DynamicWetSource)
	{
		DynamicWetSource->InitializeRainVolume(RainBounds, RainNiagara);
		DynamicWetSource->ApplyRainNiagaraParameters();
	}
}
