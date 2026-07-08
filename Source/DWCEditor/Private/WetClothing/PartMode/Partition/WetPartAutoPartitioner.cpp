/*
 *  텍스처 색상 샘플링 결과를 바탕으로 UV Island를 자동 파티션 클러스터로 묶는 로직을 구현합니다.
 */

#include "WetClothing/PartMode/Partition/WetPartAutoPartitioner.h"

#include "WetClothing/Common/Analysis/WetClothingAssetMeshAnalyzer.h"
#include "WetClothing/Common/Texture/WetClothingTextureReadback.h"
#include "WetClothing/Common/Texture/WetClothingTextureAddressUtils.h"

namespace
{
    struct FColorBinAccumulator
    {
        FLinearColor ColorSum = FLinearColor::Black;
        int32        Count = 0;
    };

    struct FLabColor
    {
        double L = 0.0;
        double A = 0.0;
        double B = 0.0;
    };

    double ComputeColorDistanceSquared(const FLinearColor& A, const FLinearColor& B)
    {
        const double DeltaR = A.R - B.R;
        const double DeltaG = A.G - B.G;
        const double DeltaB = A.B - B.B;
        return DeltaR * DeltaR + DeltaG * DeltaG + DeltaB * DeltaB;
    }

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

    const TCHAR* GetColorModeDebugName(EWetPartAutoPartitionColorMode ColorMode)
    {
        switch (ColorMode)
        {
        case EWetPartAutoPartitionColorMode::AverageColor:
            return TEXT("Average");

        case EWetPartAutoPartitionColorMode::MedianColor:
            return TEXT("Median");

        case EWetPartAutoPartitionColorMode::KMeansColor:
            return TEXT("K-Means");

        case EWetPartAutoPartitionColorMode::DominantColor:
        default:
            return TEXT("Dominant");
        }
    }

    FString BuildUVIslandIDListString(const TArray<int32>& UVIslandIDs)
    {
        TArray<FString> UVIslandIDStrings;
        UVIslandIDStrings.Reserve(UVIslandIDs.Num());
        for (const int32 UVIslandID : UVIslandIDs)
        {
            UVIslandIDStrings.Add(FString::FromInt(UVIslandID));
        }

        return FString::Printf(TEXT("[%s]"), *FString::Join(UVIslandIDStrings, TEXT(", ")));
    }

    const TCHAR* GetTextureAddressDebugName(TextureAddress AddressMode)
    {
        switch (AddressMode)
        {
        case TA_Wrap:
            return TEXT("Wrap");

        case TA_Mirror:
            return TEXT("Mirror");

        case TA_Clamp:
        default:
            return TEXT("Clamp");
        }
    }

    FVector2D ApplyTextureAddressToUV(const FVector2D& UV, const FVector2D& IslandCenter, const FWetClothingTextureReadback& TextureData)
    {
        return FVector2D(
            WetClothingTextureAddressUtils::Apply(UV.X, IslandCenter.X, TextureData.AddressX),
            WetClothingTextureAddressUtils::Apply(UV.Y, IslandCenter.Y, TextureData.AddressY));
    }

    FLabColor ConvertLinearRgbToLab(const FLinearColor& Color)
    {
        const double R = FMath::Clamp(static_cast<double>(Color.R), 0.0, 1.0);
        const double G = FMath::Clamp(static_cast<double>(Color.G), 0.0, 1.0);
        const double B = FMath::Clamp(static_cast<double>(Color.B), 0.0, 1.0);

        const double X = R * 0.4124564 + G * 0.3575761 + B * 0.1804375;
        const double Y = R * 0.2126729 + G * 0.7151522 + B * 0.0721750;
        const double Z = R * 0.0193339 + G * 0.1191920 + B * 0.9503041;

        constexpr double ReferenceX = 0.95047;
        constexpr double ReferenceY = 1.00000;
        constexpr double ReferenceZ = 1.08883;

        const auto LabPivot = [](double Value)
        {
            constexpr double Epsilon = 216.0 / 24389.0;
            constexpr double Kappa = 24389.0 / 27.0;
            return Value > Epsilon
                       ? FMath::Pow(Value, 1.0 / 3.0)
                       : (Kappa * Value + 16.0) / 116.0;
        };

        const double Fx = LabPivot(X / ReferenceX);
        const double Fy = LabPivot(Y / ReferenceY);
        const double Fz = LabPivot(Z / ReferenceZ);

        FLabColor Result;
        Result.L = 116.0 * Fy - 16.0;
        Result.A = 500.0 * (Fx - Fy);
        Result.B = 200.0 * (Fy - Fz);
        return Result;
    }

