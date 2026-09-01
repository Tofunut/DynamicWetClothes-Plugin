// Copyright 2026 Team Tofunut. All Rights Reserved.

// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WetInputSystem/DWCPersistentWetnessProvider.h"
#include "DWCDemoRainWetAreaSource.generated.h"
class UBoxComponent;
class UDynamicWetClothesComponent;
class UNiagaraComponent;
struct FDWCWetAreaData;

UCLASS(BlueprintType, Blueprintable, meta = (DisplayName = "DWC Demo Rain Wet Area Source"))
class DWCDEMO_API ADWCDemoRainWetAreaSource
    : public AActor
    , public IDWCPersistentWetnessProvider
{
    GENERATED_BODY()

  public:
    ADWCDemoRainWetAreaSource();

    /** Explicitly connects a DWC receiver to this demo source. */
    UFUNCTION(BlueprintCallable, Category = "DWC Demo|Rain Wet Area Source|Receivers")
    bool AddWetnessReceiver(UDynamicWetClothesComponent* Receiver);

    /** Convenience API that connects the first DWC component found on ReceiverActor. */
    UFUNCTION(BlueprintCallable, Category = "DWC Demo|Rain Wet Area Source|Receivers")
    bool AddWetnessReceiverFromActor(AActor* ReceiverActor);

    UFUNCTION(BlueprintCallable, Category = "DWC Demo|Rain Wet Area Source|Receivers")
    void RemoveWetnessReceiver(UDynamicWetClothesComponent* Receiver);

    UFUNCTION(BlueprintCallable, Category = "DWC Demo|Rain Wet Area Source|Receivers")
    void ClearWetnessReceivers();

    UFUNCTION(BlueprintPure, Category = "DWC Demo|Rain Wet Area Source|Receivers")
    int32 GetWetnessReceiverCount() const;

  protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void ApplyPersistentWetness(
        UDynamicWetClothesComponent& Receiver,
        float                        DeltaSeconds) override;

  private:
    bool    IsReceiverInsideRainBounds(const UDynamicWetClothesComponent& Receiver) const;
    FVector GetRainDirectionWorld() const;
    void    BuildRainWetAreaData(FDWCWetAreaData& OutAreaData, float DeltaSeconds) const;
    void    ApplyRainToReceiver(UDynamicWetClothesComponent& Receiver, float DeltaSeconds) const;
    void    ApplyRainNiagaraParameters() const;

    UPROPERTY(VisibleAnywhere, Category = "DWC Demo|Rain Wet Area Source")
    UBoxComponent* RainBounds;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWC Demo|Rain Wet Area Source", meta = (AllowPrivateAccess = "true"))
    UNiagaraComponent* RainNiagara;

    UPROPERTY(EditAnywhere, Category = "DWC Demo|Rain Wet Area Source", meta = (ClampMin = "0.0"))
    float WetAmountPerSecond = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DWC Demo|Rain Wet Area Source", meta = (AllowPrivateAccess = "true"))
    bool bApplyWetArea = true;

    /** Level actors connected when play begins. Runtime callers can use the receiver API instead. */
    UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category = "DWC Demo|Rain Wet Area Source|Receivers", meta = (AllowPrivateAccess = "true"))
    TArray<TObjectPtr<AActor>> InitialReceiverActors;

    UPROPERTY(EditAnywhere, Category = "DWC Demo|Rain Wet Area Source", meta = (ClampMin = "1", DisplayName = "Rain Samples Per Simulation"))
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

    TSet<TWeakObjectPtr<UDynamicWetClothesComponent>> RegisteredReceivers;

  public:
};
