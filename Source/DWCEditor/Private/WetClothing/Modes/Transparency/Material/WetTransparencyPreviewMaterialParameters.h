#pragma once

#include "CoreMinimal.h"

namespace DWCTransparencyPreviewMaterialParameters
{
    inline const FName& TransparencyMap()
    {
        static const FName Name(TEXT("DWC_TransparencyPreviewMap"));
        return Name;
    }

    inline const FName& UseTransparencyMap()
    {
        static const FName Name(TEXT("DWC_UseTransparencyPreviewMap"));
        return Name;
    }

    inline const FName& TransparencyStrength()
    {
        static const FName Name(TEXT("DWC_TransparencyPreviewStrength"));
        return Name;
    }

    inline const FName& WrinkleSuppressionMap()
    {
        static const FName Name(TEXT("DWC_TransparencyPreviewSuppressionMap"));
        return Name;
    }

    inline const FName& UseWrinkleSuppressionMap()
    {
        static const FName Name(TEXT("DWC_UseTransparencyPreviewSuppression"));
        return Name;
    }

    inline const FName& WrinkleSuppressionStrength()
    {
        static const FName Name(TEXT("DWC_TransparencyPreviewWrinkleSuppressionStrength"));
        return Name;
    }
}
