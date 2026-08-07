//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WetClothingSettings.generated.h"

USTRUCT(BlueprintType)
struct DWC_API FWetClothingSettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Simulation")
    float WetnessUpdateInterval = 0.1f;

    UPROPERTY(EditAnywhere, Category = "CPU Rendering", meta = (ClampMin = "0.001"))
    float WetnessRenderUpdateInterval = 0.1f;

    UPROPERTY(EditAnywhere, Category = "Simulation", meta = (ClampMin = "0.0"))
    float MaxWetness = 1.15f;

    // Internal CPU vertex-color normalization. Appearance authoring is not exposed on the component.
    float VisualSaturationWetness = 1.0f;

    UPROPERTY(EditAnywhere, Category = "Simulation", meta = (ClampMin = "0.0", AdvancedDisplay))
    float WetnessDryHoldDuration = 0.15f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Simulation", meta = (ClampMin = "0.0", AdvancedDisplay))
    float DryRateScale = 1.0f;

    UPROPERTY(EditAnywhere, Category = "Simulation", meta = (ClampMin = "0.0", ClampMax = "1.0", AdvancedDisplay))
    float CapillaryImmediateAbsorptionFraction = 0.65f;

    UPROPERTY(EditAnywhere, Category = "Simulation", meta = (ClampMin = "0.0", ClampMax = "1.0", AdvancedDisplay))
    float CrossWetPartSpreadScale = 1.0f;

    UPROPERTY(EditAnywhere, Category = "Input|Area", meta = (DisplayName = "Exposure Min", ClampMin = "-1.0", ClampMax = "1.0", AdvancedDisplay))
    float AreaExposureMin = 0.5f;

    UPROPERTY(EditAnywhere, Category = "Input|Area", meta = (DisplayName = "Exposure Max", ClampMin = "-1.0", ClampMax = "1.0", AdvancedDisplay))
    float AreaExposureMax = 0.9f;

    UPROPERTY(EditAnywhere, Category = "Input|Area", meta = (DisplayName = "Minimum Influence", ClampMin = "0.0", ClampMax = "1.0", AdvancedDisplay))
    float AreaExposureMinInfluence = 0.05f;

    UPROPERTY(EditAnywhere, Category = "Input|Contact", meta = (DisplayName = "Backface Depth Tolerance", ClampMin = "0.0", AdvancedDisplay))
    float WetContactBackfaceDepthTolerance = 2.0f;

    UPROPERTY(EditAnywhere, Category = "Input|Contact", meta = (DisplayName = "Backface Radius Scale", ClampMin = "0.0", ClampMax = "1.0", AdvancedDisplay))
    float WetContactBackfaceDepthRadiusScale = 0.05f;

    // Internal directional thresholds for Contact input. Kept separate from Area exposure controls.
    float ContactExposureMin = 0.5f;
    float ContactExposureMax = 0.9f;

    UPROPERTY(EditAnywhere, Category = "Performance", meta = (ClampMin = "1", AdvancedDisplay))
    int32 MaxPendingWetnessVerticesPerUpdate = 4096;

    UPROPERTY(EditAnywhere, Category = "Performance", meta = (ClampMin = "0.0", AdvancedDisplay))
    float MinPendingWetnessAmount = 0.0001f;
};
