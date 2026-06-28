#include "Demo/Volumes/WaterVolume.h"

#include "Components/BoxComponent.h"
#include "DynamicWet/DynamicWetSourceComponent.h"

AWaterVolume::AWaterVolume()
{
	PrimaryActorTick.bCanEverTick = false;

	VolumeBox = CreateDefaultSubobject<UBoxComponent>(TEXT("VolumeBox"));
	SetRootComponent(VolumeBox);

	VolumeBox->SetBoxExtent(FVector(200.0f, 200.0f, 100.0f));
	VolumeBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	VolumeBox->SetCollisionObjectType(ECC_WorldDynamic);
	VolumeBox->SetCollisionResponseToAllChannels(ECR_Ignore);
	VolumeBox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	VolumeBox->SetGenerateOverlapEvents(true);

	DynamicWetSource = CreateDefaultSubobject<UDynamicWetSourceComponent>(TEXT("DynamicWetSource"));
}

void AWaterVolume::BeginPlay()
{
	Super::BeginPlay();

	if (DynamicWetSource)
	{
		DynamicWetSource->InitializeWaterVolume(VolumeBox);
	}
}

void AWaterVolume::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	if (DynamicWetSource)
	{
		DynamicWetSource->InitializeWaterVolume(VolumeBox);
	}
}
