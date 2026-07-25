#include "DataAssets/WetnessProfile.h"

bool FWetnessProfileParameters::MigrateLegacyAbsorbedWetness()
{
    bool bMigrated = false;

    if (Absorption >= 0.0f)
    {
        AbsorbedWetness.bEnabled = true;
        AbsorbedWetness.AbsorptionFraction = 1.0f;
        AbsorbedWetness.AbsorptionRate = Absorption;
        Absorption = -1.0f;
        bMigrated = true;
    }

    if (SpreadRate >= 0.0f)
    {
        AbsorbedWetness.SpreadRate = SpreadRate;
        SpreadRate = -1.0f;
        bMigrated = true;
    }

    if (DryRate >= 0.0f)
    {
        AbsorbedWetness.DryRate = DryRate;
        DryRate = -1.0f;
        bMigrated = true;
    }

    if (GravityFlowStrength >= 0.0f)
    {
        AbsorbedWetness.GravityFlowStrength = GravityFlowStrength;
        GravityFlowStrength = -1.0f;
        bMigrated = true;
    }

    return bMigrated;
}

bool FWetnessProfileParameters::MigrateLegacySurfaceWaterRendering()
{
    bool bMigrated = false;
    FSurfaceWaterProfileParameters& Surface = SurfaceWater;

    if (Surface.NormalStrength >= 0.0f || Surface.FlowNormalStrength >= 0.0f)
    {
        const float DropletStrength = Surface.NormalStrength >= 0.0f
            ? Surface.NormalStrength
            : Surface.SurfaceWaterNormalStrength;
        const float RivuletStrength = Surface.FlowNormalStrength >= 0.0f
            ? Surface.FlowNormalStrength
            : Surface.SurfaceWaterNormalStrength;
        Surface.SurfaceWaterNormalStrength = FMath::Max(DropletStrength, RivuletStrength);
        Surface.NormalStrength = -1.0f;
        Surface.FlowNormalStrength = -1.0f;
        bMigrated = true;
    }

    if (Surface.SurfaceRoughness >= 0.0f || Surface.FlowRoughness >= 0.0f)
    {
        // The legacy fields were target roughness values. The new contract stores
        // only blend strength; preserve the fact that Surface Water roughness was
        // enabled and let the material-wide target define the final value.
        Surface.SurfaceWaterRoughnessStrength = 1.0f;
        Surface.SurfaceRoughness = -1.0f;
        Surface.FlowRoughness = -1.0f;
        bMigrated = true;
    }

    if (Surface.SurfaceAmountThresholdMin >= 0.0f ||
        Surface.SurfaceAmountThresholdMax >= 0.0f)
    {
        Surface.SurfaceVisibilityThreshold = FMath::Clamp(
            Surface.SurfaceAmountThresholdMin >= 0.0f
                ? Surface.SurfaceAmountThresholdMin
                : Surface.SurfaceVisibilityThreshold,
            0.0f,
            1.0f);
        Surface.SurfaceAmountThresholdMin = -1.0f;
        Surface.SurfaceAmountThresholdMax = -1.0f;
        bMigrated = true;
    }

    if (!FMath::IsNearlyZero(Surface.FlowPanningX) ||
        !FMath::IsNearlyZero(Surface.FlowPanningY))
    {
        Surface.RivuletUVScrollSpeed = FVector2D(
            Surface.FlowPanningX,
            Surface.FlowPanningY).Size();
        Surface.FlowPanningX = 0.0f;
        Surface.FlowPanningY = 0.0f;
        bMigrated = true;
    }

    if (Surface.RivuletNormalTexture == nullptr && Surface.FlowNormalTexture != nullptr)
    {
        Surface.RivuletNormalTexture = Surface.FlowNormalTexture;
        Surface.FlowNormalTexture = nullptr;
        bMigrated = true;
    }

    return bMigrated;
}

void UWetnessProfile::PostLoad()
{
    Super::PostLoad();
    Parameters.MigrateLegacyAbsorbedWetness();
    Parameters.MigrateLegacySurfaceWaterRendering();
}
