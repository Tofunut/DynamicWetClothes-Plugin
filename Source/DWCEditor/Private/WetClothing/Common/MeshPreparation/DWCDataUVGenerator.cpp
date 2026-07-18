#include "DWCDataUVGenerator.h"

#include "Engine/SkeletalMesh.h"
#include "MeshDescription.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "SkeletalMeshAttributes.h"

namespace DWCDataUVGeneratorInternal
{
    static constexpr int32 InternalPackingResolution = 4096;
    static constexpr int32 InternalPaddingPixels = 32; // Same normalized padding as the previous 8 / 1024 default.

    struct FTriangleRecord
    {
        FTriangleID TriangleID;
        int32 MaterialSlotIndex = INDEX_NONE;
        FVertexInstanceID VertexInstances[3];
        FVertexID Vertices[3];
        FVector Positions[3] = { FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector };
        FVector2D SourceUVs[3] = { FVector2D::ZeroVector, FVector2D::ZeroVector, FVector2D::ZeroVector };
    };

    struct FIslandRecord
    {
        int32 MaterialSlotIndex = INDEX_NONE;
        TArray<int32> TriangleIndices;
        TMap<int32, FVector2D> RawUVByVertexInstance;
        FBox2D RawBounds;
        double RawArea = 0.0;
    };


    struct FSourceEdgeEndpointKey
    {
        FIntVector Position;
        FIntPoint UV;

        bool operator==(const FSourceEdgeEndpointKey& Other) const
        {
            return Position == Other.Position && UV == Other.UV;
        }
    };

    FORCEINLINE uint32 GetTypeHash(const FSourceEdgeEndpointKey& Key)
    {
        return HashCombine(GetTypeHash(Key.Position), GetTypeHash(Key.UV));
    }

    struct FSourceEdgeKey
    {
        FSourceEdgeEndpointKey A;
        FSourceEdgeEndpointKey B;

        bool operator==(const FSourceEdgeKey& Other) const
        {
            return A == Other.A && B == Other.B;
        }
    };

    FORCEINLINE uint32 GetTypeHash(const FSourceEdgeKey& Key)
    {
        return HashCombine(GetTypeHash(Key.A), GetTypeHash(Key.B));
    }

    static FIntVector QuantizeSourcePosition(const FVector& Position)
    {
        constexpr double PositionScale = 1000.0;
        return FIntVector(
            FMath::RoundToInt(Position.X * PositionScale),
            FMath::RoundToInt(Position.Y * PositionScale),
            FMath::RoundToInt(Position.Z * PositionScale));
    }

    static FIntPoint QuantizeSourceUV(const FVector2D& UV)
    {
        constexpr double UVScale = 100000.0;
        return FIntPoint(
            FMath::RoundToInt(UV.X * UVScale),
            FMath::RoundToInt(UV.Y * UVScale));
    }

    static bool ShouldSwapSourceEdgeEndpoints(const FSourceEdgeEndpointKey& A, const FSourceEdgeEndpointKey& B)
    {
        if (A.Position.X != B.Position.X) { return A.Position.X > B.Position.X; }
        if (A.Position.Y != B.Position.Y) { return A.Position.Y > B.Position.Y; }
        if (A.Position.Z != B.Position.Z) { return A.Position.Z > B.Position.Z; }
        if (A.UV.X != B.UV.X) { return A.UV.X > B.UV.X; }
        return A.UV.Y > B.UV.Y;
    }

    static FSourceEdgeKey MakeSourceEdgeKey(
        const FTriangleRecord& Triangle,
        int32 CornerA,
        int32 CornerB)
    {
        FSourceEdgeEndpointKey A;
        A.Position = QuantizeSourcePosition(Triangle.Positions[CornerA]);
        A.UV = QuantizeSourceUV(Triangle.SourceUVs[CornerA]);

        FSourceEdgeEndpointKey B;
        B.Position = QuantizeSourcePosition(Triangle.Positions[CornerB]);
        B.UV = QuantizeSourceUV(Triangle.SourceUVs[CornerB]);

        if (ShouldSwapSourceEdgeEndpoints(A, B))
        {
            Swap(A, B);
        }

        FSourceEdgeKey Key;
        Key.A = A;
        Key.B = B;
        return Key;
    }

    static void SetFailure(FDWCDataUVGenerationResult& Result, const FString& Message)
    {
        Result.bSucceeded = false;
        Result.Message = Message;
    }


    template <typename ElementIDType>
    static bool IsValidElementID(ElementIDType ElementID)
    {
        return ElementID.GetValue() != INDEX_NONE;
    }

    static int32 FindParent(TArray<int32>& Parents, int32 Index)
    {
        if (!Parents.IsValidIndex(Index))
        {
            return INDEX_NONE;
        }

        if (Parents[Index] == Index)
        {
            return Index;
        }

        Parents[Index] = FindParent(Parents, Parents[Index]);
        return Parents[Index];
    }

    static void UnionParents(TArray<int32>& Parents, int32 A, int32 B)
    {
        const int32 RootA = FindParent(Parents, A);
        const int32 RootB = FindParent(Parents, B);
        if (RootA != INDEX_NONE && RootB != INDEX_NONE && RootA != RootB)
        {
            Parents[RootB] = RootA;
        }
    }

