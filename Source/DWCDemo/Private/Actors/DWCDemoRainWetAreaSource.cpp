// Fill out your copyright notice in the Description page of Project Settings.

#include "Actors/DWCDemoRainWetAreaSource.h"

#include "Components/BoxComponent.h"
#include "Components/DynamicWetClothesComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "NiagaraComponent.h"
#include "TimerManager.h"

ADWCDemoRainWetAreaSource::ADWCDemoRainWetAreaSource()
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

void ADWCDemoRainWetAreaSource::BeginPlay()
{
    Super::BeginPlay();

    if (!RainBounds)
    {
        return;
    }

    RainBounds->OnComponentBeginOverlap.AddUniqueDynamic(
        this,
        &ADWCDemoRainWetAreaSource::OnRainBeginOverlap);

    RainBounds->OnComponentEndOverlap.AddUniqueDynamic(
        this,
        &ADWCDemoRainWetAreaSource::OnRainEndOverlap);

    RefreshExistingOverlaps();
    RefreshReceiversInsideBounds();

    if (GetWorld() && UpdateInterval > 0.0f)
    {
        GetWorldTimerManager().SetTimer(
            WetnessTimer,
            this,
            &ADWCDemoRainWetAreaSource::ApplyWetnessTick,
            UpdateInterval,
            true);
    }

    ApplyRainNiagaraParameters();

    if (bEnableDebugLogging)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("DWC Demo Rain Wet Area Source: BeginPlay actor=%s bounds=%s receivers=%d wetPerSecond=%.3f interval=%.3f samples=%d"),
            *GetNameSafe(this),
            RainBounds ? *RainBounds->GetScaledBoxExtent().ToString() : TEXT("None"),
            ReceiverOverlapCounts.Num(),
            WetAmountPerSecond,
            UpdateInterval,
            RainSamplesPerTick);
    }
}

void ADWCDemoRainWetAreaSource::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (GetWorld())
    {
        GetWorldTimerManager().ClearTimer(WetnessTimer);
    }

    if (RainBounds)
    {
        RainBounds->OnComponentBeginOverlap.RemoveDynamic(
            this,
            &ADWCDemoRainWetAreaSource::OnRainBeginOverlap);

        RainBounds->OnComponentEndOverlap.RemoveDynamic(
            this,
            &ADWCDemoRainWetAreaSource::OnRainEndOverlap);
    }

    ReceiverOverlapCounts.Reset();

    Super::EndPlay(EndPlayReason);
}

void ADWCDemoRainWetAreaSource::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);

    ApplyRainNiagaraParameters();
}

void ADWCDemoRainWetAreaSource::RefreshExistingOverlaps()
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

void ADWCDemoRainWetAreaSource::RefreshReceiversInsideBounds()
{
    UWorld* World = GetWorld();
    if (!World || !RainBounds)
    {
        return;
    }

    const FBox RainWorldBounds = RainBounds->Bounds.GetBox();
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* CandidateActor = *It;
        if (!IsValid(CandidateActor) || CandidateActor == this)
        {
            continue;
        }

        UDynamicWetClothesComponent* Receiver =
            CandidateActor->FindComponentByClass<UDynamicWetClothesComponent>();
        if (!IsValid(Receiver))
        {
            continue;
        }

        FBox WetBounds(ForceInit);
        if (!Receiver->GetWetnessWorldBounds(WetBounds) || !WetBounds.IsValid)
        {
            continue;
        }

        if (RainWorldBounds.Intersect(WetBounds))
        {
            int32& OverlapCount = ReceiverOverlapCounts.FindOrAdd(Receiver);
            OverlapCount = FMath::Max(OverlapCount, 1);
        }
    }
}

void ADWCDemoRainWetAreaSource::ApplyWetnessTick()
{
    if (ReceiverOverlapCounts.Num() == 0)
    {
        RefreshExistingOverlaps();
        RefreshReceiversInsideBounds();
        if (ReceiverOverlapCounts.Num() == 0)
        {
            if (ShouldLogDebug())
            {
                UE_LOG(
                    LogTemp,
                    Warning,
                    TEXT("DWC Demo Rain Wet Area Source: no wet receivers actor=%s bounds=%s"),
                    *GetNameSafe(this),
                    RainBounds ? *RainBounds->Bounds.GetBox().ToString() : TEXT("None"));
            }
            return;
        }
    }

    for (auto It = ReceiverOverlapCounts.CreateIterator(); It; ++It)
    {
        UDynamicWetClothesComponent* Receiver = It.Key().Get();
        if (!IsValid(Receiver) || It.Value() <= 0)
        {
            It.RemoveCurrent();
            continue;
        }

        ApplyRainToReceiver(*Receiver);
    }
}

