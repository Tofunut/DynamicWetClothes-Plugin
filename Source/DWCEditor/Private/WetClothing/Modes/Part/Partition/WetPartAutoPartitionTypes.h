//Copyright 2026 Team Tofunut. All Rights Reserved.
/*
 *  색상 기반 Auto Partition Preview 계산에 필요한 통계, 클러스터, 미리보기 데이터 타입을 정의합니다.
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
