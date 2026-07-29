#include "DWCDataUVPacker.h"

#include "WetClothing/Foundation/UV/DWCUVGeometry.h"

namespace DWCDataUVPackerPrivate
{
    struct FPackingRecord
    {
        int32 ChartIndex = INDEX_NONE;
        FVector2D DesiredSize = FVector2D::ZeroVector;
        FVector2D PackedMin = FVector2D::ZeroVector;
        FVector2D PackedSize = FVector2D::ZeroVector;
    };

    struct FFreeRect
    {
        FVector2D Min = FVector2D::ZeroVector;
        FVector2D Size = FVector2D::ZeroVector;

        double Right() const { return Min.X + Size.X; }
        double Top() const { return Min.Y + Size.Y; }
    };

    static FVector2D GetSafeRawSize(const FDWCDataUVChart& Chart)
    {
        FVector2D RawSize = Chart.RawBounds.GetSize();
        RawSize.X = FMath::Max(RawSize.X, 1.0e-4);
        RawSize.Y = FMath::Max(RawSize.Y, 1.0e-4);
        return RawSize;
    }

    static void PruneContainedFreeRects(TArray<FFreeRect>& FreeRects)
    {
        for (int32 A = FreeRects.Num() - 1; A >= 0; --A)
        {
            const FFreeRect RectA = FreeRects[A];
            bool bContained = false;
            for (int32 B = 0; B < FreeRects.Num(); ++B)
            {
                if (A == B)
                {
                    continue;
                }

                const FFreeRect& RectB = FreeRects[B];
                constexpr double Tolerance = 1.0e-9;
                if (RectA.Min.X >= RectB.Min.X - Tolerance &&
                    RectA.Min.Y >= RectB.Min.Y - Tolerance &&
                    RectA.Right() <= RectB.Right() + Tolerance &&
                    RectA.Top() <= RectB.Top() + Tolerance)
                {
                    bContained = true;
                    break;
                }
            }

            if (bContained)
            {
                FreeRects.RemoveAtSwap(A, 1, EAllowShrinking::No);
            }
        }
    }

    static void SplitFreeRects(TArray<FFreeRect>& FreeRects, const FFreeRect& UsedRect)
    {
        for (int32 RectIndex = FreeRects.Num() - 1; RectIndex >= 0; --RectIndex)
        {
            const FFreeRect FreeRect = FreeRects[RectIndex];
            const bool bIntersects =
                UsedRect.Min.X < FreeRect.Right() &&
                UsedRect.Right() > FreeRect.Min.X &&
                UsedRect.Min.Y < FreeRect.Top() &&
                UsedRect.Top() > FreeRect.Min.Y;
            if (!bIntersects)
            {
                continue;
            }

            FreeRects.RemoveAtSwap(RectIndex, 1, EAllowShrinking::No);

            if (UsedRect.Min.X > FreeRect.Min.X)
            {
                FreeRects.Add({ FreeRect.Min, FVector2D(UsedRect.Min.X - FreeRect.Min.X, FreeRect.Size.Y) });
            }
            if (UsedRect.Right() < FreeRect.Right())
            {
                FreeRects.Add({
                    FVector2D(UsedRect.Right(), FreeRect.Min.Y),
                    FVector2D(FreeRect.Right() - UsedRect.Right(), FreeRect.Size.Y) });
            }
            if (UsedRect.Min.Y > FreeRect.Min.Y)
            {
                FreeRects.Add({ FreeRect.Min, FVector2D(FreeRect.Size.X, UsedRect.Min.Y - FreeRect.Min.Y) });
            }
            if (UsedRect.Top() < FreeRect.Top())
            {
                FreeRects.Add({
                    FVector2D(FreeRect.Min.X, UsedRect.Top()),
                    FVector2D(FreeRect.Size.X, FreeRect.Top() - UsedRect.Top()) });
            }
        }

        FreeRects.RemoveAllSwap(
            [](const FFreeRect& Rect)
            {
                return Rect.Size.X <= 1.0e-9 || Rect.Size.Y <= 1.0e-9;
            },
            EAllowShrinking::No);
        PruneContainedFreeRects(FreeRects);
    }

