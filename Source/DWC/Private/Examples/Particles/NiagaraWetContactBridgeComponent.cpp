#include "Examples/Particles/NiagaraWetContactBridgeComponent.h"

#include "Components/SkeletalMeshComponent.h"
#include "Components/DynamicWetClothesComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "NiagaraComponent.h"

namespace
{
    void AddUniqueBoneName(TArray<FName>& BoneNames, const FName BoneName)
    {
        if (!BoneName.IsNone())
        {
            BoneNames.AddUnique(BoneName);
        }
    }

    void CollectSphereSweepBoneNames(
        UWorld&                                  World,
        const UNiagaraWetContactBridgeComponent& Bridge,
        const USkeletalMeshComponent&            HitSkeletalMesh,
        const FDWCWetContact&                    Contact,
        TArray<FName>&                           InOutBoneNames)
    {
        if (Contact.Radius <= 0.0f)
        {
            return;
        }

        FVector SweepNormal = Contact.Normal.GetSafeNormal();
        if (SweepNormal.IsNearlyZero())
        {
            SweepNormal = FVector::UpVector;
        }

        FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(NiagaraWetContactBridgeBoneSweep), false);
        QueryParams.bReturnPhysicalMaterial = false;

        const float           SweepHalfDistance = FMath::Max(1.0f, Contact.Radius * 0.1f);
        const FVector         SweepStart = Contact.Location - SweepNormal * SweepHalfDistance;
        const FVector         SweepEnd = Contact.Location + SweepNormal * SweepHalfDistance;
        const FCollisionShape SweepShape = FCollisionShape::MakeSphere(Contact.Radius);

        TArray<FHitResult> SweepHits;
        if (!World.SweepMultiByChannel(
                SweepHits,
                SweepStart,
                SweepEnd,
                FQuat::Identity,
                Bridge.TraceChannel,
                SweepShape,
                QueryParams))
        {
            return;
        }

        for (const FHitResult& SweepHit : SweepHits)
        {
            if (SweepHit.GetComponent() != &HitSkeletalMesh)
            {
                continue;
            }

            FName BoneName = SweepHit.BoneName;
            if (BoneName.IsNone())
            {
                const FVector QueryLocation = SweepHit.ImpactPoint.IsNearlyZero() ? Contact.Location : SweepHit.ImpactPoint;
                BoneName = HitSkeletalMesh.FindClosestBone(QueryLocation);
            }

            AddUniqueBoneName(InOutBoneNames, BoneName);
        }
    }
} // namespace

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
    UNiagaraSystem*,
    const FVector& SimulationPositionOffset)
{
    UDynamicWetClothesComponent* TargetReceiver = Receiver;
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

    TArray<FDWCWetContact> ParticleContacts;
    TArray<FDWCWetContact> ContactsToApply;
    ContactsToApply.Reserve(ParticleCount);

    for (int32 ParticleIndex = 0; ParticleIndex < ParticleCount; ++ParticleIndex)
    {
        if (BuildContactsFromParticle(Data[ParticleIndex], SimulationPositionOffset, ParticleContacts))
        {
            ContactsToApply.Append(ParticleContacts);
        }
    }

    if (!ContactsToApply.IsEmpty())
    {
        TargetReceiver->ApplyWetContacts(ContactsToApply, true);
    }
}

