//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "Water/WaterBodyWetContactComponent.h"

#include "Components/BoxComponent.h"
#include "Components/PrimitiveComponent.h"
#include "WetInputSystem/WetContactTypes.h"
#include "Components/DynamicWetClothesComponent.h"
#include "WaterBodyComponent.h"
#include "WaterBodyTypes.h"
#include "Utility/DWCProfiling.h"

DEFINE_LOG_CATEGORY_STATIC(LogDWCWaterIntegration, Log, All);

UWaterBodyWetContactComponent::UWaterBodyWetContactComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
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

    ReceiverOverlapCounts.Reset();
    DestroyOverlapProxy();

    Super::EndPlay(EndPlayReason);
}

void UWaterBodyWetContactComponent::TickComponent(
    const float                        DeltaTime,
    const ELevelTick                   TickType,
    FActorComponentTickFunction* const ThisTickFunction)
{
    DWC_PROFILE_SCOPE(DWC_Water_TickComponent);

    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    ApplyWetnessTick(DeltaTime);
}

void UWaterBodyWetContactComponent::InitializeWaterBody()
{
    AActor* Owner = GetOwner();
    WaterBodyComponent = IsValid(Owner) ? Owner->FindComponentByClass<UWaterBodyComponent>() : nullptr;

    if (!IsValid(WaterBodyComponent) && IsValid(Owner))
    {
        UE_LOG(LogDWCWaterIntegration, Warning, TEXT("WaterBodyWetContactComponent: WaterBodyComponent not found on %s."), *Owner->GetName());
    }
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
        UE_LOG(LogDWCWaterIntegration, Warning, TEXT("WaterBodyWetContactComponent: Could not resolve water body bounds on %s."), *Owner->GetName());
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

void UWaterBodyWetContactComponent::ApplyWetnessTick(const float DeltaTime)
{
    DWC_PROFILE_SCOPE(DWC_Water_ApplyWetnessTick);

    if (!IsValid(WaterBodyComponent) || ReceiverOverlapCounts.Num() == 0)
    {
        return;
    }

    constexpr float WetAmount = 1.0f;

    for (auto It = ReceiverOverlapCounts.CreateIterator(); It; ++It)
    {
        UDynamicWetClothesComponent* Receiver = It.Key().Get();
        if (!IsValid(Receiver) || It.Value() <= 0)
        {
            It.RemoveCurrent();
            continue;
        }

        FDWCWaterSurfaceData WaterSurfaceData;
        const double         BuildStartSeconds = FPlatformTime::Seconds();
        if (!BuildWaterSurfaceDataForReceiver(*Receiver, WaterSurfaceData))
        {
            if (bEnablePerformanceLogging)
            {
                AccumulatedBuildSurfaceDataSeconds += FPlatformTime::Seconds() - BuildStartSeconds;
            }
            continue;
        }

        const double BuildEndSeconds = FPlatformTime::Seconds();
        const double ApplyStartSeconds = BuildEndSeconds;
        {
            DWC_PROFILE_SCOPE(DWC_Water_ApplyWetSurface);

            Receiver->ApplyWetSurface(WaterSurfaceData, WetAmount, false);
        }
        const double ApplyEndSeconds = FPlatformTime::Seconds();

        if (bEnablePerformanceLogging)
        {
            AccumulatedBuildSurfaceDataSeconds += BuildEndSeconds - BuildStartSeconds;
            AccumulatedApplyWetSurfaceSeconds += ApplyEndSeconds - ApplyStartSeconds;
            ++AccumulatedProcessedReceivers;
            AccumulatedWaterSurfaceSamples += WaterSurfaceData.SizeX * WaterSurfaceData.SizeY;
        }
    }

    if (ReceiverOverlapCounts.Num() == 0)
    {
        SetComponentTickEnabled(false);
    }

    if (bEnablePerformanceLogging)
    {
        ++AccumulatedPerformanceFrames;
        AccumulatedPerformanceLogSeconds += DeltaTime;
        if (AccumulatedPerformanceLogSeconds >= FMath::Max(0.1f, PerformanceLogInterval))
        {
            const double BuildMilliseconds = AccumulatedBuildSurfaceDataSeconds * 1000.0;
            const double ApplyMilliseconds = AccumulatedApplyWetSurfaceSeconds * 1000.0;
            const double TotalMilliseconds = BuildMilliseconds + ApplyMilliseconds;
            const int32  SafeFrames = FMath::Max(1, AccumulatedPerformanceFrames);
            const int32  SafeReceivers = FMath::Max(1, AccumulatedProcessedReceivers);

            UE_LOG(
                LogDWCWaterIntegration,
                Log,
                TEXT("WaterBodyWetContact perf: frames=%d receivers=%d samples=%d total=%.3fms build/query=%.3fms apply=%.3fms avg/frame=%.3fms avg/receiver build=%.3fms apply=%.3fms"),
                AccumulatedPerformanceFrames,
                AccumulatedProcessedReceivers,
                AccumulatedWaterSurfaceSamples,
                TotalMilliseconds,
                BuildMilliseconds,
                ApplyMilliseconds,
                TotalMilliseconds / static_cast<double>(SafeFrames),
                BuildMilliseconds / static_cast<double>(SafeReceivers),
                ApplyMilliseconds / static_cast<double>(SafeReceivers));

            AccumulatedBuildSurfaceDataSeconds = 0.0;
            AccumulatedApplyWetSurfaceSeconds = 0.0;
            AccumulatedPerformanceLogSeconds = 0.0f;
            AccumulatedPerformanceFrames = 0;
            AccumulatedProcessedReceivers = 0;
            AccumulatedWaterSurfaceSamples = 0;
        }
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

    int32& OverlapCount = ReceiverOverlapCounts.FindOrAdd(Receiver);
    ++OverlapCount;
    SetComponentTickEnabled(true);
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
        ReceiverOverlapCounts.Remove(Receiver);
    }

    if (ReceiverOverlapCounts.Num() == 0)
    {
        SetComponentTickEnabled(false);
    }
}

bool UWaterBodyWetContactComponent::BuildWaterSurfaceDataForReceiver(
    const UDynamicWetClothesComponent& Receiver,
    FDWCWaterSurfaceData&              OutWaterSurfaceData) const
{
    DWC_PROFILE_SCOPE(DWC_Water_BuildWaterSurfaceDataForReceiver);

    FBox ReceiverBounds(ForceInit);
    if (!Receiver.GetWetnessWorldBounds(ReceiverBounds))
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
    const float   SampleZ = ReceiverBounds.GetCenter().Z;

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

    OutSurfaceZ = QueryResult.GetValue().GetWaterSurfaceLocation().Z;
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