    static bool TryPackRecords(
        TArray<FPackingRecord>& Records,
        const FVector2D& AtlasMin,
        const FVector2D& AtlasSize,
        const double IslandPaddingUV,
        const double Scale)
    {
        TArray<FFreeRect> FreeRects;
        FreeRects.Add({ AtlasMin, AtlasSize });

        for (FPackingRecord& Record : Records)
        {
            const FVector2D ContentSize(
                FMath::Max(Record.DesiredSize.X * Scale, 1.0e-7),
                FMath::Max(Record.DesiredSize.Y * Scale, 1.0e-7));
            const FVector2D PaddedSize = ContentSize + FVector2D(IslandPaddingUV * 2.0, IslandPaddingUV * 2.0);

            int32 BestRectIndex = INDEX_NONE;
            double BestShortSide = TNumericLimits<double>::Max();
            double BestLongSide = TNumericLimits<double>::Max();
            double BestAreaWaste = TNumericLimits<double>::Max();

            for (int32 RectIndex = 0; RectIndex < FreeRects.Num(); ++RectIndex)
            {
                const FFreeRect& FreeRect = FreeRects[RectIndex];
                if (PaddedSize.X > FreeRect.Size.X + 1.0e-9 ||
                    PaddedSize.Y > FreeRect.Size.Y + 1.0e-9)
                {
                    continue;
                }

                const double LeftoverX = FreeRect.Size.X - PaddedSize.X;
                const double LeftoverY = FreeRect.Size.Y - PaddedSize.Y;
                const double ShortSide = FMath::Min(LeftoverX, LeftoverY);
                const double LongSide = FMath::Max(LeftoverX, LeftoverY);
                const double AreaWaste = FreeRect.Size.X * FreeRect.Size.Y - PaddedSize.X * PaddedSize.Y;

                if (ShortSide < BestShortSide ||
                    (FMath::IsNearlyEqual(ShortSide, BestShortSide) && LongSide < BestLongSide) ||
                    (FMath::IsNearlyEqual(ShortSide, BestShortSide) &&
                     FMath::IsNearlyEqual(LongSide, BestLongSide) &&
                     AreaWaste < BestAreaWaste))
                {
                    BestRectIndex = RectIndex;
                    BestShortSide = ShortSide;
                    BestLongSide = LongSide;
                    BestAreaWaste = AreaWaste;
                }
            }

            if (BestRectIndex == INDEX_NONE)
            {
                return false;
            }

            const FFreeRect PlacementRect = {
                FreeRects[BestRectIndex].Min,
                PaddedSize
            };
            Record.PackedMin = PlacementRect.Min + FVector2D(IslandPaddingUV, IslandPaddingUV);
            Record.PackedSize = ContentSize;
            SplitFreeRects(FreeRects, PlacementRect);
        }

        return true;
    }

    static void PackRecordsInGrid(
        TArray<FPackingRecord>& Records,
        const FVector2D& AtlasMin,
        const FVector2D& AtlasSize,
        const double IslandPaddingUV)
    {
        if (Records.IsEmpty())
        {
            return;
        }

        Records.Sort(
            [](const FPackingRecord& A, const FPackingRecord& B)
            {
                const double AreaA = A.DesiredSize.X * A.DesiredSize.Y;
                const double AreaB = B.DesiredSize.X * B.DesiredSize.Y;
                return !FMath::IsNearlyEqual(AreaA, AreaB) ? AreaA > AreaB : A.ChartIndex < B.ChartIndex;
            });

        const int32 ColumnCount = FMath::CeilToInt(FMath::Sqrt(static_cast<double>(Records.Num())));
        const int32 RowCount = FMath::DivideAndRoundUp(Records.Num(), ColumnCount);
        const FVector2D CellSize(
            AtlasSize.X / static_cast<double>(ColumnCount),
            AtlasSize.Y / static_cast<double>(RowCount));
        const double CellPadding = FMath::Min(
            IslandPaddingUV,
            FMath::Max(FMath::Min(CellSize.X, CellSize.Y) * 0.25, 0.0));
        const FVector2D ContentCellSize(
            FMath::Max(CellSize.X - CellPadding * 2.0, 1.0e-7),
            FMath::Max(CellSize.Y - CellPadding * 2.0, 1.0e-7));

        for (int32 RecordIndex = 0; RecordIndex < Records.Num(); ++RecordIndex)
        {
            FPackingRecord& Record = Records[RecordIndex];
            const int32 ColumnIndex = RecordIndex % ColumnCount;
            const int32 RowIndex = RecordIndex / ColumnCount;
            const FVector2D CellMin = AtlasMin + FVector2D(
                static_cast<double>(ColumnIndex) * CellSize.X,
                static_cast<double>(RowIndex) * CellSize.Y);
            const double UniformScale = FMath::Min(
                ContentCellSize.X / FMath::Max(Record.DesiredSize.X, 1.0e-7),
                ContentCellSize.Y / FMath::Max(Record.DesiredSize.Y, 1.0e-7));
            Record.PackedSize = Record.DesiredSize * UniformScale;
            Record.PackedMin = CellMin + FVector2D(CellPadding, CellPadding) +
                (ContentCellSize - Record.PackedSize) * 0.5;
        }
    }

