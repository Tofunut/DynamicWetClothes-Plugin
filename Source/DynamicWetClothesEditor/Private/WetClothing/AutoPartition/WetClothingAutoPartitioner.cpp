/*
 *  텍스처 색상 샘플링 결과를 바탕으로 UV Island를 자동 파티션 클러스터로 묶는 로직을 구현합니다.
 */

#include "WetClothing/AutoPartition/WetClothingAutoPartitioner.h"

#include "WetClothing/Analysis/WetClothingAssetMeshAnalyzer.h"
#include "WetClothing/Texture/WetClothingTextureReadback.h"

namespace
{
    bool IsPointInsideTriangle(const FVector2D& Point, const FVector2D& A, const FVector2D& B, const FVector2D& C)
    {
        const auto Sign = [](const FVector2D& P1, const FVector2D& P2, const FVector2D& P3)
        {
            return (P1.X - P3.X) * (P2.Y - P3.Y) - (P2.X - P3.X) * (P1.Y - P3.Y);
        };

        const double D1 = Sign(Point, A, B);
        const double D2 = Sign(Point, B, C);
        const double D3 = Sign(Point, C, A);
        const bool   bHasNegative = D1 < 0.0 || D2 < 0.0 || D3 < 0.0;
        const bool   bHasPositive = D1 > 0.0 || D2 > 0.0 || D3 > 0.0;
        return !(bHasNegative && bHasPositive);
    }

    bool TryComputeIslandAverageColor(
        const FWetClothingAssetUVIsland& Island,
        const FWetClothingTextureReadback& TextureData,
        FWetClothingIslandColorStats&      OutStats)
    {
        if (!TextureData.IsValid())
        {
            return false;
        }

        FLinearColor WeightedColorSum = FLinearColor::Black;
        double       SampleWeight = 0.0;

        for (const FWetClothingAssetUVTriangle& Triangle : Island.UVTriangles)
        {
            const FVector2D& A = Triangle.UVs[0];
            const FVector2D& B = Triangle.UVs[1];
            const FVector2D& C = Triangle.UVs[2];

            const double MinU = FMath::Min3(A.X, B.X, C.X);
            const double MaxU = FMath::Max3(A.X, B.X, C.X);
            const double MinV = FMath::Min3(A.Y, B.Y, C.Y);
            const double MaxV = FMath::Max3(A.Y, B.Y, C.Y);

            const int32 MinX = FMath::Clamp(FMath::FloorToInt(MinU * TextureData.Width), 0, TextureData.Width - 1);
            const int32 MaxX = FMath::Clamp(FMath::FloorToInt(MaxU * TextureData.Width), 0, TextureData.Width - 1);
            const int32 MinY = FMath::Clamp(FMath::FloorToInt((1.0 - MaxV) * TextureData.Height), 0, TextureData.Height - 1);
            const int32 MaxY = FMath::Clamp(FMath::FloorToInt((1.0 - MinV) * TextureData.Height), 0, TextureData.Height - 1);

            double TriangleSampleWeight = 0.0;
            for (int32 PixelY = MinY; PixelY <= MaxY; ++PixelY)
            {
                for (int32 PixelX = MinX; PixelX <= MaxX; ++PixelX)
                {
                    const FVector2D SampleUV(
                        (static_cast<double>(PixelX) + 0.5) / TextureData.Width,
                        1.0 - ((static_cast<double>(PixelY) + 0.5) / TextureData.Height));

                    if (!IsPointInsideTriangle(SampleUV, A, B, C))
                    {
                        continue;
                    }

                    WeightedColorSum += TextureData.GetLinearColor(PixelX, PixelY);
                    ++SampleWeight;
                    ++TriangleSampleWeight;
                }
            }

            if (TriangleSampleWeight <= 0.0)
            {
                const FVector2D TriangleCenter = (A + B + C) / 3.0f;
                const int32     FallbackX = FMath::Clamp(FMath::FloorToInt(TriangleCenter.X * TextureData.Width), 0, TextureData.Width - 1);
                const int32     FallbackY = FMath::Clamp(FMath::FloorToInt((1.0 - TriangleCenter.Y) * TextureData.Height), 0, TextureData.Height - 1);
                WeightedColorSum += TextureData.GetLinearColor(FallbackX, FallbackY);
                ++SampleWeight;
            }
        }

        if (SampleWeight <= 0.0)
        {
            return false;
        }

        OutStats.IslandID = Island.IslandID;
        OutStats.UVArea = Island.UVArea;
        OutStats.SampleWeight = SampleWeight;
        OutStats.AverageColor = WeightedColorSum / static_cast<float>(SampleWeight);
        return true;
    }