UDynamicWetClothesComponent* UNiagaraWetContactBridgeComponent::ResolveReceiver() const
{
    const AActor* Owner = GetOwner();
    return Owner ? Owner->FindComponentByClass<UDynamicWetClothesComponent>() : nullptr;
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

bool UNiagaraWetContactBridgeComponent::BuildContactsFromParticle(
    const FBasicParticleData& Particle,
    const FVector&            SimulationPositionOffset,
    TArray<FDWCWetContact>&   OutContacts) const
{
    OutContacts.Reset();

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

    FDWCWetContact BaseContact;
    BaseContact.Amount = ContactAmount;
    BaseContact.Location = ParticlePosition;
    BaseContact.Direction = ContactDirection;
    BaseContact.Radius =
        bUseParticleSizeAsRadius
            ? FMath::Max(ContactRadius, Particle.Size * ParticleSizeRadiusScale)
            : ContactRadius;
    BaseContact.Normal = FVector::UpVector;
    BaseContact.BoneName = NAME_None;

    const bool bShouldTraceForContactData = bTraceForWaterSurfaceData || bRequireBoneNameForContact;
    if (!bShouldTraceForContactData)
    {
        OutContacts.Add(BaseContact);
        return true;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        if (bRequireBoneNameForContact)
        {
            return false;
        }

        OutContacts.Add(BaseContact);
        return true;
    }

    FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(NiagaraWetContactBridge), bTraceComplex);
    QueryParams.AddIgnoredActor(GetOwner());

    const FVector TraceStart = ParticlePosition - ContactDirection * TraceDistance;
    const FVector TraceEnd = ParticlePosition + ContactDirection * TraceDistance;

    USkeletalMeshComponent* ReceiverSkeletalMesh =
        Receiver ? Receiver->TargetSkeletalMesh.Get() : nullptr;

    FHitResult              Hit;
    USkeletalMeshComponent* HitSkeletalMesh = nullptr;
    bool                    bHit = false;

    if (ReceiverSkeletalMesh)
    {
        FHitResult ComponentHit;
        if (ReceiverSkeletalMesh->LineTraceComponent(ComponentHit, TraceStart, TraceEnd, QueryParams))
        {
            Hit = ComponentHit;
            HitSkeletalMesh = ReceiverSkeletalMesh;
            bHit = true;
        }
    }

    if (!bHit)
    {
        TArray<FHitResult> TraceHits;
        if (World->LineTraceMultiByChannel(TraceHits, TraceStart, TraceEnd, TraceChannel, QueryParams))
        {
            for (const FHitResult& TraceHit : TraceHits)
            {
                USkeletalMeshComponent* CandidateSkeletalMesh = Cast<USkeletalMeshComponent>(TraceHit.GetComponent());
                if (!CandidateSkeletalMesh)
                {
                    continue;
                }

                if (ReceiverSkeletalMesh && CandidateSkeletalMesh != ReceiverSkeletalMesh)
                {
                    continue;
                }

                Hit = TraceHit;
                HitSkeletalMesh = CandidateSkeletalMesh;
                bHit = true;
                break;
            }

            if (!bHit && !bRequireBoneNameForContact)
            {
                Hit = TraceHits.Last();
                bHit = true;
            }
        }
    }

    if (bHit)
    {
        BaseContact.Location = Hit.ImpactPoint.IsNearlyZero() ? Hit.Location : Hit.ImpactPoint;
        BaseContact.Normal = Hit.ImpactNormal.IsNearlyZero() ? BaseContact.Normal : Hit.ImpactNormal;
        BaseContact.BoneName = Hit.BoneName;

        if (BaseContact.BoneName.IsNone())
        {
            if (HitSkeletalMesh)
            {
                BaseContact.BoneName = HitSkeletalMesh->FindClosestBone(BaseContact.Location);
            }
        }

        TArray<FName> CandidateBoneNames;
        AddUniqueBoneName(CandidateBoneNames, BaseContact.BoneName);

        if (HitSkeletalMesh)
        {
            CollectSphereSweepBoneNames(*World, *this, *HitSkeletalMesh, BaseContact, CandidateBoneNames);
        }

        for (const FName CandidateBoneName : CandidateBoneNames)
        {
            FDWCWetContact Contact = BaseContact;
            Contact.BoneName = CandidateBoneName;
            OutContacts.Add(Contact);
        }
    }

    if (OutContacts.IsEmpty() && bRequireBoneNameForContact && ReceiverSkeletalMesh)
    {
        BaseContact.BoneName = ReceiverSkeletalMesh->FindClosestBone(BaseContact.Location);
        if (!BaseContact.BoneName.IsNone())
        {
            OutContacts.Add(BaseContact);
        }
    }

    if (OutContacts.IsEmpty() && !bRequireBoneNameForContact)
    {
        OutContacts.Add(BaseContact);
    }

    if (bRequireBoneNameForContact && OutContacts.IsEmpty())
    {
        return false;
    }

    return !OutContacts.IsEmpty();
}
