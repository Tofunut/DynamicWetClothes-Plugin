#include "DynamicWet/WaterBodyWetSourceComponent.h"

#include "Components/BoxComponent.h"
#include "Components/PrimitiveComponent.h"
#include "WaterBodyComponent.h"
#include "WaterBodyTypes.h"

UWaterBodyWetSourceComponent::UWaterBodyWetSourceComponent()
{
    PrimaryComponentTick.bCanEverTick = false;
    SetAutoSourceBindingEnabled(false);
}

void UWaterBodyWetSourceComponent::BeginPlay()
{
    AActor* Owner = GetOwner();
    if (!IsValid(Owner))
    {
        Super::BeginPlay();
        return;
    }

    WaterBodyComponent = Owner->FindComponentByClass<UWaterBodyComponent>();
    if (!IsValid(WaterBodyComponent))
    {
        UE_LOG(LogTemp, Warning, TEXT("WaterBodyWetSourceComponent: WaterBodyComponent not found on %s."), *Owner->GetName());
        Super::BeginPlay();
        return;
    }

    CreateWetnessOverlapProxy();

    if (WetnessOverlapProxy)
    {
        SetOverlapComponent(WetnessOverlapProxy);
    }

    Super::BeginPlay();
}

void UWaterBodyWetSourceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    Super::EndPlay(EndPlayReason);

    DestroyWetnessOverlapProxy();
}

bool UWaterBodyWetSourceComponent::BuildWetSourceData(FDWCWetSourceData& OutSourceData) const
{
    if (!IsValid(WaterBodyComponent))
    {
        return false;
    }

    OutSourceData = FDWCWetSourceData();
    OutSourceData.SourceBinding = EDWCSourceBindingType::UnrealWater;
    OutSourceData.InfluenceType = EDWCInfluenceType::Volume;
    OutSourceData.InfluenceShape = EDWCInfluenceShape::Custom;
    OutSourceData.Intensity = WetAmountPerSecond;
    OutSourceData.bUseSourceSurfaceHeightQuery = true;
    OutSourceData.bIsValid = WetAmountPerSecond > 0.0f;

    if (const AActor* Owner = GetOwner())
    {
        OutSourceData.WorldLocation = Owner->GetActorLocation();
    }

    if (const UPrimitiveComponent* ProxyComponent = WetnessOverlapProxy.Get())
    {
        OutSourceData.WorldBounds = ProxyComponent->Bounds.GetBox();
    }

    return OutSourceData.bIsValid;
}

void UWaterBodyWetSourceComponent::CreateWetnessOverlapProxy()
{
    if (!bCreateWetnessOverlapProxy || IsValid(WetnessOverlapProxy))
    {
        return;
    }

    AActor* Owner = GetOwner();
    if (!IsValid(Owner))
    {
        return;
    }

    USceneComponent* RootComponent = Owner->GetRootComponent();
    if (!IsValid(RootComponent))
    {
        return;
    }

    FBox WaterBodyBounds(ForceInit);
    if (!GetWaterBodyProxyBounds(WaterBodyBounds))
    {
        UE_LOG(LogTemp, Warning, TEXT("WaterBodyWetSourceComponent: Could not resolve water body bounds on %s."), *Owner->GetName());
        return;
    }

    const float DesiredTopZ = WaterBodyBounds.Max.Z + WaterBodyComponent->GetMaxWaveHeight();
    const float DesiredBottomZ = WaterBodyBounds.Min.Z;

    const FVector ProxyOrigin(
        (WaterBodyBounds.Min.X + WaterBodyBounds.Max.X) * 0.5f,
        (WaterBodyBounds.Min.Y + WaterBodyBounds.Max.Y) * 0.5f,
        (DesiredTopZ + DesiredBottomZ) * 0.5f);

    const FVector ProxyExtent(
        FMath::Max((WaterBodyBounds.Max.X - WaterBodyBounds.Min.X) * 0.5f, 1.0f),
        FMath::Max((WaterBodyBounds.Max.Y - WaterBodyBounds.Min.Y) * 0.5f, 1.0f),
        FMath::Max((DesiredTopZ - DesiredBottomZ) * 0.5f, 1.0f));

    WetnessOverlapProxy = NewObject<UBoxComponent>(Owner, TEXT("WetnessOverlapProxy"));
    if (!IsValid(WetnessOverlapProxy))
    {
        return;
    }

    Owner->AddInstanceComponent(WetnessOverlapProxy);
    WetnessOverlapProxy->SetupAttachment(RootComponent);
    WetnessOverlapProxy->SetBoxExtent(ProxyExtent, false);
    WetnessOverlapProxy->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    WetnessOverlapProxy->SetCollisionObjectType(ECC_WorldDynamic);
    WetnessOverlapProxy->SetCollisionResponseToAllChannels(ECR_Overlap);
    WetnessOverlapProxy->SetGenerateOverlapEvents(true);
    WetnessOverlapProxy->RegisterComponent();
    WetnessOverlapProxy->SetWorldLocation(ProxyOrigin);
}

void UWaterBodyWetSourceComponent::DestroyWetnessOverlapProxy()
{
    if (!IsValid(WetnessOverlapProxy))
    {
        return;
    }

    WetnessOverlapProxy->DestroyComponent();
    WetnessOverlapProxy = nullptr;
}

bool UWaterBodyWetSourceComponent::GetWaterBodyProxyBounds(FBox& OutBounds) const
{
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

bool UWaterBodyWetSourceComponent::QueryWetSurfaceZ(
    const FVector& WorldPosition,
    float&         OutSurfaceZ) const
{
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

    OutSurfaceZ = QueryResult.GetValue().GetWaterSurfaceLocation().Z;
    return true;
}
