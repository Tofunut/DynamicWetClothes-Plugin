#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "WetWrinklePreset.generated.h"

class UTexture2D;

USTRUCT(BlueprintType)
struct DWC_API FWetWrinklePresetCorrectionSettings
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
struct DWC_API FWetWrinklePresetSeparationSettings
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Separation", meta = (ClampMin = "0", ClampMax = "8"))
    int32 InputBlurRadiusPixels = 2;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Separation", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float ConvexityThreshold = 0.2f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Separation", meta = (ClampMin = "1", ClampMax = "1024"))
    int32 MinimumComponentPixels = 8;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Separation")
    bool bInvertConvexity = false;
};

USTRUCT(BlueprintType)
struct DWC_API FWetWrinklePresetBrushDefaults
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Brush", meta = (ClampMin = "0.01", Units = "cm"))
    float DefaultSizeCm = 8.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Brush", meta = (ClampMin = "0.0", ClampMax = "4.0"))
    float DefaultStrength = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Brush", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float DefaultFalloff = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Brush")
    FVector2D DefaultScale = FVector2D(1.0, 1.0);
};

USTRUCT(BlueprintType)
struct DWC_API FWetWrinklePresetBuildStats
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

struct DWC_API FWetWrinklePresetPixelBuffer
{
    FIntPoint Size = FIntPoint::ZeroValue;
    TArray<FColor> Pixels;

    bool IsValid() const
    {
        return Size.X > 0 && Size.Y > 0 && Pixels.Num() == Size.X * Size.Y;
    }
};

struct DWC_API FWetWrinklePresetScalarBuffer
{
    FIntPoint Size = FIntPoint::ZeroValue;
    TArray<float> Values;

    bool IsValid() const
    {
        return Size.X > 0 && Size.Y > 0 && Values.Num() == Size.X * Size.Y;
    }

    float SampleBilinear(const FVector2D& UV) const;
};

struct DWC_API FWetWrinklePresetBuildOutput
{
    FWetWrinklePresetPixelBuffer CorrectedNormal;
    FWetWrinklePresetPixelBuffer DeviationPreview;
    FWetWrinklePresetPixelBuffer CorrectedDeviationPreview;
    FWetWrinklePresetPixelBuffer ConvexSeparationPreview;
    FWetWrinklePresetScalarBuffer ConvexSeparation;
    FWetWrinklePresetBuildStats Stats;
};

UCLASS(BlueprintType)
class DWC_API UWetWrinklePreset : public UDataAsset
{
    GENERATED_BODY()

  public:
    bool HasValidSource() const;
    bool HasGeneratedTextures() const;
    bool IsBuildStale() const;
    bool IsUsableForBrush(FString* OutReason = nullptr) const;
    UTexture2D* GetNormalTextureForBrush() const;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wet Wrinkle Preset|Source")
    TObjectPtr<UTexture2D> SourceNormalTexture = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wet Wrinkle Preset|Build")
    bool bUseCorrection = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wet Wrinkle Preset|Correction")
    FWetWrinklePresetCorrectionSettings CorrectionSettings;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wet Wrinkle Preset|Separation")
    FWetWrinklePresetSeparationSettings SeparationSettings;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wet Wrinkle Preset|Brush")
    FWetWrinklePresetBrushDefaults BrushDefaults;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wet Wrinkle Preset|Generated")
    TObjectPtr<UTexture2D> CorrectedNormalTexture = nullptr;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wet Wrinkle Preset|Build")
    FWetWrinklePresetBuildStats BuildStats;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wet Wrinkle Preset|Build")
    FGuid BuildGuid;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Wet Wrinkle Preset|Build")
    FString BuildSignature;
};
