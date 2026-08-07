//Copyright 2026 Team Tofunut. All Rights Reserved.
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
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wetness Profile")
    FName PresetDisplayName;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wetness Profile")
    float Absorption = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wetness Profile")
    float SpreadRate = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wetness Profile")
    float MaxSpreadDistance = 50.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wetness Profile", meta = (ClampMin = "0.0", ClampMax = "100.0", Units = "Percent"))
    float DryRate = 10.0f;

    float GetDryRateCoefficientPerSecond() const;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wetness Profile")
    float GravityFlowStrength = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wetness Profile")
    float SpecularBoost = 1.0f;
};
