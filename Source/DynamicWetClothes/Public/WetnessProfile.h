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

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Simulation")
    float DryRate = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Simulation")
    float GravityFlowStrength = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Surface")
    float SurfaceWaterStrength = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Surface")
    float RunoffStrength = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Rendering")
    float WetVisualStrength = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wet Rendering")
    float TransparencyStrength = 0.0f;

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
        return FMath::Max(0.0f, DryRate);
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