    static int32 ResolveMaterialSlotIndex(
        const USkeletalMesh* SkeletalMesh,
        const FMeshDescription& MeshDescription,
        FSkeletalMeshAttributes& Attributes,
        FTriangleID TriangleID)
    {
        const FPolygonID PolygonID = MeshDescription.GetTrianglePolygon(TriangleID);
        if (!IsValidElementID(PolygonID))
        {
            return INDEX_NONE;
        }

        const FPolygonGroupID PolygonGroupID = MeshDescription.GetPolygonPolygonGroup(PolygonID);
        int32 FallbackIndex = PolygonGroupID.GetValue();

        if (SkeletalMesh == nullptr || !IsValidElementID(PolygonGroupID))
        {
            return FallbackIndex;
        }

        const TArray<FSkeletalMaterial>& Materials = SkeletalMesh->GetMaterials();
        const auto MaterialSlotNames = Attributes.GetPolygonGroupMaterialSlotNames();
        const FName PolygonGroupMaterialName = IsValidElementID(PolygonGroupID) ? MaterialSlotNames[PolygonGroupID] : NAME_None;

        if (!PolygonGroupMaterialName.IsNone())
        {
            for (int32 MaterialIndex = 0; MaterialIndex < Materials.Num(); ++MaterialIndex)
            {
                const FSkeletalMaterial& Material = Materials[MaterialIndex];
                if (Material.MaterialSlotName == PolygonGroupMaterialName || Material.ImportedMaterialSlotName == PolygonGroupMaterialName)
                {
                    return MaterialIndex;
                }
            }
        }

        return Materials.IsValidIndex(FallbackIndex) ? FallbackIndex : INDEX_NONE;
    }

    static double ComputeTriangleArea2D(const FVector2D& A, const FVector2D& B, const FVector2D& C)
    {
        return FMath::Abs((B.X - A.X) * (C.Y - A.Y) - (B.Y - A.Y) * (C.X - A.X)) * 0.5;
    }


    static double ComputeTriangleDoubleArea3D(const FVector& A, const FVector& B, const FVector& C)
    {
        return FVector::CrossProduct(B - A, C - A).Size();
    }

    static bool IsFiniteReasonableUV(const FVector2D& UV)
    {
        constexpr double MaximumAbsoluteCoordinate = 1.0e6;
        return FMath::IsFinite(UV.X) && FMath::IsFinite(UV.Y) &&
               FMath::Abs(UV.X) <= MaximumAbsoluteCoordinate &&
               FMath::Abs(UV.Y) <= MaximumAbsoluteCoordinate;
    }

    static double Cross2D(const FVector2D& A, const FVector2D& B)
    {
        return A.X * B.Y - A.Y * B.X;
    }

    static double SignedDistanceToEdge(const FVector2D& Point, const FVector2D& EdgeA, const FVector2D& EdgeB, double OrientationSign)
    {
        return OrientationSign * Cross2D(EdgeB - EdgeA, Point - EdgeA);
    }

    static FVector2D IntersectSegmentWithClipEdge(
        const FVector2D& SegmentStart,
        const FVector2D& SegmentEnd,
        double StartDistance,
        double EndDistance)
    {
        const double Denominator = StartDistance - EndDistance;
        if (FMath::Abs(Denominator) <= 1.0e-12)
        {
            return (SegmentStart + SegmentEnd) * 0.5;
        }

        const double T = FMath::Clamp(StartDistance / Denominator, 0.0, 1.0);
        return SegmentStart + (SegmentEnd - SegmentStart) * T;
    }

    /** Returns the actual interior intersection area. Edge/vertex touching produces zero. */
    static double ComputeTriangleIntersectionArea(
        const FVector2D& A0,
        const FVector2D& A1,
        const FVector2D& A2,
        const FVector2D& B0,
        const FVector2D& B1,
        const FVector2D& B2)
    {
        TArray<FVector2D, TInlineAllocator<8>> Polygon;
        Polygon.Add(A0);
        Polygon.Add(A1);
        Polygon.Add(A2);

        const FVector2D ClipVertices[3] = { B0, B1, B2 };
        const double ClipSignedDoubleArea = Cross2D(B1 - B0, B2 - B0);
        if (FMath::Abs(ClipSignedDoubleArea) <= 1.0e-12)
        {
            return 0.0;
        }

        const double OrientationSign = ClipSignedDoubleArea >= 0.0 ? 1.0 : -1.0;
        constexpr double InsideTolerance = 1.0e-12;

        for (int32 ClipEdgeIndex = 0; ClipEdgeIndex < 3 && Polygon.Num() > 0; ++ClipEdgeIndex)
        {
            const FVector2D EdgeA = ClipVertices[ClipEdgeIndex];
            const FVector2D EdgeB = ClipVertices[(ClipEdgeIndex + 1) % 3];
            const TArray<FVector2D, TInlineAllocator<8>> InputPolygon = Polygon;
            Polygon.Reset();

            FVector2D Previous = InputPolygon.Last();
            double PreviousDistance = SignedDistanceToEdge(Previous, EdgeA, EdgeB, OrientationSign);
            bool bPreviousInside = PreviousDistance >= -InsideTolerance;

            for (const FVector2D& Current : InputPolygon)
            {
                const double CurrentDistance = SignedDistanceToEdge(Current, EdgeA, EdgeB, OrientationSign);
                const bool bCurrentInside = CurrentDistance >= -InsideTolerance;

                if (bCurrentInside != bPreviousInside)
                {
                    Polygon.Add(IntersectSegmentWithClipEdge(Previous, Current, PreviousDistance, CurrentDistance));
                }

                if (bCurrentInside)
                {
                    Polygon.Add(Current);
                }

                Previous = Current;
                PreviousDistance = CurrentDistance;
                bPreviousInside = bCurrentInside;
            }
        }

        if (Polygon.Num() < 3)
        {
            return 0.0;
        }

        double SignedDoubleArea = 0.0;
        for (int32 Index = 0; Index < Polygon.Num(); ++Index)
        {
            SignedDoubleArea += Cross2D(Polygon[Index], Polygon[(Index + 1) % Polygon.Num()]);
        }
        return FMath::Abs(SignedDoubleArea) * 0.5;
    }

    static bool DoTrianglesOverlapByArea(
        const FVector2D& A0,
        const FVector2D& A1,
        const FVector2D& A2,
        const FVector2D& B0,
        const FVector2D& B1,
        const FVector2D& B2)
    {
        const double AreaA = ComputeTriangleArea2D(A0, A1, A2);
        const double AreaB = ComputeTriangleArea2D(B0, B1, B2);
        if (AreaA <= 1.0e-12 || AreaB <= 1.0e-12)
        {
            return false;
        }

        const double IntersectionArea = ComputeTriangleIntersectionArea(A0, A1, A2, B0, B1, B2);
        const double RelativeTolerance = FMath::Min(AreaA, AreaB) * 1.0e-8;
        return IntersectionArea > FMath::Max(1.0e-12, RelativeTolerance);
    }

