#pragma once

#include "CoreMinimal.h"
#include "WetClothingSettings.generated.h"

USTRUCT(BlueprintType)
struct DWC_API FWetClothingSettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Wetness")
    float WetnessUpdateInterval = 0.1f;

    UPROPERTY(EditAnywhere, Category = "Wetness|Rendering", meta = (ClampMin = "0.001"))
    float WetnessRenderUpdateInterval = 0.1f;

    UPROPERTY(EditAnywhere, Category = "Wetness", meta = (ClampMin = "0.0"))
    float MaxWetness = 1.15f;

    UPROPERTY(EditAnywhere, Category = "Wetness|Visual", meta = (ClampMin = "0.001", AdvancedDisplay))
    float VisualSaturationWetness = 1.0f;

    UPROPERTY(EditAnywhere, Category = "Wetness", meta = (ClampMin = "0.0", AdvancedDisplay))
    float WetnessDryHoldDuration = 0.15f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness", meta = (ClampMin = "0.0", AdvancedDisplay))
    float DryRateScale = 1.0f;

    UPROPERTY(EditAnywhere, Category = "Wetness|Capillary", meta = (ClampMin = "0.0", ClampMax = "1.0", AdvancedDisplay))
    float CapillaryImmediateAbsorptionFraction = 0.65f;

    UPROPERTY(EditAnywhere, Category = "Wetness|Capillary", meta = (ClampMin = "0.0", ClampMax = "1.0", AdvancedDisplay))
    float CrossWetPartSpreadScale = 1.0f;

    UPROPERTY(EditAnywhere, Category = "Wetness|Rain", meta = (ClampMin = "-1.0", ClampMax = "1.0", AdvancedDisplay))
    float RainExposureMin = 0.5f;

    UPROPERTY(EditAnywhere, Category = "Wetness|Rain", meta = (ClampMin = "-1.0", ClampMax = "1.0", AdvancedDisplay))
    float RainExposureMax = 0.9f;

    UPROPERTY(EditAnywhere, Category = "Wetness|Rain", meta = (ClampMin = "0.0", ClampMax = "1.0", AdvancedDisplay))
    float RainExposureMinInfluence = 0.05f;

    UPROPERTY(EditAnywhere, Category = "Wetness|Contact", meta = (ClampMin = "0.0", AdvancedDisplay))
    float WetContactBackfaceDepthTolerance = 2.0f;

    UPROPERTY(EditAnywhere, Category = "Wetness|Contact", meta = (ClampMin = "0.0", ClampMax = "1.0", AdvancedDisplay))
    float WetContactBackfaceDepthRadiusScale = 0.05f;

    UPROPERTY(EditAnywhere, Category = "Wetness|Performance", meta = (ClampMin = "1", AdvancedDisplay))
    int32 MaxPendingWetnessVerticesPerUpdate = 4096;

    UPROPERTY(EditAnywhere, Category = "Wetness|Performance", meta = (ClampMin = "0.0", AdvancedDisplay))
    float MinPendingWetnessAmount = 0.0001f;
};
