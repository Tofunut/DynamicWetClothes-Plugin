#pragma once

#include "CoreMinimal.h"

namespace DWCWetMaterialParameters
{
    DWC_API const FName& WetnessProfileMap0();
    DWC_API const FName& UseWetnessProfileMap0();

    DWC_API const FName& WrinkleNormalMap();
    DWC_API const FName& UseWrinkleNormalMap();
    DWC_API const FName& WrinkleStrength();
    DWC_API const FName& WrinkleWetnessMin();
    DWC_API const FName& WrinkleWetnessMax();

    DWC_API float DefaultWrinkleStrength();
    DWC_API float DefaultWrinkleWetnessMin();
    DWC_API float DefaultWrinkleWetnessMax();
}
