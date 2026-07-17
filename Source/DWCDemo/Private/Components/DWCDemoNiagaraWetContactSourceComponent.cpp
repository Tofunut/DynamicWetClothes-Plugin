#include "Components/DWCDemoNiagaraWetContactSourceComponent.h"

#include "Components/SkeletalMeshComponent.h"
#include "Components/DynamicWetClothesComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
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
        UWorld&                                         World,
        const UDWCDemoNiagaraWetContactSourceComponent& Source,
        const USkeletalMeshComponent&                   HitSkeletalMesh,
        const FDWCWetContact&                           Contact,
        TArray<FName>&                                  InOutBoneNames)
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

        FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(DWCDemoNiagaraWetContactSourceBoneSweep), false);
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
                Source.TraceChannel,
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

UDWCDemoNiagaraWetContactSourceComponent::UDWCDemoNiagaraWetContactSourceComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UDWCDemoNiagaraWetContactSourceComponent::BeginPlay()
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

void UDWCDemoNiagaraWetContactSourceComponent::ReceiveParticleData_Implementation(
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

    if (FMath::IsNearlyZero(ContactAmount) || Data.IsEmpty())
    {
        if (ShouldLogDebug())
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("DWC Demo Niagara Wet Contact Source: skipped callback owner=%s particles=%d contactAmount=%.3f"),
                *GetNameSafe(GetOwner()),
                Data.Num(),
                ContactAmount);
        }
        return;
    }

    const int32 MaxParticles = FMath::Max(0, MaxParticlesPerCallback);
    const int32 ParticleCount = MaxParticles > 0 ? FMath::Min(Data.Num(), MaxParticles) : Data.Num();

    TArray<FDWCWetContact>                                                    ParticleContacts;
    TMap<TWeakObjectPtr<UDynamicWetClothesComponent>, TArray<FDWCWetContact>> ContactsByReceiver;
    int32                                                                     RejectedParticles = 0;
    int32                                                                     BuiltContacts = 0;

    for (int32 ParticleIndex = 0; ParticleIndex < ParticleCount; ++ParticleIndex)
    {
        UDynamicWetClothesComponent* ParticleReceiver = TargetReceiver;
        if (BuildContactsFromParticle(Data[ParticleIndex], SimulationPositionOffset, ParticleReceiver, ParticleContacts) &&
            IsValid(ParticleReceiver))
        {
            TArray<FDWCWetContact>& ContactsToApply = ContactsByReceiver.FindOrAdd(ParticleReceiver);
            ContactsToApply.Append(ParticleContacts);
            BuiltContacts += ParticleContacts.Num();
        }
        else
        {
            ++RejectedParticles;
        }
    }

    int32 AppliedReceiverCount = 0;
    int32 AppliedContactCount = 0;
    for (TPair<TWeakObjectPtr<UDynamicWetClothesComponent>, TArray<FDWCWetContact>>& Pair : ContactsByReceiver)
    {
        UDynamicWetClothesComponent* ReceiverToApply = Pair.Key.Get();
        if (IsValid(ReceiverToApply) && !Pair.Value.IsEmpty())
        {
            if (ReceiverToApply->ApplyWetContacts(Pair.Value, true))
            {
                ++AppliedReceiverCount;
                AppliedContactCount += Pair.Value.Num();
            }
        }
    }

    if (ShouldLogDebug())
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("DWC Demo Niagara Wet Contact Source: callback owner=%s particles=%d used=%d rejected=%d builtContacts=%d receiverGroups=%d appliedReceivers=%d appliedContacts=%d explicitReceiver=%s"),
            *GetNameSafe(GetOwner()),
            Data.Num(),
            ParticleCount,
            RejectedParticles,
            BuiltContacts,
            ContactsByReceiver.Num(),
            AppliedReceiverCount,
            AppliedContactCount,
            *GetNameSafe(TargetReceiver));
    }
}

UDynamicWetClothesComponent* UDWCDemoNiagaraWetContactSourceComponent::ResolveReceiver() const
{
    const AActor* Owner = GetOwner();
    return Owner ? Owner->FindComponentByClass<UDynamicWetClothesComponent>() : nullptr;
}

UNiagaraComponent* UDWCDemoNiagaraWetContactSourceComponent::ResolveNiagaraComponent() const
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

void UDWCDemoNiagaraWetContactSourceComponent::BindCallbackUserParameter()
{
    if (!NiagaraComponent || CallbackUserParameterName.IsNone())
    {
        if (bEnableDebugLogging)
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("DWC Demo Niagara Wet Contact Source: cannot bind callback owner=%s niagara=%s callbackParam=%s"),
                *GetNameSafe(GetOwner()),
                *GetNameSafe(NiagaraComponent.Get()),
                *CallbackUserParameterName.ToString());
        }
        return;
    }

    NiagaraComponent->SetVariableObject(CallbackUserParameterName, this);
}

bool UDWCDemoNiagaraWetContactSourceComponent::ShouldLogDebug() const
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

