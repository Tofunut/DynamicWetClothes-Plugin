// Fill out your copyright notice in the Description page of Project Settings.

#include "Demo/Volumes/RainVolume.h"

#include "Components/BoxComponent.h"
#include "DynamicWet/DynamicWetReceiverComponent.h"
#include "Engine/World.h"
#include "NiagaraComponent.h"
#include "TimerManager.h"

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

}

void ARainVolume::BeginPlay()
{
    Super::BeginPlay();

    if (!RainBounds)
    {
        return;
    }

    RainBounds->OnComponentBeginOverlap.AddUniqueDynamic(
        this,
        &ARainVolume::OnRainBeginOverlap);

    RainBounds->OnComponentEndOverlap.AddUniqueDynamic(
        this,
        &ARainVolume::OnRainEndOverlap);

    RefreshExistingOverlaps();

    if (GetWorld() && UpdateInterval > 0.0f)
    {
        GetWorldTimerManager().SetTimer(
            WetnessTimer,
            this,
            &ARainVolume::ApplyWetnessTick,
            UpdateInterval,
            true);
    }

    ApplyRainNiagaraParameters();
}

void ARainVolume::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (GetWorld())
    {
        GetWorldTimerManager().ClearTimer(WetnessTimer);
    }

    if (RainBounds)
    {
        RainBounds->OnComponentBeginOverlap.RemoveDynamic(
            this,
            &ARainVolume::OnRainBeginOverlap);

        RainBounds->OnComponentEndOverlap.RemoveDynamic(
            this,
            &ARainVolume::OnRainEndOverlap);
    }

    ReceiverOverlapCounts.Reset();

    Super::EndPlay(EndPlayReason);
}

void ARainVolume::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);

    ApplyRainNiagaraParameters();
}

void ARainVolume::RefreshExistingOverlaps()
{
    if (!RainBounds)
    {
        return;
    }

    RainBounds->UpdateOverlaps();

    TArray<AActor*> OverlappingActors;
    RainBounds->GetOverlappingActors(OverlappingActors);
    for (AActor* OverlappingActor : OverlappingActors)
    {
        AddReceiverFromActor(OverlappingActor);
    }
}

void ARainVolume::ApplyWetnessTick()
{
    if (ReceiverOverlapCounts.Num() == 0)
    {
        return;
    }

    for (auto It = ReceiverOverlapCounts.CreateIterator(); It; ++It)
    {
        UDynamicWetReceiverComponent* Receiver = It.Key().Get();
        if (!IsValid(Receiver) || It.Value() <= 0)
        {
            It.RemoveCurrent();
            continue;
        }

        ApplyWetRainToReceiver(*Receiver);
    }
}

void ARainVolume::AddReceiverFromActor(AActor* OtherActor)
{
    if (!IsValid(OtherActor) || OtherActor == this)
    {
        return;
    }

    UDynamicWetReceiverComponent* Receiver = OtherActor->FindComponentByClass<UDynamicWetReceiverComponent>();
    if (!IsValid(Receiver))
    {
        return;
    }

    int32& OverlapCount = ReceiverOverlapCounts.FindOrAdd(Receiver);
    ++OverlapCount;
}

void ARainVolume::RemoveReceiverFromActor(AActor* OtherActor)
{
    if (!IsValid(OtherActor))
    {
        return;
    }

    UDynamicWetReceiverComponent* Receiver = OtherActor->FindComponentByClass<UDynamicWetReceiverComponent>();
    if (!IsValid(Receiver))
    {
        return;
    }

    int32* OverlapCount = ReceiverOverlapCounts.Find(Receiver);
    if (!OverlapCount)
    {
        return;
    }

    --(*OverlapCount);
    if (*OverlapCount <= 0)
    {
        ReceiverOverlapCounts.Remove(Receiver);
    }
}

void ARainVolume::ApplyWetRainToReceiver(UDynamicWetReceiverComponent& Receiver) const
{
    if (!RainBounds || WetAmountPerSecond <= 0.0f || UpdateInterval <= 0.0f)
    {
        return;
    }

    const FVector SafeRainDirection =
        RainDirection.IsNearlyZero()
            ? FVector::DownVector
            : GetActorTransform().TransformVectorNoScale(RainDirection).GetSafeNormal();

    FDWCWetRainData RainData;
    RainData.Amount = WetAmountPerSecond * UpdateInterval;
    RainData.Direction = SafeRainDirection;
    RainData.SampleCount = RainSamplesPerTick;
    RainData.bUseNormalExposure = bUseNormalExposure;
    RainData.bUseSkinnedNormalsForExposure = bUseSkinnedNormalsForExposure;

    Receiver.ApplyWetRain(RainData, false);
}

void ARainVolume::ApplyRainNiagaraParameters() const
{
    if (!IsValid(RainNiagara) || !IsValid(RainBounds))
    {
        return;
    }

    RainNiagara->SetVariableVec3(RainDirectionParameterName, RainDirection);
    RainNiagara->SetVariableVec3(RainBoundsExtentParameterName, RainBounds->GetScaledBoxExtent() * 2.0f);
    RainNiagara->SetVariableFloat(RainIntensityParameterName, WetAmountPerSecond);
}

void ARainVolume::OnRainBeginOverlap(
    UPrimitiveComponent* OverlappedComponent,
    AActor*              OtherActor,
    UPrimitiveComponent* OtherComp,
    int32                OtherBodyIndex,
    bool                 bFromSweep,
    const FHitResult&    SweepResult)
{
    AddReceiverFromActor(OtherActor);
}

void ARainVolume::OnRainEndOverlap(
    UPrimitiveComponent* OverlappedComponent,
    AActor*              OtherActor,
    UPrimitiveComponent* OtherComp,
    int32                OtherBodyIndex)
{
    RemoveReceiverFromActor(OtherActor);
}
