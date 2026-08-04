#include "DWCDataUVChartBuilder.h"

#include "WetClothing/Foundation/UV/DWCUVEdgeKey.h"
#include "WetClothing/Foundation/UV/DWCUVGeometry.h"
#include "WetClothing/Foundation/UV/DWCUVIslandBuilder.h"

namespace DWCDataUVChartBuilderPrivate
{
    // Sweep-and-prune examines exact candidates only inside one physical Source UV shell.
    // Keep pathological self-overlap bounded without falling back to one chart per triangle.
    static constexpr int64 MaxSweepPairComparisonsPerSourceUVShell = 10000000;

    class FDisjointSet
    {
    public:
        explicit FDisjointSet(const int32 Count)
        {
            Parents.SetNumUninitialized(Count);
            Ranks.Init(0, Count);
            for (int32 Index = 0; Index < Count; ++Index)
            {
                Parents[Index] = Index;
            }
        }

        int32 Find(const int32 Index)
        {
            if (Parents[Index] != Index)
            {
                Parents[Index] = Find(Parents[Index]);
            }
            return Parents[Index];
        }

        void Union(const int32 A, const int32 B)
        {
            int32 RootA = Find(A);
            int32 RootB = Find(B);
            if (RootA == RootB)
            {
                return;
            }

            if (Ranks[RootA] < Ranks[RootB])
            {
                Swap(RootA, RootB);
            }

            Parents[RootB] = RootA;
            if (Ranks[RootA] == Ranks[RootB])
            {
                ++Ranks[RootA];
            }
        }

    private:
        TArray<int32> Parents;
        TArray<uint8> Ranks;
    };

    /** Direction-independent MeshDescription vertex edge. */
    struct FSourceMeshEdgeKey
    {
        int32 VertexA = INDEX_NONE;
        int32 VertexB = INDEX_NONE;

        FSourceMeshEdgeKey() = default;

        FSourceMeshEdgeKey(const FVertexID InA, const FVertexID InB)
        {
            const int32 ValueA = InA.GetValue();
            const int32 ValueB = InB.GetValue();
            if (ValueA <= ValueB)
            {
                VertexA = ValueA;
                VertexB = ValueB;
            }
            else
            {
                VertexA = ValueB;
                VertexB = ValueA;
            }
        }

        bool operator==(const FSourceMeshEdgeKey& Other) const
        {
            return VertexA == Other.VertexA && VertexB == Other.VertexB;
        }
    };

    FORCEINLINE uint32 GetTypeHash(const FSourceMeshEdgeKey& Edge)
    {
        return HashCombine(::GetTypeHash(Edge.VertexA), ::GetTypeHash(Edge.VertexB));
    }

    struct FSourceMeshEdgeOccurrence
    {
        int32 LocalTriangleIndex = INDEX_NONE;
        FDWCQuantizedUVPoint UVA;
        FDWCQuantizedUVPoint UVB;
    };

    struct FSweepTriangle
    {
        int32 LocalTriangleIndex = INDEX_NONE;
        FBox2D Bounds = FBox2D(ForceInit);
    };

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

