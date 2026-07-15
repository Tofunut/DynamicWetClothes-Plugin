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

    if (WetVisualStrength >= 0.0f)
    {
        AbsorbedWetness.WetVisualStrength = WetVisualStrength;
        WetVisualStrength = -1.0f;
        bMigrated = true;
    }

    if (TransparencyStrength >= 0.0f)
    {
        AbsorbedWetness.TransparencyStrength = TransparencyStrength;
        TransparencyStrength = -1.0f;
        bMigrated = true;
    }

    return bMigrated;
}

void UWetnessProfile::PostLoad()
{
    Super::PostLoad();
    Parameters.MigrateLegacyAbsorbedWetness();
}
