#include "Demo/Particles/NiagaraWetContactBridgeComponent.h"

#include "Components/SkeletalMeshComponent.h"
#include "DynamicWet/DynamicWetReceiverComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"
#include "NiagaraComponent.h"

namespace
{
static TAutoConsoleVariable<int32> CVarDynamicWetNiagaraContactProfile(
    TEXT("dwc.NiagaraWetContact.Profile"),
    0,
    TEXT("Logs DynamicWet Niagara wet contact callback stats. 0=off, 1=on."),
    ECVF_Default);
}

UNiagaraWetContactBridgeComponent::UNiagaraWetContactBridgeComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UNiagaraWetContactBridgeComponent::BeginPlay()
{
    Super::BeginPlay();

    if (!Receiver && bFindReceiverOnOwner)
    {
        Receiver = ResolveReceiver();
    }

    if (!NiagaraComponent && bFindNiagaraComponentOnOwner)
    {
        NiagaraComponent = ResolveNiagaraComponent();
    }

    if (bBindCallbackUserParameterOnBeginPlay)
    {
        BindCallbackUserParameter();
    }
}

void UNiagaraWetContactBridgeComponent::ReceiveParticleData_Implementation(
    const TArray<FBasicParticleData>& Data,
    UNiagaraSystem* NiagaraSystem,
    const FVector& SimulationPositionOffset)
{
    UDynamicWetReceiverComponent* TargetReceiver = Receiver;
    if (!TargetReceiver && bFindReceiverOnOwner)
    {
        TargetReceiver = ResolveReceiver();
        Receiver = TargetReceiver;
    }

    if (!TargetReceiver || FMath::IsNearlyZero(ContactAmount) || Data.IsEmpty())
    {
        return;
    }

    const bool bProfile = CVarDynamicWetNiagaraContactProfile.GetValueOnGameThread() != 0;
    const double StartSeconds = bProfile ? FPlatformTime::Seconds() : 0.0;
    const int32 MaxParticles = FMath::Max(0, MaxParticlesPerCallback);
    const int32 ParticleCount = MaxParticles > 0 ? FMath::Min(Data.Num(), MaxParticles) : Data.Num();

    int32 BuiltContactCount = 0;
    int32 EnqueuedContactCount = 0;
    const double BuildStartSeconds = bProfile ? FPlatformTime::Seconds() : 0.0;
    double EnqueueSeconds = 0.0;

    for (int32 ParticleIndex = 0; ParticleIndex < ParticleCount; ++ParticleIndex)
    {
        FDWCWetContact Contact;
        if (BuildContactFromParticle(Data[ParticleIndex], SimulationPositionOffset, Contact))
        {
            ++BuiltContactCount;
            const double EnqueueStartSeconds = bProfile ? FPlatformTime::Seconds() : 0.0;
            if (TargetReceiver->ApplyWetContact(Contact, true))
            {
                ++EnqueuedContactCount;
            }
            if (bProfile)
            {
                EnqueueSeconds += FPlatformTime::Seconds() - EnqueueStartSeconds;
            }
        }
    }

    if (bProfile)
    {
        const double EndSeconds = FPlatformTime::Seconds();
        UE_LOG(
            LogTemp,
            Log,
            TEXT("DWC NiagaraWetContact Profile: Owner=%s Niagara=%s Trace=%s IncomingParticles=%d ProcessedParticles=%d BuiltContacts=%d EnqueuedContacts=%d BuildAndTraceMs=%.3f EnqueueMs=%.3f TotalMs=%.3f"),
            *GetNameSafe(GetOwner()),
            *GetNameSafe(NiagaraSystem),
            bTraceForSurfaceData ? TEXT("true") : TEXT("false"),
            Data.Num(),
            ParticleCount,
            BuiltContactCount,
            EnqueuedContactCount,
            ((EndSeconds - BuildStartSeconds) - EnqueueSeconds) * 1000.0,
            EnqueueSeconds * 1000.0,
            (EndSeconds - StartSeconds) * 1000.0);
    }
}

UDynamicWetReceiverComponent* UNiagaraWetContactBridgeComponent::ResolveReceiver() const
{
    const AActor* Owner = GetOwner();
    return Owner ? Owner->FindComponentByClass<UDynamicWetReceiverComponent>() : nullptr;
}

UNiagaraComponent* UNiagaraWetContactBridgeComponent::ResolveNiagaraComponent() const
{
    const AActor* Owner = GetOwner();
    if (!Owner)
    {
        return nullptr;
    }

    TArray<UNiagaraComponent*> NiagaraComponents;
    Owner->GetComponents<UNiagaraComponent>(NiagaraComponents);
    for (UNiagaraComponent* Candidate : NiagaraComponents)
    {
        if (Candidate)
        {
            return Candidate;
        }
    }

    return nullptr;
}

void UNiagaraWetContactBridgeComponent::BindCallbackUserParameter()
{
    if (!NiagaraComponent || CallbackUserParameterName.IsNone())
    {
        return;
    }

    NiagaraComponent->SetVariableObject(CallbackUserParameterName, this);
}

bool UNiagaraWetContactBridgeComponent::BuildContactFromParticle(
    const FBasicParticleData& Particle,
    const FVector& SimulationPositionOffset,
    FDWCWetContact& OutContact) const
{
    const FVector ParticlePosition = FVector(Particle.Position) + SimulationPositionOffset;
    const FVector ParticleVelocity = FVector(Particle.Velocity);

    if (MinVelocityForContact > 0.0f &&
        ParticleVelocity.SizeSquared() < FMath::Square(MinVelocityForContact))
    {
        return false;
    }

    const FVector ContactDirection =
        ParticleVelocity.IsNearlyZero()
            ? FVector::DownVector
            : ParticleVelocity.GetSafeNormal();

    OutContact.Amount = ContactAmount;
    OutContact.Location = ParticlePosition;
    OutContact.Direction = ContactDirection;
    OutContact.Radius =
        bUseParticleSizeAsRadius
            ? FMath::Max(ContactRadius, Particle.Size * ParticleSizeRadiusScale)
            : ContactRadius;
    OutContact.Normal = FVector::UpVector;
    OutContact.BoneName = NAME_None;

    if (!bTraceForSurfaceData)
    {
        return !bRequireBoneNameForContact;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return !bRequireBoneNameForContact;
    }

    FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(NiagaraWetContactBridge), bTraceComplex);
    QueryParams.AddIgnoredActor(GetOwner());

    const FVector TraceStart = ParticlePosition - ContactDirection * TraceDistance;
    const FVector TraceEnd = ParticlePosition + ContactDirection * TraceDistance;

    FHitResult Hit;
    if (World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, TraceChannel, QueryParams))
    {
        OutContact.Location = Hit.ImpactPoint;
        OutContact.Normal = Hit.ImpactNormal;
        OutContact.BoneName = Hit.BoneName;

        if (OutContact.BoneName.IsNone())
        {
            if (const USkeletalMeshComponent* HitSkeletalMesh = Cast<USkeletalMeshComponent>(Hit.GetComponent()))
            {
                OutContact.BoneName = HitSkeletalMesh->FindClosestBone(Hit.ImpactPoint);
            }
        }
    }

    if (bRequireBoneNameForContact && OutContact.BoneName.IsNone())
    {
        return false;
    }

    return true;
}
