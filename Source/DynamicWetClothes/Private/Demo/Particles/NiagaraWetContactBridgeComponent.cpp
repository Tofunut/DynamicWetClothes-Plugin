#include "Demo/Particles/NiagaraWetContactBridgeComponent.h"

#include "DynamicWet/DynamicWetReceiverComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "NiagaraComponent.h"

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

    const int32 MaxParticles = FMath::Max(0, MaxParticlesPerCallback);
    const int32 ParticleCount = MaxParticles > 0 ? FMath::Min(Data.Num(), MaxParticles) : Data.Num();

    for (int32 ParticleIndex = 0; ParticleIndex < ParticleCount; ++ParticleIndex)
    {
        FDWCWetContact Contact;
        if (BuildContactFromParticle(Data[ParticleIndex], SimulationPositionOffset, Contact))
        {
            TargetReceiver->ApplyWetContact(Contact, true);
        }
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
        return true;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return true;
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
    }

    return true;
}
