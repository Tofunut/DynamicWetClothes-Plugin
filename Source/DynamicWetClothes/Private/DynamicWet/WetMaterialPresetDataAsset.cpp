// Fill out your copyright notice in the Description page of Project Settings.

#include "DynamicWet/WetMaterialPresetDataAsset.h"

float UWetMaterialPresetDataAsset::GetDryRateCoefficientPerSecond() const
{
    const float DryPercentPerSecond = FMath::Clamp(DryRate, 0.0f, 100.0f);
    const float RemainingFraction = FMath::Max(1.0f - DryPercentPerSecond * 0.01f, KINDA_SMALL_NUMBER);
    return -FMath::Loge(RemainingFraction);
}
