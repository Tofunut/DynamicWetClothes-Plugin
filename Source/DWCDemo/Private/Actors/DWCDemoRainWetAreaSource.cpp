// Copyright 2026 Team Tofunut. All Rights Reserved.

// Fill out your copyright notice in the Description page of Project Settings.

#include "Actors/DWCDemoRainWetAreaSource.h"

#include "Components/BoxComponent.h"
#include "Components/DynamicWetClothesComponent.h"
#include "NiagaraComponent.h"
#include "Utility/DWCProfiling.h"
#include "WetInputSystem/WetContactTypes.h"

ADWCDemoRainWetAreaSource::ADWCDemoRainWetAreaSource()
{
    PrimaryActorTick.bCanEverTick = false;

    RainBounds = CreateDefaultSubobject<UBoxComponent>(TEXT("RainBounds"));
    SetRootComponent(RainBounds);

    RainBounds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    RainBounds->SetGenerateOverlapEvents(false);
    RainBounds->SetBoxExtent(FVector(500.0f, 500.0f, 500.0f));

    RainNiagara = CreateDefaultSubobject<UNiagaraComponent>(TEXT("RainNiagara"));
    RainNiagara->SetupAttachment(RootComponent);
}

void ADWCDemoRainWetAreaSource::BeginPlay()
{
    Super::BeginPlay();

    if (!RainBounds)
    {
        return;
    }

    for (AActor* ReceiverActor : InitialReceiverActors)
    {
        AddWetnessReceiverFromActor(ReceiverActor);
    }

    ApplyRainNiagaraParameters();

    if (bEnableDebugLogging)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("DWC Demo Rain Wet Area Source: BeginPlay actor=%s bounds=%s receivers=%d wetPerSecond=%.3f samplesPerSimulation=%d"),
            *GetNameSafe(this),
            RainBounds ? *RainBounds->GetScaledBoxExtent().ToString() : TEXT("None"),
            RegisteredReceivers.Num(),
            WetAmountPerSecond,
            RainSamplesPerTick);
    }
}

void ADWCDemoRainWetAreaSource::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    ClearWetnessReceivers();

    Super::EndPlay(EndPlayReason);
}

void ADWCDemoRainWetAreaSource::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);

    ApplyRainNiagaraParameters();
}

bool ADWCDemoRainWetAreaSource::AddWetnessReceiver(UDynamicWetClothesComponent* Receiver)
{
    if (!IsValid(Receiver))
    {
        return false;
    }

    if (RegisteredReceivers.Contains(Receiver))
    {
        return true;
    }

    if (!Receiver->RegisterPersistentWetnessProvider(this))
    {
        return false;
    }

    RegisteredReceivers.Add(Receiver);
    return true;
}

bool ADWCDemoRainWetAreaSource::AddWetnessReceiverFromActor(AActor* ReceiverActor)
{
    if (!IsValid(ReceiverActor) || ReceiverActor == this)
    {
        return false;
    }

    return AddWetnessReceiver(ReceiverActor->FindComponentByClass<UDynamicWetClothesComponent>());
}

void ADWCDemoRainWetAreaSource::RemoveWetnessReceiver(UDynamicWetClothesComponent* Receiver)
{
    if (!Receiver || RegisteredReceivers.Remove(Receiver) == 0)
    {
        return;
    }

    Receiver->UnregisterPersistentWetnessProvider(this);
}

void ADWCDemoRainWetAreaSource::ClearWetnessReceivers()
{
    const TArray<TWeakObjectPtr<UDynamicWetClothesComponent>> Receivers = RegisteredReceivers.Array();
    RegisteredReceivers.Reset();

    for (const TWeakObjectPtr<UDynamicWetClothesComponent>& WeakReceiver : Receivers)
    {
        if (UDynamicWetClothesComponent* Receiver = WeakReceiver.Get(); IsValid(Receiver))
        {
            Receiver->UnregisterPersistentWetnessProvider(this);
        }
    }
}

int32 ADWCDemoRainWetAreaSource::GetWetnessReceiverCount() const
{
    int32 ValidReceiverCount = 0;
    for (const TWeakObjectPtr<UDynamicWetClothesComponent>& WeakReceiver : RegisteredReceivers)
    {
        ValidReceiverCount += IsValid(WeakReceiver.Get()) ? 1 : 0;
    }
    return ValidReceiverCount;
}

void ADWCDemoRainWetAreaSource::ApplyPersistentWetness(
    UDynamicWetClothesComponent& Receiver,
    const float                  DeltaSeconds)
{
    DWC_PROFILE_SCOPE(DWC_DemoRain_ApplyPersistentWetness);

    if (!bApplyWetArea || !IsReceiverInsideRainBounds(Receiver))
    {
        return;
    }

    ApplyRainToReceiver(Receiver, DeltaSeconds);
}

bool ADWCDemoRainWetAreaSource::IsReceiverInsideRainBounds(const UDynamicWetClothesComponent& Receiver) const
{
    if (!RainBounds)
    {
        return false;
    }

    FBox WetBounds(ForceInit);
    if (!Receiver.GetWetnessWorldBounds(WetBounds) || !WetBounds.IsValid)
    {
        return false;
    }

    return RainBounds->Bounds.GetBox().Intersect(WetBounds);
}

FVector ADWCDemoRainWetAreaSource::GetRainDirectionWorld() const
{
    if (RainDirection.IsNearlyZero())
    {
        return FVector::DownVector;
    }

    return GetActorTransform().TransformVectorNoScale(RainDirection).GetSafeNormal();
}

void ADWCDemoRainWetAreaSource::BuildRainWetAreaData(
    FDWCWetAreaData& OutAreaData,
    const float      DeltaSeconds) const
{
    OutAreaData.Amount = WetAmountPerSecond * FMath::Max(0.0f, DeltaSeconds);
    OutAreaData.Direction = GetRainDirectionWorld();
    OutAreaData.SampleCount = RainSamplesPerTick;
    OutAreaData.bUseNormalExposure = bUseNormalExposure;
    OutAreaData.bUseSkinnedNormalsForExposure = bUseSkinnedNormalsForExposure;
}

void ADWCDemoRainWetAreaSource::ApplyRainToReceiver(
    UDynamicWetClothesComponent& Receiver,
    const float                  DeltaSeconds) const
{
    DWC_PROFILE_SCOPE(DWC_DemoRain_ApplyRainToReceiver);

    if (!RainBounds || WetAmountPerSecond <= 0.0f || DeltaSeconds <= 0.0f)
    {
        return;
    }

    FDWCWetAreaData AreaData;
    BuildRainWetAreaData(AreaData, DeltaSeconds);

    Receiver.ApplyWetArea(AreaData, true);
}

void ADWCDemoRainWetAreaSource::ApplyRainNiagaraParameters() const
{
    if (!IsValid(RainNiagara) || !IsValid(RainBounds))
    {
        return;
    }

    FDWCWetAreaData AreaData;
    BuildRainWetAreaData(AreaData, 0.0f);

    RainNiagara->SetVariableVec3(RainDirectionParameterName, AreaData.Direction);
    RainNiagara->SetVariableVec3(RainBoundsExtentParameterName, RainBounds->GetScaledBoxExtent() * 2.0f);
    RainNiagara->SetVariableFloat(RainIntensityParameterName, WetAmountPerSecond);
}

