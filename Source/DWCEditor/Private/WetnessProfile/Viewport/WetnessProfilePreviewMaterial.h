#pragma once

#include "CoreMinimal.h"

class UMaterialInterface;

namespace DWCWetnessProfilePreviewMaterial
{
    inline const FName AbsorbedWaterParameter(TEXT("DWCPreview_AbsorbedWater"));
    inline const FName SurfaceWaterParameter(TEXT("DWCPreview_SurfaceWater"));
    inline const FName AbsorbedEnabledParameter(TEXT("DWCPreview_AbsorbedEnabled"));
    inline const FName SurfaceEnabledParameter(TEXT("DWCPreview_SurfaceEnabled"));
    inline const FName AbsorbedDarkeningStrengthParameter(TEXT("DWCPreview_AbsorbedDarkeningStrength"));
    inline const FName AbsorbedGlossinessStrengthParameter(TEXT("DWCPreview_AbsorbedGlossinessStrength"));
    inline const FName SurfaceTargetRoughnessParameter(TEXT("DWCPreview_SurfaceTargetRoughness"));
    inline const FName SurfaceNormalStrengthParameter(TEXT("DWCPreview_SurfaceNormalStrength"));
    inline const FName SurfaceRoughnessBlendParameter(TEXT("DWCPreview_SurfaceRoughnessBlend"));
    inline const FName OriginalSurfaceDetailParameter(TEXT("DWCPreview_OriginalSurfaceDetail"));
    inline const FName SurfaceVisibilityThresholdParameter(TEXT("DWCPreview_SurfaceVisibilityThreshold"));
    inline const FName DropletsEnabledParameter(TEXT("DWCPreview_DropletsEnabled"));
    inline const FName RivuletsEnabledParameter(TEXT("DWCPreview_RivuletsEnabled"));
    inline const FName RivuletScrollSpeedParameter(TEXT("DWCPreview_RivuletScrollSpeed"));
    inline const FName DropletDetailSizeParameter(TEXT("DWCPreview_DropletDetailSize"));
    inline const FName RivuletDetailSizeParameter(TEXT("DWCPreview_RivuletDetailSize"));
    inline const FName DropletNormalTextureParameter(TEXT("DWCPreview_DropletNormal"));
    inline const FName RivuletNormalTextureParameter(TEXT("DWCPreview_RivuletNormal"));
    inline const FName DropletMaskTextureParameter(TEXT("DWCPreview_DropletMask"));
    inline const FName RivuletMaskTextureParameter(TEXT("DWCPreview_RivuletMask"));

    /** Loads the persistent, precompiled preview material without creating it. */
    UMaterialInterface* LoadBaseMaterial();

    /**
     * Loads the persistent preview material, creating and saving it only when the first
     * Wetness Profile viewport is opened and the asset is still missing. This must not
     * be called from module startup because MaterialEditingLibrary is not guaranteed to
     * be ready during that phase.
     */
    UMaterialInterface* LoadOrCreateBaseMaterial();
}
