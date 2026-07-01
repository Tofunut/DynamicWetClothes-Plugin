/*
 *  색상 기반 Auto Partition Preview 계산에 필요한 통계, 클러스터, 미리보기 데이터 타입을 정의합니다.
 */

#pragma once

#include "CoreMinimal.h"

enum class EWetClothingAutoPartitionColorMode : uint8
{
    AverageColor,
    MedianColor,
    DominantColor,
    KMeansColor
};

struct FWetClothingIslandColorStats
{
    int32        IslandID = INDEX_NONE;
    double       UVArea = 0.0;
    double       SampleWeight = 0.0;
    FLinearColor RepresentativeColor = FLinearColor::Black;
};

struct FWetClothingAutoPartitionCluster
{
    TArray<int32> IslandIDs;
    FLinearColor  WeightedColorSum = FLinearColor::Black;
    double        SampleWeight = 0.0;
};
