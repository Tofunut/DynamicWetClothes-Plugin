#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "WaterBodyWetContactComponent.generated.h"

class UBoxComponent;
class UDynamicWetClothesComponent;
class UPrimitiveComponent;
class UWaterBodyComponent;
struct FDWCWaterSurfaceData;

UCLASS(ClassGroup = (Wetness), DisplayName = "Water Body Wet Contact", meta = (BlueprintSpawnableComponent))
class DWCWATER_API UWaterBodyWetContactComponent : public UActorComponent
{
    GENERATED_BODY()

  public:
    UWaterBodyWetContactComponent();

  protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

  private:
    void InitializeWaterBody();
    void CreateOverlapProxy();
    void DestroyOverlapProxy();
    void RefreshExistingOverlaps();
    void ApplyWetnessTick();
    void AddReceiverFromActor(AActor* OtherActor);
    void RemoveReceiverFromActor(AActor* OtherActor);
    bool BuildWaterSurfaceDataForReceiver(const UDynamicWetClothesComponent& Receiver, FDWCWaterSurfaceData& OutWaterSurfaceData) const;
    bool QueryWaterSurfaceZ(const FVector& WorldPosition, float& OutSurfaceZ) const;
    bool GetWaterBodyProxyBounds(FBox& OutBounds) const;

    UFUNCTION()
    void OnProxyBeginOverlap(
        UPrimitiveComponent* OverlappedComponent,
        AActor*              OtherActor,
        UPrimitiveComponent* OtherComp,
        int32                OtherBodyIndex,
        bool                 bFromSweep,
        const FHitResult&    SweepResult);

    UFUNCTION()
    void OnProxyEndOverlap(
        UPrimitiveComponent* OverlappedComponent,
        AActor*              OtherActor,
        UPrimitiveComponent* OtherComp,
        int32                OtherBodyIndex);

  private:
    UPROPERTY(EditAnywhere, Category = "Wetness", meta = (ClampMin = "0.0"))
    float WetAmountPerSecond = 0.5f;

    UPROPERTY(EditAnywhere, Category = "Wetness", meta = (ClampMin = "0.01"))
    float UpdateInterval = 0.1f;

    UPROPERTY(EditAnywhere, Category = "Wetness")
    bool bApplyToExistingOverlapsOnBeginPlay = true;

    UPROPERTY(EditAnywhere, Category = "Wetness|Overlap Proxy")
    bool bCreateOverlapProxy = true;

    UPROPERTY(Transient)
    TObjectPtr<UWaterBodyComponent> WaterBodyComponent;

    UPROPERTY(Transient)
    TObjectPtr<UBoxComponent> OverlapProxy;

    TMap<TWeakObjectPtr<UDynamicWetClothesComponent>, int32> ReceiverOverlapCounts;
    FTimerHandle                                             WetnessTimer;
};
