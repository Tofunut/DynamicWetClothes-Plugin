#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "WetnessProfile.generated.h"

class UTexture2D;
class USkeletalMesh;

USTRUCT(BlueprintType)
struct DWC_API FAbsorbedWetnessProfileParameters
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Absorbed Wetness")
    bool bEnabled = true;

    /** Fraction of incoming water routed to the absorbed-wetness channel. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Absorbed Wetness", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float AbsorptionFraction = 0.5f;

    /** Absorption response multiplier. This is intentionally not a per-second rate. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Absorbed Wetness", meta = (ClampMin = "0.0", DisplayName = "Absorption Rate"))
    float AbsorptionRate = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Absorbed Wetness", meta = (ClampMin = "0.0"))
    float SpreadRate = 6.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Absorbed Wetness", meta = (ClampMin = "0.0", ClampMax = "100.0", UIMin = "0.0", UIMax = "100.0", Units = "Percent", DisplayName = "Dry Rate"))
    float DryRate = 20.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Absorbed Wetness", meta = (ClampMin = "0.0", DisplayName = "Gravity Spread Bias"))
    float GravityFlowStrength = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Absorbed Wetness|Rendering", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float AbsorbedDarkeningStrength = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Absorbed Wetness|Rendering", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float AbsorbedGlossinessStrength = 0.5f;

    float GetDryRatePerSecond() const
    {
        const float DryPercentPerSecond = FMath::Clamp(DryRate, 0.0f, 100.0f);
        const float RemainingFraction = FMath::Max(1.0f - DryPercentPerSecond * 0.01f, KINDA_SMALL_NUMBER);
        return -FMath::Loge(RemainingFraction);
    }
};

USTRUCT(BlueprintType)
struct DWC_API FSurfaceWaterProfileParameters
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water", meta = (DisplayName = "Enabled (GPU Simulation Only)", ToolTip = "Surface Water is available only when the component uses GPU Simulation."))
    bool bEnabled = false;

    /** Enables droplet stamp generation for this profile. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Droplet", meta = (EditCondition = "false", EditConditionHides))
    bool bEnableDroplets = true;

    /** Legacy representation scale. Surface Water now routes all unabsorbed water to droplets. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Simulation", meta = (ClampMin = "0.0", ClampMax = "1.0", EditCondition = "false", EditConditionHides))
    float SurfaceRepresentationFraction = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Droplet", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float DropletSpawnProbability = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Droplet", meta = (ClampMin = "0.01", Units = "s"))
    float DropletLifetimeSeconds = 5.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Droplet", meta = (ClampMin="0.0", ClampMax="256.0", DisplayName="Droplet Stamp Size"))
    float DropletRadiusPixels = 16.0f;


    /** Multiplies all Surface Water rendering response after final droplet coverage is resolved. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Rendering", meta=(ClampMin="0.0", ClampMax="1.0", DisplayName="Total Strength"))
    float SurfaceWaterTotalStrength = 0.5f;

    /** Roughness reached by fully visible surface water. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Rendering", meta=(ClampMin="0.0", ClampMax="1.0", DisplayName="Water Roughness"))
    float SurfaceWaterTargetRoughness = 0.02f;

    /** Strength applied to the droplet normal. 1.0 is the authored texture strength. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Rendering", meta=(ClampMin="0.0", ClampMax="3.0", DisplayName="Water Normal Strength"))
    float SurfaceWaterNormalStrength = 3.0f;

    /** Strength of the Surface Water roughness blend toward Wet Surface Roughness. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Rendering", meta=(ClampMin="0.0", ClampMax="1.0", DisplayName="Roughness Blend"))
    float SurfaceWaterRoughnessBlend = 0.85f;

    /** Specular reached by fully visible surface water. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Rendering", meta=(ClampMin="0.0", ClampMax="1.0", DisplayName="Water Specular"))
    float SurfaceWaterSpecular = 0.5f;

    /** Optional profile override. Null disables the Droplet normal contribution. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Droplet|Rendering")
    TObjectPtr<UTexture2D> DropletNormalTexture = nullptr;

    /** Optional mask used to localize the Droplet normal detail. Null means no authored mask. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Droplet|Rendering")
    TObjectPtr<UTexture2D> DropletMaskTexture = nullptr;

};

USTRUCT(BlueprintType)
struct DWC_API FWetnessProfileParameters
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Response", meta = (ShowOnlyInnerProperties))
    FAbsorbedWetnessProfileParameters AbsorbedWetness;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Response", meta = (ShowOnlyInnerProperties))
    FSurfaceWaterProfileParameters SurfaceWater;

    float GetAbsorptionFraction() const
    {
        return AbsorbedWetness.bEnabled ? FMath::Clamp(AbsorbedWetness.AbsorptionFraction, 0.0f, 1.0f) : 0.0f;
    }

    float GetAbsorptionRate() const
    {
        return AbsorbedWetness.bEnabled ? FMath::Max(0.0f, AbsorbedWetness.AbsorptionRate) : 0.0f;
    }

    bool SupportsAbsorbedWetness() const
    {
        return GetAbsorptionFraction() > 0.0f && GetAbsorptionRate() > 0.0f;
    }

    bool SupportsSurfaceWater() const
    {
        return SurfaceWater.bEnabled;
    }

    float GetAbsorptionMultiplier() const
    {
        return GetAbsorptionFraction() * GetAbsorptionRate();
    }

    float GetRejectedWaterFraction() const { return 1.0f - GetAbsorptionFraction(); }

    float GetSpreadRatePerSecond() const
    {
        return FMath::Max(0.0f, AbsorbedWetness.SpreadRate);
    }

    float GetDryRatePerSecond() const
    {
        const float DryPercentPerSecond = FMath::Clamp(AbsorbedWetness.DryRate, 0.0f, 100.0f);
        const float RemainingFraction = FMath::Max(1.0f - DryPercentPerSecond * 0.01f, KINDA_SMALL_NUMBER);
        return -FMath::Loge(RemainingFraction);
    }

    float GetGravityFlowStrength() const
    {
        return FMath::Max(0.0f, AbsorbedWetness.GravityFlowStrength);
    }

    float GetAbsorbedDarkeningStrength() const
    {
        return AbsorbedWetness.bEnabled
                   ? FMath::Clamp(AbsorbedWetness.AbsorbedDarkeningStrength, 0.0f, 1.0f)
                   : 0.0f;
    }

    float GetAbsorbedGlossinessStrength() const
    {
        return AbsorbedWetness.bEnabled
                   ? FMath::Clamp(AbsorbedWetness.AbsorbedGlossinessStrength, 0.0f, 1.0f)
                   : 0.0f;
    }

    float GetSurfaceWaterStrength() const { return SupportsSurfaceWater() ? 1.0f : 0.0f; }
};

UCLASS(BlueprintType)
class DWC_API UWetnessProfile : public UDataAsset
{
    GENERATED_BODY()

  public:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wetness Profile", meta = (ShowOnlyInnerProperties))
    FWetnessProfileParameters Parameters;

    const FWetnessProfileParameters& GetParameters() const { return Parameters; }

    UFUNCTION(BlueprintPure, Category = "Wetness Profile")
    float GetAbsorptionMultiplier() const { return Parameters.GetAbsorptionMultiplier(); }
    UFUNCTION(BlueprintPure, Category = "Wetness Profile")
    float GetSpreadRatePerSecond() const { return Parameters.GetSpreadRatePerSecond(); }
    UFUNCTION(BlueprintPure, Category = "Wetness Profile")
    float GetDryRatePerSecond() const { return Parameters.GetDryRatePerSecond(); }
    UFUNCTION(BlueprintPure, Category = "Wetness Profile")
    float GetGravityFlowStrength() const { return Parameters.GetGravityFlowStrength(); }
    UFUNCTION(BlueprintPure, Category = "Wetness Profile")
    float GetSurfaceWaterStrength() const { return Parameters.GetSurfaceWaterStrength(); }
    UFUNCTION(BlueprintPure, Category = "Wetness Profile")
    float GetAbsorbedDarkeningStrength() const { return Parameters.GetAbsorbedDarkeningStrength(); }
    UFUNCTION(BlueprintPure, Category = "Wetness Profile")
    float GetAbsorbedGlossinessStrength() const { return Parameters.GetAbsorbedGlossinessStrength(); }
#if WITH_EDITORONLY_DATA
    UPROPERTY(EditAnywhere, Category = "Wetness Profile")
    FString PreferredSaveDirectory = TEXT("/Game/WetnessProfiles");

    /** Editor preview mesh shown by default when this Wetness Profile is opened. */
    UPROPERTY(EditAnywhere, Category = "Wetness Profile|Preview")
    TObjectPtr<USkeletalMesh> PreviewSkeletalMesh = nullptr;
#endif
};
