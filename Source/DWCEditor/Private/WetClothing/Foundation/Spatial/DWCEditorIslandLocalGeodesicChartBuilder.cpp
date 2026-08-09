//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Foundation/Spatial/DWCEditorIslandLocalGeodesicChartBuilder.h"

#include "HAL/PlatformTime.h"
#include "WetClothing/Foundation/Jobs/DWCEditorCancellationToken.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSpatialQueryService.h"

namespace DWCEditorIslandLocalGeodesicChartBuilderPrivate
{
    constexpr float DistanceTolerance = 1.0e-4f;
    constexpr float ResidualTolerance = 1.0e-3f;

    struct FEdgeKey
    {
        int64 A = INDEX_NONE;
        int64 B = INDEX_NONE;

        FEdgeKey() = default;
        FEdgeKey(const int64 InA, const int64 InB)
            : A(FMath::Min(InA, InB)), B(FMath::Max(InA, InB))
        {
        }

        bool operator==(const FEdgeKey& Other) const
        {
            return A == Other.A && B == Other.B;
        }

        friend uint32 GetTypeHash(const FEdgeKey& Key)
        {
            return HashCombine(::GetTypeHash(Key.A), ::GetTypeHash(Key.B));
        }
    };

    struct FWorkVertex
    {
        int64 TopologyVertexID = INDEX_NONE;
        FVector3f LocalPosition = FVector3f::ZeroVector;
        FVector2f ChartCoordinate = FVector2f::ZeroVector;
        float GeodesicDistance = TNumericLimits<float>::Max();
        int64 PredecessorTopologyVertexID = INDEX_NONE;
        bool bCoordinateAssigned = false;
        bool bBoundary = false;
    };

    struct FGraphEdge
    {
        int32 OtherVertexIndex = INDEX_NONE;
        float Length = 0.0f;
    };

    struct FVertexFrontierEntry
    {
        int32 VertexIndex = INDEX_NONE;
        float Distance = TNumericLimits<float>::Max();
    };

    struct FTriangleFrontierEntry
    {
        int32 TriangleIndex = INDEX_NONE;
        float Distance = TNumericLimits<float>::Max();
    };

    uint64 MakeTriangleLookupKey(const int32 MaterialSlotIndex, const int32 TriangleID)
    {
        return (static_cast<uint64>(static_cast<uint32>(MaterialSlotIndex)) << 32) |
            static_cast<uint32>(TriangleID);
    }

    float Cross2D(const FVector2f& A, const FVector2f& B)
    {
        return A.X * B.Y - A.Y * B.X;
    }

    uint64 EstimateWorkingSetBytes(
        const int32 CandidateTriangleCount,
        const int32 VertexCount,
        const int32 EdgeCount,
        const int32 FrontierCount)
    {
        constexpr uint64 SetNodeOverhead = 24ull;
        constexpr uint64 MapNodeOverhead = 40ull;
        return static_cast<uint64>(CandidateTriangleCount) *
                (sizeof(int32) + SetNodeOverhead) +
            static_cast<uint64>(VertexCount) *
                (sizeof(FWorkVertex) + sizeof(int64) + MapNodeOverhead + sizeof(TArray<FGraphEdge>)) +
            static_cast<uint64>(EdgeCount) *
                (sizeof(FEdgeKey) + sizeof(float) + MapNodeOverhead + sizeof(FGraphEdge) * 2ull) +
            static_cast<uint64>(FrontierCount) *
                FMath::Max(sizeof(FVertexFrontierEntry), sizeof(FTriangleFrontierEntry));
    }

    void SetFailure(
        FDWCEditorIslandLocalChartResult& Result,
        const EDWCEditorIslandLocalChartStatus Status,
        const TCHAR* Error)
    {
        Result.Status = Status;
        Result.Error = Error;
        Result.Chart.Reset();
    }

    bool IsCanceled(const FDWCEditorCancellationToken* CancellationToken)
    {
        return CancellationToken != nullptr && CancellationToken->IsCanceled();
    }

    float MaxTriangleEdgeLength(const FDWCEditorSpatialTriangle& Triangle)
    {
        return FMath::Max3(
            FVector3f::Distance(Triangle.LocalPositions[0], Triangle.LocalPositions[1]),
            FVector3f::Distance(Triangle.LocalPositions[1], Triangle.LocalPositions[2]),
            FVector3f::Distance(Triangle.LocalPositions[2], Triangle.LocalPositions[0]));
    }

    bool TriangleTouchesEuclideanNeighborhood(
        const FDWCEditorSpatialTriangle& Triangle,
        const FVector3f& AnchorPosition,
        const float SearchRadius)
    {
        float MinDistanceSquared = TNumericLimits<float>::Max();
        for (const FVector3f& Position : Triangle.LocalPositions)
        {
            MinDistanceSquared = FMath::Min(
                MinDistanceSquared, (Position - AnchorPosition).SizeSquared());
        }
        const float ConservativeRadius = SearchRadius + MaxTriangleEdgeLength(Triangle);
        return MinDistanceSquared <= ConservativeRadius * ConservativeRadius;
    }

