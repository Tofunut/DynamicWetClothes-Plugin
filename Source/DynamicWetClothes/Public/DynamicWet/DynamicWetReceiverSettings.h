#pragma once

#include "CoreMinimal.h"
#include "DynamicWetReceiverSettings.generated.h"

USTRUCT(BlueprintType)
struct DYNAMICWETCLOTHES_API FDynamicWetReceiverSettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, Category = "Wetness")
    float WetnessUpdateInterval = 0.1f;

    UPROPERTY(EditAnywhere, Category = "Wetness", meta = (ClampMin = "0.0"))
    float MaxStoredWetness = 1.0f;

    UPROPERTY(EditAnywhere, Category = "Wetness|Visual", meta = (ClampMin = "0.001"))
    float VisualSaturationWetness = 1.0f;

    UPROPERTY(EditAnywhere, Category = "Wetness", meta = (ClampMin = "0.0"))
    float WetnessDryHoldDuration = 0.3f;

    UPROPERTY(EditAnywhere, Category = "Wetness|Capillary", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float CapillaryImmediateAbsorptionFraction = 0.65f;

    UPROPERTY(EditAnywhere, Category = "Wetness|Capillary", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float CrossWetPartSpreadScale = 1.0f;

    UPROPERTY(EditAnywhere, Category = "Wetness|Rain", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
    float RainExposureMin = 0.5f;

    UPROPERTY(EditAnywhere, Category = "Wetness|Rain", meta = (ClampMin = "-1.0", ClampMax = "1.0"))
    float RainExposureMax = 0.9f;

    UPROPERTY(EditAnywhere, Category = "Wetness|Performance", meta = (ClampMin = "1"))
    int32 MaxPendingWetnessVerticesPerUpdate = 4096;

    UPROPERTY(EditAnywhere, Category = "Wetness|Performance", meta = (ClampMin = "0.0"))
    float MinPendingWetnessAmount = 0.0001f;
};
