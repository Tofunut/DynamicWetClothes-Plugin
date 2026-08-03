#include "DWCDataUVChartBuilder.h"

#include "WetClothing/Foundation/UV/DWCUVGeometry.h"
#include "WetClothing/Foundation/UV/DWCUVIslandBuilder.h"

namespace DWCDataUVChartBuilderPrivate
{
    // Candidate pairs are materialized as a hash set by the exact overlap path. Keep
    // that work bounded; dense stacked UV shells otherwise grow quadratically.
    static constexpr int64 MaxSpatialCellInsertionsPerIsland = 1000000;
    static constexpr int64 MaxCandidatePairInsertionsPerIsland = 1000000;

    FDWCDataUVSlotWarning& FindOrAddSlotWarning(
        TArray<FDWCDataUVSlotWarning>& SlotWarnings,
        const int32 MaterialSlotIndex)
    {
        for (FDWCDataUVSlotWarning& SlotWarning : SlotWarnings)
        {
            if (SlotWarning.MaterialSlotIndex == MaterialSlotIndex)
            {
                return SlotWarning;
            }
        }

        FDWCDataUVSlotWarning& SlotWarning = SlotWarnings.AddDefaulted_GetRef();
        SlotWarning.MaterialSlotIndex = MaterialSlotIndex;
        return SlotWarning;
    }
}

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

bool FDWCDataUVChartBuilder::BuildOverlapConflictGraph(
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
        return true;
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
    int64 SpatialCellInsertionCount = 0;
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
        SpatialCellInsertionCount +=
            static_cast<int64>(MaxCellX - MinCellX + 1) *
            static_cast<int64>(MaxCellY - MinCellY + 1);
        if (SpatialCellInsertionCount >
            DWCDataUVChartBuilderPrivate::MaxSpatialCellInsertionsPerIsland)
        {
            OutConflicts.Reset();
            OutOverlapPairCount = 0;
            return false;
        }

        for (int32 CellY = MinCellY; CellY <= MaxCellY; ++CellY)
        {
            for (int32 CellX = MinCellX; CellX <= MaxCellX; ++CellX)
            {
                CellToLocalTriangles.FindOrAdd(CellY * GridDimension + CellX).Add(LocalIndex);
            }
        }
    }

    TSet<uint64> CandidatePairs;
    int64 CandidatePairInsertionCount = 0;
    for (const TPair<int32, TArray<int32>>& CellPair : CellToLocalTriangles)
    {
        const TArray<int32>& LocalTriangles = CellPair.Value;
        const int64 CellTriangleCount = LocalTriangles.Num();
        const int64 CellPairInsertionCount =
            CellTriangleCount > 1 ? CellTriangleCount * (CellTriangleCount - 1) / 2 : 0;
        CandidatePairInsertionCount += CellPairInsertionCount;
        if (CandidatePairInsertionCount >
            DWCDataUVChartBuilderPrivate::MaxCandidatePairInsertionsPerIsland)
        {
            OutConflicts.Reset();
            OutOverlapPairCount = 0;
            return false;
        }

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

    return true;
}

bool FDWCDataUVChartBuilder::BuildNonOverlappingCharts(
    const TArray<FDWCDataUVTriangle>& Triangles,
    const TArray<FDWCDataUVChart>& OriginalUVIslands,
    TArray<FDWCDataUVChart>& OutCharts,
    int32& OutSplitOriginalUVIslandCount,
    int32& OutOverlapPairCount,
    TArray<FDWCDataUVSlotWarning>& InOutSlotWarnings)
{
    OutCharts.Reset();
    OutSplitOriginalUVIslandCount = 0;
    OutOverlapPairCount = 0;

    for (const FDWCDataUVChart& OriginalUVIsland : OriginalUVIslands)
    {
        TArray<TSet<int32>> Conflicts;
        int32 IslandOverlapPairCount = 0;
        const bool bConflictGraphWithinBudget = BuildOverlapConflictGraph(
            Triangles,
            OriginalUVIsland,
            Conflicts,
            IslandOverlapPairCount);
        if (!bConflictGraphWithinBudget)
        {
            FDWCDataUVSlotWarning& Warning = DWCDataUVChartBuilderPrivate::FindOrAddSlotWarning(
                InOutSlotWarnings,
                OriginalUVIsland.MaterialSlotIndex);
            ++Warning.BudgetFallbackIslandCount;
            for (const int32 TriangleIndex : OriginalUVIsland.TriangleIndices)
            {
                if (!Triangles.IsValidIndex(TriangleIndex))
                {
                    continue;
                }

                FDWCDataUVChart& TriangleChart = OutCharts.AddDefaulted_GetRef();
                TriangleChart.MaterialSlotIndex = OriginalUVIsland.MaterialSlotIndex;
                TriangleChart.TriangleIndices.Add(TriangleIndex);
            }
            continue;
        }
        OutOverlapPairCount += IslandOverlapPairCount;

        if (IslandOverlapPairCount == 0)
        {
            OutCharts.Add(OriginalUVIsland);
            continue;
        }

        ++OutSplitOriginalUVIslandCount;
        FDWCDataUVSlotWarning& Warning = DWCDataUVChartBuilderPrivate::FindOrAddSlotWarning(
            InOutSlotWarnings,
            OriginalUVIsland.MaterialSlotIndex);
        ++Warning.SplitOriginalUVIslandCount;
        Warning.SelfOverlapPairCount += IslandOverlapPairCount;

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

    return true;
}