    FLinearColor ComputeAverageColor(const TArray<FLinearColor>& Samples, const FLinearColor& ColorSum)
    {
        return Samples.Num() > 0 ? ColorSum / static_cast<float>(Samples.Num()) : FLinearColor::Black;
    }

    FLinearColor ComputeMedianColor(const TArray<FLinearColor>& Samples)
    {
        if (Samples.Num() == 0)
        {
            return FLinearColor::Black;
        }

        TArray<float> RedValues;
        TArray<float> GreenValues;
        TArray<float> BlueValues;
        RedValues.Reserve(Samples.Num());
        GreenValues.Reserve(Samples.Num());
        BlueValues.Reserve(Samples.Num());

        for (const FLinearColor& Sample : Samples)
        {
            RedValues.Add(Sample.R);
            GreenValues.Add(Sample.G);
            BlueValues.Add(Sample.B);
        }

        RedValues.Sort();
        GreenValues.Sort();
        BlueValues.Sort();

        const auto GetMedian = [](const TArray<float>& Values)
        {
            const int32 MiddleIndex = Values.Num() / 2;
            return Values.Num() % 2 == 0
                       ? (Values[MiddleIndex - 1] + Values[MiddleIndex]) * 0.5f
                       : Values[MiddleIndex];
        };

        return FLinearColor(GetMedian(RedValues), GetMedian(GreenValues), GetMedian(BlueValues), 1.0f);
    }

    FLinearColor ComputeDominantColor(const TArray<FLinearColor>& Samples, const FLinearColor& ColorSum)
    {
        if (Samples.Num() == 0)
        {
            return FLinearColor::Black;
        }

        TMap<int32, FColorBinAccumulator> Bins;
        Bins.Reserve(Samples.Num());

        const auto QuantizeChannel = [](float Value)
        {
            constexpr int32 QuantizationLevels = 32;
            return FMath::Clamp(FMath::FloorToInt(FMath::Clamp(Value, 0.0f, 1.0f) * QuantizationLevels), 0, QuantizationLevels - 1);
        };

        for (const FLinearColor& Sample : Samples)
        {
            const int32 RedBin = QuantizeChannel(Sample.R);
            const int32 GreenBin = QuantizeChannel(Sample.G);
            const int32 BlueBin = QuantizeChannel(Sample.B);
            const int32 BinKey = RedBin | (GreenBin << 5) | (BlueBin << 10);

            FColorBinAccumulator& Bin = Bins.FindOrAdd(BinKey);
            Bin.ColorSum += Sample;
            ++Bin.Count;
        }

        int32                       BestBinKey = MAX_int32;
        const FColorBinAccumulator* BestBin = nullptr;
        for (const TPair<int32, FColorBinAccumulator>& BinPair : Bins)
        {
            if (BestBin == nullptr || BinPair.Value.Count > BestBin->Count || (BinPair.Value.Count == BestBin->Count && BinPair.Key < BestBinKey))
            {
                BestBinKey = BinPair.Key;
                BestBin = &BinPair.Value;
            }
        }

        return BestBin != nullptr && BestBin->Count > 0
                   ? BestBin->ColorSum / static_cast<float>(BestBin->Count)
                   : ComputeAverageColor(Samples, ColorSum);
    }

