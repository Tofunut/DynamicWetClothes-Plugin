#include "WetClothing/Modes/Transparency/Material/WetTransparencyPreviewMaterialBuilder.h"

#include "WetClothing/Modes/Wrinkle/Material/WetWrinklePreviewMaterialBuilder.h"

FWetTransparencyPreviewMaterialBuildResult FWetTransparencyPreviewMaterialBuilder::Build(
    const FWetTransparencyPreviewMaterialBuildArgs& Args)
{
    FWetTransparencyPreviewMaterialBuildResult Result;
    FWetWrinklePreviewMaterialBuildArgs WetPreviewArgs;
    WetPreviewArgs.SourceMaterial = Args.SourceMaterial;
    WetPreviewArgs.UVChannelIndex = Args.UVChannelIndex;
    WetPreviewArgs.bOverrideCpuWetnessInput = true;
    WetPreviewArgs.bBuildNormalOverlay = false;

    const FWetWrinklePreviewMaterialBuildResult WetPreviewResult =
        FWetWrinklePreviewMaterialBuilder::Build(WetPreviewArgs);
    if (!WetPreviewResult.bSucceeded)
    {
        Result.ErrorMessage = WetPreviewResult.ErrorMessage;
        return Result;
    }

    Result.TransientBaseMaterial = WetPreviewResult.TransientBaseMaterial;
    Result.TransientMaterialParent = WetPreviewResult.TransientMaterialParent;
    Result.PreviewMID = WetPreviewResult.PreviewMID;
    Result.bSucceeded = Result.PreviewMID != nullptr;
    return Result;
}