    bool VertexHasHigherPriority(
        const FVertexFrontierEntry& Left,
        const FVertexFrontierEntry& Right,
        const TArray<FWorkVertex>& Vertices)
    {
        if (Left.Distance < Right.Distance - DistanceTolerance)
        {
            return true;
        }
        if (!FMath::IsNearlyEqual(Left.Distance, Right.Distance, DistanceTolerance))
        {
            return false;
        }
        return Vertices[Left.VertexIndex].TopologyVertexID <
            Vertices[Right.VertexIndex].TopologyVertexID;
    }

    template <typename EntryType, typename HigherPriorityType>
    void PushHeapEntry(
        TArray<EntryType>& Heap,
        const EntryType& Entry,
        HigherPriorityType&& HasHigherPriority)
    {
        int32 Child = Heap.Add(Entry);
        while (Child > 0)
        {
            const int32 Parent = (Child - 1) / 2;
            if (!HasHigherPriority(Heap[Child], Heap[Parent]))
            {
                break;
            }
            Heap.Swap(Child, Parent);
            Child = Parent;
        }
    }

    template <typename EntryType, typename HigherPriorityType>
    EntryType PopHeapEntry(TArray<EntryType>& Heap, HigherPriorityType&& HasHigherPriority)
    {
        check(!Heap.IsEmpty());
        const EntryType Result = Heap[0];
        const EntryType Last = Heap.Pop(EAllowShrinking::No);
        if (Heap.IsEmpty())
        {
            return Result;
        }
        Heap[0] = Last;
        int32 Parent = 0;
        while (true)
        {
            const int32 Left = Parent * 2 + 1;
            if (!Heap.IsValidIndex(Left))
            {
                break;
            }
            const int32 Right = Left + 1;
            int32 Best = Left;
            if (Heap.IsValidIndex(Right) && HasHigherPriority(Heap[Right], Heap[Left]))
            {
                Best = Right;
            }
            if (!HasHigherPriority(Heap[Best], Heap[Parent]))
            {
                break;
            }
            Heap.Swap(Best, Parent);
            Parent = Best;
        }
        return Result;
    }

    bool FindSharedCorners(
        const FDWCEditorSpatialTriangle& Current,
        const int32 EdgeIndex,
        const FDWCEditorSpatialTriangle& Adjacent,
        int32& OutAdjacentStart,
        int32& OutAdjacentEnd,
        int32& OutAdjacentThird)
    {
        const int64 StartID = Current.TopologyVertexIDs[EdgeIndex];
        const int64 EndID = Current.TopologyVertexIDs[(EdgeIndex + 1) % 3];
        OutAdjacentStart = INDEX_NONE;
        OutAdjacentEnd = INDEX_NONE;
        OutAdjacentThird = INDEX_NONE;
        for (int32 Corner = 0; Corner < 3; ++Corner)
        {
            const int64 VertexID = Adjacent.TopologyVertexIDs[Corner];
            if (VertexID == StartID)
            {
                OutAdjacentStart = Corner;
            }
            else if (VertexID == EndID)
            {
                OutAdjacentEnd = Corner;
            }
            else
            {
                OutAdjacentThird = Corner;
            }
        }
        return StartID != INDEX_NONE && EndID != INDEX_NONE &&
            OutAdjacentStart != INDEX_NONE && OutAdjacentEnd != INDEX_NONE &&
            OutAdjacentThird != INDEX_NONE;
    }

