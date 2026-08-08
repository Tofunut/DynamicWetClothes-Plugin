// Copyright 2026 Team Tofunut. All Rights Reserved.

/*
 * Defines statistics, cluster data, and preview types used by color-based Auto Partition calculations.
 */

#pragma once

#include "CoreMinimal.h"

enum class EWetPartAutoPartitionColorMode : uint8
{
    AverageColor,
    MedianColor,
    DominantColor,
    KMeansColor
};

struct FWetPartUVIslandColorStats
{
    int32        UVIslandID = INDEX_NONE;
    double       UVArea = 0.0;
    double       SampleWeight = 0.0;
    FLinearColor RepresentativeColor = FLinearColor::Black;
};

struct FWetPartAutoPartitionCluster
{
    TArray<int32> UVIslandIDs;
    FLinearColor  WeightedColorSum = FLinearColor::Black;
    double        SampleWeight = 0.0;
};