    static uint64 MakeTrianglePairKey(int32 A, int32 B)
    {
        const uint32 MinIndex = static_cast<uint32>(FMath::Min(A, B));
        const uint32 MaxIndex = static_cast<uint32>(FMath::Max(A, B));
        return (static_cast<uint64>(MinIndex) << 32) | static_cast<uint64>(MaxIndex);
    }

    static void BuildSourceOverlapConflictGraph(
        const TArray<FTriangleRecord>& Triangles,
        const FIslandRecord& Island,
        TArray<TSet<int32>>& OutConflicts,
        int32& OutOverlapPairCount)
    {
        const int32 TriangleCount = Island.TriangleIndices.Num();
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
            const FTriangleRecord& Triangle = Triangles[Island.TriangleIndices[LocalIndex]];
            FBox2D& TriangleBox = TriangleBounds[LocalIndex];
            TriangleBox = FBox2D(ForceInit);
            TriangleBox += Triangle.SourceUVs[0];
            TriangleBox += Triangle.SourceUVs[1];
            TriangleBox += Triangle.SourceUVs[2];
            Bounds += TriangleBox.Min;
            Bounds += TriangleBox.Max;
        }

        const int32 GridDimension = FMath::Clamp(FMath::CeilToInt(FMath::Sqrt(static_cast<double>(TriangleCount))), 1, 64);
        FVector2D BoundsSize = Bounds.GetSize();
        BoundsSize.X = FMath::Max(BoundsSize.X, 1.0e-9);
        BoundsSize.Y = FMath::Max(BoundsSize.Y, 1.0e-9);
        const FVector2D CellSize = BoundsSize / static_cast<double>(GridDimension);

