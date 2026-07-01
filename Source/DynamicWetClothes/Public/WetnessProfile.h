#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "WetnessProfile.generated.h"

USTRUCT(BlueprintType)
struct FWetnessProfileParameters
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Simulation")
    float Absorption = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Simulation")
    float SpreadRate = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Simulation", meta = (ClampMin = "0.0", ClampMax = "100.0", UIMin = "0.0", UIMax = "100.0", Units = "Percent", DisplayName = "Dry Rate"))
    float DryRate = 10.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Simulation")
    float GravityFlowStrength = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Absorbed Wetness Rendering")
    float WetVisualStrength = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Absorbed Wetness Rendering")
    float TransparencyStrength = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water Rendering")
    float SurfaceWaterStrength = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water Rendering")
    float RunoffStrength = 1.0f;

    float GetAbsorptionMultiplier() const
    {
        return FMath::Max(0.0f, Absorption);
    }

    float GetSpreadRatePerSecond() const
    {
        return FMath::Max(0.0f, SpreadRate);
    }

    float GetDryRatePerSecond() const
    {
        const float DryPercentPerSecond = FMath::Clamp(DryRate, 0.0f, 100.0f);
        const float RemainingFraction = FMath::Max(1.0f - DryPercentPerSecond * 0.01f, KINDA_SMALL_NUMBER);
        return -FMath::Loge(RemainingFraction);
    }

    float GetGravityFlowStrength() const
    {
        return FMath::Max(0.0f, GravityFlowStrength);
    }

    float GetSurfaceWaterStrength() const
    {
        return FMath::Max(0.0f, SurfaceWaterStrength);
    }

    float GetRunoffStrength() const
    {
        return FMath::Max(0.0f, RunoffStrength);
    }

    float GetWetVisualStrength() const
    {
        return FMath::Max(0.0f, WetVisualStrength);
    }

    float GetTransparencyStrength() const
    {
        return FMath::Max(0.0f, TransparencyStrength);
    }
};

UCLASS(BlueprintType)
class DYNAMICWETCLOTHES_API UWetnessProfile : public UDataAsset
{
    GENERATED_BODY()

  public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness Profile", meta = (ShowOnlyInnerProperties))
    FWetnessProfileParameters Parameters;

    const FWetnessProfileParameters& GetParameters() const
    {
        return Parameters;
    }

    UFUNCTION(BlueprintPure, Category = "Wetness Profile")
    float GetAbsorptionMultiplier() const
    {
        return Parameters.GetAbsorptionMultiplier();
    }

    UFUNCTION(BlueprintPure, Category = "Wetness Profile")
    float GetSpreadRatePerSecond() const
    {
        return Parameters.GetSpreadRatePerSecond();
    }

    UFUNCTION(BlueprintPure, Category = "Wetness Profile")
    float GetDryRatePerSecond() const
    {
        return Parameters.GetDryRatePerSecond();
    }

    UFUNCTION(BlueprintPure, Category = "Wetness Profile")
    float GetGravityFlowStrength() const
    {
        return Parameters.GetGravityFlowStrength();
    }

    UFUNCTION(BlueprintPure, Category = "Wetness Profile")
    float GetSurfaceWaterStrength() const
    {
        return Parameters.GetSurfaceWaterStrength();
    }

    UFUNCTION(BlueprintPure, Category = "Wetness Profile")
    float GetRunoffStrength() const
    {
        return Parameters.GetRunoffStrength();
    }

    UFUNCTION(BlueprintPure, Category = "Wetness Profile")
    float GetWetVisualStrength() const
    {
        return Parameters.GetWetVisualStrength();
    }

    UFUNCTION(BlueprintPure, Category = "Wetness Profile")
    float GetTransparencyStrength() const
    {
        return Parameters.GetTransparencyStrength();
    }

#if WITH_EDITORONLY_DATA
    UPROPERTY(EditAnywhere, Category = "Wetness Profile")
    FString PreferredSaveDirectory = TEXT("/Game/WetnessProfiles");
#endif
};
