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
class DWCWATERSYSTEMINTEGRATION_API UWaterBodyWetContactComponent : public UActorComponent
{
    GENERATED_BODY()

  public:
    UWaterBodyWetContactComponent();

  protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

  private:
    void InitializeWaterBody();
    void CreateOverlapProxy();
    void DestroyOverlapProxy();
    void RefreshExistingOverlaps();
    void ApplyWetnessTick(float DeltaTime);
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
    UPROPERTY(EditAnywhere, Category = "Wetness")
    bool bApplyToExistingOverlapsOnBeginPlay = true;

    UPROPERTY(EditAnywhere, Category = "Wetness|Overlap Proxy")
    bool bCreateOverlapProxy = true;

    UPROPERTY(EditAnywhere, Category = "Wetness|Profiling")
    bool bEnablePerformanceLogging = false;

    UPROPERTY(EditAnywhere, Category = "Wetness|Profiling", meta = (ClampMin = "0.1"))
    float PerformanceLogInterval = 1.0f;

    UPROPERTY(Transient)
    TObjectPtr<UWaterBodyComponent> WaterBodyComponent;

    UPROPERTY(Transient)
    TObjectPtr<UBoxComponent> OverlapProxy;

    TMap<TWeakObjectPtr<UDynamicWetClothesComponent>, int32> ReceiverOverlapCounts;

    double AccumulatedBuildSurfaceDataSeconds = 0.0;
    double AccumulatedApplyWetSurfaceSeconds = 0.0;
    float  AccumulatedPerformanceLogSeconds = 0.0f;
    int32  AccumulatedPerformanceFrames = 0;
    int32  AccumulatedProcessedReceivers = 0;
    int32  AccumulatedWaterSurfaceSamples = 0;
};
