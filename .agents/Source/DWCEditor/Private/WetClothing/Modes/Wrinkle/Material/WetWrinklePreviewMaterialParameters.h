#pragma once

#include "CoreMinimal.h"

namespace WetWrinklePreviewMaterialParameters
{
    inline const FName PreviewWetness(TEXT("DWC_PreviewWetness"));
    inline const FName AccumulatedNormal(TEXT("DWC_WrinklePreview_AccumulatedNormal"));
    inline const FName AccumulatedEnabled(TEXT("DWC_WrinklePreview_AccumulatedEnabled"));
    inline const FName AccumulatedStrength(TEXT("DWC_WrinklePreview_AccumulatedStrength"));
    inline const FName TransientRidgeNormal(TEXT("DWC_WrinklePreview_TransientRidgeNormal"));
    inline const FName TransientRidgeEnabled(TEXT("DWC_WrinklePreview_TransientRidgeEnabled"));
    inline const FName HoverNormal(TEXT("DWC_WrinklePreview_HoverNormal"));
    inline const FName HoverEnabled(TEXT("DWC_WrinklePreview_HoverEnabled"));
    inline const FName HoverCenterUV(TEXT("DWC_WrinklePreview_HoverCenterUV"));
    inline const FName HoverRadiusUV(TEXT("DWC_WrinklePreview_HoverRadiusUV"));
    inline const FName HoverRotation(TEXT("DWC_WrinklePreview_HoverRotation"));
    inline const FName HoverScale(TEXT("DWC_WrinklePreview_HoverScale"));
    inline const FName HoverStrength(TEXT("DWC_WrinklePreview_HoverStrength"));
    inline const FName HoverFalloff(TEXT("DWC_WrinklePreview_HoverFalloff"));
}
