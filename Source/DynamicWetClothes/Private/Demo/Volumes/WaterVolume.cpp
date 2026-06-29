#include "Demo/Volumes/WaterVolume.h"

#include "Components/BoxComponent.h"
#include "DynamicWet/DynamicWetContactTypes.h"
#include "DynamicWet/DynamicWetReceiverComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"

AWaterVolume::AWaterVolume()
{
    PrimaryActorTick.bCanEverTick = false;

    VolumeBox = CreateDefaultSubobject<UBoxComponent>(TEXT("VolumeBox"));
    SetRootComponent(VolumeBox);

    VolumeBox->SetBoxExtent(FVector(200.0f, 200.0f, 100.0f));
    VolumeBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    VolumeBox->SetCollisionObjectType(ECC_WorldDynamic);
    VolumeBox->SetCollisionResponseToAllChannels(ECR_Ignore);
    VolumeBox->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
    VolumeBox->SetGenerateOverlapEvents(true);

}

void AWaterVolume::BeginPlay()
{
    Super::BeginPlay();

    if (!VolumeBox)
    {
        return;
    }

    VolumeBox->OnComponentBeginOverlap.AddUniqueDynamic(
        this,
        &AWaterVolume::OnVolumeBeginOverlap);

    VolumeBox->OnComponentEndOverlap.AddUniqueDynamic(
        this,
        &AWaterVolume::OnVolumeEndOverlap);

    RefreshExistingOverlaps();

    if (GetWorld() && UpdateInterval > 0.0f)
    {
        GetWorldTimerManager().SetTimer(
            WetnessTimer,
            this,
            &AWaterVolume::ApplyWetnessTick,
            UpdateInterval,
            true);
    }
}

void AWaterVolume::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (GetWorld())
    {
        GetWorldTimerManager().ClearTimer(WetnessTimer);
    }

    if (VolumeBox)
    {
        VolumeBox->OnComponentBeginOverlap.RemoveDynamic(
            this,
            &AWaterVolume::OnVolumeBeginOverlap);

        VolumeBox->OnComponentEndOverlap.RemoveDynamic(
            this,
            &AWaterVolume::OnVolumeEndOverlap);
    }

    ReceiverOverlapCounts.Reset();

    Super::EndPlay(EndPlayReason);
}

void AWaterVolume::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);

}

void AWaterVolume::RefreshExistingOverlaps()
{
    if (!VolumeBox)
    {
        return;
    }

    VolumeBox->UpdateOverlaps();

    TArray<AActor*> OverlappingActors;
    VolumeBox->GetOverlappingActors(OverlappingActors);
    for (AActor* OverlappingActor : OverlappingActors)
    {
        AddReceiverFromActor(OverlappingActor);
    }
}

void AWaterVolume::ApplyWetnessTick()
{
    if (!VolumeBox || ReceiverOverlapCounts.Num() == 0 || WetAmountPerSecond <= 0.0f || UpdateInterval <= 0.0f)
    {
        return;
    }

    const float TickAmount = WetAmountPerSecond * UpdateInterval;

    for (auto It = ReceiverOverlapCounts.CreateIterator(); It; ++It)
    {
        UDynamicWetReceiverComponent* Receiver = It.Key().Get();
        if (!IsValid(Receiver) || It.Value() <= 0)
        {
            It.RemoveCurrent();
            continue;
        }

        FDWCWetSurfaceData SurfaceData;
        if (!BuildSurfaceDataForReceiver(*Receiver, SurfaceData))
        {
            continue;
        }

        Receiver->ApplyWetSurface(SurfaceData, TickAmount, false);
    }
}

void AWaterVolume::AddReceiverFromActor(AActor* OtherActor)
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

void AWaterVolume::RemoveReceiverFromActor(AActor* OtherActor)
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

bool AWaterVolume::BuildSurfaceDataForReceiver(
    const UDynamicWetReceiverComponent& Receiver,
    FDWCWetSurfaceData&                 OutSurfaceData) const
{
    if (!VolumeBox)
    {
        return false;
    }

    FBox ReceiverBounds(ForceInit);
    if (!Receiver.GetWetnessWorldBounds(ReceiverBounds))
    {
        return false;
    }

    const int32 GridSize = FMath::Max(2, SurfaceSampleResolution);
    const int32 SampleCount = GridSize * GridSize;
    const FBox  VolumeBounds = VolumeBox->Bounds.GetBox();
    const float SurfaceZ = VolumeBounds.Max.Z;

    OutSurfaceData = FDWCWetSurfaceData();
    OutSurfaceData.Bounds = ReceiverBounds;
    OutSurfaceData.SizeX = GridSize;
    OutSurfaceData.SizeY = GridSize;
    OutSurfaceData.SurfaceZ.SetNumZeroed(SampleCount);
    OutSurfaceData.Valid.Init(0, SampleCount);

    const FVector BoundsMin = ReceiverBounds.Min;
    const FVector BoundsMax = ReceiverBounds.Max;

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
                SurfaceZ);

            if (!VolumeBounds.IsInsideXY(SamplePosition))
            {
                continue;
            }

            const int32 SampleIndex = OutSurfaceData.GetSampleIndex(X, Y);
            OutSurfaceData.SurfaceZ[SampleIndex] = SurfaceZ;
            OutSurfaceData.Valid[SampleIndex] = 1;
            bHasValidSample = true;
        }
    }

    return bHasValidSample;
}

void AWaterVolume::OnVolumeBeginOverlap(
    UPrimitiveComponent* OverlappedComponent,
    AActor*              OtherActor,
    UPrimitiveComponent* OtherComp,
    int32                OtherBodyIndex,
    bool                 bFromSweep,
    const FHitResult&    SweepResult)
{
    AddReceiverFromActor(OtherActor);
}

void AWaterVolume::OnVolumeEndOverlap(
    UPrimitiveComponent* OverlappedComponent,
    AActor*              OtherActor,
    UPrimitiveComponent* OtherComp,
    int32                OtherBodyIndex)
{
    RemoveReceiverFromActor(OtherActor);
}
