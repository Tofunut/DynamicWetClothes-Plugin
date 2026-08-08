// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "WetWrinkleNormalTextureData.generated.h"

USTRUCT(BlueprintType)
struct DWC_API FWetWrinkleNormalCorrectionSettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Correction", meta = (ClampMin = "0.0", ClampMax = "50.0", Units = "Percent"))
    float BorderPercent = 5.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Correction", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float FlatThreshold = 0.03f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Correction")
    bool bFlipGreen = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Correction", meta = (ClampMin = "0.0", ClampMax = "64.0"))
    float DeviationPreviewAmplify = 2.0f;
};

USTRUCT(BlueprintType)
struct DWC_API FWetWrinkleNormalBuildStats
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Build")
    FIntPoint SourceSize = FIntPoint::ZeroValue;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Build")
    FVector2D BackgroundAverageXY = FVector2D::ZeroVector;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Build")
    float FlatPixelRatio = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Build")
    float MaxXYDeviation = 0.0f;
};

struct DWC_API FWetWrinkleTexturePixelBuffer
{
    FIntPoint      Size = FIntPoint::ZeroValue;
    TArray<FColor> Pixels;

    bool IsValid() const
    {
        return Size.X > 0 && Size.Y > 0 && Pixels.Num() == Size.X * Size.Y;
    }
};

struct DWC_API FWetWrinkleTextureScalarBuffer
{
    FIntPoint     Size = FIntPoint::ZeroValue;
    TArray<float> Values;

    bool IsValid() const
    {
        return Size.X > 0 && Size.Y > 0 && Values.Num() == Size.X * Size.Y;
    }

    float SampleBilinear(const FVector2D& UV) const;
};

struct DWC_API FWetWrinkleNormalBuildOutput
{
    FWetWrinkleTexturePixelBuffer  CorrectedNormal;
    FWetWrinkleTexturePixelBuffer  DeviationPreview;
    FWetWrinkleTexturePixelBuffer  CorrectedDeviationPreview;
    FWetWrinkleTexturePixelBuffer  ConvexSeparationPreview;
    FWetWrinkleTextureScalarBuffer ConvexSeparation;
    FWetWrinkleNormalBuildStats    Stats;
};