    bool UnfoldThirdVertex(
        const FDWCEditorSpatialTriangle& Current,
        const int32 CurrentEdge,
        const FDWCEditorSpatialTriangle& Adjacent,
        const TMap<int64, int32>& VertexIndexByID,
        const TArray<FWorkVertex>& Vertices,
        FVector2f& OutThirdCoordinate,
        int64& OutThirdVertexID)
    {
        int32 AdjacentStart = INDEX_NONE;
        int32 AdjacentEnd = INDEX_NONE;
        int32 AdjacentThird = INDEX_NONE;
        if (!FindSharedCorners(
                Current, CurrentEdge, Adjacent,
                AdjacentStart, AdjacentEnd, AdjacentThird))
        {
            return false;
        }
        const int32* StartWorkIndex = VertexIndexByID.Find(Current.TopologyVertexIDs[CurrentEdge]);
        const int32* EndWorkIndex = VertexIndexByID.Find(
            Current.TopologyVertexIDs[(CurrentEdge + 1) % 3]);
        const int32* CurrentThirdWorkIndex = VertexIndexByID.Find(
            Current.TopologyVertexIDs[(CurrentEdge + 2) % 3]);
        if (StartWorkIndex == nullptr || EndWorkIndex == nullptr ||
            CurrentThirdWorkIndex == nullptr ||
            !Vertices[*StartWorkIndex].bCoordinateAssigned ||
            !Vertices[*EndWorkIndex].bCoordinateAssigned ||
            !Vertices[*CurrentThirdWorkIndex].bCoordinateAssigned)
        {
            return false;
        }

        const FVector2f Start2D = Vertices[*StartWorkIndex].ChartCoordinate;
        const FVector2f End2D = Vertices[*EndWorkIndex].ChartCoordinate;
        const FVector2f Edge2D = End2D - Start2D;
        const float EdgeLength2D = Edge2D.Size();
        const FVector3f& Start3D = Adjacent.LocalPositions[AdjacentStart];
        const FVector3f& End3D = Adjacent.LocalPositions[AdjacentEnd];
        const FVector3f& Third3D = Adjacent.LocalPositions[AdjacentThird];
        const float EdgeLength3D = FVector3f::Distance(Start3D, End3D);
        const float StartToThird = FVector3f::Distance(Start3D, Third3D);
        const float EndToThird = FVector3f::Distance(End3D, Third3D);
        if (EdgeLength2D <= UE_SMALL_NUMBER || EdgeLength3D <= UE_SMALL_NUMBER ||
            StartToThird <= UE_SMALL_NUMBER || EndToThird <= UE_SMALL_NUMBER)
        {
            return false;
        }

        const float Along =
            (StartToThird * StartToThird + EdgeLength3D * EdgeLength3D -
             EndToThird * EndToThird) / (2.0f * EdgeLength3D);
        const float HeightSquared = StartToThird * StartToThird - Along * Along;
        if (HeightSquared < -DistanceTolerance)
        {
            return false;
        }
        const FVector2f EdgeDirection = Edge2D / EdgeLength2D;
        const FVector2f Perpendicular(-EdgeDirection.Y, EdgeDirection.X);
        const float CurrentSide = Cross2D(
            Edge2D,
            Vertices[*CurrentThirdWorkIndex].ChartCoordinate - Start2D);
        OutThirdCoordinate = Start2D + EdgeDirection * Along +
            Perpendicular * (CurrentSide >= 0.0f ? -1.0f : 1.0f) *
                FMath::Sqrt(FMath::Max(0.0f, HeightSquared));
        OutThirdVertexID = Adjacent.TopologyVertexIDs[AdjacentThird];
        return true;
    }
}

