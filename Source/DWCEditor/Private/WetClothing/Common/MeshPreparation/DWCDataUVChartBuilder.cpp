#include "DWCDataUVChartBuilder.h"

#include "WetClothing/Common/UV/DWCUVGeometry.h"
#include "WetClothing/Common/UV/DWCUVIslandBuilder.h"

void FDWCDataUVChartBuilder::BuildOriginalUVIslands(
    const TArray<FDWCDataUVTriangle>& Triangles,
    const TMap<int32, TArray<int32>>& TriangleIndicesByMaterialSlot,
    TArray<FDWCDataUVChart>& OutOriginalUVIslands)
{
    OutOriginalUVIslands.Reset();

    TArray<int32> MaterialSlotIndices;
    TriangleIndicesByMaterialSlot.GetKeys(MaterialSlotIndices);
    MaterialSlotIndices.Sort();

    for (const int32 MaterialSlotIndex : MaterialSlotIndices)
    {
        BuildOriginalUVIslandsForSlot(
            Triangles,
            TriangleIndicesByMaterialSlot.FindChecked(MaterialSlotIndex),
            OutOriginalUVIslands);
    }
}

void FDWCDataUVChartBuilder::BuildOriginalUVIslandsForSlot(
    const TArray<FDWCDataUVTriangle>& Triangles,
    const TArray<int32>& SlotTriangleIndices,
    TArray<FDWCDataUVChart>& OutOriginalUVIslands)
{
    if (SlotTriangleIndices.IsEmpty())
    {
        return;
    }

    TArray<FDWCUVIslandBuildTriangle> BuildTriangles;
    BuildTriangles.Reserve(SlotTriangleIndices.Num());
    for (const int32 TriangleArrayIndex : SlotTriangleIndices)
    {
        if (!Triangles.IsValidIndex(TriangleArrayIndex))
        {
            continue;
        }

        const FDWCDataUVTriangle& Triangle = Triangles[TriangleArrayIndex];
        FDWCUVIslandBuildTriangle& BuildTriangle = BuildTriangles.AddDefaulted_GetRef();
        BuildTriangle.TriangleID = TriangleArrayIndex;
        BuildTriangle.MaterialSlotIndex = Triangle.MaterialSlotIndex;
        BuildTriangle.UVs[0] = Triangle.SourceUVs[0];
        BuildTriangle.UVs[1] = Triangle.SourceUVs[1];
        BuildTriangle.UVs[2] = Triangle.SourceUVs[2];
    }

    TArray<FDWCOriginalUVIslandBuildResult> BuiltIslands;
    FDWCUVIslandBuilder::Build(BuildTriangles, BuiltIslands);
    for (const FDWCOriginalUVIslandBuildResult& BuiltIsland : BuiltIslands)
    {
        FDWCDataUVChart& OriginalUVIsland = OutOriginalUVIslands.AddDefaulted_GetRef();
        OriginalUVIsland.MaterialSlotIndex = BuiltIsland.MaterialSlotIndex;
        OriginalUVIsland.TriangleIndices.Reserve(BuiltIsland.TriangleInputIndices.Num());

        for (const int32 BuildTriangleIndex : BuiltIsland.TriangleInputIndices)
        {
            if (BuildTriangles.IsValidIndex(BuildTriangleIndex))
            {
                OriginalUVIsland.TriangleIndices.Add(BuildTriangles[BuildTriangleIndex].TriangleID);
            }
        }
    }
}