        TMap<int32, TArray<int32>> CellToLocalTriangles;
        for (int32 LocalIndex = 0; LocalIndex < TriangleCount; ++LocalIndex)
        {
            const FBox2D& TriangleBox = TriangleBounds[LocalIndex];
            const int32 MinCellX = FMath::Clamp(FMath::FloorToInt((TriangleBox.Min.X - Bounds.Min.X) / CellSize.X), 0, GridDimension - 1);
            const int32 MaxCellX = FMath::Clamp(FMath::FloorToInt((TriangleBox.Max.X - Bounds.Min.X) / CellSize.X), 0, GridDimension - 1);
            const int32 MinCellY = FMath::Clamp(FMath::FloorToInt((TriangleBox.Min.Y - Bounds.Min.Y) / CellSize.Y), 0, GridDimension - 1);
            const int32 MaxCellY = FMath::Clamp(FMath::FloorToInt((TriangleBox.Max.Y - Bounds.Min.Y) / CellSize.Y), 0, GridDimension - 1);

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
                    CandidatePairs.Add(MakeTrianglePairKey(LocalTriangles[AListIndex], LocalTriangles[BListIndex]));
                }
            }
        }

        for (const uint64 PairKey : CandidatePairs)
        {
            const int32 LocalA = static_cast<int32>(PairKey >> 32);
            const int32 LocalB = static_cast<int32>(PairKey & 0xffffffffu);
            if (!TriangleBounds[LocalA].Intersect(TriangleBounds[LocalB]))
            {
                continue;
            }

            const FTriangleRecord& A = Triangles[Island.TriangleIndices[LocalA]];
            const FTriangleRecord& B = Triangles[Island.TriangleIndices[LocalB]];
            if (DoTrianglesOverlapByArea(
                    A.SourceUVs[0], A.SourceUVs[1], A.SourceUVs[2],
                    B.SourceUVs[0], B.SourceUVs[1], B.SourceUVs[2]))
            {
                OutConflicts[LocalA].Add(LocalB);
                OutConflicts[LocalB].Add(LocalA);
                ++OutOverlapPairCount;
            }
        }
    }

    static void BuildConnectedIslandsForSlot(
        const TArray<FTriangleRecord>& Triangles,
        const TArray<int32>& SlotTriangleIndices,
        TArray<FIslandRecord>& OutIslands)
    {
        if (SlotTriangleIndices.Num() == 0)
        {
            return;
        }

        TArray<int32> Parents;
        Parents.SetNum(SlotTriangleIndices.Num());
        for (int32 LocalIndex = 0; LocalIndex < SlotTriangleIndices.Num(); ++LocalIndex)
        {
            Parents[LocalIndex] = LocalIndex;
        }

        // Group by source UV island rather than by individual polygon or by mesh topology alone.
        // The edge key includes both endpoint positions and source UVs, so duplicated vertices at
        // import seams can still weld, while intentional UV seams remain separate islands.
        TMap<FSourceEdgeKey, int32> EdgeToLocalTriangleIndex;
        for (int32 LocalIndex = 0; LocalIndex < SlotTriangleIndices.Num(); ++LocalIndex)
        {
            const FTriangleRecord& Triangle = Triangles[SlotTriangleIndices[LocalIndex]];
            const int32 EdgeCorners[3][2] = { {0, 1}, {1, 2}, {2, 0} };
            for (int32 EdgeIndex = 0; EdgeIndex < 3; ++EdgeIndex)
            {
                const FSourceEdgeKey EdgeKey = MakeSourceEdgeKey(Triangle, EdgeCorners[EdgeIndex][0], EdgeCorners[EdgeIndex][1]);
                if (int32* ExistingLocalTriangleIndex = EdgeToLocalTriangleIndex.Find(EdgeKey))
                {
                    UnionParents(Parents, *ExistingLocalTriangleIndex, LocalIndex);
                }
                else
                {
                    EdgeToLocalTriangleIndex.Add(EdgeKey, LocalIndex);
                }
            }
        }

        TMap<int32, int32> RootToIslandIndex;
        for (int32 LocalIndex = 0; LocalIndex < SlotTriangleIndices.Num(); ++LocalIndex)
        {
            const int32 Root = FindParent(Parents, LocalIndex);
            int32* ExistingIslandIndex = RootToIslandIndex.Find(Root);
            if (ExistingIslandIndex == nullptr)
            {
                FIslandRecord Island;
                Island.MaterialSlotIndex = Triangles[SlotTriangleIndices[LocalIndex]].MaterialSlotIndex;
                Island.RawBounds = FBox2D(ForceInit);
                const int32 NewIslandIndex = OutIslands.Add(Island);
                RootToIslandIndex.Add(Root, NewIslandIndex);
                ExistingIslandIndex = RootToIslandIndex.Find(Root);
            }

            OutIslands[*ExistingIslandIndex].TriangleIndices.Add(SlotTriangleIndices[LocalIndex]);
        }
    }


    static void SplitSelfOverlappingIslands(
        const TArray<FTriangleRecord>& Triangles,
        const TArray<FIslandRecord>& SourceIslands,
        TArray<FIslandRecord>& OutCharts,
        int32& OutSplitSourceIslandCount,
        int32& OutOverlapPairCount)
    {
        OutCharts.Reset();
        OutSplitSourceIslandCount = 0;
        OutOverlapPairCount = 0;

        for (const FIslandRecord& SourceIsland : SourceIslands)
        {
            TArray<TSet<int32>> Conflicts;
            int32 IslandOverlapPairCount = 0;
            BuildSourceOverlapConflictGraph(Triangles, SourceIsland, Conflicts, IslandOverlapPairCount);
            OutOverlapPairCount += IslandOverlapPairCount;

            if (IslandOverlapPairCount == 0)
            {
                OutCharts.Add(SourceIsland);
                continue;
            }

            ++OutSplitSourceIslandCount;

            TArray<int32> ColoringOrder;
            ColoringOrder.Reserve(SourceIsland.TriangleIndices.Num());
            for (int32 LocalIndex = 0; LocalIndex < SourceIsland.TriangleIndices.Num(); ++LocalIndex)
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
            Colors.Init(INDEX_NONE, SourceIsland.TriangleIndices.Num());
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

            // Coloring guarantees no overlapping pair remains inside one group. Split each color
            // again by source-UV edge connectivity so disconnected fragments are packed separately.
            for (int32 ColorIndex = 0; ColorIndex < ColorCount; ++ColorIndex)
            {
                TArray<int32> ColorTriangleIndices;
                for (int32 LocalIndex = 0; LocalIndex < Colors.Num(); ++LocalIndex)
                {
                    if (Colors[LocalIndex] == ColorIndex)
                    {
                        ColorTriangleIndices.Add(SourceIsland.TriangleIndices[LocalIndex]);
                    }
                }

                if (ColorTriangleIndices.Num() == 0)
                {
                    continue;
                }

                TArray<FIslandRecord> ConnectedColorCharts;
                BuildConnectedIslandsForSlot(Triangles, ColorTriangleIndices, ConnectedColorCharts);
                for (FIslandRecord& ConnectedChart : ConnectedColorCharts)
                {
                    OutCharts.Add(MoveTemp(ConnectedChart));
                }
            }
        }
    }

    static void BuildTriangleFallbackChartsForMaterialSlots(
        const TArray<FTriangleRecord>& Triangles,
        const TArray<FIslandRecord>& ExistingCharts,
        const TSet<int32>& MaterialSlotsToReplace,
        TArray<FIslandRecord>& OutCharts,
        int32& OutFallbackChartCount)
    {
        OutCharts.Reset();
        OutFallbackChartCount = 0;

        TMap<int32, TSet<int32>> TriangleIndicesByMaterial;
        for (const FIslandRecord& Chart : ExistingCharts)
        {
            if (!MaterialSlotsToReplace.Contains(Chart.MaterialSlotIndex))
            {
                OutCharts.Add(Chart);
                continue;
            }

            TSet<int32>& TriangleSet = TriangleIndicesByMaterial.FindOrAdd(Chart.MaterialSlotIndex);
            for (const int32 TriangleIndex : Chart.TriangleIndices)
            {
                TriangleSet.Add(TriangleIndex);
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
                FIslandRecord TriangleChart;
                TriangleChart.MaterialSlotIndex = MaterialSlotIndex;
                TriangleChart.RawBounds = FBox2D(ForceInit);
                TriangleChart.TriangleIndices.Add(TriangleIndex);
                OutCharts.Add(MoveTemp(TriangleChart));
                ++OutFallbackChartCount;
            }
        }
    }

    static void BuildRawIslandUVs(const TArray<FTriangleRecord>& Triangles, FIslandRecord& Island)
    {
        Island.RawBounds = FBox2D(ForceInit);
        Island.RawArea = 0.0;
        Island.RawUVByVertexInstance.Reset();

        for (int32 TriangleIndex : Island.TriangleIndices)
        {
            const FTriangleRecord& Triangle = Triangles[TriangleIndex];

            for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
            {
                const FVector2D RawUV = Triangle.SourceUVs[CornerIndex];
                Island.RawUVByVertexInstance.FindOrAdd(Triangle.VertexInstances[CornerIndex].GetValue()) = RawUV;
                Island.RawBounds += RawUV;
            }

            Island.RawArea += ComputeTriangleArea2D(Triangle.SourceUVs[0], Triangle.SourceUVs[1], Triangle.SourceUVs[2]);
        }

        if (!Island.RawBounds.bIsValid || Island.RawBounds.GetSize().IsNearlyZero())
        {
            Island.RawBounds = FBox2D(FVector2D(-0.5, -0.5), FVector2D(0.5, 0.5));
        }
    }

    static void PackIslandsIntoUnitSquare(
        const TArray<FTriangleRecord>& Triangles,
        TArray<FIslandRecord>& Islands,
        int32 Resolution,
        int32 PaddingPixels,
        TMap<int32, FVector2f>& OutPackedUVByVertexInstance)
    {
        OutPackedUVByVertexInstance.Reset();
        if (Islands.IsEmpty())
        {
            return;
        }

        for (FIslandRecord& Island : Islands)
        {
            BuildRawIslandUVs(Triangles, Island);
        }

        TMap<int32, TArray<int32>> IslandIndicesByMaterial;
        for (int32 IslandIndex = 0; IslandIndex < Islands.Num(); ++IslandIndex)
        {
            IslandIndicesByMaterial.FindOrAdd(Islands[IslandIndex].MaterialSlotIndex).Add(IslandIndex);
        }

        TArray<int32> MaterialSlots;
        IslandIndicesByMaterial.GetKeys(MaterialSlots);
        MaterialSlots.Sort();

        for (const int32 MaterialSlotIndex : MaterialSlots)
        {
            TArray<int32>& SlotIslandIndices = IslandIndicesByMaterial.FindChecked(MaterialSlotIndex);
            SlotIslandIndices.Sort(
                [&Islands](const int32 A, const int32 B)
                {
                    return Islands[A].RawArea > Islands[B].RawArea;
                });

            // Every material slot owns an independent 0..1 data texture, so each slot is packed
            // into the full unit square rather than sharing one atlas with other slots.
            const int32 IslandCount = SlotIslandIndices.Num();
            const int32 Columns = FMath::Max(1, FMath::CeilToInt(FMath::Sqrt(static_cast<float>(IslandCount))));
            const int32 Rows = FMath::Max(1, FMath::CeilToInt(static_cast<float>(IslandCount) / static_cast<float>(Columns)));
            const float CellWidth = 1.0f / static_cast<float>(Columns);
            const float CellHeight = 1.0f / static_cast<float>(Rows);
            const float RequestedPaddingUV = Resolution > 0 ? static_cast<float>(PaddingPixels) / static_cast<float>(Resolution) : 0.0f;
            const float PaddingUV = FMath::Clamp(RequestedPaddingUV, 0.0f, FMath::Min(CellWidth, CellHeight) * 0.35f);

            for (int32 LocalIslandIndex = 0; LocalIslandIndex < SlotIslandIndices.Num(); ++LocalIslandIndex)
            {
                FIslandRecord& Island = Islands[SlotIslandIndices[LocalIslandIndex]];
                const int32 Column = LocalIslandIndex % Columns;
                const int32 Row = LocalIslandIndex / Columns;
                const FVector2D CellMin(CellWidth * Column, CellHeight * Row);
                const FVector2D CellMax(CellMin.X + CellWidth, CellMin.Y + CellHeight);
                FVector2D InnerMin = CellMin + FVector2D(PaddingUV, PaddingUV);
                FVector2D InnerMax = CellMax - FVector2D(PaddingUV, PaddingUV);
                if (InnerMax.X <= InnerMin.X || InnerMax.Y <= InnerMin.Y)
                {
                    InnerMin = CellMin + FVector2D(CellWidth * 0.08f, CellHeight * 0.08f);
                    InnerMax = CellMax - FVector2D(CellWidth * 0.08f, CellHeight * 0.08f);
                }

                FVector2D RawSize = Island.RawBounds.GetSize();
                RawSize.X = FMath::Max(RawSize.X, 1.0e-4);
                RawSize.Y = FMath::Max(RawSize.Y, 1.0e-4);
                const FVector2D InnerSize = InnerMax - InnerMin;
                const double UniformScale = FMath::Min(InnerSize.X / RawSize.X, InnerSize.Y / RawSize.Y);
                const FVector2D PackedSize = RawSize * UniformScale;
                const FVector2D PackedMin = InnerMin + (InnerSize - PackedSize) * 0.5;

                for (const TPair<int32, FVector2D>& Pair : Island.RawUVByVertexInstance)
                {
                    FVector2D PackedUV = PackedMin + (Pair.Value - Island.RawBounds.Min) * UniformScale;
                    PackedUV.X = FMath::Clamp(PackedUV.X, CellMin.X + PaddingUV * 0.5f, CellMax.X - PaddingUV * 0.5f);
                    PackedUV.Y = FMath::Clamp(PackedUV.Y, CellMin.Y + PaddingUV * 0.5f, CellMax.Y - PaddingUV * 0.5f);
                    OutPackedUVByVertexInstance.Add(Pair.Key, FVector2f(PackedUV));
                }
            }
        }
    }


    struct FPackedTriangleRecord
    {
        int32 SourceTriangleIndex = INDEX_NONE;
        int32 MaterialSlotIndex = INDEX_NONE;
        FVector2D UVs[3] = { FVector2D::ZeroVector, FVector2D::ZeroVector, FVector2D::ZeroVector };
        FBox2D Bounds = FBox2D(ForceInit);
    };

    static bool ValidatePackedUVLayout(
        const TArray<FTriangleRecord>& Triangles,
        const TArray<FIslandRecord>& Charts,
        const TMap<int32, FVector2f>& PackedUVByVertexInstance,
        TSet<int32>& OutProblemMaterialSlots,
        FString& OutError)
    {
        OutProblemMaterialSlots.Reset();
        OutError.Reset();

        TMap<int32, TSet<int32>> TriangleIndicesByMaterial;
        for (const FIslandRecord& Chart : Charts)
        {
            TSet<int32>& TriangleSet = TriangleIndicesByMaterial.FindOrAdd(Chart.MaterialSlotIndex);
            for (const int32 TriangleIndex : Chart.TriangleIndices)
            {
                TriangleSet.Add(TriangleIndex);
            }
        }

        TMap<int32, TArray<FPackedTriangleRecord>> PackedTrianglesByMaterial;
        for (const TPair<int32, TSet<int32>>& MaterialPair : TriangleIndicesByMaterial)
        {
            TArray<FPackedTriangleRecord>& PackedTriangles = PackedTrianglesByMaterial.FindOrAdd(MaterialPair.Key);
            for (const int32 TriangleIndex : MaterialPair.Value)
            {
                if (!Triangles.IsValidIndex(TriangleIndex))
                {
                    OutProblemMaterialSlots.Add(MaterialPair.Key);
                    OutError = TEXT("Generated DWC UV references an invalid source triangle.");
                    continue;
                }

                const FTriangleRecord& Triangle = Triangles[TriangleIndex];
                FPackedTriangleRecord PackedTriangle;
                PackedTriangle.SourceTriangleIndex = TriangleIndex;
                PackedTriangle.MaterialSlotIndex = MaterialPair.Key;
                PackedTriangle.Bounds = FBox2D(ForceInit);

                bool bHasAllCorners = true;
                for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
                {
                    const FVector2f* PackedUV = PackedUVByVertexInstance.Find(Triangle.VertexInstances[CornerIndex].GetValue());
                    if (PackedUV == nullptr)
                    {
                        bHasAllCorners = false;
                        break;
                    }

                    PackedTriangle.UVs[CornerIndex] = FVector2D(PackedUV->X, PackedUV->Y);
                    PackedTriangle.Bounds += PackedTriangle.UVs[CornerIndex];
                }

                if (!bHasAllCorners)
                {
                    OutProblemMaterialSlots.Add(MaterialPair.Key);
                    OutError = FString::Printf(
                        TEXT("Generated DWC UV is missing a triangle corner in material slot %d."),
                        MaterialPair.Key);
                    continue;
                }

                constexpr double RangeTolerance = 1.0e-6;
                bool bValidCoordinates = true;
                for (const FVector2D& UV : PackedTriangle.UVs)
                {
                    bValidCoordinates &= IsFiniteReasonableUV(UV) &&
                                         UV.X >= -RangeTolerance && UV.X <= 1.0 + RangeTolerance &&
                                         UV.Y >= -RangeTolerance && UV.Y <= 1.0 + RangeTolerance;
                }

                if (!bValidCoordinates ||
                    ComputeTriangleArea2D(PackedTriangle.UVs[0], PackedTriangle.UVs[1], PackedTriangle.UVs[2]) <= 1.0e-12)
                {
                    OutProblemMaterialSlots.Add(MaterialPair.Key);
                    OutError = FString::Printf(
                        TEXT("Generated DWC UV contains an invalid or degenerate packed triangle in material slot %d."),
                        MaterialPair.Key);
                    continue;
                }

                PackedTriangles.Add(MoveTemp(PackedTriangle));
            }
        }

        for (const TPair<int32, TArray<FPackedTriangleRecord>>& MaterialPair : PackedTrianglesByMaterial)
        {
            const int32 MaterialSlotIndex = MaterialPair.Key;
            const TArray<FPackedTriangleRecord>& PackedTriangles = MaterialPair.Value;
            if (PackedTriangles.Num() < 2)
            {
                continue;
            }

            const int32 GridDimension = FMath::Clamp(FMath::CeilToInt(FMath::Sqrt(static_cast<double>(PackedTriangles.Num()))), 1, 64);
            TMap<int32, TArray<int32>> CellToTriangleIndices;
            for (int32 LocalIndex = 0; LocalIndex < PackedTriangles.Num(); ++LocalIndex)
            {
                const FBox2D& Bounds = PackedTriangles[LocalIndex].Bounds;
                const int32 MinCellX = FMath::Clamp(FMath::FloorToInt(Bounds.Min.X * GridDimension), 0, GridDimension - 1);
                const int32 MaxCellX = FMath::Clamp(FMath::FloorToInt(Bounds.Max.X * GridDimension), 0, GridDimension - 1);
                const int32 MinCellY = FMath::Clamp(FMath::FloorToInt(Bounds.Min.Y * GridDimension), 0, GridDimension - 1);
                const int32 MaxCellY = FMath::Clamp(FMath::FloorToInt(Bounds.Max.Y * GridDimension), 0, GridDimension - 1);

                for (int32 CellY = MinCellY; CellY <= MaxCellY; ++CellY)
                {
                    for (int32 CellX = MinCellX; CellX <= MaxCellX; ++CellX)
                    {
                        CellToTriangleIndices.FindOrAdd(CellY * GridDimension + CellX).Add(LocalIndex);
                    }
                }
            }

            TSet<uint64> CandidatePairs;
            for (const TPair<int32, TArray<int32>>& CellPair : CellToTriangleIndices)
            {
                const TArray<int32>& LocalTriangles = CellPair.Value;
                for (int32 AListIndex = 0; AListIndex < LocalTriangles.Num(); ++AListIndex)
                {
                    for (int32 BListIndex = AListIndex + 1; BListIndex < LocalTriangles.Num(); ++BListIndex)
                    {
                        CandidatePairs.Add(MakeTrianglePairKey(LocalTriangles[AListIndex], LocalTriangles[BListIndex]));
                    }
                }
            }

            for (const uint64 PairKey : CandidatePairs)
            {
                const int32 LocalA = static_cast<int32>(PairKey >> 32);
                const int32 LocalB = static_cast<int32>(PairKey & 0xffffffffu);
                const FPackedTriangleRecord& A = PackedTriangles[LocalA];
                const FPackedTriangleRecord& B = PackedTriangles[LocalB];
                if (!A.Bounds.Intersect(B.Bounds))
                {
                    continue;
                }

                if (DoTrianglesOverlapByArea(
                        A.UVs[0], A.UVs[1], A.UVs[2],
                        B.UVs[0], B.UVs[1], B.UVs[2]))
                {
                    OutProblemMaterialSlots.Add(MaterialSlotIndex);
                    OutError = FString::Printf(
                        TEXT("Generated DWC UV still contains triangle self-overlap in material slot %d."),
                        MaterialSlotIndex);
                    break;
                }
            }
        }

        return OutProblemMaterialSlots.Num() == 0;
    }

} // namespace DWCDataUVGeneratorInternal

