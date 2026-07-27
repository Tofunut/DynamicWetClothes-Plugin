// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DWCDemoRainWetAreaSource.generated.h"

class UBoxComponent;
class UDynamicWetClothesComponent;
class UDWCGPUNiagaraWetCollisionBridgeComponent;
class UPrimitiveComponent;
class UNiagaraComponent;

UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "DWC Demo Rain Wet Area Source"))
class DWCDEMO_API ADWCDemoRainWetAreaSource : public AActor
{
    GENERATED_BODY()

  public:
    ADWCDemoRainWetAreaSource();

  protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void OnConstruction(const FTransform& Transform) override;

  private:
    void RefreshExistingOverlaps();
    void RefreshReceiversInsideBounds();
    void ApplyWetnessTick();
    void AddReceiverFromActor(AActor* OtherActor);
    void RemoveReceiverFromActor(AActor* OtherActor);
    bool IsReceiverInsideRainBounds(const UDynamicWetClothesComponent& Receiver) const;
    void ApplyRainToReceiver(UDynamicWetClothesComponent& Receiver) const;
    void ApplyRainNiagaraParameters() const;
    TArray<UDynamicWetClothesComponent*> ResolveRainNiagaraReceivers() const;
    bool ShouldLogDebug() const;

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

    UPROPERTY(VisibleAnywhere)
    UDWCGPUNiagaraWetCollisionBridgeComponent* RainNiagaraWetCollisionBridge;

    UPROPERTY(EditAnywhere, Category = "DWC Demo|Rain Wet Area Source", meta = (ClampMin = "0.0"))
    float WetAmountPerSecond = 0.5f;

    UPROPERTY(EditAnywhere, Category = "DWC Demo|Rain Wet Area Source", meta = (ClampMin = "0.01"))
    float UpdateInterval = 0.1f;

    UPROPERTY(EditAnywhere, Category = "DWC Demo|Rain Wet Area Source|CPU")
    bool bApplyCPUWetArea = true;

    UPROPERTY(EditAnywhere, Category = "DWC Demo|Rain Wet Area Source|CPU", meta = (ClampMin = "1"))
    int32 RainSamplesPerTick = 300;

    UPROPERTY(EditAnywhere, Category = "DWC Demo|Rain Wet Area Source|CPU")
    bool bUseNormalExposure = true;

    UPROPERTY(EditAnywhere, Category = "DWC Demo|Rain Wet Area Source|CPU", meta = (EditCondition = "bUseNormalExposure"))
    bool bUseSkinnedNormalsForExposure = true;

    UPROPERTY(EditAnywhere, Category = "DWC Demo|Rain Wet Area Source")
    FVector RainDirection = FVector(0.0f, 0.0f, -1.0f);

    UPROPERTY(EditAnywhere, Category = "DWC Demo|Rain Wet Area Source")
    FName RainDirectionParameterName = TEXT("User.RainDirection");

    UPROPERTY(EditAnywhere, Category = "DWC Demo|Rain Wet Area Source")
    FName RainBoundsExtentParameterName = TEXT("User.RainBoundsExtent");

    UPROPERTY(EditAnywhere, Category = "DWC Demo|Rain Wet Area Source")
    FName RainIntensityParameterName = TEXT("User.RainIntensity");

    UPROPERTY(EditAnywhere, Category = "DWC Demo|Rain Wet Area Source|GPU Niagara", meta = (ClampMin = "0.0"))
    float RainParticleWetAmount = 0.1f;

    UPROPERTY(EditAnywhere, Category = "DWC Demo|Rain Wet Area Source|GPU Niagara", meta = (ClampMin = "0.0"))
    float RainParticleWetRadius = 10.0f;

    UPROPERTY(EditAnywhere, Category = "DWC Demo|Rain Wet Area Source|Debug")
    bool bEnableDebugLogging = true;

    UPROPERTY(EditAnywhere, Category = "DWC Demo|Rain Wet Area Source|Debug", meta = (ClampMin = "0.1"))
    float DebugLogInterval = 1.0f;

    TMap<TWeakObjectPtr<UDynamicWetClothesComponent>, int32> ReceiverOverlapCounts;
    FTimerHandle                                             WetnessTimer;
    mutable double                                           LastDebugLogTime = -1000000.0;
};
