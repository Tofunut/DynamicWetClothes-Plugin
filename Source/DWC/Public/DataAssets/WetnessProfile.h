#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "WetnessProfile.generated.h"

class UTexture2D;

USTRUCT(BlueprintType)
struct DWC_API FAbsorbedWetnessProfileParameters
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Absorbed Wetness")
    bool bEnabled = true;

    /** Fraction of incoming water routed to the absorbed-wetness channel. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Absorbed Wetness", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float AbsorptionFraction = 0.5f;

    /** Legacy absorption response multiplier. This is intentionally not a per-second rate. */
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

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water")
    bool bEnabled = false;

    /** Enables droplet stamp generation for this profile. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Droplet")
    bool bEnableDroplets = true;

    /** Enables rivulet stamp generation for this profile. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Rivulet")
    bool bEnableRivulets = true;

    /** Fraction of water rejected by absorption that is represented visually. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float SurfaceRepresentationFraction = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Droplet", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float DropletSpawnProbability = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Rivulet", meta = (ClampMin = "0.0", ClampMax = "1.0", DisplayName = "Rivulet Spawn Probability"))
    float FlowSpawnProbability = 0.2f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Droplet", meta = (ClampMin = "0.0"))
    float DropletIntensityMultiplier = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Rivulet", meta = (ClampMin = "0.0", DisplayName = "Rivulet Intensity Multiplier"))
    float FlowIntensityMultiplier = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Droplet", meta = (ClampMin = "0.01", Units = "s"))
    float DropletLifetimeSeconds = 5.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Droplet", meta = (ClampMin="0.0", ClampMax="256.0", DisplayName="Base Radius (RT Pixels)"))
    float DropletRadiusPixels = 16.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Rivulet", meta = (ClampMin = "0.01", Units = "s", DisplayName = "Rivulet Lifetime"))
    float FlowLifetimeSeconds = 7.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Rivulet", meta = (ClampMin = "0.0", DisplayName = "Minimum Rivulet Surface Amount"))
    float MinimumFlowSurfaceAmount = 0.2f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Rivulet", meta = (ClampMin="0.0", ClampMax="256.0", DisplayName="Rivulet Base Width (RT Pixels)"))
    float FlowWidthPixels = 8.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Rivulet", meta = (ClampMin="0.0", ClampMax="512.0", DisplayName="Rivulet Base Length (RT Pixels)"))
    float FlowLengthPixels = 48.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Rendering", meta=(ClampMin="0.001"))
    float MaterialTimeUpdateInterval = 1.0f / 30.0f;

    /** Shared strength applied to both droplet and rivulet normals. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Rendering", meta=(ClampMin="0.0", ClampMax="8.0"))
    float SurfaceWaterNormalStrength = 1.0f;

    /** Strength of the Surface Water roughness blend toward the material-wide target roughness. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Rendering", meta=(ClampMin="0.0", ClampMax="1.0"))
    float SurfaceWaterRoughnessStrength = 0.5f;

    /** Minimum visible RT amount before Surface Water rendering begins. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Rendering", meta=(ClampMin="0.0", ClampMax="1.0"))
    float SurfaceVisibilityThreshold = 0.2f;

    /** UV scroll speed along the decoded rivulet flow direction. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Rivulet|Rendering")
    float RivuletUVScrollSpeed = 0.5f;

    // Legacy mask fields are serialized only so old assets can load and resave.
    UPROPERTY()
    float FlowMaskMin = -1.0f;

    UPROPERTY()
    float FlowMaskMax = -1.0f;

    UPROPERTY()
    TObjectPtr<UTexture2D> FlowMaskTexture = nullptr;

    /** Optional profile override. Null disables the rivulet normal contribution. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Rivulet|Rendering")
    TObjectPtr<UTexture2D> RivuletNormalTexture = nullptr;

    UPROPERTY()
    float DropletMaskMin = -1.0f;

    UPROPERTY()
    float DropletMaskMax = -1.0f;

    UPROPERTY()
    TObjectPtr<UTexture2D> DropletMaskTexture = nullptr;

    /** Optional profile override. Null disables the normal contribution. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Droplet|Rendering")
    TObjectPtr<UTexture2D> DropletNormalTexture = nullptr;

    // Legacy rendering fields kept for one compatibility cycle. PostLoad migrates
    // these values into the consolidated rendering parameters above.
    UPROPERTY()
    float NormalStrength = -1.0f;

    UPROPERTY()
    float SurfaceRoughness = -1.0f;

    UPROPERTY()
    float FlowTiling = 1.0f;

    UPROPERTY()
    float FlowPanningX = 0.0f;

    UPROPERTY()
    float FlowPanningY = 0.0f;

    UPROPERTY()
    float FlowNormalStrength = -1.0f;

    UPROPERTY()
    float FlowRoughness = -1.0f;

    UPROPERTY()
    TObjectPtr<UTexture2D> FlowNormalTexture = nullptr;

    UPROPERTY()
    float DropletTiling = 1.0f;

    UPROPERTY()
    float SurfaceAmountThresholdMin = -1.0f;

    UPROPERTY()
    float SurfaceAmountThresholdMax = -1.0f;

};

USTRUCT(BlueprintType)
struct DWC_API FWetnessProfileParameters
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Response", meta = (ShowOnlyInnerProperties))
    FAbsorbedWetnessProfileParameters AbsorbedWetness;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Water Response", meta = (ShowOnlyInnerProperties))
    FSurfaceWaterProfileParameters SurfaceWater;

    // Hidden compatibility fields for profiles saved before absorbed and surface
    // water parameters were split into nested structures. Keep their original
    // serialized names until all existing assets have been loaded and resaved.
    UPROPERTY()
    float Absorption = -1.0f;

    UPROPERTY()
    float SpreadRate = -1.0f;

    UPROPERTY()
    float DryRate = -1.0f;

    UPROPERTY()
    float GravityFlowStrength = -1.0f;

    bool MigrateLegacyAbsorbedWetness();
    bool MigrateLegacySurfaceWaterRendering();

    float GetAbsorptionFraction() const
    {
        return AbsorbedWetness.bEnabled ? FMath::Clamp(AbsorbedWetness.AbsorptionFraction, 0.0f, 1.0f) : 0.0f;
    }

    float GetAbsorptionRate() const
    {
        const float EffectiveRate = Absorption >= 0.0f ? Absorption : AbsorbedWetness.AbsorptionRate;
        return AbsorbedWetness.bEnabled ? FMath::Max(0.0f, EffectiveRate) : 0.0f;
    }

    bool SupportsAbsorbedWetness() const
    {
        return GetAbsorptionFraction() > 0.0f && GetAbsorptionRate() > 0.0f;
    }

    bool SupportsSurfaceWater() const
    {
        return SurfaceWater.bEnabled && SurfaceWater.SurfaceRepresentationFraction > 0.0f;
    }

    float GetAbsorptionMultiplier() const
    {
        return GetAbsorptionFraction() * GetAbsorptionRate();
    }

    float GetRejectedWaterFraction() const { return 1.0f - GetAbsorptionFraction(); }

    float GetSpreadRatePerSecond() const
    {
        return FMath::Max(0.0f, SpreadRate >= 0.0f ? SpreadRate : AbsorbedWetness.SpreadRate);
    }

    float GetDryRatePerSecond() const
    {
        const float DryPercentPerSecond = FMath::Clamp(DryRate >= 0.0f ? DryRate : AbsorbedWetness.DryRate, 0.0f, 100.0f);
        const float RemainingFraction = FMath::Max(1.0f - DryPercentPerSecond * 0.01f, KINDA_SMALL_NUMBER);
        return -FMath::Loge(RemainingFraction);
    }

    float GetGravityFlowStrength() const
    {
        return FMath::Max(0.0f, GravityFlowStrength >= 0.0f ? GravityFlowStrength : AbsorbedWetness.GravityFlowStrength);
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

    /** Legacy material-map compatibility. Surface visibility itself comes from the independent RTs. */
    float GetSurfaceWaterStrength() const { return SurfaceWater.bEnabled ? 1.0f : 0.0f; }
    float GetRunoffStrength() const { return SurfaceWater.bEnabled ? FMath::Max(0.0f, SurfaceWater.FlowIntensityMultiplier) : 0.0f; }
};

UCLASS(BlueprintType)
class DWC_API UWetnessProfile : public UDataAsset
{
    GENERATED_BODY()

  public:
    virtual void PostLoad() override;

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
    float GetRunoffStrength() const { return Parameters.GetRunoffStrength(); }
    UFUNCTION(BlueprintPure, Category = "Wetness Profile")
    float GetAbsorbedDarkeningStrength() const { return Parameters.GetAbsorbedDarkeningStrength(); }
    UFUNCTION(BlueprintPure, Category = "Wetness Profile")
    float GetAbsorbedGlossinessStrength() const { return Parameters.GetAbsorbedGlossinessStrength(); }
#if WITH_EDITORONLY_DATA
    UPROPERTY(EditAnywhere, Category = "Wetness Profile")
    FString PreferredSaveDirectory = TEXT("/Game/WetnessProfiles");
#endif
};