    void BuildSourceMeshTriangleAdjacency(
        const TArray<FDWCDataUVTriangle>& Triangles,
        const TArray<int32>& TriangleIndices,
        TArray<TSet<int32>>& OutAdjacency)
    {
        OutAdjacency.Reset();
        OutAdjacency.SetNum(TriangleIndices.Num());

        TMap<FSourceMeshEdgeKey, TArray<FSourceMeshEdgeOccurrence>> EdgeOccurrences;
        for (int32 LocalTriangleIndex = 0; LocalTriangleIndex < TriangleIndices.Num(); ++LocalTriangleIndex)
        {
            const int32 TriangleIndex = TriangleIndices[LocalTriangleIndex];
            if (!Triangles.IsValidIndex(TriangleIndex))
            {
                continue;
            }

            const FDWCDataUVTriangle& Triangle = Triangles[TriangleIndex];
            for (int32 EdgeIndex = 0; EdgeIndex < 3; ++EdgeIndex)
            {
                const int32 NextEdgeIndex = (EdgeIndex + 1) % 3;
                const FVertexID StartVertex = Triangle.Vertices[EdgeIndex];
                const FVertexID EndVertex = Triangle.Vertices[NextEdgeIndex];
                const FSourceMeshEdgeKey Edge(StartVertex, EndVertex);

                FSourceMeshEdgeOccurrence& Occurrence =
                    EdgeOccurrences.FindOrAdd(Edge).AddDefaulted_GetRef();
                Occurrence.LocalTriangleIndex = LocalTriangleIndex;
                if (StartVertex.GetValue() == Edge.VertexA)
                {
                    Occurrence.UVA = FDWCQuantizedUVPoint(Triangle.SourceUVs[EdgeIndex]);
                    Occurrence.UVB = FDWCQuantizedUVPoint(Triangle.SourceUVs[NextEdgeIndex]);
                }
                else
                {
                    Occurrence.UVA = FDWCQuantizedUVPoint(Triangle.SourceUVs[NextEdgeIndex]);
                    Occurrence.UVB = FDWCQuantizedUVPoint(Triangle.SourceUVs[EdgeIndex]);
                }
            }
        }

        for (const TPair<FSourceMeshEdgeKey, TArray<FSourceMeshEdgeOccurrence>>& Pair : EdgeOccurrences)
        {
            const TArray<FSourceMeshEdgeOccurrence>& Occurrences = Pair.Value;
            if (Occurrences.Num() != 2)
            {
                continue;
            }

            const FSourceMeshEdgeOccurrence& A = Occurrences[0];
            const FSourceMeshEdgeOccurrence& B = Occurrences[1];
            if (A.UVA == B.UVA && A.UVB == B.UVB &&
                OutAdjacency.IsValidIndex(A.LocalTriangleIndex) &&
                OutAdjacency.IsValidIndex(B.LocalTriangleIndex))
            {
                OutAdjacency[A.LocalTriangleIndex].Add(B.LocalTriangleIndex);
                OutAdjacency[B.LocalTriangleIndex].Add(A.LocalTriangleIndex);
            }
        }
    }

    uint64 MakeChartPairKey(const int32 ChartA, const int32 ChartB)
    {
        const uint32 MinChart = static_cast<uint32>(FMath::Min(ChartA, ChartB));
        const uint32 MaxChart = static_cast<uint32>(FMath::Max(ChartA, ChartB));
        return (static_cast<uint64>(MinChart) << 32) | static_cast<uint64>(MaxChart);
    }

    void DecodeChartPairKey(const uint64 Key, int32& OutChartA, int32& OutChartB)
    {
        OutChartA = static_cast<int32>(static_cast<uint32>(Key >> 32));
        OutChartB = static_cast<int32>(static_cast<uint32>(Key & 0xffffffffull));
    }

    bool ChartsHaveCrossConflict(
        const TArray<FDWCDataUVChart>& Charts,
        const int32 ChartA,
        const int32 ChartB,
        const TMap<int32, int32>& LocalIndexByTriangleIndex,
        const TArray<TSet<int32>>& Conflicts,
        const TArray<int32>& ChartByLocalTriangle)
    {
        if (!Charts.IsValidIndex(ChartA) || !Charts.IsValidIndex(ChartB))
        {
            return true;
        }

        const int32 SmallerChart = Charts[ChartA].TriangleIndices.Num() <= Charts[ChartB].TriangleIndices.Num()
            ? ChartA
            : ChartB;
        const int32 OtherChart = SmallerChart == ChartA ? ChartB : ChartA;

        for (const int32 TriangleIndex : Charts[SmallerChart].TriangleIndices)
        {
            const int32* LocalIndex = LocalIndexByTriangleIndex.Find(TriangleIndex);
            if (LocalIndex == nullptr || !Conflicts.IsValidIndex(*LocalIndex))
            {
                continue;
            }

            for (const int32 ConflictLocalIndex : Conflicts[*LocalIndex])
            {
                if (ChartByLocalTriangle.IsValidIndex(ConflictLocalIndex) &&
                    ChartByLocalTriangle[ConflictLocalIndex] == OtherChart)
                {
                    return true;
                }
            }
        }
        return false;
    }