UDynamicWetClothesComponent* UDWCDemoNiagaraWetContactSourceComponent::FindNearestReceiver(
    const FVector& Location,
    const float    MaxDistance) const
{
    UWorld* World = GetWorld();
    if (!World)
    {
        return nullptr;
    }

    UDynamicWetClothesComponent* BestReceiver = nullptr;
    double                       BestDistanceSq = FMath::Square(FMath::Max(0.0f, MaxDistance));

    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* CandidateActor = *It;
        if (!IsValid(CandidateActor) || CandidateActor == GetOwner())
        {
            continue;
        }

        UDynamicWetClothesComponent* CandidateReceiver =
            CandidateActor->FindComponentByClass<UDynamicWetClothesComponent>();
        if (!IsValid(CandidateReceiver))
        {
            continue;
        }

        FBox WetBounds(ForceInit);
        if (!CandidateReceiver->GetWetnessWorldBounds(WetBounds) || !WetBounds.IsValid)
        {
            continue;
        }

        const double DistanceSq = WetBounds.ComputeSquaredDistanceToPoint(Location);
        if (DistanceSq <= BestDistanceSq)
        {
            BestDistanceSq = DistanceSq;
            BestReceiver = CandidateReceiver;
        }
    }

    return BestReceiver;
}

bool UDWCDemoNiagaraWetContactSourceComponent::BuildContactsFromParticle(
    const FBasicParticleData&     Particle,
    const FVector&                SimulationPositionOffset,
    UDynamicWetClothesComponent*& InOutReceiver,
    TArray<FDWCWetContact>&       OutContacts) const
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

    FDWCWetContact Contact;
    Contact.Amount = ContactAmount;
    Contact.Location = ParticlePosition;
    Contact.Direction = ContactDirection;
    Contact.Radius =
        bUseParticleSizeAsRadius
            ? FMath::Max(ContactRadius, Particle.Size * ParticleSizeRadiusScale)
            : ContactRadius;
    Contact.Normal = FVector::UpVector;
    Contact.BoneName = NAME_None;

    // Without a trace there is intentionally no synthetic closest-bone lookup.
    // WetInputStage will log and perform a full-vertex fallback for NAME_None.
    if (!bTraceForContactData)
    {
        if (!IsValid(InOutReceiver))
        {
            return false;
        }

        OutContacts.Add(Contact);
        return true;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        if (!IsValid(InOutReceiver))
        {
            return false;
        }

        OutContacts.Add(Contact);
        return true;
    }

    const FVector TraceStart = ParticlePosition - ContactDirection * TraceDistance;
    const FVector TraceEnd = ParticlePosition + ContactDirection * TraceDistance;

    USkeletalMeshComponent* ReceiverSkeletalMesh =
        IsValid(InOutReceiver) ? InOutReceiver->TargetSkeletalMesh.Get() : nullptr;

    FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(DWCDemoNiagaraWetContactSource), bTraceComplex);
    const AActor* SourceOwner = GetOwner();
    const AActor* ReceiverOwner = ReceiverSkeletalMesh ? ReceiverSkeletalMesh->GetOwner() : nullptr;
    if (SourceOwner && SourceOwner != ReceiverOwner)
    {
        QueryParams.AddIgnoredActor(SourceOwner);
    }

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
                USkeletalMeshComponent* CandidateSkeletalMesh =
                    Cast<USkeletalMeshComponent>(TraceHit.GetComponent());
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
        }
    }

    if (bHit)
    {
        if (!IsValid(InOutReceiver) && HitSkeletalMesh)
        {
            if (AActor* HitOwner = HitSkeletalMesh->GetOwner())
            {
                InOutReceiver = HitOwner->FindComponentByClass<UDynamicWetClothesComponent>();
            }
        }

        if (!IsValid(InOutReceiver))
        {
            return false;
        }

        Contact.Location = Hit.ImpactPoint.IsNearlyZero() ? Hit.Location : Hit.ImpactPoint;
        Contact.Normal = Hit.ImpactNormal.IsNearlyZero() ? Contact.Normal : Hit.ImpactNormal;
        Contact.BoneName = Hit.BoneName;

        if (Contact.BoneName.IsNone() && HitSkeletalMesh)
        {
            Contact.BoneName = HitSkeletalMesh->FindClosestBone(Contact.Location);
        }

        TArray<FName> CandidateBoneNames;
        AddUniqueBoneName(CandidateBoneNames, Contact.BoneName);

        if (HitSkeletalMesh)
        {
            CollectSphereSweepBoneNames(*World, *this, *HitSkeletalMesh, Contact, CandidateBoneNames);
        }

        if (CandidateBoneNames.IsEmpty())
        {
            OutContacts.Add(Contact);
        }
        else
        {
            for (const FName CandidateBoneName : CandidateBoneNames)
            {
                FDWCWetContact CandidateContact = Contact;
                CandidateContact.BoneName = CandidateBoneName;
                OutContacts.Add(CandidateContact);
            }
        }
        return true;
    }

    if (!IsValid(InOutReceiver))
    {
        const float FallbackDistance = FMath::Max(TraceDistance + ContactRadius, ContactRadius * 2.0f);
        InOutReceiver = FindNearestReceiver(Contact.Location, FallbackDistance);
    }

    if (!IsValid(InOutReceiver))
    {
        return false;
    }

    // Trace miss or missing HitBone uses the defined full-vertex fallback path.
    OutContacts.Add(Contact);
    return true;
}
