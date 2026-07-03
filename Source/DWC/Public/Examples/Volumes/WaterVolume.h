#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WaterVolume.generated.h"

class UBoxComponent;
class UDynamicWetClothesComponent;
class UPrimitiveComponent;
struct FDWCWaterSurfaceData;

UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "Water Volume"))
class DWC_API AWaterVolume : public AActor
{
    GENERATED_BODY()

  public:
    // Sets default values for this actor's properties
    AWaterVolume();

  protected:
    // Called when the game starts or when spawned
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void OnConstruction(const FTransform& Transform) override;

  private:
    void RefreshExistingOverlaps();
    void ApplyWetnessTick();
    void AddReceiverFromActor(AActor* OtherActor);
    void RemoveReceiverFromActor(AActor* OtherActor);
    bool BuildWaterSurfaceDataForReceiver(const UDynamicWetClothesComponent& Receiver, FDWCWaterSurfaceData& OutWaterSurfaceData) const;

    UFUNCTION()
    void OnVolumeBeginOverlap(
        UPrimitiveComponent* OverlappedComponent,
        AActor*              OtherActor,
        UPrimitiveComponent* OtherComp,
        int32                OtherBodyIndex,
        bool                 bFromSweep,
        const FHitResult&    SweepResult);

    UFUNCTION()
    void OnVolumeEndOverlap(
        UPrimitiveComponent* OverlappedComponent,
        AActor*              OtherActor,
        UPrimitiveComponent* OtherComp,
        int32                OtherBodyIndex);

    UPROPERTY(VisibleAnywhere, Category = "Water Volume")
    UBoxComponent* VolumeBox;

    UPROPERTY(EditAnywhere, Category = "Water Volume", meta = (ClampMin = "0.0"))
    float WetAmountPerSecond = 0.5f;

    UPROPERTY(EditAnywhere, Category = "Water Volume", meta = (ClampMin = "0.01"))
    float UpdateInterval = 0.1f;

    UPROPERTY(EditAnywhere, Category = "Water Volume", meta = (ClampMin = "2", ClampMax = "64"))
    int32 SurfaceSampleResolution = 8;

    TMap<TWeakObjectPtr<UDynamicWetClothesComponent>, int32> ReceiverOverlapCounts;
    FTimerHandle                                             WetnessTimer;
};