void ADWCDemoRainWetAreaSource::AddReceiverFromActor(AActor* OtherActor)
{
    if (!IsValid(OtherActor) || OtherActor == this)
    {
        return;
    }

    UDynamicWetClothesComponent* Receiver = OtherActor->FindComponentByClass<UDynamicWetClothesComponent>();
    if (!IsValid(Receiver))
    {
        return;
    }

    int32& OverlapCount = ReceiverOverlapCounts.FindOrAdd(Receiver);
    ++OverlapCount;
}

void ADWCDemoRainWetAreaSource::RemoveReceiverFromActor(AActor* OtherActor)
{
    if (!IsValid(OtherActor))
    {
        return;
    }

    UDynamicWetClothesComponent* Receiver = OtherActor->FindComponentByClass<UDynamicWetClothesComponent>();
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

void ADWCDemoRainWetAreaSource::ApplyRainToReceiver(UDynamicWetClothesComponent& Receiver) const
{
    if (!RainBounds || WetAmountPerSecond <= 0.0f || UpdateInterval <= 0.0f)
    {
        if (ShouldLogDebug())
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("DWC Demo Rain Wet Area Source: skipped apply actor=%s receiver=%s bounds=%s wetPerSecond=%.3f interval=%.3f"),
                *GetNameSafe(this),
                *GetNameSafe(&Receiver),
                RainBounds ? TEXT("valid") : TEXT("null"),
                WetAmountPerSecond,
                UpdateInterval);
        }
        return;
    }

    const FVector SafeRainDirection =
        RainDirection.IsNearlyZero()
            ? FVector::DownVector
            : GetActorTransform().TransformVectorNoScale(RainDirection).GetSafeNormal();

    FDWCWetAreaData AreaData;
    AreaData.Amount = WetAmountPerSecond * UpdateInterval;
    AreaData.Direction = SafeRainDirection;
    AreaData.SampleCount = RainSamplesPerTick;
    AreaData.bUseNormalExposure = bUseNormalExposure;
    AreaData.bUseSkinnedNormalsForExposure = bUseSkinnedNormalsForExposure;

    const bool bChanged = Receiver.ApplyWetArea(AreaData, false);
    if (ShouldLogDebug())
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("DWC Demo Rain Wet Area Source: apply actor=%s receiver=%s changed=%s amount=%.3f samples=%d receivers=%d"),
            *GetNameSafe(this),
            *GetNameSafe(&Receiver),
            bChanged ? TEXT("true") : TEXT("false"),
            AreaData.Amount,
            AreaData.SampleCount,
            ReceiverOverlapCounts.Num());
    }
}

void ADWCDemoRainWetAreaSource::ApplyRainNiagaraParameters() const
{
    if (!IsValid(RainNiagara) || !IsValid(RainBounds))
    {
        return;
    }

    RainNiagara->SetVariableVec3(RainDirectionParameterName, RainDirection);
    RainNiagara->SetVariableVec3(RainBoundsExtentParameterName, RainBounds->GetScaledBoxExtent() * 2.0f);
    RainNiagara->SetVariableFloat(RainIntensityParameterName, WetAmountPerSecond);
}

void ADWCDemoRainWetAreaSource::OnRainBeginOverlap(
    UPrimitiveComponent* OverlappedComponent,
    AActor*              OtherActor,
    UPrimitiveComponent* OtherComp,
    int32                OtherBodyIndex,
    bool                 bFromSweep,
    const FHitResult&    SweepResult)
{
    AddReceiverFromActor(OtherActor);
}

void ADWCDemoRainWetAreaSource::OnRainEndOverlap(
    UPrimitiveComponent* OverlappedComponent,
    AActor*              OtherActor,
    UPrimitiveComponent* OtherComp,
    int32                OtherBodyIndex)
{
    RemoveReceiverFromActor(OtherActor);
}

bool ADWCDemoRainWetAreaSource::ShouldLogDebug() const
{
    if (!bEnableDebugLogging)
    {
        return false;
    }

    const UWorld* World = GetWorld();
    const double  Now = World ? World->GetTimeSeconds() : FPlatformTime::Seconds();
    if (Now - LastDebugLogTime < FMath::Max(0.1f, DebugLogInterval))
    {
        return false;
    }

    LastDebugLogTime = Now;
    return true;
}
