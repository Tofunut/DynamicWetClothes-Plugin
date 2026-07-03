// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RainVolume.generated.h"

class UBoxComponent;
class UDynamicWetClothesComponent;
class UPrimitiveComponent;
class UNiagaraComponent;

UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "Rain Volume"))
class DWC_API ARainVolume : public AActor
{
    GENERATED_BODY()

  public:
    ARainVolume();

  protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void OnConstruction(const FTransform& Transform) override;

  private:
    void RefreshExistingOverlaps();
    void ApplyWetnessTick();
    void AddReceiverFromActor(AActor* OtherActor);
    void RemoveReceiverFromActor(AActor* OtherActor);
    void ApplyRainToReceiver(UDynamicWetClothesComponent& Receiver) const;
    void ApplyRainNiagaraParameters() const;

    UFUNCTION()
    void OnRainBeginOverlap(
        UPrimitiveComponent* OverlappedComponent,
        AActor*              OtherActor,
        UPrimitiveComponent* OtherComp,
        int32                OtherBodyIndex,
        bool                 bFromSweep,
        const FHitResult&    SweepResult);

    UFUNCTION()
    void OnRainEndOverlap(
        UPrimitiveComponent* OverlappedComponent,
        AActor*              OtherActor,
        UPrimitiveComponent* OtherComp,
        int32                OtherBodyIndex);

    UPROPERTY(VisibleAnywhere)
    UBoxComponent* RainBounds;

    UPROPERTY(VisibleAnywhere)
    UNiagaraComponent* RainNiagara;

    UPROPERTY(EditAnywhere, Category = "Rain Volume", meta = (ClampMin = "0.0"))
    float WetAmountPerSecond = 0.5f;

    UPROPERTY(EditAnywhere, Category = "Rain Volume", meta = (ClampMin = "0.01"))
    float UpdateInterval = 0.1f;

    UPROPERTY(EditAnywhere, Category = "Rain Volume|Sampling", meta = (ClampMin = "1"))
    int32 RainSamplesPerTick = 300;

    UPROPERTY(EditAnywhere, Category = "Rain Volume|Sampling")
    bool bUseNormalExposure = false;

    UPROPERTY(EditAnywhere, Category = "Rain Volume|Sampling", meta = (EditCondition = "bUseNormalExposure"))
    bool bUseSkinnedNormalsForExposure = false;

    UPROPERTY(EditAnywhere, Category = "Rain Volume")
    FVector RainDirection = FVector(0.0f, 0.0f, -1.0f);

    UPROPERTY(EditAnywhere, Category = "Rain Volume")
    FName RainDirectionParameterName = TEXT("User.RainDirection");

    UPROPERTY(EditAnywhere, Category = "Rain Volume")
    FName RainBoundsExtentParameterName = TEXT("User.RainBoundsExtent");

    UPROPERTY(EditAnywhere, Category = "Rain Volume")
    FName RainIntensityParameterName = TEXT("User.RainIntensity");

    TMap<TWeakObjectPtr<UDynamicWetClothesComponent>, int32> ReceiverOverlapCounts;
    FTimerHandle                                             WetnessTimer;
};
