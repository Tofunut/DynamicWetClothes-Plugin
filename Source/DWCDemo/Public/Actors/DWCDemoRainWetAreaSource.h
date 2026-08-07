//Copyright 2026 Team Tofunut. All Rights Reserved.
// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DWCDemoRainWetAreaSource.generated.h"

class UBoxComponent;
class UDynamicWetClothesComponent;
class UPrimitiveComponent;
class UNiagaraComponent;
struct FDWCWetAreaData;

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
    FVector GetRainDirectionWorld() const;
    void BuildRainWetAreaData(FDWCWetAreaData& OutAreaData) const;
    void ApplyRainToReceiver(UDynamicWetClothesComponent& Receiver) const;
    void ApplyRainNiagaraParameters() const;
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

    UPROPERTY(EditAnywhere, BlueprintReadWrite, meta = (AllowPrivateAccess = "true"))
    UNiagaraComponent* RainNiagara;

    UPROPERTY(EditAnywhere, Category = "DWC Demo|Rain Wet Area Source", meta = (ClampMin = "0.0"))
    float WetAmountPerSecond = 0.5f;

    UPROPERTY(EditAnywhere, Category = "DWC Demo|Rain Wet Area Source", meta = (ClampMin = "0.01"))
    float UpdateInterval = 0.1f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWC Demo|Rain Wet Area Source", meta = (AllowPrivateAccess = "true"))
    bool bApplyWetArea = true;

    UPROPERTY(EditAnywhere, Category = "DWC Demo|Rain Wet Area Source", meta = (ClampMin = "1"))
    int32 RainSamplesPerTick = 300;

    UPROPERTY(EditAnywhere, Category = "DWC Demo|Rain Wet Area Source", meta = (AdvancedDisplay, ToolTip = "Uses the rain direction to favor surfaces facing the incoming rain."))
    bool bUseNormalExposure = true;

    UPROPERTY(EditAnywhere, Category = "DWC Demo|Rain Wet Area Source", meta = (AdvancedDisplay, EditCondition = "bUseNormalExposure", ToolTip = "When normal exposure is enabled, use current skinned normals when available instead of static mesh normals."))
    bool bUseSkinnedNormalsForExposure = true;

    UPROPERTY(EditAnywhere, Category = "DWC Demo|Rain Wet Area Source", meta = (ToolTip = "Local rain travel direction. The actor transform converts this to a normalized world direction before applying wetness and sending Niagara parameters."))
    FVector RainDirection = FVector(0.0f, 0.0f, -1.0f);

    UPROPERTY(EditAnywhere, Category = "DWC Demo|Rain Wet Area Source")
    FName RainDirectionParameterName = TEXT("User.RainDirection");

    UPROPERTY(EditAnywhere, Category = "DWC Demo|Rain Wet Area Source")
    FName RainBoundsExtentParameterName = TEXT("User.RainBoundsExtent");

    UPROPERTY(EditAnywhere, Category = "DWC Demo|Rain Wet Area Source")
    FName RainIntensityParameterName = TEXT("User.RainIntensity");

    UPROPERTY(EditAnywhere, Category = "DWC Demo|Rain Wet Area Source|Debug")
    bool bEnableDebugLogging = true;

    UPROPERTY(EditAnywhere, Category = "DWC Demo|Rain Wet Area Source|Debug", meta = (ClampMin = "0.1"))
    float DebugLogInterval = 1.0f;

    TMap<TWeakObjectPtr<UDynamicWetClothesComponent>, int32> ReceiverOverlapCounts;
    FTimerHandle                                             WetnessTimer;
    mutable double                                           LastDebugLogTime = -1000000.0;

    public:

};