FDWCDataUVGenerationResult FDWCDataUVGenerator::GenerateForSkeletalMesh(
    USkeletalMesh* SkeletalMesh,
    int32 LODIndex,
    int32 SourceUVChannelIndex,
    int32 PreferredUVChannelIndex,
    bool bAllowOverwriteExistingChannel,
    int32 TargetMaterialSlotIndex)
{
    using namespace DWCDataUVGeneratorInternal;

    FDWCDataUVGenerationResult Result;

    if (SkeletalMesh == nullptr)
    {
        SetFailure(Result, TEXT("No skeletal mesh is assigned."));
        return Result;
    }

    FMeshDescription* MeshDescription = SkeletalMesh->GetMeshDescription(LODIndex);
    if (MeshDescription == nullptr)
    {
        SetFailure(Result, FString::Printf(TEXT("The target skeletal mesh does not expose editable mesh description data for LOD %d."), LODIndex));
        return Result;
    }

    SkeletalMesh->Modify();

    FSkeletalMeshAttributes Attributes(*MeshDescription);
    Attributes.Register();

    auto VertexPositions = Attributes.GetVertexPositions();
    auto VertexInstanceUVs = Attributes.GetVertexInstanceUVs();

    const int32 ExistingUVChannelCount = VertexInstanceUVs.GetNumChannels();
    const int32 SafeSourceUVChannelIndex = FMath::Clamp(SourceUVChannelIndex, 0, 7);
    if (SafeSourceUVChannelIndex >= ExistingUVChannelCount)
    {
        SetFailure(Result, FString::Printf(
            TEXT("Source UV Channel %d does not exist. A DWC Data UV channel needs an existing material UV channel to preserve material-slot UV islands."),
            SafeSourceUVChannelIndex));
        return Result;
    }

    const int32 SafePreferredUVChannelIndex = FMath::Clamp(PreferredUVChannelIndex, 0, 7);

    int32 NewUVChannelIndex = INDEX_NONE;
    bool bOverwritingExistingChannel = false;
    bool bAppendedBecausePreferredChannelWasOccupied = false;

    if (SafePreferredUVChannelIndex >= ExistingUVChannelCount)
    {
        NewUVChannelIndex = SafePreferredUVChannelIndex;
        VertexInstanceUVs.SetNumChannels(NewUVChannelIndex + 1);
    }
    else if (bAllowOverwriteExistingChannel)
    {
        NewUVChannelIndex = SafePreferredUVChannelIndex;
        bOverwritingExistingChannel = true;
    }
    else
    {
        if (ExistingUVChannelCount >= 8)
        {
            SetFailure(Result, FString::Printf(
                TEXT("UV Channel %d already exists and is not marked as generated by DWC. The target mesh also already has 8 UV channels, so a new safe DWC Data UV channel cannot be appended."),
                SafePreferredUVChannelIndex));
            return Result;
        }

        NewUVChannelIndex = ExistingUVChannelCount;
        bAppendedBecausePreferredChannelWasOccupied = true;
        VertexInstanceUVs.SetNumChannels(ExistingUVChannelCount + 1);
    }

    TArray<FTriangleRecord> Triangles;
    TMap<int32, TArray<int32>> SlotToTriangleIndices;
    TSet<int32> ExcludedVertexInstanceIDs;

    for (const FTriangleID TriangleID : MeshDescription->Triangles().GetElementIDs())
    {
        const int32 MaterialSlotIndex = ResolveMaterialSlotIndex(SkeletalMesh, *MeshDescription, Attributes, TriangleID);
        if (MaterialSlotIndex == INDEX_NONE)
        {
            continue;
        }

        if (TargetMaterialSlotIndex != INDEX_NONE && MaterialSlotIndex != TargetMaterialSlotIndex)
        {
            continue;
        }

        const auto VertexInstances = MeshDescription->GetTriangleVertexInstances(TriangleID);
        if (VertexInstances.Num() < 3)
        {
            continue;
        }

        FTriangleRecord Triangle;
        Triangle.TriangleID = TriangleID;
        Triangle.MaterialSlotIndex = MaterialSlotIndex;

        for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
        {
            Triangle.VertexInstances[CornerIndex] = VertexInstances[CornerIndex];
            Triangle.Vertices[CornerIndex] = MeshDescription->GetVertexInstanceVertex(VertexInstances[CornerIndex]);
            Triangle.Positions[CornerIndex] = FVector(VertexPositions[Triangle.Vertices[CornerIndex]]);
            const FVector2f SourceUV = VertexInstanceUVs.Get(VertexInstances[CornerIndex], SafeSourceUVChannelIndex);
            Triangle.SourceUVs[CornerIndex] = FVector2D(SourceUV.X, SourceUV.Y);
        }

        const bool bSourceUVIsFinite =
            IsFiniteReasonableUV(Triangle.SourceUVs[0]) &&
            IsFiniteReasonableUV(Triangle.SourceUVs[1]) &&
            IsFiniteReasonableUV(Triangle.SourceUVs[2]);
        if (!bSourceUVIsFinite)
        {
            ++Result.InvalidSourceUVTriangleCount;
            for (const FVertexInstanceID VertexInstanceID : Triangle.VertexInstances)
            {
                ExcludedVertexInstanceIDs.Add(VertexInstanceID.GetValue());
            }
            continue;
        }

        // Degenerate geometry and UV triangles are filtered before connectivity/overlap analysis.
        // Point/line triangles would otherwise create false conflicts and cannot be rasterized.
        if (ComputeTriangleDoubleArea3D(Triangle.Positions[0], Triangle.Positions[1], Triangle.Positions[2]) <= 1.0e-10)
        {
            ++Result.Degenerate3DTriangleCount;
            for (const FVertexInstanceID VertexInstanceID : Triangle.VertexInstances)
            {
                ExcludedVertexInstanceIDs.Add(VertexInstanceID.GetValue());
            }
            continue;
        }

        if (ComputeTriangleArea2D(Triangle.SourceUVs[0], Triangle.SourceUVs[1], Triangle.SourceUVs[2]) <= 1.0e-12)
        {
            ++Result.DegenerateSourceUVTriangleCount;
            for (const FVertexInstanceID VertexInstanceID : Triangle.VertexInstances)
            {
                ExcludedVertexInstanceIDs.Add(VertexInstanceID.GetValue());
            }
            continue;
        }

        const int32 TriangleArrayIndex = Triangles.Add(Triangle);
        SlotToTriangleIndices.FindOrAdd(MaterialSlotIndex).Add(TriangleArrayIndex);
    }

    if (Triangles.Num() == 0)
    {
        if (TargetMaterialSlotIndex != INDEX_NONE)
        {
            SetFailure(Result, FString::Printf(TEXT("Material Slot %d does not contain triangles that can be unwrapped."), TargetMaterialSlotIndex));
        }
        else
        {
            SetFailure(Result, TEXT("The target mesh does not contain triangles that can be unwrapped."));
        }
        return Result;
    }

    TArray<FIslandRecord> SourceIslands;
    TArray<int32> SortedSlotIndices;
    SlotToTriangleIndices.GenerateKeyArray(SortedSlotIndices);
    SortedSlotIndices.Sort();

    for (int32 MaterialSlotIndex : SortedSlotIndices)
    {
        const TArray<int32>* SlotTriangleIndices = SlotToTriangleIndices.Find(MaterialSlotIndex);
        if (SlotTriangleIndices != nullptr)
        {
            BuildConnectedIslandsForSlot(Triangles, *SlotTriangleIndices, SourceIslands);
        }
    }

    if (SourceIslands.Num() == 0)
    {
        SetFailure(Result, TEXT("No valid connected surface islands could be generated after degenerate triangles were excluded."));
        return Result;
    }

    Result.SourceIslandCount = SourceIslands.Num();

    TArray<FIslandRecord> Islands;
    SplitSelfOverlappingIslands(
        Triangles,
        SourceIslands,
        Islands,
        Result.SplitSourceIslandCount,
        Result.SelfOverlapPairCount);

    if (Islands.Num() == 0)
    {
        SetFailure(Result, TEXT("Source UV islands were found, but no non-overlapping DWC charts could be generated."));
        return Result;
    }

    TMap<int32, FVector2f> PackedUVByVertexInstance;
    PackIslandsIntoUnitSquare(Triangles, Islands, InternalPackingResolution, InternalPaddingPixels, PackedUVByVertexInstance);

    TSet<int32> ProblemMaterialSlots;
    FString PackedValidationError;
    if (!ValidatePackedUVLayout(Triangles, Islands, PackedUVByVertexInstance, ProblemMaterialSlots, PackedValidationError))
    {
        TArray<FIslandRecord> FallbackIslands;
        BuildTriangleFallbackChartsForMaterialSlots(
            Triangles,
            Islands,
            ProblemMaterialSlots,
            FallbackIslands,
            Result.TriangleFallbackChartCount);
        Islands = MoveTemp(FallbackIslands);

        PackIslandsIntoUnitSquare(Triangles, Islands, InternalPackingResolution, InternalPaddingPixels, PackedUVByVertexInstance);
        ProblemMaterialSlots.Reset();
        PackedValidationError.Reset();
        if (!ValidatePackedUVLayout(Triangles, Islands, PackedUVByVertexInstance, ProblemMaterialSlots, PackedValidationError))
        {
            SetFailure(Result, FString::Printf(
                TEXT("DWC Data UV generation failed final non-overlap validation: %s"),
                *PackedValidationError));
            return Result;
        }
    }

    for (const TPair<int32, FVector2f>& Pair : PackedUVByVertexInstance)
    {
        const FVertexInstanceID VertexInstanceID(Pair.Key);
        if (IsValidElementID(VertexInstanceID))
        {
            VertexInstanceUVs.Set(VertexInstanceID, NewUVChannelIndex, Pair.Value);
        }
    }

    // When regenerating an existing DWC-owned channel, explicitly clear corners belonging only
    // to excluded triangles so stale UVs cannot make them appear valid to later GPU builders.
    for (const int32 ExcludedVertexInstanceValue : ExcludedVertexInstanceIDs)
    {
        if (PackedUVByVertexInstance.Contains(ExcludedVertexInstanceValue))
        {
            continue;
        }

        const FVertexInstanceID VertexInstanceID(ExcludedVertexInstanceValue);
        if (IsValidElementID(VertexInstanceID))
        {
            VertexInstanceUVs.Set(VertexInstanceID, NewUVChannelIndex, FVector2f(0.0f, 0.0f));
        }
    }

    SkeletalMesh->CommitMeshDescription(LODIndex);
    SkeletalMesh->PostEditChange();
    SkeletalMesh->MarkPackageDirty();

    Result.bSucceeded = true;
    Result.UVChannelIndex = NewUVChannelIndex;
    Result.MaterialSlotIndex = TargetMaterialSlotIndex;
    Result.IslandCount = Islands.Num();

    const FString TargetLabel = TargetMaterialSlotIndex != INDEX_NONE
                                    ? FString::Printf(TEXT("Material Slot %d"), TargetMaterialSlotIndex)
                                    : FString(TEXT("all material slots"));
    if (bOverwritingExistingChannel)
    {
        Result.Message = FString::Printf(
            TEXT("Regenerated %s in DWC-owned DWC Data UV channel %d with %d packed source UV island(s)."),
            *TargetLabel,
            NewUVChannelIndex,
            Islands.Num());
    }
    else if (bAppendedBecausePreferredChannelWasOccupied)
    {
        Result.Message = FString::Printf(
            TEXT("Preferred UV Channel %d already existed and was not marked as DWC-generated, so created safe DWC Data UV channel %d and generated %s with %d packed source UV island(s)."),
            SafePreferredUVChannelIndex,
            NewUVChannelIndex,
            *TargetLabel,
            Islands.Num());
    }
    else
    {
        Result.Message = FString::Printf(
            TEXT("Created DWC Data UV channel %d and generated %s with %d packed source UV island(s)."),
            NewUVChannelIndex,
            *TargetLabel,
            Islands.Num());
    }

    if (Result.HasWarnings())
    {
        Result.Message += FString::Printf(
            TEXT(" Warnings: excluded %d degenerate source-UV triangle(s) and %d invalid source-UV triangle(s); split %d self-overlapping source island(s) across %d overlap pair(s); triangle fallback charts: %d. The source Skeletal Mesh was not modified."),
            Result.DegenerateSourceUVTriangleCount,
            Result.InvalidSourceUVTriangleCount,
            Result.SplitSourceIslandCount,
            Result.SelfOverlapPairCount,
            Result.TriangleFallbackChartCount);
    }

    return Result;
}
