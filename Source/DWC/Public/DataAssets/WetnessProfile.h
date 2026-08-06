#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "WetnessProfile.generated.h"

class UTexture2D;
class USkeletalMesh;

/**
 * Runtime-ready Absorbed Water simulation parameters.
 *
 * These values are dynamic solver inputs. They are materialized from the
 * authored profile when runtime simulation state or GPU compute buffers are
 * rebuilt, and must not be treated as GPU Simulation Map bake dependencies.
 */
USTRUCT(BlueprintType)
struct DWC_API FResolvedAbsorbedWaterSimulationParameters
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Absorbed Water|Simulation")
    float AbsorptionMultiplier = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Absorbed Water|Simulation")
    float SpreadRatePerSecond = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Absorbed Water|Simulation")
    float DryRatePerSecond = 0.0f;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Absorbed Water|Simulation")
    float GravityFlowStrength = 0.0f;

    bool Equals(
        const FResolvedAbsorbedWaterSimulationParameters& Other,
        const float Tolerance = KINDA_SMALL_NUMBER) const
    {
        return FMath::IsNearlyEqual(AbsorptionMultiplier, Other.AbsorptionMultiplier, Tolerance) &&
               FMath::IsNearlyEqual(SpreadRatePerSecond, Other.SpreadRatePerSecond, Tolerance) &&
               FMath::IsNearlyEqual(DryRatePerSecond, Other.DryRatePerSecond, Tolerance) &&
               FMath::IsNearlyEqual(GravityFlowStrength, Other.GravityFlowStrength, Tolerance);
    }
};