void FDWCDataUVChartBuilder::BuildOverlapConflictGraph(
    const TArray<FDWCDataUVTriangle>& Triangles,
    const FDWCDataUVChart& OriginalUVIsland,
    TArray<TSet<int32>>& OutConflicts,
    int32& OutOverlapPairCount)
{
    const int32 TriangleCount = OriginalUVIsland.TriangleIndices.Num();
    OutConflicts.SetNum(TriangleCount);
    OutOverlapPairCount = 0;
    if (TriangleCount < 2)
    {
        return;
    }

    FBox2D Bounds(ForceInit);
    TArray<FBox2D> TriangleBounds;
    TriangleBounds.SetNum(TriangleCount);
    for (int32 LocalIndex = 0; LocalIndex < TriangleCount; ++LocalIndex)
    {
        const int32 TriangleIndex = OriginalUVIsland.TriangleIndices[LocalIndex];
        if (!Triangles.IsValidIndex(TriangleIndex))
        {
            continue;
        }

        const FDWCDataUVTriangle& Triangle = Triangles[TriangleIndex];
        FBox2D& TriangleBox = TriangleBounds[LocalIndex];
        TriangleBox = FBox2D(ForceInit);
        TriangleBox += Triangle.SourceUVs[0];
        TriangleBox += Triangle.SourceUVs[1];
        TriangleBox += Triangle.SourceUVs[2];
        Bounds += TriangleBox.Min;
        Bounds += TriangleBox.Max;
    }

    const int32 GridDimension = FMath::Clamp(
        FMath::CeilToInt(FMath::Sqrt(static_cast<double>(TriangleCount))),
        1,
        64);
    FVector2D BoundsSize = Bounds.GetSize();
    BoundsSize.X = FMath::Max(BoundsSize.X, 1.0e-9);
    BoundsSize.Y = FMath::Max(BoundsSize.Y, 1.0e-9);
    const FVector2D CellSize = BoundsSize / static_cast<double>(GridDimension);

    TMap<int32, TArray<int32>> CellToLocalTriangles;
    for (int32 LocalIndex = 0; LocalIndex < TriangleCount; ++LocalIndex)
    {
        const FBox2D& TriangleBox = TriangleBounds[LocalIndex];
        if (!TriangleBox.bIsValid)
        {
            continue;
        }

        const int32 MinCellX = FMath::Clamp(
            FMath::FloorToInt((TriangleBox.Min.X - Bounds.Min.X) / CellSize.X),
            0,
            GridDimension - 1);
        const int32 MaxCellX = FMath::Clamp(
            FMath::FloorToInt((TriangleBox.Max.X - Bounds.Min.X) / CellSize.X),
            0,
            GridDimension - 1);
        const int32 MinCellY = FMath::Clamp(
            FMath::FloorToInt((TriangleBox.Min.Y - Bounds.Min.Y) / CellSize.Y),
            0,
            GridDimension - 1);
        const int32 MaxCellY = FMath::Clamp(
            FMath::FloorToInt((TriangleBox.Max.Y - Bounds.Min.Y) / CellSize.Y),
            0,
            GridDimension - 1);

        for (int32 CellY = MinCellY; CellY <= MaxCellY; ++CellY)
        {
            for (int32 CellX = MinCellX; CellX <= MaxCellX; ++CellX)
            {
                CellToLocalTriangles.FindOrAdd(CellY * GridDimension + CellX).Add(LocalIndex);
            }
        }
    }

    TSet<uint64> CandidatePairs;
    for (const TPair<int32, TArray<int32>>& CellPair : CellToLocalTriangles)
    {
        const TArray<int32>& LocalTriangles = CellPair.Value;
        for (int32 AListIndex = 0; AListIndex < LocalTriangles.Num(); ++AListIndex)
        {
            for (int32 BListIndex = AListIndex + 1; BListIndex < LocalTriangles.Num(); ++BListIndex)
            {
                CandidatePairs.Add(FDWCUVGeometry::MakeTrianglePairKey(
                    LocalTriangles[AListIndex],
                    LocalTriangles[BListIndex]));
            }
        }
    }

    for (const uint64 PairKey : CandidatePairs)
    {
        const int32 LocalA = static_cast<int32>(PairKey >> 32);
        const int32 LocalB = static_cast<int32>(PairKey & 0xffffffffu);
        if (!TriangleBounds.IsValidIndex(LocalA) ||
            !TriangleBounds.IsValidIndex(LocalB) ||
            !TriangleBounds[LocalA].Intersect(TriangleBounds[LocalB]))
        {
            continue;
        }

        const int32 TriangleIndexA = OriginalUVIsland.TriangleIndices[LocalA];
        const int32 TriangleIndexB = OriginalUVIsland.TriangleIndices[LocalB];
        if (!Triangles.IsValidIndex(TriangleIndexA) || !Triangles.IsValidIndex(TriangleIndexB))
        {
            continue;
        }

        const FDWCDataUVTriangle& A = Triangles[TriangleIndexA];
        const FDWCDataUVTriangle& B = Triangles[TriangleIndexB];
        if (FDWCUVGeometry::DoTrianglesOverlapByArea(
                A.SourceUVs[0], A.SourceUVs[1], A.SourceUVs[2],
                B.SourceUVs[0], B.SourceUVs[1], B.SourceUVs[2]))
        {
            OutConflicts[LocalA].Add(LocalB);
            OutConflicts[LocalB].Add(LocalA);
            ++OutOverlapPairCount;
        }
    }
}