    FLinearColor ComputeKMeansColor(const TArray<FLinearColor>& Samples, const FLinearColor& ColorSum)
    {
        if (Samples.Num() <= 1)
        {
            return ComputeAverageColor(Samples, ColorSum);
        }

        constexpr int32 KMeansClusterCount = 3;
        constexpr int32 MaxIterations = 8;

        TArray<FLinearColor> Centers;
        Centers.Reserve(KMeansClusterCount);
        Centers.Add(ComputeAverageColor(Samples, ColorSum));

        const auto FindFarthestSampleFromCenters = [&Samples, &Centers]()
        {
            int32  BestSampleIndex = INDEX_NONE;
            double BestDistance = -1.0;

            for (int32 SampleIndex = 0; SampleIndex < Samples.Num(); ++SampleIndex)
            {
                double NearestDistance = TNumericLimits<double>::Max();
                for (const FLinearColor& Center : Centers)
                {
                    NearestDistance = FMath::Min(NearestDistance, ComputeColorDistanceSquared(Samples[SampleIndex], Center));
                }

                if (NearestDistance > BestDistance)
                {
                    BestDistance = NearestDistance;
                    BestSampleIndex = SampleIndex;
                }
            }

            return BestSampleIndex;
        };

        while (Centers.Num() < KMeansClusterCount && Centers.Num() < Samples.Num())
        {
            const int32 FarthestSampleIndex = FindFarthestSampleFromCenters();
            if (FarthestSampleIndex == INDEX_NONE || ComputeColorDistanceSquared(Samples[FarthestSampleIndex], Centers.Last()) <= KINDA_SMALL_NUMBER)
            {
                break;
            }

            Centers.Add(Samples[FarthestSampleIndex]);
        }

        if (Centers.Num() <= 1)
        {
            return ComputeAverageColor(Samples, ColorSum);
        }

        TArray<FLinearColor> ClusterSums;
        TArray<int32>        ClusterCounts;
        for (int32 Iteration = 0; Iteration < MaxIterations; ++Iteration)
        {
            ClusterSums.Init(FLinearColor::Black, Centers.Num());
            ClusterCounts.Init(0, Centers.Num());

            for (const FLinearColor& Sample : Samples)
            {
                int32  BestClusterIndex = 0;
                double BestDistance = ComputeColorDistanceSquared(Sample, Centers[0]);
                for (int32 ClusterIndex = 1; ClusterIndex < Centers.Num(); ++ClusterIndex)
                {
                    const double Distance = ComputeColorDistanceSquared(Sample, Centers[ClusterIndex]);
                    if (Distance < BestDistance)
                    {
                        BestDistance = Distance;
                        BestClusterIndex = ClusterIndex;
                    }
                }

                ClusterSums[BestClusterIndex] += Sample;
                ++ClusterCounts[BestClusterIndex];
            }

            for (int32 ClusterIndex = 0; ClusterIndex < Centers.Num(); ++ClusterIndex)
            {
                if (ClusterCounts[ClusterIndex] > 0)
                {
                    Centers[ClusterIndex] = ClusterSums[ClusterIndex] / static_cast<float>(ClusterCounts[ClusterIndex]);
                }
            }
        }

        ClusterSums.Init(FLinearColor::Black, Centers.Num());
        ClusterCounts.Init(0, Centers.Num());
        for (const FLinearColor& Sample : Samples)
        {
            int32  BestClusterIndex = 0;
            double BestDistance = ComputeColorDistanceSquared(Sample, Centers[0]);
            for (int32 ClusterIndex = 1; ClusterIndex < Centers.Num(); ++ClusterIndex)
            {
                const double Distance = ComputeColorDistanceSquared(Sample, Centers[ClusterIndex]);
                if (Distance < BestDistance)
                {
                    BestDistance = Distance;
                    BestClusterIndex = ClusterIndex;
                }
            }

            ClusterSums[BestClusterIndex] += Sample;
            ++ClusterCounts[BestClusterIndex];
        }

        int32 BestClusterIndex = INDEX_NONE;
        for (int32 ClusterIndex = 0; ClusterIndex < ClusterCounts.Num(); ++ClusterIndex)
        {
            if (ClusterCounts[ClusterIndex] > 0 && (BestClusterIndex == INDEX_NONE || ClusterCounts[ClusterIndex] > ClusterCounts[BestClusterIndex]))
            {
                BestClusterIndex = ClusterIndex;
            }
        }

        return BestClusterIndex != INDEX_NONE
                   ? ClusterSums[BestClusterIndex] / static_cast<float>(ClusterCounts[BestClusterIndex])
                   : ComputeAverageColor(Samples, ColorSum);
    }

    FLinearColor ComputeRepresentativeColor(
        const TArray<FLinearColor>&    Samples,
        const FLinearColor&            ColorSum,
        EWetPartAutoPartitionColorMode ColorMode)
    {
        switch (ColorMode)
        {
        case EWetPartAutoPartitionColorMode::AverageColor:
            return ComputeAverageColor(Samples, ColorSum);

        case EWetPartAutoPartitionColorMode::MedianColor:
            return ComputeMedianColor(Samples);

        case EWetPartAutoPartitionColorMode::KMeansColor:
            return ComputeKMeansColor(Samples, ColorSum);

        case EWetPartAutoPartitionColorMode::DominantColor:
        default:
            return ComputeDominantColor(Samples, ColorSum);
        }
    }

