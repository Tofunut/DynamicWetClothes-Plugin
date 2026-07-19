#include "DWCDataUVPacker.h"

#include "WetClothing/Common/UV/DWCUVGeometry.h"

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
        SlotChartIndices.Sort(
            [&Charts](const int32 A, const int32 B)
            {
                return Charts[A].RawArea > Charts[B].RawArea;
            });

        // Every material slot owns an independent data texture, so each slot may use 0..1 fully.
        const int32 ChartCount = SlotChartIndices.Num();
        const int32 Columns = FMath::Max(
            1,
            FMath::CeilToInt(FMath::Sqrt(static_cast<float>(ChartCount))));
        const int32 Rows = FMath::Max(
            1,
            FMath::CeilToInt(static_cast<float>(ChartCount) / static_cast<float>(Columns)));
        const float CellWidth = 1.0f / static_cast<float>(Columns);
        const float CellHeight = 1.0f / static_cast<float>(Rows);
        const float RequestedPaddingUV = Resolution > 0
            ? static_cast<float>(PaddingPixels) / static_cast<float>(Resolution)
            : 0.0f;
        const float PaddingUV = FMath::Clamp(
            RequestedPaddingUV,
            0.0f,
            FMath::Min(CellWidth, CellHeight) * 0.35f);

        for (int32 LocalChartIndex = 0; LocalChartIndex < SlotChartIndices.Num(); ++LocalChartIndex)
        {
            FDWCDataUVChart& Chart = Charts[SlotChartIndices[LocalChartIndex]];
            const int32 Column = LocalChartIndex % Columns;
            const int32 Row = LocalChartIndex / Columns;
            const FVector2D CellMin(CellWidth * Column, CellHeight * Row);
            const FVector2D CellMax(CellMin.X + CellWidth, CellMin.Y + CellHeight);
            FVector2D InnerMin = CellMin + FVector2D(PaddingUV, PaddingUV);
            FVector2D InnerMax = CellMax - FVector2D(PaddingUV, PaddingUV);
            if (InnerMax.X <= InnerMin.X || InnerMax.Y <= InnerMin.Y)
            {
                InnerMin = CellMin + FVector2D(CellWidth * 0.08f, CellHeight * 0.08f);
                InnerMax = CellMax - FVector2D(CellWidth * 0.08f, CellHeight * 0.08f);
            }

            FVector2D RawSize = Chart.RawBounds.GetSize();
            RawSize.X = FMath::Max(RawSize.X, 1.0e-4);
            RawSize.Y = FMath::Max(RawSize.Y, 1.0e-4);
            const FVector2D InnerSize = InnerMax - InnerMin;
            const double UniformScale = FMath::Min(
                InnerSize.X / RawSize.X,
                InnerSize.Y / RawSize.Y);
            const FVector2D PackedSize = RawSize * UniformScale;
            const FVector2D PackedMin = InnerMin + (InnerSize - PackedSize) * 0.5;

            for (const TPair<int32, FVector2D>& Pair : Chart.RawUVByVertexInstance)
            {
                FVector2D PackedUV = PackedMin + (Pair.Value - Chart.RawBounds.Min) * UniformScale;
                PackedUV.X = FMath::Clamp(
                    PackedUV.X,
                    CellMin.X + PaddingUV * 0.5f,
                    CellMax.X - PaddingUV * 0.5f);
                PackedUV.Y = FMath::Clamp(
                    PackedUV.Y,
                    CellMin.Y + PaddingUV * 0.5f,
                    CellMax.Y - PaddingUV * 0.5f);
                OutPackedUVByVertexInstance.Add(Pair.Key, FVector2f(PackedUV));
            }
        }
    }
}
