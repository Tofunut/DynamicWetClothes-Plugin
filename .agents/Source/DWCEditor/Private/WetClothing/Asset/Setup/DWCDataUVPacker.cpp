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
        bool bRotated90 = false;
    };

    struct FFreeRect
    {
        FVector2D Min = FVector2D::ZeroVector;
        FVector2D Size = FVector2D::ZeroVector;

        double Right() const { return Min.X + Size.X; }
        double Top() const { return Min.Y + Size.Y; }
    };

    struct FShelf
    {
        double MinY = 0.0;
        double Height = 0.0;
        double NextX = 0.0;
    };

    static FVector2D GetSafeRawSize(const FDWCDataUVChart& Chart)
    {
        FVector2D RawSize = Chart.RawBounds.GetSize();
        RawSize.X = FMath::Max(RawSize.X, 1.0e-4);
        RawSize.Y = FMath::Max(RawSize.Y, 1.0e-4);
        return RawSize;
    }

    static FVector2D GetContentSize(
        const FPackingRecord& Record,
        const double Scale,
        const bool bRotated90)
    {
        FVector2D ContentSize(
            FMath::Max(Record.DesiredSize.X * Scale, 1.0e-7),
            FMath::Max(Record.DesiredSize.Y * Scale, 1.0e-7));
        if (bRotated90)
        {
            Swap(ContentSize.X, ContentSize.Y);
        }
        return ContentSize;
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

    static bool TryPackRecordsMaxRects(
        TArray<FPackingRecord>& Records,
        const FVector2D& AtlasMin,
        const FVector2D& AtlasSize,
        const double ChartPaddingUV,
        const double Scale)
    {
        TArray<FFreeRect> FreeRects;
        FreeRects.Add({ AtlasMin, AtlasSize });

        for (FPackingRecord& Record : Records)
        {
            int32 BestRectIndex = INDEX_NONE;
            bool bBestRotated90 = false;
            FVector2D BestContentSize = FVector2D::ZeroVector;
            FVector2D BestPaddedSize = FVector2D::ZeroVector;
            double BestShortSide = TNumericLimits<double>::Max();
            double BestLongSide = TNumericLimits<double>::Max();
            double BestAreaWaste = TNumericLimits<double>::Max();

            for (int32 RectIndex = 0; RectIndex < FreeRects.Num(); ++RectIndex)
            {
                const FFreeRect& FreeRect = FreeRects[RectIndex];
                for (int32 RotationIndex = 0; RotationIndex < 2; ++RotationIndex)
                {
                    const bool bRotated90 = RotationIndex == 1;
                    if (bRotated90 &&
                        FMath::IsNearlyEqual(Record.DesiredSize.X, Record.DesiredSize.Y, 1.0e-12))
                    {
                        continue;
                    }

                    const FVector2D ContentSize = GetContentSize(Record, Scale, bRotated90);
                    const FVector2D PaddedSize = ContentSize +
                        FVector2D(ChartPaddingUV * 2.0, ChartPaddingUV * 2.0);
                    if (PaddedSize.X > FreeRect.Size.X + 1.0e-9 ||
                        PaddedSize.Y > FreeRect.Size.Y + 1.0e-9)
                    {
                        continue;
                    }

                    const double LeftoverX = FreeRect.Size.X - PaddedSize.X;
                    const double LeftoverY = FreeRect.Size.Y - PaddedSize.Y;
                    const double ShortSide = FMath::Min(LeftoverX, LeftoverY);
                    const double LongSide = FMath::Max(LeftoverX, LeftoverY);
                    const double AreaWaste = FreeRect.Size.X * FreeRect.Size.Y -
                        PaddedSize.X * PaddedSize.Y;

                    if (ShortSide < BestShortSide ||
                        (FMath::IsNearlyEqual(ShortSide, BestShortSide) && LongSide < BestLongSide) ||
                        (FMath::IsNearlyEqual(ShortSide, BestShortSide) &&
                         FMath::IsNearlyEqual(LongSide, BestLongSide) &&
                         AreaWaste < BestAreaWaste))
                    {
                        BestRectIndex = RectIndex;
                        bBestRotated90 = bRotated90;
                        BestContentSize = ContentSize;
                        BestPaddedSize = PaddedSize;
                        BestShortSide = ShortSide;
                        BestLongSide = LongSide;
                        BestAreaWaste = AreaWaste;
                    }
                }
            }

            if (BestRectIndex == INDEX_NONE)
            {
                return false;
            }

            const FFreeRect PlacementRect = {
                FreeRects[BestRectIndex].Min,
                BestPaddedSize
            };
            Record.PackedMin = PlacementRect.Min + FVector2D(ChartPaddingUV, ChartPaddingUV);
            Record.PackedSize = BestContentSize;
            Record.bRotated90 = bBestRotated90;
            SplitFreeRects(FreeRects, PlacementRect);
        }

        return true;
    }

    static bool TryPackRecordsInShelves(
        TArray<FPackingRecord>& Records,
        const FVector2D& AtlasMin,
        const FVector2D& AtlasSize,
        const double ChartPaddingUV,
        const double Scale)
    {
        if (Records.IsEmpty())
        {
            return true;
        }

        const double AtlasRight = AtlasMin.X + AtlasSize.X;
        const double AtlasTop = AtlasMin.Y + AtlasSize.Y;
        TArray<FShelf> Shelves;

        for (FPackingRecord& Record : Records)
        {
            int32 BestShelfIndex = INDEX_NONE;
            bool bBestRotated90 = false;
            FVector2D BestContentSize = FVector2D::ZeroVector;
            FVector2D BestPaddedSize = FVector2D::ZeroVector;
            double BestHorizontalWaste = TNumericLimits<double>::Max();
            double BestVerticalWaste = TNumericLimits<double>::Max();

            for (int32 ShelfIndex = 0; ShelfIndex < Shelves.Num(); ++ShelfIndex)
            {
                const FShelf& Shelf = Shelves[ShelfIndex];
                for (int32 RotationIndex = 0; RotationIndex < 2; ++RotationIndex)
                {
                    const bool bRotated90 = RotationIndex == 1;
                    if (bRotated90 &&
                        FMath::IsNearlyEqual(Record.DesiredSize.X, Record.DesiredSize.Y, 1.0e-12))
                    {
                        continue;
                    }

                    const FVector2D ContentSize = GetContentSize(Record, Scale, bRotated90);
                    const FVector2D PaddedSize = ContentSize +
                        FVector2D(ChartPaddingUV * 2.0, ChartPaddingUV * 2.0);
                    if (PaddedSize.Y > Shelf.Height + 1.0e-9 ||
                        Shelf.NextX + PaddedSize.X > AtlasRight + 1.0e-9)
                    {
                        continue;
                    }

                    const double HorizontalWaste = AtlasRight - (Shelf.NextX + PaddedSize.X);
                    const double VerticalWaste = Shelf.Height - PaddedSize.Y;
                    if (HorizontalWaste < BestHorizontalWaste ||
                        (FMath::IsNearlyEqual(HorizontalWaste, BestHorizontalWaste) &&
                         VerticalWaste < BestVerticalWaste))
                    {
                        BestShelfIndex = ShelfIndex;
                        bBestRotated90 = bRotated90;
                        BestContentSize = ContentSize;
                        BestPaddedSize = PaddedSize;
                        BestHorizontalWaste = HorizontalWaste;
                        BestVerticalWaste = VerticalWaste;
                    }
                }
            }

            if (BestShelfIndex == INDEX_NONE)
            {
                const double NewShelfY = Shelves.IsEmpty()
                    ? AtlasMin.Y
                    : Shelves.Last().MinY + Shelves.Last().Height;
                bool bFoundNewShelfOrientation = false;
                double BestNewShelfHeight = TNumericLimits<double>::Max();
                double BestNewShelfWidth = TNumericLimits<double>::Max();

                for (int32 RotationIndex = 0; RotationIndex < 2; ++RotationIndex)
                {
                    const bool bRotated90 = RotationIndex == 1;
                    if (bRotated90 &&
                        FMath::IsNearlyEqual(Record.DesiredSize.X, Record.DesiredSize.Y, 1.0e-12))
                    {
                        continue;
                    }

                    const FVector2D ContentSize = GetContentSize(Record, Scale, bRotated90);
                    const FVector2D PaddedSize = ContentSize +
                        FVector2D(ChartPaddingUV * 2.0, ChartPaddingUV * 2.0);
                    if (PaddedSize.X > AtlasSize.X + 1.0e-9 ||
                        NewShelfY + PaddedSize.Y > AtlasTop + 1.0e-9)
                    {
                        continue;
                    }

                    if (!bFoundNewShelfOrientation ||
                        PaddedSize.Y < BestNewShelfHeight ||
                        (FMath::IsNearlyEqual(PaddedSize.Y, BestNewShelfHeight) &&
                         PaddedSize.X < BestNewShelfWidth))
                    {
                        bFoundNewShelfOrientation = true;
                        bBestRotated90 = bRotated90;
                        BestContentSize = ContentSize;
                        BestPaddedSize = PaddedSize;
                        BestNewShelfHeight = PaddedSize.Y;
                        BestNewShelfWidth = PaddedSize.X;
                    }
                }

                if (!bFoundNewShelfOrientation)
                {
                    return false;
                }

                FShelf& NewShelf = Shelves.AddDefaulted_GetRef();
                NewShelf.MinY = NewShelfY;
                NewShelf.Height = BestPaddedSize.Y;
                NewShelf.NextX = AtlasMin.X;
                BestShelfIndex = Shelves.Num() - 1;
            }

            FShelf& Shelf = Shelves[BestShelfIndex];
            Record.PackedMin = FVector2D(
                Shelf.NextX + ChartPaddingUV,
                Shelf.MinY + ChartPaddingUV);
            Record.PackedSize = BestContentSize;
            Record.bRotated90 = bBestRotated90;
            Shelf.NextX += BestPaddedSize.X;
        }

        return true;
    }

    static bool PackRecordsWithMaximumScale(
        TArray<FPackingRecord>& Records,
        const FVector2D& AtlasMin,
        const FVector2D& AtlasSize,
        const double ChartPaddingUV,
        const bool bUseShelfPacker)
    {
        if (Records.IsEmpty())
        {
            return true;
        }

        Records.Sort(
            [](const FPackingRecord& A, const FPackingRecord& B)
            {
                const double MaxDimensionA = FMath::Max(A.DesiredSize.X, A.DesiredSize.Y);
                const double MaxDimensionB = FMath::Max(B.DesiredSize.X, B.DesiredSize.Y);
                if (!FMath::IsNearlyEqual(MaxDimensionA, MaxDimensionB))
                {
                    return MaxDimensionA > MaxDimensionB;
                }

                const double AreaA = A.DesiredSize.X * A.DesiredSize.Y;
                const double AreaB = B.DesiredSize.X * B.DesiredSize.Y;
                if (!FMath::IsNearlyEqual(AreaA, AreaB))
                {
                    return AreaA > AreaB;
                }
                return A.ChartIndex < B.ChartIndex;
            });

        const auto TryPackAtScale =
            [bUseShelfPacker, &AtlasMin, &AtlasSize, ChartPaddingUV](
                TArray<FPackingRecord>& CandidateRecords,
                const double Scale)
            {
                return bUseShelfPacker
                    ? TryPackRecordsInShelves(
                        CandidateRecords,
                        AtlasMin,
                        AtlasSize,
                        ChartPaddingUV,
                        Scale)
                    : TryPackRecordsMaxRects(
                        CandidateRecords,
                        AtlasMin,
                        AtlasSize,
                        ChartPaddingUV,
                        Scale);
            };

        TArray<FPackingRecord> CandidateRecords = Records;
        if (!TryPackAtScale(CandidateRecords, 1.0))
        {
            double Low = 0.0;
            double High = 1.0;
            TArray<FPackingRecord> BestRecords;
            bool bHasFit = false;

            for (int32 Iteration = 0; Iteration < 28; ++Iteration)
            {
                const double Mid = (Low + High) * 0.5;
                CandidateRecords = Records;
                if (TryPackAtScale(CandidateRecords, Mid))
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

bool FDWCDataUVPacker::Pack(
    const TArray<FDWCDataUVTriangle>& Triangles,
    TArray<FDWCDataUVChart>& Charts,
    const double ChartPaddingUV,
    const double BorderPaddingUV,
    TMap<int32, FVector2f>& OutPackedUVByVertexInstance,
    int32& OutFailedMaterialSlotIndex,
    int32* OutFailedChartCount)
{
    OutPackedUVByVertexInstance.Reset();
    OutFailedMaterialSlotIndex = INDEX_NONE;
    if (OutFailedChartCount != nullptr)
    {
        *OutFailedChartCount = 0;
    }
    if (Charts.IsEmpty())
    {
        return true;
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

        const double SafeChartPaddingUV = FMath::Max(ChartPaddingUV, 0.0);
        const double SafeBorderPaddingUV = FMath::Max(BorderPaddingUV, 0.0);
        if (SafeBorderPaddingUV >= 0.5)
        {
            OutFailedMaterialSlotIndex = MaterialSlotIndex;
            if (OutFailedChartCount != nullptr)
            {
                *OutFailedChartCount = SlotChartIndices.Num();
            }
            OutPackedUVByVertexInstance.Reset();
            return false;
        }

        const FVector2D AtlasMin(SafeBorderPaddingUV, SafeBorderPaddingUV);
        const FVector2D AtlasSize(
            FMath::Max(1.0 - SafeBorderPaddingUV * 2.0, 1.0e-6),
            FMath::Max(1.0 - SafeBorderPaddingUV * 2.0, 1.0e-6));
        const double UsableAtlasArea = AtlasSize.X * AtlasSize.Y;

        // This is only a necessary lower bound: even at an infinitesimal chart
        // content scale, every chart still owns padding on all four sides.
        const double MinimumPaddingArea = static_cast<double>(SlotChartIndices.Num()) *
            FMath::Square(SafeChartPaddingUV * 2.0);
        if (MinimumPaddingArea > UsableAtlasArea + 1.0e-9)
        {
            OutFailedMaterialSlotIndex = MaterialSlotIndex;
            if (OutFailedChartCount != nullptr)
            {
                *OutFailedChartCount = SlotChartIndices.Num();
            }
            OutPackedUVByVertexInstance.Reset();
            return false;
        }

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

        // MaxRects gives better utilization for ordinary chart counts. A bounded
        // shelf packer avoids the former fixed square-grid failure mode for large
        // chart sets while keeping runtime predictable. Both paths allow 90-degree
        // chart rotation and search for the largest common scale that fits.
        constexpr int32 MaxRectsChartLimit = 256;
        const bool bUseShelfPacker = Records.Num() > MaxRectsChartLimit;
        const bool bPacked = DWCDataUVPackerPrivate::PackRecordsWithMaximumScale(
            Records,
            AtlasMin,
            AtlasSize,
            SafeChartPaddingUV,
            bUseShelfPacker);

        if (!bPacked)
        {
            OutFailedMaterialSlotIndex = MaterialSlotIndex;
            if (OutFailedChartCount != nullptr)
            {
                *OutFailedChartCount = SlotChartIndices.Num();
            }
            OutPackedUVByVertexInstance.Reset();
            return false;
        }

        for (const DWCDataUVPackerPrivate::FPackingRecord& Record : Records)
        {
            if (!Charts.IsValidIndex(Record.ChartIndex))
            {
                continue;
            }

            FDWCDataUVChart& Chart = Charts[Record.ChartIndex];
            const FVector2D RawSize = DWCDataUVPackerPrivate::GetSafeRawSize(Chart);
            const double UniformScale = Record.bRotated90
                ? FMath::Min(
                    Record.PackedSize.X / RawSize.Y,
                    Record.PackedSize.Y / RawSize.X)
                : FMath::Min(
                    Record.PackedSize.X / RawSize.X,
                    Record.PackedSize.Y / RawSize.Y);

            for (const TPair<int32, FVector2D>& Pair : Chart.RawUVByVertexInstance)
            {
                const FVector2D RawLocal = Pair.Value - Chart.RawBounds.Min;
                const FVector2D OrientedLocal = Record.bRotated90
                    ? FVector2D(RawSize.Y - RawLocal.Y, RawLocal.X)
                    : RawLocal;
                FVector2D PackedUV = Record.PackedMin + OrientedLocal * UniformScale;
                PackedUV.X = FMath::Clamp(
                    PackedUV.X,
                    SafeBorderPaddingUV,
                    1.0 - SafeBorderPaddingUV);
                PackedUV.Y = FMath::Clamp(
                    PackedUV.Y,
                    SafeBorderPaddingUV,
                    1.0 - SafeBorderPaddingUV);
                OutPackedUVByVertexInstance.Add(Pair.Key, FVector2f(PackedUV));
            }
        }
    }

    return true;
}