    static bool PackRecordsWithMaximumScale(
        TArray<FPackingRecord>& Records,
        const FVector2D& AtlasMin,
        const FVector2D& AtlasSize,
        const double IslandPaddingUV)
    {
        if (Records.IsEmpty())
        {
            return true;
        }

        Records.Sort(
            [](const FPackingRecord& A, const FPackingRecord& B)
            {
                const double AreaA = A.DesiredSize.X * A.DesiredSize.Y;
                const double AreaB = B.DesiredSize.X * B.DesiredSize.Y;
                if (!FMath::IsNearlyEqual(AreaA, AreaB))
                {
                    return AreaA > AreaB;
                }
                return FMath::Max(A.DesiredSize.X, A.DesiredSize.Y) > FMath::Max(B.DesiredSize.X, B.DesiredSize.Y);
            });

        TArray<FPackingRecord> CandidateRecords = Records;
        if (!TryPackRecords(CandidateRecords, AtlasMin, AtlasSize, IslandPaddingUV, 1.0))
        {
            double Low = 0.0;
            double High = 1.0;
            TArray<FPackingRecord> BestRecords;
            bool bHasFit = false;

            for (int32 Iteration = 0; Iteration < 28; ++Iteration)
            {
                const double Mid = (Low + High) * 0.5;
                CandidateRecords = Records;
                if (TryPackRecords(CandidateRecords, AtlasMin, AtlasSize, IslandPaddingUV, Mid))
                {
                    Low = Mid;
                    BestRecords = MoveTemp(CandidateRecords);
                    bHasFit = true;
                }
                else
                {
                    High = Mid;
                }
            }

            if (!bHasFit)
            {
                return false;
            }

            Records = MoveTemp(BestRecords);
            return true;
        }

        Records = MoveTemp(CandidateRecords);
        return true;
    }
}

void FDWCDataUVPacker::BuildRawChartUVs(
    const TArray<FDWCDataUVTriangle>& Triangles,
    FDWCDataUVChart& Chart)
{
    Chart.RawBounds = FBox2D(ForceInit);
    Chart.RawArea = 0.0;
    Chart.RawUVByVertexInstance.Reset();

    for (const int32 TriangleIndex : Chart.TriangleIndices)
    {
        if (!Triangles.IsValidIndex(TriangleIndex))
        {
            continue;
        }

        const FDWCDataUVTriangle& Triangle = Triangles[TriangleIndex];
        for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
        {
            const FVector2D RawUV = Triangle.SourceUVs[CornerIndex];
            Chart.RawUVByVertexInstance.FindOrAdd(
                Triangle.VertexInstances[CornerIndex].GetValue()) = RawUV;
            Chart.RawBounds += RawUV;
        }

        Chart.RawArea += FDWCUVGeometry::ComputeTriangleArea2D(
            Triangle.SourceUVs[0],
            Triangle.SourceUVs[1],
            Triangle.SourceUVs[2]);
    }

    if (!Chart.RawBounds.bIsValid || Chart.RawBounds.GetSize().IsNearlyZero())
    {
        Chart.RawBounds = FBox2D(
            FVector2D(-0.5, -0.5),
            FVector2D(0.5, 0.5));
    }
}

