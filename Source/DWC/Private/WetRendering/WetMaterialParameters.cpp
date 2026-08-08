// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "WetRendering/WetMaterialParameters.h"

namespace DWCWetMaterialParameters
{
#define DWC_DEFINE_MATERIAL_PARAMETER(Name, TextValue)     \
    const FName& Name()                                    \
    {                                                      \
        static const FName ParameterName(TEXT(TextValue)); \
        return ParameterName;                              \
    }

    DWC_DEFINE_MATERIAL_PARAMETER(WetnessMap, "DWC_WetnessMap")
    DWC_DEFINE_MATERIAL_PARAMETER(WetPartDataTexture, "DWC_WetPartDataTexture")
    DWC_DEFINE_MATERIAL_PARAMETER(ProfileRemapLUT, "DWC_ProfileRemapLUT")
    DWC_DEFINE_MATERIAL_PARAMETER(GlobalRenderProfileLUT, "DWC_GlobalRenderProfileLUT")
    DWC_DEFINE_MATERIAL_PARAMETER(GlobalRenderProfileTexelSize, "DWC_GlobalRenderProfileTexelSize")
    DWC_DEFINE_MATERIAL_PARAMETER(UseRenderProfileLUT, "DWC_UseRenderProfileLUT")
    DWC_DEFINE_MATERIAL_PARAMETER(DropletMaskTextureArray, "DWC_DropletMaskTextureArray")
    DWC_DEFINE_MATERIAL_PARAMETER(DropletNormalTextureArray, "DWC_DropletNormalTextureArray")
    DWC_DEFINE_MATERIAL_PARAMETER(SurfaceWaterTargetRoughness, "DWC_SurfaceWaterTargetRoughness")
    DWC_DEFINE_MATERIAL_PARAMETER(UseSurfaceWater, "DWC_UseSurfaceWater")
    DWC_DEFINE_MATERIAL_PARAMETER(UseDropletNormal, "DWC_UseDropletNormal")
    DWC_DEFINE_MATERIAL_PARAMETER(UseGPUBackend, "DWC_UseGPUBackend")
    DWC_DEFINE_MATERIAL_PARAMETER(WetPartDebugStrength, "DWC_WetPartDebugStrength")
    DWC_DEFINE_MATERIAL_PARAMETER(SurfaceWaterDebugStrength, "DWC_SurfaceWaterDebugStrength")
    DWC_DEFINE_MATERIAL_PARAMETER(SurfaceWaterDebugDropletColor, "DWC_SurfaceWaterDebugDropletColor")
    DWC_DEFINE_MATERIAL_PARAMETER(UnderColor, "DWC_UnderColor")
    DWC_DEFINE_MATERIAL_PARAMETER(UnderColorBlendStrength, "DWC_UnderColorBlendStrength")
    DWC_DEFINE_MATERIAL_PARAMETER(SurfaceDroplet1RT, "DWC_SurfaceDroplet1RT")
    DWC_DEFINE_MATERIAL_PARAMETER(SurfaceDroplet2RT, "DWC_SurfaceDroplet2RT")
    DWC_DEFINE_MATERIAL_PARAMETER(Droplet1RenderingEnabled, "DWC_Droplet1RenderingEnabled")
    DWC_DEFINE_MATERIAL_PARAMETER(Droplet2RenderingEnabled, "DWC_Droplet2RenderingEnabled")
    DWC_DEFINE_MATERIAL_PARAMETER(SurfaceWaterTexelSize, "DWC_SurfaceWaterTexelSize")

#undef DWC_DEFINE_MATERIAL_PARAMETER

    FName FallbackRenderProfileTexel(const int32 TexelIndex)
    {
        return FName(*FString::Printf(TEXT("DWC_FallbackRenderProfile%d"), TexelIndex));
    }

    const FName& WrinkleNormalMap()
    {
        static const FName Name(TEXT("DWC_WrinkleNormalMap"));
        return Name;
    }

    const FName& UseWrinkleNormalMap()
    {
        static const FName Name(TEXT("DWC_UseWrinkleNormalMap"));
        return Name;
    }

    const FName& WrinkleStrength()
    {
        static const FName Name(TEXT("DWC_WrinkleStrength"));
        return Name;
    }

    const FName& WrinkleWetnessMin()
    {
        static const FName Name(TEXT("DWC_WrinkleWetnessMin"));
        return Name;
    }

    const FName& WrinkleWetnessMax()
    {
        static const FName Name(TEXT("DWC_WrinkleWetnessMax"));
        return Name;
    }

    const FName& TransparencyMap()
    {
        static const FName Name(TEXT("DWC_TransparencyMap"));
        return Name;
    }

    const FName& UseTransparencyMap()
    {
        static const FName Name(TEXT("DWC_UseTransparencyMap"));
        return Name;
    }

    const FName& TransparencyWetnessMin()
    {
        static const FName Name(TEXT("DWC_TransparencyWetnessMin"));
        return Name;
    }

    const FName& TransparencyWetnessMax()
    {
        static const FName Name(TEXT("DWC_TransparencyWetnessMax"));
        return Name;
    }

    const FName& TransparencyUVChannel()
    {
        static const FName Name(TEXT("DWC_TransparencyUVChannel"));
        return Name;
    }

    const FName& WrinkleSuppressionStrength()
    {
        static const FName Name(TEXT("DWC_WrinkleSuppressionStrength"));
        return Name;
    }

    float DefaultWrinkleStrength()
    {
        return 1.0f;
    }

    float DefaultWrinkleWetnessMin()
    {
        return 0.25f;
    }

    float DefaultWrinkleWetnessMax()
    {
        return 1.0f;
    }

    float DefaultTransparencyWetnessMin()
    {
        return 0.0f;
    }

    float DefaultTransparencyWetnessMax()
    {
        return 1.0f;
    }
} // namespace DWCWetMaterialParameters
