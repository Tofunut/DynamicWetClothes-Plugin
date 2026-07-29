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

    /** Half-size of stationary stamps written into the static Droplet RT. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Droplet", meta = (ClampMin = "0.0", ClampMax = "256.0", DisplayName = "Static Stamp Size"))
    float DropletRadiusPixels = 16.0f;

    /** Maximum number of concurrently alive static stamps for each Wet Part/profile. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Droplet", meta = (ClampMin = "1", ClampMax = "4096", UIMin = "1", UIMax = "1024", DisplayName = "Max Active Stamps"))
    int32 DropletMaxActiveStamps = 256;

    /** Enables independently stamped flowing droplets in the dedicated Flow Droplet RT. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Droplet|Flow")
    bool bEnableDropletFlow = false;

    /** Independent spawn probability for stamps written into the Flow Droplet RT. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Droplet|Flow", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float DropletFlowSpawnProbability = 0.5f;

    /** Lifetime of stamps written into the Flow Droplet RT. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Droplet|Flow", meta = (ClampMin = "0.01", Units = "s"))
    float DropletFlowLifetimeSeconds = 5.0f;

    /** Horizontal half-size of stamps written into the Flow Droplet RT. Kept under the legacy name for asset compatibility. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Droplet|Flow", meta = (ClampMin = "0.0", ClampMax = "256.0", DisplayName = "Flow Stamp Width"))
    float DropletFlowRadiusPixels = 16.0f;

    /** Vertical half-size of stamps written into the Flow Droplet RT. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Droplet|Flow", meta = (ClampMin = "0.0", ClampMax = "256.0", DisplayName = "Flow Stamp Height"))
    float DropletFlowHeightPixels = 32.0f;

    /** Blends the independently selected flow contact toward a random point in the same UV triangle. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Droplet|Flow", meta = (ClampMin = "0.0", ClampMax = "1.0", DisplayName = "Spawn Position Spread"))
    float DropletFlowSpawnPositionSpread = 0.35f;

    /** Maximum number of concurrently alive flow stamps for each Wet Part/profile. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Droplet|Flow", meta = (ClampMin = "1", ClampMax = "4096", UIMin = "1", UIMax = "1024", DisplayName = "Max Active Stamps"))
    int32 DropletFlowMaxActiveStamps = 256;

    /** Signed UV distance travelled per second. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Droplet|Flow", meta = (ClampMin = "-4.0", ClampMax = "4.0", UIMin = "-1.0", UIMax = "1.0"))
    float DropletFlowSpeed = 0.25f;

    /** UV distance per second used to advect the Flow Droplet RT along pose-dependent surface gravity. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Droplet|Flow", meta = (ClampMin = "0.0", ClampMax = "4.0", UIMin = "0.0", UIMax = "1.0"))
    float DropletFlowAdvectionSpeed = 0.08f;

    /** Flow direction in UV space. 0 points along +U and 90 points along +V. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Droplet|Flow", meta = (ClampMin = "-360.0", ClampMax = "360.0", UIMin = "-180.0", UIMax = "180.0", Units = "deg"))
    float DropletFlowDirectionDegrees = 90.0f;

    /** Optional normal texture for flowing droplets. Empty falls back to the static Droplet normal. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Droplet|Flow")
    TObjectPtr<UTexture2D> DropletFlowNormalTexture = nullptr;

    /** Optional mask texture for flowing droplets. Empty falls back to the static Droplet mask. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Droplet|Flow")
    TObjectPtr<UTexture2D> DropletFlowMaskTexture = nullptr;

    /** Optional grayscale noise used to bend the flowing UV path sideways. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Droplet|Flow")
    TObjectPtr<UTexture2D> DropletFlowNoiseTexture = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Droplet|Flow", meta = (ClampMin = "0.01", ClampMax = "64.0"))
    float DropletFlowNoiseTiling = 2.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Droplet|Flow", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float DropletFlowNoiseStrength = 0.05f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Droplet|Flow", meta = (ClampMin = "-4.0", ClampMax = "4.0", UIMin = "-1.0", UIMax = "1.0"))
    float DropletFlowNoiseSpeed = 0.15f;

    /** Roughness reached by fully visible surface water. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Rendering", meta=(ClampMin="0.0", ClampMax="1.0", DisplayName="Water Roughness"))
    float SurfaceWaterTargetRoughness = 0.02f;

    /** Normal-map strength for stationary droplets. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Rendering", meta=(ClampMin="0.0", ClampMax="3.0", DisplayName="Water Normal Strength"))
    float SurfaceWaterNormalStrength = 3.0f;

    /** Strength of the Surface Water roughness blend toward Wet Surface Roughness. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Rendering", meta=(ClampMin="0.0", ClampMax="1.0", DisplayName="Roughness Blend"))
    float SurfaceWaterRoughnessBlend = 0.85f;

    /** Overall Surface Water rendering strength after droplet coverage is resolved. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Rendering", meta=(ClampMin="0.0", ClampMax="1.0", DisplayName="Total Strength"))
    float SurfaceWaterTotalStrength = 0.5f;

    /** How strongly stationary droplets modify the underlying Base Color. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Rendering", meta=(ClampMin="0.0", ClampMax="1.0", DisplayName="Color Blend"))
    float SurfaceWaterColorBlend = 1.0f;

    /** Specular reached by fully visible surface water. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Rendering", meta=(ClampMin="0.0", ClampMax="1.0", DisplayName="Water Specular"))
    float SurfaceWaterSpecular = 0.5f;

    /** Roughness reached by fully visible flowing droplets. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Droplet|Flow|Rendering", meta=(ClampMin="0.0", ClampMax="1.0", DisplayName="Water Roughness"))
    float DropletFlowTargetRoughness = 0.02f;

    /** Strength of the Flow Droplet roughness blend toward Water Roughness. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Droplet|Flow|Rendering", meta=(ClampMin="0.0", ClampMax="1.0", DisplayName="Roughness Blend"))
    float DropletFlowRoughnessBlend = 0.85f;

    /** Overall Flow Droplet rendering strength after coverage is resolved. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Droplet|Flow|Rendering", meta=(ClampMin="0.0", ClampMax="1.0", DisplayName="Total Strength"))
    float DropletFlowTotalStrength = 0.5f;

    /** How strongly flowing droplets modify the underlying Base Color. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Droplet|Flow|Rendering", meta=(ClampMin="0.0", ClampMax="1.0", DisplayName="Color Blend"))
    float DropletFlowColorBlend = 1.0f;

    /** Normal-map strength for flowing droplets. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Droplet|Flow|Rendering", meta=(ClampMin="0.0", ClampMax="3.0", DisplayName="Water Normal Strength"))
    float DropletFlowNormalStrength = 3.0f;

    /** Specular reached by fully visible flowing droplets. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Droplet|Flow|Rendering", meta=(ClampMin="0.0", ClampMax="1.0", DisplayName="Water Specular"))
    float DropletFlowSpecular = 0.5f;

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

    static constexpr float AbsorptionMultiplierScale = 10.0f;

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
        return GetAbsorptionFraction() > 0.0f;
    }

    bool SupportsSurfaceWater() const
    {
        return SurfaceWater.bEnabled;
    }

    float GetAbsorptionMultiplier() const
    {
        return GetAbsorptionFraction() * AbsorptionMultiplierScale;
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
