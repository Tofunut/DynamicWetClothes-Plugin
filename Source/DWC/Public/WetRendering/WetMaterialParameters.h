#pragma once

#include "CoreMinimal.h"

namespace DWCWetMaterialParameters
{
    DWC_API const FName& WetnessMap();

    // Render-profile lookup resources. ProfileIDTexture is always sampled with DWC Data UV.
    DWC_API const FName& ProfileIDTexture();
    DWC_API const FName& ProfileRemapLUT();
    DWC_API const FName& GlobalRenderProfileLUT();
    DWC_API const FName& GlobalRenderProfileTexelSize();
    DWC_API const FName& UseRenderProfileLUT();
    DWC_API FName FallbackRenderProfileTexel(int32 TexelIndex);
    DWC_API const FName& DropletNormalTextureArray();
    DWC_API const FName& RivuletNormalTextureArray();
    DWC_API const FName& DropletUVTiling();
    DWC_API const FName& RivuletUVTiling();
    DWC_API const FName& SurfaceWaterTargetRoughness();
    DWC_API const FName& UseDropletNormal();
    DWC_API const FName& UseRivuletNormal();

    DWC_API const FName& WetPartDebugStrength();
    DWC_API const FName& SurfaceWaterDebugStrength();
    DWC_API const FName& SurfaceWaterDebugDropletColor();
    DWC_API const FName& SurfaceWaterDebugRivuletColor();
    DWC_API const FName& UnderColor();
    DWC_API const FName& UnderColorBlendStrength();
    DWC_API const FName& SurfaceWaterRT();
    DWC_API const FName& SurfaceDropletRT();
    DWC_API const FName& SurfaceRivuletRT();
    DWC_API const FName& SurfaceFlowRT(); // deprecated alias
    DWC_API const FName& SurfaceWaterTime();
    DWC_API const FName& SurfaceWaterTexelSize();
    DWC_API const FName& SurfaceWaterNormalStrength();
    DWC_API const FName& SurfaceWaterRoughness();
    DWC_API const FName& SurfaceDropletTiling();
    DWC_API const FName& SurfaceAmountThresholdMin();
    DWC_API const FName& SurfaceAmountThresholdMax();
    DWC_API const FName& SurfaceDropletMaskMin();
    DWC_API const FName& SurfaceDropletMaskMax();
    DWC_API const FName& SurfaceDropletMaskTexture();
    DWC_API const FName& SurfaceDropletNormalTexture();
    DWC_API const FName& SurfaceFlowTiling();
    DWC_API const FName& SurfaceFlowPanningX();
    DWC_API const FName& SurfaceFlowPanningY();
    DWC_API const FName& SurfaceFlowNormalStrength();
    DWC_API const FName& SurfaceFlowRoughness();
    DWC_API const FName& SurfaceFlowMaskMin();
    DWC_API const FName& SurfaceFlowMaskMax();
    DWC_API const FName& SurfaceFlowMaskTexture();
    DWC_API const FName& SurfaceFlowNormalTexture();


    DWC_API const FName& WrinkleNormalMap();
    DWC_API const FName& UseWrinkleNormalMap();
    DWC_API const FName& WrinkleStrength();
    DWC_API const FName& WrinkleWetnessMin();
    DWC_API const FName& WrinkleWetnessMax();

    DWC_API const FName& TransparencyMap();
    DWC_API const FName& UseTransparencyMap();
    DWC_API const FName& TransparencyWetnessMin();
    DWC_API const FName& TransparencyWetnessMax();
    DWC_API const FName& TransparencyUVChannel();
    DWC_API const FName& WrinkleSuppressionStrength();

    DWC_API float DefaultWrinkleStrength();
    DWC_API float DefaultWrinkleWetnessMin();
    DWC_API float DefaultWrinkleWetnessMax();

    DWC_API float DefaultTransparencyWetnessMin();
    DWC_API float DefaultTransparencyWetnessMax();
}