FDWCEditorIslandLocalChartResult FDWCEditorIslandLocalGeodesicChartBuilder::Build(
    const FDWCEditorIslandLocalChartRequest& Request,
    const FDWCEditorCancellationToken* CancellationToken)
{
    using namespace DWCEditorIslandLocalGeodesicChartBuilderPrivate;

    FDWCEditorIslandLocalChartResult Result;
    const double TotalStart = FPlatformTime::Seconds();
    const FDWCEditorSpatialHandle& Spatial = Request.SpatialHandle;
    if (!Spatial.IsValid())
    {
        SetFailure(Result, EDWCEditorIslandLocalChartStatus::InvalidSpatialHandle,
            TEXT("The island-local chart request has no valid spatial payload."));
        return Result;
    }

    FVector3f AnchorBarycentric;
    if (Request.MaterialSlotIndex == INDEX_NONE || Request.AnchorTriangleID == INDEX_NONE ||
        Request.GeodesicRadiusLocal <= UE_SMALL_NUMBER ||
        Request.NeighborhoodMarginLocal < 0.0f ||
        !FMath::IsFinite(Request.GeodesicRadiusLocal) ||
        !FMath::IsFinite(Request.NeighborhoodMarginLocal) ||
        !FDWCEditorSpatialQueryService::NormalizeSurfaceAnchor(
            Request.AnchorBarycentric, AnchorBarycentric))
    {
        SetFailure(Result, EDWCEditorIslandLocalChartStatus::InvalidRequest,
            TEXT("The island-local chart request has an invalid anchor or radius."));
        return Result;
    }

    const int32* AnchorTriangleIndex = Spatial->TriangleLookup.Find(
        MakeTriangleLookupKey(Request.MaterialSlotIndex, Request.AnchorTriangleID));
    if (AnchorTriangleIndex == nullptr || !Spatial->Triangles.IsValidIndex(*AnchorTriangleIndex))
    {
        SetFailure(Result, EDWCEditorIslandLocalChartStatus::AnchorNotFound,
            TEXT("The island-local chart anchor triangle was not found."));
        return Result;
    }

    const FDWCEditorSpatialTriangle& AnchorTriangle = Spatial->Triangles[*AnchorTriangleIndex];
    if (AnchorTriangle.UVIslandID == INDEX_NONE)
    {
        SetFailure(Result, EDWCEditorIslandLocalChartStatus::InvalidTopology,
            TEXT("The island-local chart anchor has no UV island identity."));
        return Result;
    }
    const FVector3f AnchorPosition =
        AnchorTriangle.LocalPositions[0] * AnchorBarycentric.X +
        AnchorTriangle.LocalPositions[1] * AnchorBarycentric.Y +
        AnchorTriangle.LocalPositions[2] * AnchorBarycentric.Z;
    const FVector3f AnchorNormal = (
        AnchorTriangle.LocalNormals[0] * AnchorBarycentric.X +
        AnchorTriangle.LocalNormals[1] * AnchorBarycentric.Y +
        AnchorTriangle.LocalNormals[2] * AnchorBarycentric.Z).GetSafeNormal(
            UE_SMALL_NUMBER, AnchorTriangle.LocalNormal);
    FVector3f FrameU;
    FVector3f FrameV;
    if (!FDWCEditorSpatialQueryService::BuildStableSurfaceFrame(
            AnchorNormal, Request.SurfaceFrameU, Request.SurfaceFrameV, FrameU, FrameV))
    {
        SetFailure(Result, EDWCEditorIslandLocalChartStatus::DegenerateSurface,
            TEXT("The island-local chart anchor cannot provide a stable surface frame."));
        return Result;
    }

    const int32 VisitLimit = Request.MaxVisitedTriangles > 0
        ? FMath::Min(Request.MaxVisitedTriangles, Spatial->Triangles.Num())
        : Spatial->Triangles.Num();
    const float SearchRadius = Request.GeodesicRadiusLocal + Request.NeighborhoodMarginLocal;
    const double NeighborhoodStart = FPlatformTime::Seconds();
    TSet<int32> CandidateTriangles;
    TSet<int32> QueuedCandidateTriangles;
    TArray<int32> CandidateFrontier;
    CandidateFrontier.Add(*AnchorTriangleIndex);
    QueuedCandidateTriangles.Add(*AnchorTriangleIndex);
    while (!CandidateFrontier.IsEmpty())
    {
        if (IsCanceled(CancellationToken))
        {
            SetFailure(Result, EDWCEditorIslandLocalChartStatus::Canceled,
                TEXT("The island-local chart build was canceled."));
            return Result;
        }
        const int32 TriangleIndex = CandidateFrontier.Pop(EAllowShrinking::No);
        QueuedCandidateTriangles.Remove(TriangleIndex);
        if (CandidateTriangles.Contains(TriangleIndex) ||
            !Spatial->Triangles.IsValidIndex(TriangleIndex))
        {
            continue;
        }
        const FDWCEditorSpatialTriangle& Triangle = Spatial->Triangles[TriangleIndex];
        if (Triangle.MaterialSlotIndex != Request.MaterialSlotIndex ||
            Triangle.UVIslandID != AnchorTriangle.UVIslandID ||
            (TriangleIndex != *AnchorTriangleIndex &&
             !TriangleTouchesEuclideanNeighborhood(Triangle, AnchorPosition, SearchRadius)))
        {
            continue;
        }
        if (CandidateTriangles.Num() >= VisitLimit)
        {
            SetFailure(Result, EDWCEditorIslandLocalChartStatus::TraversalBudgetExceeded,
                TEXT("The island-local chart exceeded its triangle traversal budget."));
            return Result;
        }
        CandidateTriangles.Add(TriangleIndex);
        for (int32 Edge = 0; Edge < 3; ++Edge)
        {
            if (Triangle.EdgeTypes[Edge] != EDWCEditorSpatialEdgeType::Regular)
            {
                continue;
            }
            const int32 Adjacent = Triangle.AdjacentTriangleIndices[Edge];
            if (Spatial->Triangles.IsValidIndex(Adjacent) &&
                !CandidateTriangles.Contains(Adjacent) &&
                !QueuedCandidateTriangles.Contains(Adjacent))
            {
                CandidateFrontier.Add(Adjacent);
                QueuedCandidateTriangles.Add(Adjacent);
            }
        }
        const uint64 WorkingBytes = EstimateWorkingSetBytes(
            CandidateTriangles.Num() + QueuedCandidateTriangles.Num(),
            0, 0, CandidateFrontier.Num());
        if (WorkingBytes > Request.MaxWorkingSetBytes)
        {
            SetFailure(Result, EDWCEditorIslandLocalChartStatus::TraversalBudgetExceeded,
                TEXT("The island-local chart neighborhood exceeds its memory budget."));
            return Result;
        }
    }
    if (CandidateTriangles.IsEmpty())
    {
        SetFailure(Result, EDWCEditorIslandLocalChartStatus::NoUsableNeighborhood,
            TEXT("The island-local chart found no usable neighborhood."));
        return Result;
    }

    TArray<int32> SortedCandidateTriangles = CandidateTriangles.Array();
    SortedCandidateTriangles.Sort([&Spatial](const int32 A, const int32 B)
    {
        return Spatial->Triangles[A].TriangleID < Spatial->Triangles[B].TriangleID;
    });

    TArray<FWorkVertex> WorkVertices;
    TMap<int64, int32> VertexIndexByID;
    TMap<FEdgeKey, float> UniqueEdges;
    for (const int32 TriangleIndex : SortedCandidateTriangles)
    {
        const FDWCEditorSpatialTriangle& Triangle = Spatial->Triangles[TriangleIndex];
        for (int32 Corner = 0; Corner < 3; ++Corner)
        {
            const int64 VertexID = Triangle.TopologyVertexIDs[Corner];
            if (VertexID == INDEX_NONE)
            {
                SetFailure(Result, EDWCEditorIslandLocalChartStatus::InvalidTopology,
                    TEXT("The island-local chart encountered an invalid topology vertex."));
                return Result;
            }
            if (!VertexIndexByID.Contains(VertexID))
            {
                FWorkVertex& Vertex = WorkVertices.AddDefaulted_GetRef();
                Vertex.TopologyVertexID = VertexID;
                Vertex.LocalPosition = Triangle.LocalPositions[Corner];
                VertexIndexByID.Add(VertexID, WorkVertices.Num() - 1);
            }
        }
    }
    for (const int32 TriangleIndex : SortedCandidateTriangles)
    {
        const FDWCEditorSpatialTriangle& Triangle = Spatial->Triangles[TriangleIndex];
        for (int32 Corner = 0; Corner < 3; ++Corner)
        {
            const int64 VertexID = Triangle.TopologyVertexIDs[Corner];
            if (Triangle.EdgeTypes[Corner] != EDWCEditorSpatialEdgeType::Regular)
            {
                WorkVertices[VertexIndexByID.FindChecked(VertexID)].bBoundary = true;
                const int64 EndID = Triangle.TopologyVertexIDs[(Corner + 1) % 3];
                WorkVertices[VertexIndexByID.FindChecked(EndID)].bBoundary = true;
            }
            const int64 OtherID = Triangle.TopologyVertexIDs[(Corner + 1) % 3];
            if (OtherID != INDEX_NONE)
            {
                UniqueEdges.FindOrAdd(FEdgeKey(VertexID, OtherID)) =
                    FVector3f::Distance(
                        Triangle.LocalPositions[Corner],
                        Triangle.LocalPositions[(Corner + 1) % 3]);
            }
        }
    }

    TArray<TArray<FGraphEdge>> VertexGraph;
    VertexGraph.SetNum(WorkVertices.Num());
    for (const auto& Pair : UniqueEdges)
    {
        const int32 A = VertexIndexByID.FindChecked(Pair.Key.A);
        const int32 B = VertexIndexByID.FindChecked(Pair.Key.B);
        VertexGraph[A].Add({B, Pair.Value});
        VertexGraph[B].Add({A, Pair.Value});
    }
    for (TArray<FGraphEdge>& Edges : VertexGraph)
    {
        Edges.Sort([&WorkVertices](const FGraphEdge& A, const FGraphEdge& B)
        {
            return WorkVertices[A.OtherVertexIndex].TopologyVertexID <
                WorkVertices[B.OtherVertexIndex].TopologyVertexID;
        });
    }

    FDWCEditorIslandLocalChartDiagnostics Diagnostics;
    Diagnostics.CandidateTriangleCount = CandidateTriangles.Num();
    Diagnostics.CandidateVertexCount = WorkVertices.Num();
    Diagnostics.NeighborhoodMilliseconds =
        (FPlatformTime::Seconds() - NeighborhoodStart) * 1000.0;
    Diagnostics.PeakWorkingSetBytes = EstimateWorkingSetBytes(
        CandidateTriangles.Num(), WorkVertices.Num(), UniqueEdges.Num(), CandidateFrontier.Num());
    if (Diagnostics.PeakWorkingSetBytes > Request.MaxWorkingSetBytes)
    {
        SetFailure(Result, EDWCEditorIslandLocalChartStatus::TraversalBudgetExceeded,
            TEXT("The island-local chart graph exceeds its memory budget."));
        return Result;
    }

    const double GeodesicStart = FPlatformTime::Seconds();
    TArray<FVertexFrontierEntry> VertexFrontier;
    for (int32 Corner = 0; Corner < 3; ++Corner)
    {
        const int32 VertexIndex = VertexIndexByID.FindChecked(
            AnchorTriangle.TopologyVertexIDs[Corner]);
        FWorkVertex& Vertex = WorkVertices[VertexIndex];
        Vertex.GeodesicDistance = FVector3f::Distance(AnchorPosition, Vertex.LocalPosition);
        PushHeapEntry(VertexFrontier, {VertexIndex, Vertex.GeodesicDistance},
            [&WorkVertices](const FVertexFrontierEntry& A, const FVertexFrontierEntry& B)
            {
                return VertexHasHigherPriority(A, B, WorkVertices);
            });
    }
    while (!VertexFrontier.IsEmpty())
    {
        if (IsCanceled(CancellationToken))
        {
            SetFailure(Result, EDWCEditorIslandLocalChartStatus::Canceled,
                TEXT("The island-local geodesic solve was canceled."));
            return Result;
        }
        const FVertexFrontierEntry Entry = PopHeapEntry(VertexFrontier,
            [&WorkVertices](const FVertexFrontierEntry& A, const FVertexFrontierEntry& B)
            {
                return VertexHasHigherPriority(A, B, WorkVertices);
            });
        FWorkVertex& Vertex = WorkVertices[Entry.VertexIndex];
        if (Entry.Distance > Vertex.GeodesicDistance + DistanceTolerance ||
            Entry.Distance > SearchRadius + DistanceTolerance)
        {
            continue;
        }
        for (const FGraphEdge& Edge : VertexGraph[Entry.VertexIndex])
        {
            FWorkVertex& Other = WorkVertices[Edge.OtherVertexIndex];
            const float CandidateDistance = Entry.Distance + Edge.Length;
            const bool bBetter = CandidateDistance < Other.GeodesicDistance - DistanceTolerance;
            const bool bTieWithLowerPredecessor =
                FMath::IsNearlyEqual(CandidateDistance, Other.GeodesicDistance, DistanceTolerance) &&
                (Other.PredecessorTopologyVertexID == INDEX_NONE ||
                 Vertex.TopologyVertexID < Other.PredecessorTopologyVertexID);
            if (CandidateDistance <= SearchRadius + DistanceTolerance &&
                (bBetter || bTieWithLowerPredecessor))
            {
                Other.GeodesicDistance = CandidateDistance;
                Other.PredecessorTopologyVertexID = Vertex.TopologyVertexID;
                PushHeapEntry(VertexFrontier, {Edge.OtherVertexIndex, CandidateDistance},
                    [&WorkVertices](const FVertexFrontierEntry& A, const FVertexFrontierEntry& B)
                    {
                        return VertexHasHigherPriority(A, B, WorkVertices);
                    });
            }
        }
    }
    Diagnostics.GeodesicMilliseconds =
        (FPlatformTime::Seconds() - GeodesicStart) * 1000.0;

    TSet<int32> IncludedTriangles;
    for (const int32 TriangleIndex : SortedCandidateTriangles)
    {
        const FDWCEditorSpatialTriangle& Triangle = Spatial->Triangles[TriangleIndex];
        bool bWithinRadius = TriangleIndex == *AnchorTriangleIndex;
        for (int32 Corner = 0; Corner < 3 && !bWithinRadius; ++Corner)
        {
            const FWorkVertex& Vertex = WorkVertices[
                VertexIndexByID.FindChecked(Triangle.TopologyVertexIDs[Corner])];
            bWithinRadius = Vertex.GeodesicDistance <= SearchRadius + DistanceTolerance;
        }
        if (bWithinRadius)
        {
            IncludedTriangles.Add(TriangleIndex);
        }
    }

    const double ChartStart = FPlatformTime::Seconds();
    for (int32 Corner = 0; Corner < 3; ++Corner)
    {
        FWorkVertex& Vertex = WorkVertices[
            VertexIndexByID.FindChecked(AnchorTriangle.TopologyVertexIDs[Corner])];
        const FVector3f Delta = Vertex.LocalPosition - AnchorPosition;
        Vertex.ChartCoordinate = FVector2f(
            FVector3f::DotProduct(Delta, FrameU),
            FVector3f::DotProduct(Delta, FrameV));
        Vertex.bCoordinateAssigned = true;
    }

    auto TrianglePriority = [&Spatial](const FTriangleFrontierEntry& A,
                                       const FTriangleFrontierEntry& B)
    {
        if (A.Distance < B.Distance - DistanceTolerance)
        {
            return true;
        }
        if (!FMath::IsNearlyEqual(A.Distance, B.Distance, DistanceTolerance))
        {
            return false;
        }
        return Spatial->Triangles[A.TriangleIndex].TriangleID <
            Spatial->Triangles[B.TriangleIndex].TriangleID;
    };
    auto TriangleDistance = [&VertexIndexByID, &WorkVertices, &Spatial](const int32 TriangleIndex)
    {
        const FDWCEditorSpatialTriangle& Triangle = Spatial->Triangles[TriangleIndex];
        float Distance = TNumericLimits<float>::Max();
        for (int32 Corner = 0; Corner < 3; ++Corner)
        {
            Distance = FMath::Min(Distance, WorkVertices[
                VertexIndexByID.FindChecked(Triangle.TopologyVertexIDs[Corner])].GeodesicDistance);
        }
        return Distance;
    };

    TArray<FTriangleFrontierEntry> TriangleFrontier;
    PushHeapEntry(TriangleFrontier,
        {*AnchorTriangleIndex, TriangleDistance(*AnchorTriangleIndex)}, TrianglePriority);
    TSet<int32> DiscoveredTriangles;
    DiscoveredTriangles.Add(*AnchorTriangleIndex);
    TSet<int32> FinalizedTriangles;
    TSet<uint64> ComparedTrianglePairs;
    double ResidualSum = 0.0;
    while (!TriangleFrontier.IsEmpty())
    {
        if (IsCanceled(CancellationToken))
        {
            SetFailure(Result, EDWCEditorIslandLocalChartStatus::Canceled,
                TEXT("The island-local chart solve was canceled."));
            return Result;
        }
        const FTriangleFrontierEntry Entry = PopHeapEntry(TriangleFrontier, TrianglePriority);
        if (FinalizedTriangles.Contains(Entry.TriangleIndex))
        {
            continue;
        }
        FinalizedTriangles.Add(Entry.TriangleIndex);
        const FDWCEditorSpatialTriangle& Triangle = Spatial->Triangles[Entry.TriangleIndex];
        for (int32 Edge = 0; Edge < 3; ++Edge)
        {
            switch (Triangle.EdgeTypes[Edge])
            {
                case EDWCEditorSpatialEdgeType::Regular: ++Diagnostics.RegularEdgeCount; break;
                case EDWCEditorSpatialEdgeType::UVSeam: ++Diagnostics.UVSeamEdgeCount; break;
                case EDWCEditorSpatialEdgeType::Boundary: ++Diagnostics.BoundaryEdgeCount; break;
                case EDWCEditorSpatialEdgeType::Blocked: ++Diagnostics.BlockedEdgeCount; break;
            }
            if (Triangle.EdgeTypes[Edge] != EDWCEditorSpatialEdgeType::Regular)
            {
                continue;
            }
            const int32 AdjacentIndex = Triangle.AdjacentTriangleIndices[Edge];
            if (!IncludedTriangles.Contains(AdjacentIndex))
            {
                continue;
            }
            FVector2f CandidateThird;
            int64 ThirdVertexID = INDEX_NONE;
            if (!UnfoldThirdVertex(
                    Triangle, Edge, Spatial->Triangles[AdjacentIndex],
                    VertexIndexByID, WorkVertices, CandidateThird, ThirdVertexID))
            {
                continue;
            }
            FWorkVertex& ThirdVertex = WorkVertices[VertexIndexByID.FindChecked(ThirdVertexID)];
            if (!ThirdVertex.bCoordinateAssigned)
            {
                ThirdVertex.ChartCoordinate = CandidateThird;
                ThirdVertex.bCoordinateAssigned = true;
            }
            else
            {
                const uint32 Lower = static_cast<uint32>(
                    FMath::Min(Entry.TriangleIndex, AdjacentIndex));
                const uint32 Upper = static_cast<uint32>(
                    FMath::Max(Entry.TriangleIndex, AdjacentIndex));
                const uint64 PairKey = (static_cast<uint64>(Lower) << 32) | Upper;
                if (!ComparedTrianglePairs.Contains(PairKey))
                {
                    ComparedTrianglePairs.Add(PairKey);
                    const float Residual = FVector2f::Distance(
                        ThirdVertex.ChartCoordinate, CandidateThird);
                    ++Diagnostics.LoopClosureComparisonCount;
                    ResidualSum += Residual;
                    Diagnostics.MaxLoopClosureResidual = FMath::Max(
                        Diagnostics.MaxLoopClosureResidual, Residual);
                    if (Residual > ResidualTolerance)
                    {
                        ++Diagnostics.DiscontinuousLoopClosureCount;
                    }
                }
            }
            if (!DiscoveredTriangles.Contains(AdjacentIndex))
            {
                DiscoveredTriangles.Add(AdjacentIndex);
                PushHeapEntry(TriangleFrontier,
                    {AdjacentIndex, TriangleDistance(AdjacentIndex)}, TrianglePriority);
            }
        }
    }
    if (Diagnostics.LoopClosureComparisonCount > 0)
    {
        Diagnostics.AverageLoopClosureResidual = static_cast<float>(
            ResidualSum / Diagnostics.LoopClosureComparisonCount);
    }

    TSharedRef<FDWCEditorIslandLocalGeodesicChart, ESPMode::ThreadSafe> Chart =
        MakeShared<FDWCEditorIslandLocalGeodesicChart, ESPMode::ThreadSafe>();
    Chart->MaterialSlotIndex = Request.MaterialSlotIndex;
    Chart->UVIslandID = AnchorTriangle.UVIslandID;
    Chart->AnchorTriangleID = Request.AnchorTriangleID;
    Chart->AnchorBarycentric = AnchorBarycentric;
    Chart->SurfaceFrameU = FrameU;
    Chart->SurfaceFrameV = FrameV;
    Chart->AnchorLocalPosition = AnchorPosition;
    Chart->GeodesicRadiusLocal = Request.GeodesicRadiusLocal;
    Chart->NeighborhoodMarginLocal = Request.NeighborhoodMarginLocal;

    TSet<int64> EmittedVertexIDSet;
    for (const int32 TriangleIndex : SortedCandidateTriangles)
    {
        if (!FinalizedTriangles.Contains(TriangleIndex))
        {
            continue;
        }
        const FDWCEditorSpatialTriangle& Triangle = Spatial->Triangles[TriangleIndex];
        bool bAllAssigned = true;
        for (int32 Corner = 0; Corner < 3; ++Corner)
        {
            const FWorkVertex& Vertex = WorkVertices[
                VertexIndexByID.FindChecked(Triangle.TopologyVertexIDs[Corner])];
            bAllAssigned = bAllAssigned && Vertex.bCoordinateAssigned;
        }
        if (!bAllAssigned)
        {
            continue;
        }
        for (const int64 VertexID : Triangle.TopologyVertexIDs)
        {
            EmittedVertexIDSet.Add(VertexID);
        }
    }
    TArray<int64> EmittedVertexIDs = EmittedVertexIDSet.Array();
    EmittedVertexIDs.Sort();
    TMap<int64, int32> OutputVertexIndexByID;
    Chart->Vertices.Reserve(EmittedVertexIDs.Num());
    for (const int64 VertexID : EmittedVertexIDs)
    {
        const FWorkVertex& Source = WorkVertices[VertexIndexByID.FindChecked(VertexID)];
        FDWCEditorIslandLocalChartVertex& Vertex = Chart->Vertices.AddDefaulted_GetRef();
        Vertex.TopologyVertexID = Source.TopologyVertexID;
        Vertex.LocalPosition = Source.LocalPosition;
        Vertex.ChartCoordinate = Source.ChartCoordinate;
        Vertex.GeodesicDistance = Source.GeodesicDistance;
        Vertex.PredecessorTopologyVertexID = Source.PredecessorTopologyVertexID;
        Vertex.bBoundary = Source.bBoundary;
        OutputVertexIndexByID.Add(VertexID, Chart->Vertices.Num() - 1);
    }

    for (const int32 TriangleIndex : SortedCandidateTriangles)
    {
        if (!FinalizedTriangles.Contains(TriangleIndex))
        {
            continue;
        }
        const FDWCEditorSpatialTriangle& Triangle = Spatial->Triangles[TriangleIndex];
        if (!OutputVertexIndexByID.Contains(Triangle.TopologyVertexIDs[0]) ||
            !OutputVertexIndexByID.Contains(Triangle.TopologyVertexIDs[1]) ||
            !OutputVertexIndexByID.Contains(Triangle.TopologyVertexIDs[2]))
        {
            continue;
        }
        FDWCEditorIslandLocalChartTriangle& Output = Chart->Triangles.AddDefaulted_GetRef();
        Output.SpatialTriangleIndex = TriangleIndex;
        Output.TriangleID = Triangle.TriangleID;
        for (int32 Corner = 0; Corner < 3; ++Corner)
        {
            Output.ChartVertexIndices[Corner] =
                OutputVertexIndexByID.FindChecked(Triangle.TopologyVertexIDs[Corner]);
            Output.TargetUVs[Corner] = Triangle.UVs[Corner];
            Output.EdgeTypes[Corner] = Triangle.EdgeTypes[Corner];
            Output.ChartBounds += Chart->Vertices[
                Output.ChartVertexIndices[Corner]].ChartCoordinate;
            Output.TargetUVBounds += Triangle.UVs[Corner];
        }
        const FVector2f A = Chart->Vertices[Output.ChartVertexIndices[0]].ChartCoordinate;
        const FVector2f B = Chart->Vertices[Output.ChartVertexIndices[1]].ChartCoordinate;
        const FVector2f C = Chart->Vertices[Output.ChartVertexIndices[2]].ChartCoordinate;
        if (FMath::Abs(Cross2D(B - A, C - A)) <= UE_SMALL_NUMBER)
        {
            ++Diagnostics.DegenerateTriangleCount;
        }
    }

    if (Chart->Triangles.IsEmpty())
    {
        SetFailure(Result, EDWCEditorIslandLocalChartStatus::NoUsableNeighborhood,
            TEXT("The island-local chart could not resolve any triangles."));
        return Result;
    }
    Diagnostics.EmittedTriangleCount = Chart->Triangles.Num();
    Diagnostics.EmittedVertexCount = Chart->Vertices.Num();
    Diagnostics.ChartMilliseconds = (FPlatformTime::Seconds() - ChartStart) * 1000.0;
    Diagnostics.ResultBytes = Chart->GetAllocatedSizeBytes();
    Diagnostics.PeakWorkingSetBytes = FMath::Max(
        Diagnostics.PeakWorkingSetBytes,
        EstimateWorkingSetBytes(
            CandidateTriangles.Num(), WorkVertices.Num(), UniqueEdges.Num(),
            VertexFrontier.Num() + TriangleFrontier.Num()));
    if (Diagnostics.ResultBytes > Request.MaxResultBytes)
    {
        SetFailure(Result, EDWCEditorIslandLocalChartStatus::ResultBudgetExceeded,
            TEXT("The island-local chart result exceeds its memory budget."));
        return Result;
    }
    Diagnostics.TotalMilliseconds = (FPlatformTime::Seconds() - TotalStart) * 1000.0;
    Chart->Diagnostics = Diagnostics;
    Result.Status = EDWCEditorIslandLocalChartStatus::Succeeded;
    Result.Chart = MoveTemp(Chart);
    return Result;
}
