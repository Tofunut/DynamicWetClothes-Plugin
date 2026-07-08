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
}