    void MergeAdjacentConflictFreeCharts(
        const TArray<FDWCDataUVTriangle>& Triangles,
        const FDWCDataUVChart& SourceUVShell,
        const TArray<TSet<int32>>& Conflicts,
        TArray<FDWCDataUVChart>& InOutCharts)
    {
        if (InOutCharts.Num() < 2)
        {
            return;
        }

        TMap<int32, int32> LocalIndexByTriangleIndex;
        LocalIndexByTriangleIndex.Reserve(SourceUVShell.TriangleIndices.Num());
        for (int32 LocalIndex = 0; LocalIndex < SourceUVShell.TriangleIndices.Num(); ++LocalIndex)
        {
            LocalIndexByTriangleIndex.Add(SourceUVShell.TriangleIndices[LocalIndex], LocalIndex);
        }

        TArray<TSet<int32>> TriangleAdjacency;
        BuildSourceMeshTriangleAdjacency(
            Triangles,
            SourceUVShell.TriangleIndices,
            TriangleAdjacency);

        while (InOutCharts.Num() > 1)
        {
            TArray<int32> ChartByLocalTriangle;
            ChartByLocalTriangle.Init(INDEX_NONE, SourceUVShell.TriangleIndices.Num());
            for (int32 ChartIndex = 0; ChartIndex < InOutCharts.Num(); ++ChartIndex)
            {
                for (const int32 TriangleIndex : InOutCharts[ChartIndex].TriangleIndices)
                {
                    if (const int32* LocalIndex = LocalIndexByTriangleIndex.Find(TriangleIndex))
                    {
                        ChartByLocalTriangle[*LocalIndex] = ChartIndex;
                    }
                }
            }

            TMap<uint64, int32> SharedTopologyEdgeCountByPair;
            for (int32 LocalIndex = 0; LocalIndex < TriangleAdjacency.Num(); ++LocalIndex)
            {
                const int32 ChartA = ChartByLocalTriangle.IsValidIndex(LocalIndex)
                    ? ChartByLocalTriangle[LocalIndex]
                    : INDEX_NONE;
                if (ChartA == INDEX_NONE)
                {
                    continue;
                }

                for (const int32 NeighborLocalIndex : TriangleAdjacency[LocalIndex])
                {
                    if (NeighborLocalIndex <= LocalIndex ||
                        !ChartByLocalTriangle.IsValidIndex(NeighborLocalIndex))
                    {
                        continue;
                    }

                    const int32 ChartB = ChartByLocalTriangle[NeighborLocalIndex];
                    if (ChartB == INDEX_NONE || ChartA == ChartB)
                    {
                        continue;
                    }

                    ++SharedTopologyEdgeCountByPair.FindOrAdd(MakeChartPairKey(ChartA, ChartB));
                }
            }

            int32 BestChartA = INDEX_NONE;
            int32 BestChartB = INDEX_NONE;
            int32 BestSharedEdgeCount = -1;
            int32 BestCombinedTriangleCount = -1;

            for (const TPair<uint64, int32>& Pair : SharedTopologyEdgeCountByPair)
            {
                int32 ChartA = INDEX_NONE;
                int32 ChartB = INDEX_NONE;
                DecodeChartPairKey(Pair.Key, ChartA, ChartB);
                if (!InOutCharts.IsValidIndex(ChartA) || !InOutCharts.IsValidIndex(ChartB) || ChartA == ChartB)
                {
                    continue;
                }

                if (ChartsHaveCrossConflict(
                        InOutCharts,
                        ChartA,
                        ChartB,
                        LocalIndexByTriangleIndex,
                        Conflicts,
                        ChartByLocalTriangle))
                {
                    continue;
                }

                const int32 CombinedTriangleCount =
                    InOutCharts[ChartA].TriangleIndices.Num() +
                    InOutCharts[ChartB].TriangleIndices.Num();
                if (Pair.Value > BestSharedEdgeCount ||
                    (Pair.Value == BestSharedEdgeCount && CombinedTriangleCount > BestCombinedTriangleCount))
                {
                    BestChartA = ChartA;
                    BestChartB = ChartB;
                    BestSharedEdgeCount = Pair.Value;
                    BestCombinedTriangleCount = CombinedTriangleCount;
                }
            }

            if (BestChartA == INDEX_NONE || BestChartB == INDEX_NONE)
            {
                break;
            }

            InOutCharts[BestChartA].TriangleIndices.Append(InOutCharts[BestChartB].TriangleIndices);
            InOutCharts[BestChartA].TriangleIndices.Sort();
            InOutCharts.RemoveAt(BestChartB, 1, EAllowShrinking::No);
        }
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

    // Keep editor/report diagnostics aligned with the UV-island view. Packing does
    // not use these merged UV-coordinate islands as physical Source shell boundaries.
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

void FDWCDataUVChartBuilder::BuildSourceUVShellsForSlot(
    const TArray<FDWCDataUVTriangle>& Triangles,
    const TArray<int32>& SlotTriangleIndices,
    TArray<FDWCDataUVChart>& OutSourceShells)
{
    using namespace DWCDataUVChartBuilderPrivate;

    if (SlotTriangleIndices.IsEmpty())
    {
        return;
    }

    FDisjointSet DisjointSet(SlotTriangleIndices.Num());
    TMap<FSourceMeshEdgeKey, TArray<FSourceMeshEdgeOccurrence>> EdgeOccurrences;

    for (int32 LocalTriangleIndex = 0; LocalTriangleIndex < SlotTriangleIndices.Num(); ++LocalTriangleIndex)
    {
        const int32 TriangleIndex = SlotTriangleIndices[LocalTriangleIndex];
        if (!Triangles.IsValidIndex(TriangleIndex))
        {
            continue;
        }

        const FDWCDataUVTriangle& Triangle = Triangles[TriangleIndex];
        for (int32 EdgeIndex = 0; EdgeIndex < 3; ++EdgeIndex)
        {
            const int32 NextEdgeIndex = (EdgeIndex + 1) % 3;
            const FVertexID StartVertex = Triangle.Vertices[EdgeIndex];
            const FVertexID EndVertex = Triangle.Vertices[NextEdgeIndex];
            const FSourceMeshEdgeKey Edge(StartVertex, EndVertex);

            FSourceMeshEdgeOccurrence& Occurrence =
                EdgeOccurrences.FindOrAdd(Edge).AddDefaulted_GetRef();
            Occurrence.LocalTriangleIndex = LocalTriangleIndex;

            // Store both UV endpoints in the same canonical mesh-vertex order on
            // every incident triangle, making continuity a direct equality test.
            if (StartVertex.GetValue() == Edge.VertexA)
            {
                Occurrence.UVA = FDWCQuantizedUVPoint(Triangle.SourceUVs[EdgeIndex]);
                Occurrence.UVB = FDWCQuantizedUVPoint(Triangle.SourceUVs[NextEdgeIndex]);
            }
            else
            {
                Occurrence.UVA = FDWCQuantizedUVPoint(Triangle.SourceUVs[NextEdgeIndex]);
                Occurrence.UVB = FDWCQuantizedUVPoint(Triangle.SourceUVs[EdgeIndex]);
            }
        }
    }

    for (const TPair<FSourceMeshEdgeKey, TArray<FSourceMeshEdgeOccurrence>>& Pair : EdgeOccurrences)
    {
        const TArray<FSourceMeshEdgeOccurrence>& Occurrences = Pair.Value;

        // Boundary and non-manifold mesh edges stay disconnected. Exactly two
        // incident triangles join only when Source UVs are continuous across the
        // actual shared mesh edge. UV-coordinate stacking alone can never join shells.
        if (Occurrences.Num() != 2)
        {
            continue;
        }

        const FSourceMeshEdgeOccurrence& A = Occurrences[0];
        const FSourceMeshEdgeOccurrence& B = Occurrences[1];
        if (A.UVA == B.UVA && A.UVB == B.UVB)
        {
            DisjointSet.Union(A.LocalTriangleIndex, B.LocalTriangleIndex);
        }
    }

    TMap<int32, int32> RootToOutputIndex;
    for (int32 LocalTriangleIndex = 0; LocalTriangleIndex < SlotTriangleIndices.Num(); ++LocalTriangleIndex)
    {
        const int32 TriangleIndex = SlotTriangleIndices[LocalTriangleIndex];
        if (!Triangles.IsValidIndex(TriangleIndex))
        {
            continue;
        }

        const int32 Root = DisjointSet.Find(LocalTriangleIndex);
        int32* OutputIndex = RootToOutputIndex.Find(Root);
        if (OutputIndex == nullptr)
        {
            FDWCDataUVChart& NewShell = OutSourceShells.AddDefaulted_GetRef();
            NewShell.MaterialSlotIndex = Triangles[TriangleIndex].MaterialSlotIndex;
            RootToOutputIndex.Add(Root, OutSourceShells.Num() - 1);
            OutputIndex = RootToOutputIndex.Find(Root);
            check(OutputIndex != nullptr);
        }

        OutSourceShells[*OutputIndex].TriangleIndices.Add(TriangleIndex);
    }
}

bool FDWCDataUVChartBuilder::BuildOverlapConflictGraph(
    const TArray<FDWCDataUVTriangle>& Triangles,
    const FDWCDataUVChart& SourceUVShell,
    TArray<TSet<int32>>& OutConflicts,
    int32& OutOverlapPairCount,
    int64& OutTestedCandidatePairCount)
{
    using namespace DWCDataUVChartBuilderPrivate;

    const int32 TriangleCount = SourceUVShell.TriangleIndices.Num();
    OutConflicts.Reset();
    OutConflicts.SetNum(TriangleCount);
    OutOverlapPairCount = 0;
    OutTestedCandidatePairCount = 0;
    if (TriangleCount < 2)
    {
        return true;
    }

    TArray<FBox2D> TriangleBounds;
    TriangleBounds.SetNum(TriangleCount);
    TArray<FSweepTriangle> SweepTriangles;
    SweepTriangles.Reserve(TriangleCount);

    for (int32 LocalIndex = 0; LocalIndex < TriangleCount; ++LocalIndex)
    {
        const int32 TriangleIndex = SourceUVShell.TriangleIndices[LocalIndex];
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
        if (!TriangleBox.bIsValid)
        {
            continue;
        }

        FSweepTriangle& SweepTriangle = SweepTriangles.AddDefaulted_GetRef();
        SweepTriangle.LocalTriangleIndex = LocalIndex;
        SweepTriangle.Bounds = TriangleBox;
    }

    SweepTriangles.Sort(
        [](const FSweepTriangle& A, const FSweepTriangle& B)
        {
            if (A.Bounds.Min.X != B.Bounds.Min.X)
            {
                return A.Bounds.Min.X < B.Bounds.Min.X;
            }
            if (A.Bounds.Max.X != B.Bounds.Max.X)
            {
                return A.Bounds.Max.X < B.Bounds.Max.X;
            }
            return A.LocalTriangleIndex < B.LocalTriangleIndex;
        });

    TArray<int32> ActiveLocalTriangleIndices;
    ActiveLocalTriangleIndices.Reserve(SweepTriangles.Num());

    for (const FSweepTriangle& Current : SweepTriangles)
    {
        for (int32 ActiveListIndex = ActiveLocalTriangleIndices.Num() - 1;
             ActiveListIndex >= 0;
             --ActiveListIndex)
        {
            const int32 ActiveLocalIndex = ActiveLocalTriangleIndices[ActiveListIndex];
            if (!TriangleBounds.IsValidIndex(ActiveLocalIndex) ||
                TriangleBounds[ActiveLocalIndex].Max.X < Current.Bounds.Min.X)
            {
                ActiveLocalTriangleIndices.RemoveAtSwap(
                    ActiveListIndex,
                    1,
                    EAllowShrinking::No);
            }
        }

        for (const int32 ActiveLocalIndex : ActiveLocalTriangleIndices)
        {
            const FBox2D& ActiveBounds = TriangleBounds[ActiveLocalIndex];
            if (ActiveBounds.Max.Y < Current.Bounds.Min.Y ||
                Current.Bounds.Max.Y < ActiveBounds.Min.Y)
            {
                continue;
            }

            ++OutTestedCandidatePairCount;
            if (OutTestedCandidatePairCount > MaxSweepPairComparisonsPerSourceUVShell)
            {
                OutConflicts.Reset();
                OutOverlapPairCount = 0;
                return false;
            }

            const int32 TriangleIndexA = SourceUVShell.TriangleIndices[ActiveLocalIndex];
            const int32 TriangleIndexB = SourceUVShell.TriangleIndices[Current.LocalTriangleIndex];
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
                OutConflicts[ActiveLocalIndex].Add(Current.LocalTriangleIndex);
                OutConflicts[Current.LocalTriangleIndex].Add(ActiveLocalIndex);
                ++OutOverlapPairCount;
            }
        }

        ActiveLocalTriangleIndices.Add(Current.LocalTriangleIndex);
    }

    return true;
}

bool FDWCDataUVChartBuilder::BuildNonOverlappingCharts(
    const TArray<FDWCDataUVTriangle>& Triangles,
    const TArray<FDWCDataUVChart>& OriginalUVIslands,
    TArray<FDWCDataUVChart>& OutCharts,
    int32& OutSplitOriginalUVIslandCount,
    int32& OutOverlapPairCount,
    TArray<FDWCDataUVSlotWarning>& InOutSlotWarnings,
    FDWCDataUVChartBuildFailure* OutFailure)
{
    using namespace DWCDataUVChartBuilderPrivate;

    OutCharts.Reset();
    OutSplitOriginalUVIslandCount = 0;
    OutOverlapPairCount = 0;
    if (OutFailure != nullptr)
    {
        *OutFailure = FDWCDataUVChartBuildFailure();
    }

    // Original UV islands remain editor/report diagnostics. Packing derives physical
    // Source shells from actual mesh-edge adjacency plus Source UV continuity.
    TMap<int32, TArray<int32>> TriangleIndicesByMaterialSlot;
    for (const FDWCDataUVChart& OriginalUVIsland : OriginalUVIslands)
    {
        TriangleIndicesByMaterialSlot.FindOrAdd(OriginalUVIsland.MaterialSlotIndex)
            .Append(OriginalUVIsland.TriangleIndices);
    }

    TArray<int32> MaterialSlotIndices;
    TriangleIndicesByMaterialSlot.GetKeys(MaterialSlotIndices);
    MaterialSlotIndices.Sort();

    for (const int32 MaterialSlotIndex : MaterialSlotIndices)
    {
        TArray<int32>& SlotTriangleIndices = TriangleIndicesByMaterialSlot.FindChecked(MaterialSlotIndex);
        SlotTriangleIndices.Sort();

        TArray<FDWCDataUVChart> SourceUVShells;
        BuildSourceUVShellsForSlot(
            Triangles,
            SlotTriangleIndices,
            SourceUVShells);

        for (const FDWCDataUVChart& SourceUVShell : SourceUVShells)
        {
            TArray<TSet<int32>> Conflicts;
            int32 ShellOverlapPairCount = 0;
            int64 TestedCandidatePairCount = 0;
            const bool bWithinBudget = BuildOverlapConflictGraph(
                Triangles,
                SourceUVShell,
                Conflicts,
                ShellOverlapPairCount,
                TestedCandidatePairCount);
            if (!bWithinBudget)
            {
                OutCharts.Reset();
                if (OutFailure != nullptr)
                {
                    OutFailure->bIsValid = true;
                    OutFailure->FailureReason = EDWCDataUVChartBuildFailureReason::AnalysisBudgetExceeded;
                    OutFailure->MaterialSlotIndex = MaterialSlotIndex;
                    OutFailure->SourceTriangleCount = SourceUVShell.TriangleIndices.Num();
                    OutFailure->TestedCandidatePairCount = TestedCandidatePairCount;
                    OutFailure->Reason = TEXT("Physical Source UV shell self-overlap analysis exceeded the safety limit.");
                }
                return false;
            }

            OutOverlapPairCount += ShellOverlapPairCount;
            if (ShellOverlapPairCount <= 0)
            {
                OutCharts.Add(SourceUVShell);
                continue;
            }

            ++OutSplitOriginalUVIslandCount;
            FDWCDataUVSlotWarning& Warning = FindOrAddSlotWarning(
                InOutSlotWarnings,
                MaterialSlotIndex);
            ++Warning.SplitOriginalUVIslandCount;
            Warning.SelfOverlapPairCount += ShellOverlapPairCount;

            // First separate all overlapping triangle pairs with a compact greedy
            // graph coloring. Every color is guaranteed to contain no overlap pair.
            TArray<int32> ColoringOrder;
            ColoringOrder.Reserve(SourceUVShell.TriangleIndices.Num());
            for (int32 LocalIndex = 0; LocalIndex < SourceUVShell.TriangleIndices.Num(); ++LocalIndex)
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
            Colors.Init(INDEX_NONE, SourceUVShell.TriangleIndices.Num());
            int32 ColorCount = 0;
            for (const int32 LocalIndex : ColoringOrder)
            {
                TSet<int32> UsedNeighborColors;
                for (const int32 NeighborLocalIndex : Conflicts[LocalIndex])
                {
                    if (Colors.IsValidIndex(NeighborLocalIndex) &&
                        Colors[NeighborLocalIndex] != INDEX_NONE)
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

            TArray<FDWCDataUVChart> ShellCharts;
            for (int32 ColorIndex = 0; ColorIndex < ColorCount; ++ColorIndex)
            {
                TArray<int32> ColorTriangleIndices;
                ColorTriangleIndices.Reserve(SourceUVShell.TriangleIndices.Num());
                for (int32 LocalIndex = 0; LocalIndex < Colors.Num(); ++LocalIndex)
                {
                    if (Colors[LocalIndex] == ColorIndex)
                    {
                        ColorTriangleIndices.Add(SourceUVShell.TriangleIndices[LocalIndex]);
                    }
                }

                if (!ColorTriangleIndices.IsEmpty())
                {
                    // Keep only topology-connected regions inside one conflict-free
                    // color so distant pieces never become one oversized chart box.
                    BuildSourceUVShellsForSlot(
                        Triangles,
                        ColorTriangleIndices,
                        ShellCharts);
                }
            }

            // Coloring can temporarily split neighboring conflict-free regions. Merge
            // adjacent chart fragments back together whenever the union still contains
            // no overlap pair. This preserves larger continuous shell regions and avoids
            // the checkerboard-like triangle shredding of the older coloring-only path.
            MergeAdjacentConflictFreeCharts(
                Triangles,
                SourceUVShell,
                Conflicts,
                ShellCharts);

            for (FDWCDataUVChart& ShellChart : ShellCharts)
            {
                OutCharts.Add(MoveTemp(ShellChart));
            }
        }
    }

    return true;
}