    bool TryComputeIslandAverageColor(
        const FWetClothingAssetUVIsland&   Island,
        const FWetClothingTextureReadback& TextureData,
        EWetPartAutoPartitionColorMode     ColorMode,
        FWetPartUVIslandColorStats&        OutStats)
    {
        if (!TextureData.IsValid())
        {
            return false;
        }

        FLinearColor         WeightedColorSum = FLinearColor::Black;
        double               SampleWeight = 0.0;
        TArray<FLinearColor> Samples;
        FBox2D               RawUVBounds(ForceInit);

        for (const FWetClothingAssetUVTriangle& Triangle : Island.UVTriangles)
        {
            RawUVBounds += Triangle.UVs[0];
            RawUVBounds += Triangle.UVs[1];
            RawUVBounds += Triangle.UVs[2];
        }

        const FVector2D IslandCenter = RawUVBounds.bIsValid ? (RawUVBounds.Min + RawUVBounds.Max) * 0.5 : FVector2D::ZeroVector;
        FBox2D          AddressedUVBounds(ForceInit);

        for (const FWetClothingAssetUVTriangle& Triangle : Island.UVTriangles)
        {
            const FVector2D A = ApplyTextureAddressToUV(Triangle.UVs[0], IslandCenter, TextureData);
            const FVector2D B = ApplyTextureAddressToUV(Triangle.UVs[1], IslandCenter, TextureData);
            const FVector2D C = ApplyTextureAddressToUV(Triangle.UVs[2], IslandCenter, TextureData);
            AddressedUVBounds += A;
            AddressedUVBounds += B;
            AddressedUVBounds += C;

            const double MinU = FMath::Min3(A.X, B.X, C.X);
            const double MaxU = FMath::Max3(A.X, B.X, C.X);
            const double MinV = FMath::Min3(A.Y, B.Y, C.Y);
            const double MaxV = FMath::Max3(A.Y, B.Y, C.Y);

            const int32 MinX = FMath::Clamp(FMath::FloorToInt(MinU * TextureData.Width), 0, TextureData.Width - 1);
            const int32 MaxX = FMath::Clamp(FMath::FloorToInt(MaxU * TextureData.Width), 0, TextureData.Width - 1);
            const int32 MinY = FMath::Clamp(FMath::FloorToInt(MinV * TextureData.Height), 0, TextureData.Height - 1);
            const int32 MaxY = FMath::Clamp(FMath::FloorToInt(MaxV * TextureData.Height), 0, TextureData.Height - 1);

            double TriangleSampleWeight = 0.0;
            for (int32 PixelY = MinY; PixelY <= MaxY; ++PixelY)
            {
                for (int32 PixelX = MinX; PixelX <= MaxX; ++PixelX)
                {
                    const FVector2D SampleUV(
                        (static_cast<double>(PixelX) + 0.5) / TextureData.Width,
                        (static_cast<double>(PixelY) + 0.5) / TextureData.Height);

                    if (!IsPointInsideTriangle(SampleUV, A, B, C))
                    {
                        continue;
                    }

                    const FLinearColor SampleColor = TextureData.GetLinearColor(PixelX, PixelY);
                    WeightedColorSum += SampleColor;
                    Samples.Add(SampleColor);
                    ++SampleWeight;
                    ++TriangleSampleWeight;
                }
            }

            if (TriangleSampleWeight <= 0.0)
            {
                const FVector2D    TriangleCenter = (A + B + C) / 3.0f;
                const int32        FallbackX = FMath::Clamp(FMath::FloorToInt(TriangleCenter.X * TextureData.Width), 0, TextureData.Width - 1);
                const int32        FallbackY = FMath::Clamp(FMath::FloorToInt(TriangleCenter.Y * TextureData.Height), 0, TextureData.Height - 1);
                const FLinearColor SampleColor = TextureData.GetLinearColor(FallbackX, FallbackY);
                WeightedColorSum += SampleColor;
                Samples.Add(SampleColor);
                ++SampleWeight;
            }
        }

        if (SampleWeight <= 0.0)
        {
            return false;
        }

        OutStats.UVIslandID = Island.UVIslandID;
        OutStats.UVArea = Island.UVArea;
        OutStats.SampleWeight = SampleWeight;
        OutStats.RepresentativeColor = ComputeRepresentativeColor(Samples, WeightedColorSum, ColorMode);
        UE_LOG(
            LogTemp,
            Display,
            TEXT("DWC AutoPartition: Island=%d UVAddress RawBounds=(%.4f, %.4f)-(%.4f, %.4f) Center=(%.4f, %.4f) AddressedBounds=(%.4f, %.4f)-(%.4f, %.4f) Address=(%s,%s)"),
            Island.UVIslandID,
            RawUVBounds.bIsValid ? RawUVBounds.Min.X : 0.0,
            RawUVBounds.bIsValid ? RawUVBounds.Min.Y : 0.0,
            RawUVBounds.bIsValid ? RawUVBounds.Max.X : 0.0,
            RawUVBounds.bIsValid ? RawUVBounds.Max.Y : 0.0,
            IslandCenter.X,
            IslandCenter.Y,
            AddressedUVBounds.bIsValid ? AddressedUVBounds.Min.X : 0.0,
            AddressedUVBounds.bIsValid ? AddressedUVBounds.Min.Y : 0.0,
            AddressedUVBounds.bIsValid ? AddressedUVBounds.Max.X : 0.0,
            AddressedUVBounds.bIsValid ? AddressedUVBounds.Max.Y : 0.0,
            GetTextureAddressDebugName(TextureData.AddressX),
            GetTextureAddressDebugName(TextureData.AddressY));
        return true;
    }

