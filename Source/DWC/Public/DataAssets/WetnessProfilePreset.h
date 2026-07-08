// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "WetnessProfilePreset.generated.h"

/**
 *
 */
UCLASS()
class DWC_API UWetnessProfilePreset : public UDataAsset
{
    GENERATED_BODY()

  public:
    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    FName PresetDisplayName;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float Absorption = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float SpreadRate = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float MaxSpreadDistance = 50.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (ClampMin = "0.0", ClampMax = "100.0", Units = "Percent"))
    float DryRate = 10.0f;

    float GetDryRateCoefficientPerSecond() const;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float GravityFlowStrength = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float TransparencyStrength = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    float SpecularBoost = 1.0f;
};
