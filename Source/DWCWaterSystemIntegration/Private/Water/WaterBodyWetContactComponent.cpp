// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "Water/WaterBodyWetContactComponent.h"

#include "Components/BoxComponent.h"
#include "Components/PrimitiveComponent.h"
#include "WetInputSystem/WetContactTypes.h"
#include "Components/DynamicWetClothesComponent.h"
#include "WaterBodyComponent.h"
#include "WaterBodyTypes.h"
#include "Utility/DWCProfiling.h"

UWaterBodyWetContactComponent::UWaterBodyWetContactComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
}

void UWaterBodyWetContactComponent::BeginPlay()
{
    Super::BeginPlay();

    InitializeWaterBody();
    CreateOverlapProxy();

    if (OverlapProxy)
    {
        OverlapProxy->OnComponentBeginOverlap.AddUniqueDynamic(
            this,
            &UWaterBodyWetContactComponent::OnProxyBeginOverlap);

        OverlapProxy->OnComponentEndOverlap.AddUniqueDynamic(
            this,
            &UWaterBodyWetContactComponent::OnProxyEndOverlap);

        if (bApplyToExistingOverlapsOnBeginPlay)
        {
            RefreshExistingOverlaps();
        }

    }

}

void UWaterBodyWetContactComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (OverlapProxy)
    {
        OverlapProxy->OnComponentBeginOverlap.RemoveDynamic(
            this,
            &UWaterBodyWetContactComponent::OnProxyBeginOverlap);

        OverlapProxy->OnComponentEndOverlap.RemoveDynamic(
            this,
            &UWaterBodyWetContactComponent::OnProxyEndOverlap);
    }

    UnregisterFromAllReceivers();
    DestroyOverlapProxy();

    Super::EndPlay(EndPlayReason);
}

void UWaterBodyWetContactComponent::InitializeWaterBody()
{
    AActor* Owner = GetOwner();
    WaterBodyComponent = IsValid(Owner) ? Owner->FindComponentByClass<UWaterBodyComponent>() : nullptr;

}

void UWaterBodyWetContactComponent::CreateOverlapProxy()
{
    if (!bCreateOverlapProxy || IsValid(OverlapProxy) || !IsValid(WaterBodyComponent))
    {
        return;
    }

    AActor* Owner = GetOwner();
    if (!IsValid(Owner) || !IsValid(Owner->GetRootComponent()))
    {
        return;
    }

    FBox WaterBodyBounds(ForceInit);
    if (!GetWaterBodyProxyBounds(WaterBodyBounds))
    {
        return;
    }

    const float DesiredTopZ = static_cast<float>(WaterBodyBounds.Max.Z + WaterBodyComponent->GetMaxWaveHeight());
    const float DesiredBottomZ = static_cast<float>(WaterBodyBounds.Min.Z);

    const FVector ProxyOrigin(
        (WaterBodyBounds.Min.X + WaterBodyBounds.Max.X) * 0.5f,
        (WaterBodyBounds.Min.Y + WaterBodyBounds.Max.Y) * 0.5f,
        (DesiredTopZ + DesiredBottomZ) * 0.5f);

    const FVector ProxyExtent(
        FMath::Max((WaterBodyBounds.Max.X - WaterBodyBounds.Min.X) * 0.5f, 1.0f),
        FMath::Max((WaterBodyBounds.Max.Y - WaterBodyBounds.Min.Y) * 0.5f, 1.0f),
        FMath::Max((DesiredTopZ - DesiredBottomZ) * 0.5f, 1.0f));

    OverlapProxy = NewObject<UBoxComponent>(Owner, TEXT("DynamicWetWaterOverlapProxy"));
    if (!IsValid(OverlapProxy))
    {
        return;
    }

    Owner->AddInstanceComponent(OverlapProxy);
    OverlapProxy->SetupAttachment(Owner->GetRootComponent());
    OverlapProxy->SetBoxExtent(ProxyExtent, false);
    OverlapProxy->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    OverlapProxy->SetCollisionObjectType(ECC_WorldDynamic);
    OverlapProxy->SetCollisionResponseToAllChannels(ECR_Overlap);
    OverlapProxy->SetGenerateOverlapEvents(true);
    OverlapProxy->RegisterComponent();
    OverlapProxy->SetWorldLocation(ProxyOrigin);
}