void FDWCDataUVChartBuilder::BuildNonOverlappingCharts(
    const TArray<FDWCDataUVTriangle>& Triangles,
    const TArray<FDWCDataUVChart>& OriginalUVIslands,
    TArray<FDWCDataUVChart>& OutCharts,
    int32& OutSplitOriginalUVIslandCount,
    int32& OutOverlapPairCount)
{
    OutCharts.Reset();
    OutSplitOriginalUVIslandCount = 0;
    OutOverlapPairCount = 0;

    for (const FDWCDataUVChart& OriginalUVIsland : OriginalUVIslands)
    {
        TArray<TSet<int32>> Conflicts;
        int32 IslandOverlapPairCount = 0;
        BuildOverlapConflictGraph(
            Triangles,
            OriginalUVIsland,
            Conflicts,
            IslandOverlapPairCount);
        OutOverlapPairCount += IslandOverlapPairCount;

        if (IslandOverlapPairCount == 0)
        {
            OutCharts.Add(OriginalUVIsland);
            continue;
        }

        ++OutSplitOriginalUVIslandCount;

        TArray<int32> ColoringOrder;
        ColoringOrder.Reserve(OriginalUVIsland.TriangleIndices.Num());
        for (int32 LocalIndex = 0; LocalIndex < OriginalUVIsland.TriangleIndices.Num(); ++LocalIndex)
        {
            ColoringOrder.Add(LocalIndex);
        }
        ColoringOrder.Sort(
            [&Conflicts](const int32 A, const int32 B)
            {
                if (Conflicts[A].Num() != Conflicts[B].Num())
                {
                    return Conflicts[A].Num() > Conflicts[B].Num();
                }
                return A < B;
            });

        TArray<int32> Colors;
        Colors.Init(INDEX_NONE, OriginalUVIsland.TriangleIndices.Num());
        int32 ColorCount = 0;
        for (const int32 LocalIndex : ColoringOrder)
        {
            TSet<int32> UsedNeighborColors;
            for (const int32 NeighborLocalIndex : Conflicts[LocalIndex])
            {
                if (Colors.IsValidIndex(NeighborLocalIndex) && Colors[NeighborLocalIndex] != INDEX_NONE)
                {
                    UsedNeighborColors.Add(Colors[NeighborLocalIndex]);
                }
            }

            int32 SelectedColor = 0;
            while (UsedNeighborColors.Contains(SelectedColor))
            {
                ++SelectedColor;
            }
            Colors[LocalIndex] = SelectedColor;
            ColorCount = FMath::Max(ColorCount, SelectedColor + 1);
        }

        // A color removes overlap conflicts. Connectivity is applied again so disconnected
        // fragments do not become one packing chart only because they share a graph color.
        for (int32 ColorIndex = 0; ColorIndex < ColorCount; ++ColorIndex)
        {
            TArray<int32> ColorTriangleIndices;
            for (int32 LocalIndex = 0; LocalIndex < Colors.Num(); ++LocalIndex)
            {
                if (Colors[LocalIndex] == ColorIndex)
                {
                    ColorTriangleIndices.Add(OriginalUVIsland.TriangleIndices[LocalIndex]);
                }
            }

            if (ColorTriangleIndices.IsEmpty())
            {
                continue;
            }

            TArray<FDWCDataUVChart> ConnectedColorCharts;
            BuildOriginalUVIslandsForSlot(
                Triangles,
                ColorTriangleIndices,
                ConnectedColorCharts);
            for (FDWCDataUVChart& ConnectedChart : ConnectedColorCharts)
            {
                OutCharts.Add(MoveTemp(ConnectedChart));
            }
        }
    }
}

void FDWCDataUVChartBuilder::BuildTriangleFallbackCharts(
    const TArray<FDWCDataUVTriangle>& Triangles,
    const TArray<FDWCDataUVChart>& ExistingCharts,
    const TSet<int32>& MaterialSlotsToReplace,
    TArray<FDWCDataUVChart>& OutCharts,
    int32& OutFallbackChartCount)
{
    OutCharts.Reset();
    OutFallbackChartCount = 0;

    TMap<int32, TSet<int32>> TriangleIndicesByMaterial;
    for (const FDWCDataUVChart& Chart : ExistingCharts)
    {
        if (!MaterialSlotsToReplace.Contains(Chart.MaterialSlotIndex))
        {
            OutCharts.Add(Chart);
            continue;
        }

        TSet<int32>& TriangleSet = TriangleIndicesByMaterial.FindOrAdd(Chart.MaterialSlotIndex);
        for (const int32 TriangleIndex : Chart.TriangleIndices)
        {
            if (Triangles.IsValidIndex(TriangleIndex))
            {
                TriangleSet.Add(TriangleIndex);
            }
        }
    }

    TArray<int32> SortedMaterialSlots;
    TriangleIndicesByMaterial.GetKeys(SortedMaterialSlots);
    SortedMaterialSlots.Sort();
    for (const int32 MaterialSlotIndex : SortedMaterialSlots)
    {
        TArray<int32> SortedTriangleIndices;
        for (const int32 TriangleIndex : TriangleIndicesByMaterial.FindChecked(MaterialSlotIndex))
        {
            SortedTriangleIndices.Add(TriangleIndex);
        }
        SortedTriangleIndices.Sort();

        for (const int32 TriangleIndex : SortedTriangleIndices)
        {
            FDWCDataUVChart TriangleChart;
            TriangleChart.MaterialSlotIndex = MaterialSlotIndex;
            TriangleChart.TriangleIndices.Add(TriangleIndex);
            OutCharts.Add(MoveTemp(TriangleChart));
            ++OutFallbackChartCount;
        }
    }
}