void FDWCDataUVPacker::Pack(
    const TArray<FDWCDataUVTriangle>& Triangles,
    TArray<FDWCDataUVChart>& Charts,
    const int32 Resolution,
    const int32 PaddingPixels,
    TMap<int32, FVector2f>& OutPackedUVByVertexInstance)
{
    OutPackedUVByVertexInstance.Reset();
    if (Charts.IsEmpty())
    {
        return;
    }

    for (FDWCDataUVChart& Chart : Charts)
    {
        BuildRawChartUVs(Triangles, Chart);
    }

    TMap<int32, TArray<int32>> ChartIndicesByMaterial;
    for (int32 ChartIndex = 0; ChartIndex < Charts.Num(); ++ChartIndex)
    {
        ChartIndicesByMaterial.FindOrAdd(Charts[ChartIndex].MaterialSlotIndex).Add(ChartIndex);
    }

    TArray<int32> MaterialSlots;
    ChartIndicesByMaterial.GetKeys(MaterialSlots);
    MaterialSlots.Sort();

    for (const int32 MaterialSlotIndex : MaterialSlots)
    {
        TArray<int32>& SlotChartIndices = ChartIndicesByMaterial.FindChecked(MaterialSlotIndex);

        double TotalArea = 0.0;
        for (const int32 ChartIndex : SlotChartIndices)
        {
            TotalArea += FMath::Max(Charts[ChartIndex].RawArea, 0.0);
        }

        const double RequestedPaddingUV = Resolution > 0
            ? static_cast<double>(FMath::Max(PaddingPixels, 0)) / static_cast<double>(Resolution)
            : 0.0;
        const double MaxReasonablePaddingUV = SlotChartIndices.Num() > 0
            ? 0.45 / (FMath::Sqrt(static_cast<double>(SlotChartIndices.Num())) + 1.0)
            : 0.0;
        const double PaddingUV = FMath::Clamp(RequestedPaddingUV, 0.0, MaxReasonablePaddingUV);
        const double BorderPaddingUV = PaddingUV;
        const double IslandPaddingUV = PaddingUV;

        const FVector2D AtlasMin(BorderPaddingUV, BorderPaddingUV);
        const FVector2D AtlasSize(
            FMath::Max(1.0 - BorderPaddingUV * 2.0, 1.0e-6),
            FMath::Max(1.0 - BorderPaddingUV * 2.0, 1.0e-6));
        const double UsableAtlasArea = AtlasSize.X * AtlasSize.Y;

        TArray<DWCDataUVPackerPrivate::FPackingRecord> Records;
        Records.Reserve(SlotChartIndices.Num());
        const double EqualAreaFallback = SlotChartIndices.Num() > 0
            ? UsableAtlasArea / static_cast<double>(SlotChartIndices.Num())
            : UsableAtlasArea;

        for (const int32 ChartIndex : SlotChartIndices)
        {
            FDWCDataUVChart& Chart = Charts[ChartIndex];
            const FVector2D RawSize = DWCDataUVPackerPrivate::GetSafeRawSize(Chart);
            const double AspectRatio = FMath::Clamp(RawSize.X / RawSize.Y, 1.0e-4, 1.0e4);
            const double AreaRatio = TotalArea > 1.0e-12
                ? FMath::Max(Chart.RawArea, 0.0) / TotalArea
                : 1.0 / static_cast<double>(SlotChartIndices.Num());
            const double TargetArea = TotalArea > 1.0e-12
                ? UsableAtlasArea * AreaRatio
                : EqualAreaFallback;

            DWCDataUVPackerPrivate::FPackingRecord& Record = Records.AddDefaulted_GetRef();
            Record.ChartIndex = ChartIndex;
            Record.DesiredSize = FVector2D(
                FMath::Sqrt(TargetArea * AspectRatio),
                FMath::Sqrt(TargetArea / AspectRatio));
        }

        constexpr int32 MaxRectsChartLimit = 256;
        bool bPacked = true;
        if (Records.Num() >= MaxRectsChartLimit)
        {
            // MaxRects performs free-rectangle pruning for every record and every scale
            // retry. A pathological overlap split can create thousands of charts; use a
            // deterministic bounded path before that work becomes superlinear.
            DWCDataUVPackerPrivate::PackRecordsInGrid(
                Records,
                AtlasMin,
                AtlasSize,
                IslandPaddingUV);
        }
        else
        {
            bPacked = DWCDataUVPackerPrivate::PackRecordsWithMaximumScale(
                Records,
                AtlasMin,
                AtlasSize,
                IslandPaddingUV);
        }

        if (!bPacked && PaddingUV > 0.0)
        {
            bPacked = DWCDataUVPackerPrivate::PackRecordsWithMaximumScale(
                Records,
                FVector2D::ZeroVector,
                FVector2D(1.0, 1.0),
                0.0);
        }

        if (!bPacked)
        {
            continue;
        }

        for (const DWCDataUVPackerPrivate::FPackingRecord& Record : Records)
        {
            if (!Charts.IsValidIndex(Record.ChartIndex))
            {
                continue;
            }

            FDWCDataUVChart& Chart = Charts[Record.ChartIndex];
            const FVector2D RawSize = DWCDataUVPackerPrivate::GetSafeRawSize(Chart);
            const double UniformScale = FMath::Min(
                Record.PackedSize.X / RawSize.X,
                Record.PackedSize.Y / RawSize.Y);
            const FVector2D PackedSize = RawSize * UniformScale;
            const FVector2D PackedMin = Record.PackedMin + (Record.PackedSize - PackedSize) * 0.5;

            for (const TPair<int32, FVector2D>& Pair : Chart.RawUVByVertexInstance)
            {
                FVector2D PackedUV = PackedMin + (Pair.Value - Chart.RawBounds.Min) * UniformScale;
                PackedUV.X = FMath::Clamp(
                    PackedUV.X,
                    BorderPaddingUV,
                    1.0 - BorderPaddingUV);
                PackedUV.Y = FMath::Clamp(
                    PackedUV.Y,
                    BorderPaddingUV,
                    1.0 - BorderPaddingUV);
                OutPackedUVByVertexInstance.Add(Pair.Key, FVector2f(PackedUV));
            }
        }
    }
}