void UWaterBodyWetContactComponent::DestroyOverlapProxy()
{
    if (!IsValid(OverlapProxy))
    {
        return;
    }

    OverlapProxy->DestroyComponent();
    OverlapProxy = nullptr;
}

void UWaterBodyWetContactComponent::RefreshExistingOverlaps()
{
    if (!IsValid(OverlapProxy))
    {
        return;
    }

    OverlapProxy->UpdateOverlaps();

    TArray<AActor*> OverlappingActors;
    OverlapProxy->GetOverlappingActors(OverlappingActors);

    for (AActor* OverlappingActor : OverlappingActors)
    {
        AddReceiverFromActor(OverlappingActor);
    }
}

void UWaterBodyWetContactComponent::ApplyPersistentWetness(
    UDynamicWetClothesComponent& Receiver,
    const float                  /*DeltaSeconds*/)
{
    DWC_PROFILE_SCOPE(DWC_Water_ApplyPersistentWetness);

    if (!IsValid(WaterBodyComponent))
    {
        return;
    }

    constexpr float WetAmount = 1.0f;
    FDWCWaterSurfaceData WaterSurfaceData;
    if (BuildWaterSurfaceDataForReceiver(Receiver, WaterSurfaceData))
    {
        Receiver.ApplyWetSurface(WaterSurfaceData, WetAmount, false);
    }
}

void UWaterBodyWetContactComponent::AddReceiverFromActor(AActor* OtherActor)
{
    if (!IsValid(OtherActor) || OtherActor == GetOwner())
    {
        return;
    }

    UDynamicWetClothesComponent* Receiver = OtherActor->FindComponentByClass<UDynamicWetClothesComponent>();
    if (!IsValid(Receiver))
    {
        return;
    }

    const bool bWasUnregistered = !ReceiverOverlapCounts.Contains(Receiver);
    int32& OverlapCount = ReceiverOverlapCounts.FindOrAdd(Receiver);
    ++OverlapCount;
    if (bWasUnregistered)
    {
        Receiver->RegisterPersistentWetnessProvider(this);
    }

}

void UWaterBodyWetContactComponent::RemoveReceiverFromActor(AActor* OtherActor)
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
        Receiver->UnregisterPersistentWetnessProvider(this);
        ReceiverOverlapCounts.Remove(Receiver);
    }

}

void UWaterBodyWetContactComponent::UnregisterFromAllReceivers()
{
    for (const TPair<TWeakObjectPtr<UDynamicWetClothesComponent>, int32>& Entry : ReceiverOverlapCounts)
    {
        if (UDynamicWetClothesComponent* Receiver = Entry.Key.Get(); IsValid(Receiver))
        {
            Receiver->UnregisterPersistentWetnessProvider(this);
        }
    }

    ReceiverOverlapCounts.Reset();
}

