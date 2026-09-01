// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "UObject/WeakObjectPtr.h"
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "WetInputSystem/DWCPersistentWetnessProvider.h"
#include "WaterBodyWetContactComponent.generated.h"

class UBoxComponent;
class UDynamicWetClothesComponent;
class UPrimitiveComponent;
class UWaterBodyComponent;
struct FDWCWaterSurfaceData;

UCLASS(ClassGroup = (DWC), DisplayName = "Water Body Wet Contact", meta = (BlueprintSpawnableComponent))
class DWCWATERSYSTEMINTEGRATION_API UWaterBodyWetContactComponent
    : public UActorComponent
    , public IDWCPersistentWetnessProvider
{
    GENERATED_BODY()

  public:
    UWaterBodyWetContactComponent();

  protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void ApplyPersistentWetness(
        UDynamicWetClothesComponent& Receiver,
        float                        DeltaSeconds) override;

  private:
    void InitializeWaterBody();
    void CreateOverlapProxy();
    void DestroyOverlapProxy();
    void RefreshExistingOverlaps();
    void AddReceiverFromActor(AActor* OtherActor);
    void RemoveReceiverFromActor(AActor* OtherActor);
    void UnregisterFromAllReceivers();
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
    // Internal integration policy. These values are intentionally not exposed in Details or Blueprint.
    bool  bApplyToExistingOverlapsOnBeginPlay = true;
    bool  bCreateOverlapProxy = true;

    UPROPERTY(Transient)
    TObjectPtr<UWaterBodyComponent> WaterBodyComponent;

    UPROPERTY(Transient)
    TObjectPtr<UBoxComponent> OverlapProxy;

    TMap<TWeakObjectPtr<UDynamicWetClothesComponent>, int32> ReceiverOverlapCounts;
};