    FLinearColor GetClusterAverageColor(const FWetClothingAutoPartitionCluster& Cluster)
    {
        return Cluster.SampleWeight > 0.0
                   ? Cluster.WeightedColorSum / static_cast<float>(Cluster.SampleWeight)
                   : FLinearColor::Black;
    }

    double ComputeColorDistancePercent(const FLinearColor& A, const FLinearColor& B)
    {
        const double DeltaR = A.R - B.R;
        const double DeltaG = A.G - B.G;
        const double DeltaB = A.B - B.B;
        return FMath::Sqrt((DeltaR * DeltaR + DeltaG * DeltaG + DeltaB * DeltaB) / 3.0) * 100.0;
    }
} // namespace

bool FWetClothingAutoPartitioner::BuildClusters(
    const TArray<TSharedPtr<FWetClothingAssetUVIsland>>& Islands,
    const FWetClothingTextureReadback&                     TextureData,
    float                                                  TolerancePercent,
    TArray<FWetClothingAutoPartitionCluster>&              OutClusters,
    FString*                                               OutErrorMessage)
{
    OutClusters.Reset();

    TArray<FWetClothingIslandColorStats> IslandStats;
    IslandStats.Reserve(Islands.Num());

    for (const TSharedPtr<FWetClothingAssetUVIsland>& IslandItem : Islands)
    {
        if (!IslandItem.IsValid())
        {
            continue;
        }

        FWetClothingIslandColorStats Stats;
        if (TryComputeIslandAverageColor(*IslandItem, TextureData, Stats))
        {
            IslandStats.Add(Stats);
        }
    }

    if (IslandStats.Num() == 0)
    {
        if (OutErrorMessage != nullptr)
        {
            *OutErrorMessage = TEXT("Auto-Partitioning could not extract average colors from the selected UV islands.");
        }
        return false;
    }

    IslandStats.Sort([](const FWetClothingIslandColorStats& A, const FWetClothingIslandColorStats& B)
                     {
        if (!FMath::IsNearlyEqual(A.UVArea, B.UVArea))
        {
            return A.UVArea > B.UVArea;
        }

        return A.IslandID < B.IslandID; });

    const double TolerancePercentDouble = TolerancePercent;

    for (const FWetClothingIslandColorStats& Stats : IslandStats)
    {
        int32  BestClusterIndex = INDEX_NONE;
        double BestDistance = TNumericLimits<double>::Max();

        for (int32 ClusterIndex = 0; ClusterIndex < OutClusters.Num(); ++ClusterIndex)
        {
            const double Distance = ComputeColorDistancePercent(Stats.AverageColor, GetClusterAverageColor(OutClusters[ClusterIndex]));
            if (Distance <= TolerancePercentDouble && Distance < BestDistance)
            {
                BestDistance = Distance;
                BestClusterIndex = ClusterIndex;
            }
        }

        if (BestClusterIndex == INDEX_NONE)
        {
            BestClusterIndex = OutClusters.AddDefaulted();
        }

        FWetClothingAutoPartitionCluster& Cluster = OutClusters[BestClusterIndex];
        Cluster.IslandIDs.Add(Stats.IslandID);
        Cluster.WeightedColorSum += Stats.AverageColor * static_cast<float>(Stats.SampleWeight);
        Cluster.SampleWeight += Stats.SampleWeight;
    }

    if (OutErrorMessage != nullptr)
    {
        OutErrorMessage->Reset();
    }
    return true;
}
