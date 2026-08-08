// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "Core/DWCQualityLODEvaluator.h"

void FDWCQualityLODEvaluator::NormalizeScreenSizeThresholds(
    TArray<FDWCQualityLODScreenSizeThreshold>& Thresholds) const
{
    for (int32 LODLevel = 0; LODLevel < Thresholds.Num(); ++LODLevel)
    {
        Thresholds[LODLevel].LODLevel = LODLevel;
    }
}

bool FDWCQualityLODEvaluator::ResolveLODFromScreenSize(
    const TArray<FDWCQualityLODScreenSizeThreshold>& Thresholds,
    const float                                      ScreenSize,
    int32&                                           OutLODLevel) const
{
    OutLODLevel = INDEX_NONE;

    const float ClampedScreenSize = FMath::Clamp(ScreenSize, 0.0f, 1.0f);
    int32       BestLODLevel = INDEX_NONE;
    float       BestActivationScreenSize = -1.0f;
    int32       LowestLODLevel = INDEX_NONE;
    float       LowestActivationScreenSize = 1.0f + KINDA_SMALL_NUMBER;

    for (int32 LODLevel = 0; LODLevel < Thresholds.Num(); ++LODLevel)
    {
        const FDWCQualityLODScreenSizeThreshold& Candidate = Thresholds[LODLevel];
        const float                              ActivationScreenSize = FMath::Clamp(Candidate.ScreenSize, 0.0f, 1.0f);
        if (ActivationScreenSize < LowestActivationScreenSize)
        {
            LowestLODLevel = LODLevel;
            LowestActivationScreenSize = ActivationScreenSize;
        }

        if (ClampedScreenSize >= ActivationScreenSize && ActivationScreenSize > BestActivationScreenSize)
        {
            BestLODLevel = LODLevel;
            BestActivationScreenSize = ActivationScreenSize;
        }
    }

    const int32 SelectedLODLevel = BestLODLevel != INDEX_NONE ? BestLODLevel : LowestLODLevel;
    if (SelectedLODLevel == INDEX_NONE)
    {
        return false;
    }

    OutLODLevel = SelectedLODLevel;
    return true;
}