USTRUCT(BlueprintType)
struct DWC_API FAbsorbedWetnessProfileParameters
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Absorbed Wetness")
    bool bEnabled = true;

    /** Fraction of incoming water routed to the absorbed-wetness channel. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Absorbed Wetness|Simulation", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float AbsorptionFraction = 0.5f;

    /** Absorption response multiplier. This is intentionally not a per-second rate. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Absorbed Wetness|Simulation", meta = (ClampMin = "0.0", DisplayName = "Absorption Rate"))
    float AbsorptionRate = 1.0f;

    /** Maximum Pending Water stored by one GPU simulation texel. Zero keeps the legacy unlimited behavior. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Absorbed Wetness|Simulation", meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "1000.0", DisplayName = "Max Pending Water Per Pixel (GPU Only)"))
    float MaxPendingWaterPerPixel = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Absorbed Wetness|Simulation", meta = (ClampMin = "0.0"))
    float SpreadRate = 6.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Absorbed Wetness|Simulation", meta = (ClampMin = "0.0", ClampMax = "100.0", UIMin = "0.0", UIMax = "100.0", Units = "Percent", DisplayName = "Dry Rate"))
    float DryRate = 20.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Absorbed Wetness|Simulation", meta = (ClampMin = "0.0", DisplayName = "Gravity Spread Bias"))
    float GravityFlowStrength = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Absorbed Wetness|Rendering", meta = (ClampMin = "0.0", ClampMax = "3.0", UIMin = "0.0", UIMax = "3.0"))
    float AbsorbedDarkeningStrength = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Absorbed Wetness|Rendering", meta = (ClampMin = "0.0", ClampMax = "3.0", UIMin = "0.0", UIMax = "3.0"))
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

    /** Enables the optional Secondary Droplet layer without discarding its authored values. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water", meta = (DisplayName = "Use Secondary Droplets"))
    bool bUseSecondaryDroplets = true;

    /** Shared fade-out rate for wetness stored in the Droplet1 and Droplet2 RTs. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water", meta = (ClampMin = "0.0", ClampMax = "100.0", UIMin = "0.0", UIMax = "100.0", Units = "Percent", DisplayName = "Droplet Dry Rate"))
    float DropletDryRate = 20.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Droplet1", meta = (ClampMin = "0.0", ClampMax = "1.0", DisplayName = "Spawn Chance"))
    float DropletSpawnProbability = 0.5f;

    /** Horizontal half-size of stamps written into the Droplet1 RT. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Droplet1", meta = (ClampMin = "0.0", ClampMax = "256.0", DisplayName = "Stamp Width"))
    float DropletRadiusPixels = 16.0f;

    /** Vertical half-size of stamps written into the Droplet1 RT. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Droplet1", meta = (ClampMin = "0.0", ClampMax = "256.0", DisplayName = "Stamp Height"))
    float DropletHeightPixels = 16.0f;

    /** Independent spawn probability for stamps written into the Droplet2 RT. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Droplet2", meta = (ClampMin = "0.0", ClampMax = "1.0", DisplayName = "Spawn Chance"))
    float DropletFlowSpawnProbability = 0.5f;

    /** Horizontal half-size of stamps written into the Droplet2 RT. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Droplet2", meta = (ClampMin = "0.0", ClampMax = "256.0", DisplayName = "Stamp Width"))
    float DropletFlowRadiusPixels = 16.0f;

    /** Vertical half-size of stamps written into the Droplet2 RT. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Droplet2", meta = (ClampMin = "0.0", ClampMax = "256.0", DisplayName = "Stamp Height"))
    float DropletFlowHeightPixels = 32.0f;

    /** Blends the independently selected Droplet2 contact toward a random point in the same UV triangle. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Droplet2", meta = (ClampMin = "0.0", ClampMax = "1.0", DisplayName = "Spawn Spread Rate"))
    float DropletFlowSpawnPositionSpread = 0.35f;

    /** Optional normal texture for Droplet2. Empty uses the neutral flat-normal slice. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Droplet2|Rendering")
    TObjectPtr<UTexture2D> DropletFlowNormalTexture = nullptr;

    /** Optional mask texture for Droplet2. Empty uses the neutral unmasked slice. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Droplet2|Rendering")
    TObjectPtr<UTexture2D> DropletFlowMaskTexture = nullptr;

    /** Roughness reached by fully visible Droplet1 water. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Droplet1|Rendering", meta=(ClampMin="0.0", ClampMax="1.0", DisplayName="Water Roughness"))
    float SurfaceWaterTargetRoughness = 0.02f;

    /** Normal-map strength for Droplet1. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Droplet1|Rendering", meta=(ClampMin="0.0", ClampMax="3.0", DisplayName="Water Normal Strength"))
    float SurfaceWaterNormalStrength = 3.0f;

    /** Strength of the Surface Water roughness blend toward Wet Surface Roughness. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Droplet1|Rendering", meta=(ClampMin="0.0", ClampMax="1.0", DisplayName="Roughness Blend"))
    float SurfaceWaterRoughnessBlend = 0.85f;

    /** Overall Surface Water rendering strength after droplet coverage is resolved. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Droplet1|Rendering", meta=(ClampMin="0.0", ClampMax="1.0", DisplayName="Total Strength"))
    float SurfaceWaterTotalStrength = 0.5f;

    /** How strongly Droplet1 modifies the underlying Base Color. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Droplet1|Rendering", meta=(ClampMin="0.0", ClampMax="1.0", DisplayName="Color Blend"))
    float SurfaceWaterColorBlend = 1.0f;

    /** Specular reached by fully visible surface water. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Droplet1|Rendering", meta=(ClampMin="0.0", ClampMax="1.0", DisplayName="Water Specular"))
    float SurfaceWaterSpecular = 0.5f;

    /** Roughness reached by fully visible Droplet2 water. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Droplet2|Rendering", meta=(ClampMin="0.0", ClampMax="1.0", DisplayName="Water Roughness"))
    float DropletFlowTargetRoughness = 0.02f;

    /** Strength of the Droplet2 roughness blend toward Water Roughness. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Droplet2|Rendering", meta=(ClampMin="0.0", ClampMax="1.0", DisplayName="Roughness Blend"))
    float DropletFlowRoughnessBlend = 0.85f;

    /** Overall Droplet2 rendering strength after coverage is resolved. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Droplet2|Rendering", meta=(ClampMin="0.0", ClampMax="1.0", DisplayName="Total Strength"))
    float DropletFlowTotalStrength = 0.5f;

    /** How strongly Droplet2 modifies the underlying Base Color. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Droplet2|Rendering", meta=(ClampMin="0.0", ClampMax="1.0", DisplayName="Color Blend"))
    float DropletFlowColorBlend = 1.0f;

    /** Normal-map strength for Droplet2. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Droplet2|Rendering", meta=(ClampMin="0.0", ClampMax="3.0", DisplayName="Water Normal Strength"))
    float DropletFlowNormalStrength = 3.0f;

    /** Specular reached by fully visible Droplet2 water. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Droplet2|Rendering", meta=(ClampMin="0.0", ClampMax="1.0", DisplayName="Water Specular"))
    float DropletFlowSpecular = 0.5f;

    /** Optional profile override. Null disables the Droplet normal contribution. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Droplet1|Rendering")
    TObjectPtr<UTexture2D> DropletNormalTexture = nullptr;

    /** Optional mask used to localize the Droplet normal detail. Null means no authored mask. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Surface Water|Droplet1|Rendering")
    TObjectPtr<UTexture2D> DropletMaskTexture = nullptr;

    bool SupportsSecondaryDroplets() const
    {
        return bEnabled && bUseSecondaryDroplets;
    }

    float GetDropletDryRatePerSecond() const
    {
        if (!bEnabled)
        {
            return 0.0f;
        }

        const float DryPercentPerSecond = FMath::Clamp(DropletDryRate, 0.0f, 100.0f);
        const float RemainingFraction = FMath::Max(1.0f - DryPercentPerSecond * 0.01f, KINDA_SMALL_NUMBER);
        return -FMath::Loge(RemainingFraction);
    }

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

    float GetMaxPendingWaterPerPixel() const
    {
        return FMath::IsFinite(AbsorbedWetness.MaxPendingWaterPerPixel)
                   ? FMath::Max(0.0f, AbsorbedWetness.MaxPendingWaterPerPixel)
                   : 0.0f;
    }

    bool SupportsAbsorbedWetness() const
    {
        return GetAbsorptionFraction() > 0.0f;
    }

    bool SupportsSurfaceWater() const
    {
        return SurfaceWater.bEnabled;
    }

    /**
     * Resolves the authored Absorbed Water simulation settings into the exact
     * values consumed by the CPU solver and GPU compute buffers.
     */
    FResolvedAbsorbedWaterSimulationParameters ResolveAbsorbedWaterSimulation() const
    {
        FResolvedAbsorbedWaterSimulationParameters Result;
        Result.AbsorptionMultiplier = GetAbsorptionFraction() * AbsorptionMultiplierScale;
        Result.SpreadRatePerSecond = FMath::Max(0.0f, AbsorbedWetness.SpreadRate);

        const float DryPercentPerSecond = FMath::Clamp(AbsorbedWetness.DryRate, 0.0f, 100.0f);
        const float RemainingFraction =
            FMath::Max(1.0f - DryPercentPerSecond * 0.01f, KINDA_SMALL_NUMBER);
        Result.DryRatePerSecond = -FMath::Loge(RemainingFraction);
        Result.GravityFlowStrength = FMath::Max(0.0f, AbsorbedWetness.GravityFlowStrength);
        return Result;
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

    float GetDropletDryRatePerSecond() const
    {
        return SurfaceWater.GetDropletDryRatePerSecond();
    }

    float GetGravityFlowStrength() const
    {
        return FMath::Max(0.0f, AbsorbedWetness.GravityFlowStrength);
    }

    float GetAbsorbedDarkeningStrength() const
    {
        return AbsorbedWetness.bEnabled
                   ? FMath::Clamp(AbsorbedWetness.AbsorbedDarkeningStrength, 0.0f, 3.0f)
                   : 0.0f;
    }

    float GetAbsorbedGlossinessStrength() const
    {
        return AbsorbedWetness.bEnabled
                   ? FMath::Clamp(AbsorbedWetness.AbsorbedGlossinessStrength, 0.0f, 3.0f)
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

#if WITH_EDITORONLY_DATA
    /** Captures the last saved values used by the dedicated editor's per-property Revert buttons. */
    void CaptureEditorSavedParametersSnapshot()
    {
        EditorSavedParametersSnapshot = Parameters;
        bEditorHasSavedParametersSnapshot = true;
    }

    bool HasEditorSavedParametersSnapshot() const { return bEditorHasSavedParametersSnapshot; }
    const FWetnessProfileParameters& GetEditorSavedParametersSnapshot() const
    {
        return EditorSavedParametersSnapshot;
    }

    bool HasPreparedSurfaceTextures() const { return bHasPreparedSurfaceTextures; }
    UTexture2D* GetPreparedDropletNormalTexture() const { return PreparedDropletNormalTexture; }
    UTexture2D* GetPreparedDropletMaskTexture() const { return PreparedDropletMaskTexture; }
    UTexture2D* GetPreparedDroplet2NormalTexture() const { return PreparedDroplet2NormalTexture; }
    UTexture2D* GetPreparedDroplet2MaskTexture() const { return PreparedDroplet2MaskTexture; }
    void SetPreparedSurfaceTextures(
        UTexture2D* DropletNormal,
        UTexture2D* DropletMask,
        UTexture2D* Droplet2Normal,
        UTexture2D* Droplet2Mask)
    {
        PreparedDropletNormalTexture = DropletNormal;
        PreparedDropletMaskTexture = DropletMask;
        PreparedDroplet2NormalTexture = Droplet2Normal;
        PreparedDroplet2MaskTexture = Droplet2Mask;
        bHasPreparedSurfaceTextures = true;
    }
#endif

    UFUNCTION(BlueprintPure, Category = "Wetness Profile")
    float GetAbsorptionMultiplier() const { return Parameters.GetAbsorptionMultiplier(); }
    UFUNCTION(BlueprintPure, Category = "Wetness Profile")
    float GetSpreadRatePerSecond() const { return Parameters.GetSpreadRatePerSecond(); }
    UFUNCTION(BlueprintPure, Category = "Wetness Profile")
    float GetDryRatePerSecond() const { return Parameters.GetDryRatePerSecond(); }
    UFUNCTION(BlueprintPure, Category = "Wetness Profile")
    float GetDropletDryRatePerSecond() const { return Parameters.GetDropletDryRatePerSecond(); }
    UFUNCTION(BlueprintPure, Category = "Wetness Profile")
    float GetGravityFlowStrength() const { return Parameters.GetGravityFlowStrength(); }
    UFUNCTION(BlueprintPure, Category = "Wetness Profile")
    float GetMaxPendingWaterPerPixel() const { return Parameters.GetMaxPendingWaterPerPixel(); }
    UFUNCTION(BlueprintPure, Category = "Wetness Profile")
    float GetSurfaceWaterStrength() const { return Parameters.GetSurfaceWaterStrength(); }
    UFUNCTION(BlueprintPure, Category = "Wetness Profile")
    float GetAbsorbedDarkeningStrength() const { return Parameters.GetAbsorbedDarkeningStrength(); }
    UFUNCTION(BlueprintPure, Category = "Wetness Profile")
    float GetAbsorbedGlossinessStrength() const { return Parameters.GetAbsorbedGlossinessStrength(); }
#if WITH_EDITORONLY_DATA
    /** Snapshot of the values loaded from disk when the dedicated editor opened or last saved. */
    UPROPERTY(Transient)
    FWetnessProfileParameters EditorSavedParametersSnapshot;

    UPROPERTY(Transient)
    bool bEditorHasSavedParametersSnapshot = false;

    /** Latest array-compatible textures prepared directly from this WP while editing. */
    UPROPERTY()
    bool bHasPreparedSurfaceTextures = false;
    UPROPERTY()
    TObjectPtr<UTexture2D> PreparedDropletNormalTexture = nullptr;
    UPROPERTY()
    TObjectPtr<UTexture2D> PreparedDropletMaskTexture = nullptr;
    UPROPERTY()
    TObjectPtr<UTexture2D> PreparedDroplet2NormalTexture = nullptr;
    UPROPERTY()
    TObjectPtr<UTexture2D> PreparedDroplet2MaskTexture = nullptr;

    UPROPERTY(EditAnywhere, Category = "Wetness Profile")
    FString PreferredSaveDirectory = TEXT("/Game/WetnessProfiles");

    /** Editor preview mesh shown by default when this Wetness Profile is opened. */
    UPROPERTY(EditAnywhere, Category = "Wetness Profile|Preview")
    TObjectPtr<USkeletalMesh> PreviewSkeletalMesh = nullptr;

    /** Editor-only display filter. It does not enable or disable Droplet1 at runtime. */
    UPROPERTY(Transient)
    bool bEditorShowDroplet1 = true;

    /** Editor-only display filter. It does not enable or disable Droplet2 at runtime. */
    UPROPERTY(Transient)
    bool bEditorShowDroplet2 = false;

    /** Editor-only Details panel selection: 0 = Primary Droplet, 1 = Secondary Droplet. */
    UPROPERTY(Transient)
    uint8 EditorActiveDropletLayer = 0;
#endif
};