    FLinearColor GetClusterAverageColor(const FWetPartAutoPartitionCluster& Cluster)
    {
        return Cluster.SampleWeight > 0.0
                   ? Cluster.WeightedColorSum / static_cast<float>(Cluster.SampleWeight)
                   : FLinearColor::Black;
    }

    double ComputeDeltaE(const FLinearColor& A, const FLinearColor& B)
    {
        const FLabColor LabA = ConvertLinearRgbToLab(A);
        const FLabColor LabB = ConvertLinearRgbToLab(B);
        const double    DeltaL = LabA.L - LabB.L;
        const double    DeltaA = LabA.A - LabB.A;
        const double    DeltaB = LabA.B - LabB.B;
        return FMath::Sqrt(DeltaL * DeltaL + DeltaA * DeltaA + DeltaB * DeltaB);
    }
} // namespace

bool FWetPartAutoPartitioner::BuildClusters(
    const TArray<TSharedPtr<FWetClothingAssetUVIsland>>& Islands,
    const FWetClothingTextureReadback&                   TextureData,
    float                                                TolerancePercent,
    EWetPartAutoPartitionColorMode                       ColorMode,
    TArray<FWetPartAutoPartitionCluster>&                OutClusters,
    FString*                                             OutErrorMessage)
{
    OutClusters.Reset();

    TArray<FWetPartUVIslandColorStats> IslandStats;
    IslandStats.Reserve(Islands.Num());

    for (const TSharedPtr<FWetClothingAssetUVIsland>& IslandItem : Islands)
    {
        if (!IslandItem.IsValid())
        {
            continue;
        }

        FWetPartUVIslandColorStats Stats;
        if (TryComputeIslandAverageColor(*IslandItem, TextureData, ColorMode, Stats))
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

    IslandStats.Sort([](const FWetPartUVIslandColorStats& A, const FWetPartUVIslandColorStats& B)
                     {
        if (!FMath::IsNearlyEqual(A.UVArea, B.UVArea))
        {
            return A.UVArea > B.UVArea;
        }

        return A.UVIslandID < B.UVIslandID; });

    const double TolerancePercentDouble = TolerancePercent;
    UE_LOG(
        LogTemp,
        Display,
        TEXT("DWC AutoPartition: BuildClusters Mode=%s DeltaE Tolerance=%.2f Islands=%d Texture=%dx%d"),
        GetColorModeDebugName(ColorMode),
        TolerancePercent,
        IslandStats.Num(),
        TextureData.Width,
        TextureData.Height);

    for (const FWetPartUVIslandColorStats& Stats : IslandStats)
    {
        int32           BestClusterIndex = INDEX_NONE;
        double          BestDistance = TNumericLimits<double>::Max();
        const FLabColor IslandLab = ConvertLinearRgbToLab(Stats.RepresentativeColor);
        int32           NearestUVIslandID = INDEX_NONE;
        double          NearestIslandDistance = TNumericLimits<double>::Max();

        for (const FWetPartUVIslandColorStats& OtherStats : IslandStats)
        {
            if (OtherStats.UVIslandID == Stats.UVIslandID)
            {
                continue;
            }

            const double IslandDistance = ComputeDeltaE(Stats.RepresentativeColor, OtherStats.RepresentativeColor);
            if (IslandDistance < NearestIslandDistance)
            {
                NearestIslandDistance = IslandDistance;
                NearestUVIslandID = OtherStats.UVIslandID;
            }
        }

        UE_LOG(
            LogTemp,
            Display,
            TEXT("DWC AutoPartition: Island=%d Area=%.6f Samples=%.0f RepresentativeRGB=(%.4f, %.4f, %.4f) Lab=(%.2f, %.2f, %.2f) NearestIsland=%d NearestDeltaE=%.3f"),
            Stats.UVIslandID,
            Stats.UVArea,
            Stats.SampleWeight,
            Stats.RepresentativeColor.R,
            Stats.RepresentativeColor.G,
            Stats.RepresentativeColor.B,
            IslandLab.L,
            IslandLab.A,
            IslandLab.B,
            NearestUVIslandID,
            NearestIslandDistance);

        for (int32 ClusterIndex = 0; ClusterIndex < OutClusters.Num(); ++ClusterIndex)
        {
            const FLinearColor ClusterColor = GetClusterAverageColor(OutClusters[ClusterIndex]);
            const double       Distance = ComputeDeltaE(Stats.RepresentativeColor, ClusterColor);
            const FLabColor    ClusterLab = ConvertLinearRgbToLab(ClusterColor);
            UE_LOG(
                LogTemp,
                Display,
                TEXT("DWC AutoPartition:   Compare Island=%d -> Cluster=%d DeltaE=%.3f ClusterRGB=(%.4f, %.4f, %.4f) ClusterLab=(%.2f, %.2f, %.2f)"),
                Stats.UVIslandID,
                ClusterIndex + 1,
                Distance,
                ClusterColor.R,
                ClusterColor.G,
                ClusterColor.B,
                ClusterLab.L,
                ClusterLab.A,
                ClusterLab.B);

            if (Distance <= TolerancePercentDouble && Distance < BestDistance)
            {
                BestDistance = Distance;
                BestClusterIndex = ClusterIndex;
            }
        }

        if (BestClusterIndex == INDEX_NONE)
        {
            BestClusterIndex = OutClusters.AddDefaulted();
            UE_LOG(
                LogTemp,
                Display,
                TEXT("DWC AutoPartition:   Assign Island=%d -> New Cluster=%d"),
                Stats.UVIslandID,
                BestClusterIndex + 1);
        }
        else
        {
            UE_LOG(
                LogTemp,
                Display,
                TEXT("DWC AutoPartition:   Assign Island=%d -> Existing Cluster=%d DeltaE=%.3f"),
                Stats.UVIslandID,
                BestClusterIndex + 1,
                BestDistance);
        }

        FWetPartAutoPartitionCluster& Cluster = OutClusters[BestClusterIndex];
        Cluster.UVIslandIDs.Add(Stats.UVIslandID);
        Cluster.WeightedColorSum += Stats.RepresentativeColor * static_cast<float>(Stats.SampleWeight);
        Cluster.SampleWeight += Stats.SampleWeight;
    }

    for (int32 ClusterIndex = 0; ClusterIndex < OutClusters.Num(); ++ClusterIndex)
    {
        const FLinearColor ClusterColor = GetClusterAverageColor(OutClusters[ClusterIndex]);
        const FLabColor    ClusterLab = ConvertLinearRgbToLab(ClusterColor);
        const FString      IslandIDList = BuildUVIslandIDListString(OutClusters[ClusterIndex].UVIslandIDs);
        UE_LOG(
            LogTemp,
            Display,
            TEXT("DWC AutoPartition: Final Cluster=%d Islands=%d UVIslandIDs=%s Samples=%.0f RGB=(%.4f, %.4f, %.4f) Lab=(%.2f, %.2f, %.2f)"),
            ClusterIndex + 1,
            OutClusters[ClusterIndex].UVIslandIDs.Num(),
            *IslandIDList,
            OutClusters[ClusterIndex].SampleWeight,
            ClusterColor.R,
            ClusterColor.G,
            ClusterColor.B,
            ClusterLab.L,
            ClusterLab.A,
            ClusterLab.B);
    }

    if (OutErrorMessage != nullptr)
    {
        OutErrorMessage->Reset();
    }
    return true;
}
