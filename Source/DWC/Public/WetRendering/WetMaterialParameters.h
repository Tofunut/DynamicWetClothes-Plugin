#pragma once

#include "CoreMinimal.h"

namespace DWCWetMaterialParameters
{
    DWC_API const FName& WetnessMap();

    // Render-profile lookup resources. WetPartDataTexture is always sampled with DWC Data UV.
    DWC_API const FName& WetPartDataTexture();
    DWC_API const FName& ProfileRemapLUT();
    DWC_API const FName& GlobalRenderProfileLUT();
    DWC_API const FName& GlobalRenderProfileTexelSize();
    DWC_API const FName& UseRenderProfileLUT();
    DWC_API FName FallbackRenderProfileTexel(int32 TexelIndex);
    DWC_API const FName& DropletNormalTextureArray();
    DWC_API const FName& RivuletNormalTextureArray();
    DWC_API const FName& SurfaceWaterTargetRoughness();
    DWC_API const FName& UseSurfaceWater();
    DWC_API const FName& UseDropletNormal();
    DWC_API const FName& UseRivuletNormal();

    DWC_API const FName& WetPartDebugStrength();
    DWC_API const FName& SurfaceWaterDebugStrength();
    DWC_API const FName& SurfaceWaterDebugDropletColor();
    DWC_API const FName& SurfaceWaterDebugRivuletColor();
    DWC_API const FName& UnderColor();
    DWC_API const FName& UnderColorBlendStrength();
    DWC_API const FName& SurfaceDropletRT();
    DWC_API const FName& SurfaceRivuletRT();
    DWC_API const FName& SurfaceWaterTime();
    DWC_API const FName& SurfaceWaterTexelSize();


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
