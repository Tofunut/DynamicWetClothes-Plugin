#pragma once

#include "CoreMinimal.h"
#include "Core/DWCQualityLODProfile.h"

class FDWCQualityLODEvaluator
{
  public:
    void NormalizeScreenSizeThresholds(TArray<FDWCQualityLODScreenSizeThreshold>& Thresholds) const;
    bool ResolveLODFromScreenSize(
        const TArray<FDWCQualityLODScreenSizeThreshold>& Thresholds,
        float ScreenSize,
        int32& OutLODLevel) const;
};
