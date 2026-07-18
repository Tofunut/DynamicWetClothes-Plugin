#include "WetRendering/WetMaterialParameters.h"

namespace DWCWetMaterialParameters
{
    #define DWC_DEFINE_MATERIAL_PARAMETER(Name, TextValue) \
        const FName& Name() \
        { \
            static const FName ParameterName(TEXT(TextValue)); \
            return ParameterName; \
        }

    DWC_DEFINE_MATERIAL_PARAMETER(WetnessMap, "DWC_WetnessMap")
    DWC_DEFINE_MATERIAL_PARAMETER(WetPartDebugStrength, "DWC_WetPartDebugStrength")
    DWC_DEFINE_MATERIAL_PARAMETER(UnderColor, "DWC_UnderColor")
    DWC_DEFINE_MATERIAL_PARAMETER(UnderColorBlendStrength, "DWC_UnderColorBlendStrength")
    DWC_DEFINE_MATERIAL_PARAMETER(SurfaceWaterRT, "DWC_SurfaceWaterRT")
    DWC_DEFINE_MATERIAL_PARAMETER(SurfaceDropletRT, "DWC_SurfaceDropletRT")
    DWC_DEFINE_MATERIAL_PARAMETER(SurfaceFlowRT, "DWC_SurfaceFlowRT")
    DWC_DEFINE_MATERIAL_PARAMETER(SurfaceWaterTime, "DWC_SurfaceWaterTime")
    DWC_DEFINE_MATERIAL_PARAMETER(SurfaceWaterTexelSize, "DWC_SurfaceWaterTexelSize")
    DWC_DEFINE_MATERIAL_PARAMETER(SurfaceWaterNormalStrength, "DWC_SurfaceWaterNormalStrength")
    DWC_DEFINE_MATERIAL_PARAMETER(SurfaceWaterRoughness, "DWC_SurfaceWaterRoughness")
    DWC_DEFINE_MATERIAL_PARAMETER(SurfaceDropletTiling, "DWC_SurfaceDropletTiling")
    DWC_DEFINE_MATERIAL_PARAMETER(SurfaceAmountThresholdMin, "DWC_SurfaceAmountThresholdMin")
    DWC_DEFINE_MATERIAL_PARAMETER(SurfaceAmountThresholdMax, "DWC_SurfaceAmountThresholdMax")
    DWC_DEFINE_MATERIAL_PARAMETER(SurfaceDropletMaskMin, "DWC_SurfaceDropletMaskMin")
    DWC_DEFINE_MATERIAL_PARAMETER(SurfaceDropletMaskMax, "DWC_SurfaceDropletMaskMax")
    DWC_DEFINE_MATERIAL_PARAMETER(SurfaceDropletMaskTexture, "DWC_SurfaceDropletMaskTexture")
    DWC_DEFINE_MATERIAL_PARAMETER(SurfaceDropletNormalTexture, "DWC_SurfaceDropletNormalTexture")
    DWC_DEFINE_MATERIAL_PARAMETER(SurfaceFlowTiling, "DWC_SurfaceFlowTiling")
    DWC_DEFINE_MATERIAL_PARAMETER(SurfaceFlowPanningX, "DWC_SurfaceFlowPanningX")
    DWC_DEFINE_MATERIAL_PARAMETER(SurfaceFlowPanningY, "DWC_SurfaceFlowPanningY")
    DWC_DEFINE_MATERIAL_PARAMETER(SurfaceFlowNormalStrength, "DWC_SurfaceFlowNormalStrength")
    DWC_DEFINE_MATERIAL_PARAMETER(SurfaceFlowRoughness, "DWC_SurfaceFlowRoughness")
    DWC_DEFINE_MATERIAL_PARAMETER(SurfaceFlowMaskMin, "DWC_SurfaceFlowMaskMin")
    DWC_DEFINE_MATERIAL_PARAMETER(SurfaceFlowMaskMax, "DWC_SurfaceFlowMaskMax")
    DWC_DEFINE_MATERIAL_PARAMETER(SurfaceFlowMaskTexture, "DWC_SurfaceFlowMaskTexture")
    DWC_DEFINE_MATERIAL_PARAMETER(SurfaceFlowNormalTexture, "DWC_SurfaceFlowNormalTexture")

    #undef DWC_DEFINE_MATERIAL_PARAMETER

    const FName& WetnessProfileMap0()
    {
        static const FName Name(TEXT("DWC_WetnessProfileMap0"));
        return Name;
    }

    const FName& UseWetnessProfileMap0()
    {
        static const FName Name(TEXT("DWC_UseWetnessProfileMap0"));
        return Name;
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

    const FName& TransparencyStrength()
    {
        static const FName Name(TEXT("DWC_TransparencyStrength"));
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
}