bool UWaterBodyWetContactComponent::BuildWaterSurfaceDataForReceiver(
    const UDynamicWetClothesComponent& Receiver,
    FDWCWaterSurfaceData&              OutWaterSurfaceData) const
{
    DWC_PROFILE_SCOPE(DWC_Water_BuildWaterSurfaceDataForReceiver);

    FBox ReceiverBounds(ForceInit);
    if (!IsValid(WaterBodyComponent) || !Receiver.GetWetnessWorldBounds(ReceiverBounds))
    {
        return false;
    }

    const int32 GridSize = Receiver.GetWetSurfaceSampleResolution();
    const int32 SampleCount = GridSize * GridSize;

    OutWaterSurfaceData = FDWCWaterSurfaceData();
    OutWaterSurfaceData.Bounds = ReceiverBounds;
    OutWaterSurfaceData.SizeX = GridSize;
    OutWaterSurfaceData.SizeY = GridSize;
    OutWaterSurfaceData.SurfaceZ.SetNumZeroed(SampleCount);
    OutWaterSurfaceData.Valid.Init(0, SampleCount);

    const FVector BoundsMin = ReceiverBounds.Min;
    const FVector BoundsMax = ReceiverBounds.Max;
    const float   SampleZ = static_cast<float>(ReceiverBounds.GetCenter().Z);

    bool bHasValidSample = false;

    for (int32 Y = 0; Y < GridSize; ++Y)
    {
        const float YAlpha =
            GridSize > 1
                ? static_cast<float>(Y) / static_cast<float>(GridSize - 1)
                : 0.0f;

        for (int32 X = 0; X < GridSize; ++X)
        {
            const float XAlpha =
                GridSize > 1
                    ? static_cast<float>(X) / static_cast<float>(GridSize - 1)
                    : 0.0f;

            const FVector SamplePosition(
                FMath::Lerp(BoundsMin.X, BoundsMax.X, XAlpha),
                FMath::Lerp(BoundsMin.Y, BoundsMax.Y, YAlpha),
                SampleZ);

            float       SurfaceZ = 0.0f;
            const int32 SampleIndex = OutWaterSurfaceData.GetSampleIndex(X, Y);
            if (QueryWaterSurfaceZ(SamplePosition, SurfaceZ))
            {
                OutWaterSurfaceData.SurfaceZ[SampleIndex] = SurfaceZ;
                OutWaterSurfaceData.Valid[SampleIndex] = 1;
                bHasValidSample = true;
            }
        }
    }

    return bHasValidSample;
}

bool UWaterBodyWetContactComponent::QueryWaterSurfaceZ(
    const FVector& WorldPosition,
    float&         OutSurfaceZ) const
{
    OutSurfaceZ = 0.0f;

    if (!IsValid(WaterBodyComponent))
    {
        return false;
    }

    const EWaterBodyQueryFlags QueryFlags =
        EWaterBodyQueryFlags::ComputeLocation |
        EWaterBodyQueryFlags::IncludeWaves;

    const auto QueryResult =
        WaterBodyComponent->TryQueryWaterInfoClosestToWorldLocation(
            WorldPosition,
            QueryFlags);

    if (!QueryResult.HasValue())
    {
        return false;
    }

    OutSurfaceZ = static_cast<float>(QueryResult.GetValue().GetWaterSurfaceLocation().Z);
    return true;
}

bool UWaterBodyWetContactComponent::GetWaterBodyProxyBounds(FBox& OutBounds) const
{
    OutBounds = FBox(ForceInit);

    if (!IsValid(WaterBodyComponent))
    {
        return false;
    }

    OutBounds = WaterBodyComponent->GetCollisionComponentBounds();
    if (OutBounds.IsValid && !OutBounds.GetExtent().IsNearlyZero())
    {
        return true;
    }

    OutBounds.Init();
    for (UPrimitiveComponent* CollisionComponent : WaterBodyComponent->GetCollisionComponents(false))
    {
        if (!IsValid(CollisionComponent))
        {
            continue;
        }

        const FBox ComponentBounds = CollisionComponent->Bounds.GetBox();
        if (ComponentBounds.IsValid && !ComponentBounds.GetExtent().IsNearlyZero())
        {
            OutBounds += ComponentBounds;
        }
    }

    if (OutBounds.IsValid && !OutBounds.GetExtent().IsNearlyZero())
    {
        return true;
    }

    OutBounds = WaterBodyComponent->Bounds.GetBox();
    return OutBounds.IsValid && !OutBounds.GetExtent().IsNearlyZero();
}

void UWaterBodyWetContactComponent::OnProxyBeginOverlap(
    UPrimitiveComponent* OverlappedComponent,
    AActor*              OtherActor,
    UPrimitiveComponent* OtherComp,
    int32                OtherBodyIndex,
    bool                 bFromSweep,
    const FHitResult&    SweepResult)
{
    AddReceiverFromActor(OtherActor);
}

void UWaterBodyWetContactComponent::OnProxyEndOverlap(
    UPrimitiveComponent* OverlappedComponent,
    AActor*              OtherActor,
    UPrimitiveComponent* OtherComp,
    int32                OtherBodyIndex)
{
    RemoveReceiverFromActor(OtherActor);
}
