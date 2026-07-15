#include "WetRendering/WetMaterialParameters.h"

namespace DWCWetMaterialParameters
{
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
