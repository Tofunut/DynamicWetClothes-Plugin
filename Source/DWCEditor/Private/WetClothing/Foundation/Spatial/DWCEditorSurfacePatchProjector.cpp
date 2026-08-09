//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Foundation/Spatial/DWCEditorSurfacePatchProjector.h"

#include "WetClothing/Foundation/Jobs/DWCEditorCancellationToken.h"
#include "WetClothing/Foundation/Spatial/DWCEditorIslandLocalGeodesicChartBuilder.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSpatialQueryService.h"

namespace DWCEditorSurfacePatchProjectorPrivate
{
    constexpr float ProjectionTolerance = 1.0e-4f;

    struct FTriangleProjectionState
    {
        FVector2f Coordinates[3] = {
            FVector2f::ZeroVector,
            FVector2f::ZeroVector,
            FVector2f::ZeroVector
        };
        float Cost = TNumericLimits<float>::Max();
        bool bValid = false;
    };

    struct FFrontierEntry
    {
        int32 TriangleIndex = INDEX_NONE;
        float Cost = TNumericLimits<float>::Max();
    };

    uint64 EstimateSparseWorkingSetBytes(
        const int32 StateCount,
        const int32 FinalizedCount,
        const int32 FrontierCount,
        const int32 AffectedIslandCount)
    {
        constexpr uint64 MapNodeOverhead = 40ull;
        constexpr uint64 SetNodeOverhead = 24ull;
        return static_cast<uint64>(StateCount) *
                (sizeof(FTriangleProjectionState) + sizeof(int32) + MapNodeOverhead) +
            static_cast<uint64>(FinalizedCount) * (sizeof(int32) + SetNodeOverhead) +
            static_cast<uint64>(FrontierCount) * sizeof(FFrontierEntry) +
            static_cast<uint64>(AffectedIslandCount) * (sizeof(int32) + SetNodeOverhead);
    }

    bool HasHigherPriority(
        const FFrontierEntry& Left,
        const FFrontierEntry& Right,
        const FDWCEditorSpatialData& Spatial)
    {
        if (Left.Cost < Right.Cost - ProjectionTolerance)
        {
            return true;
        }
        if (!FMath::IsNearlyEqual(Left.Cost, Right.Cost, ProjectionTolerance))
        {
            return false;
        }
        return Spatial.Triangles[Left.TriangleIndex].TriangleID <
            Spatial.Triangles[Right.TriangleIndex].TriangleID;
    }

    void PushFrontier(
        TArray<FFrontierEntry>& Frontier,
        const FFrontierEntry& Entry,
        const FDWCEditorSpatialData& Spatial)
    {
        int32 ChildIndex = Frontier.Add(Entry);
        while (ChildIndex > 0)
        {
            const int32 ParentIndex = (ChildIndex - 1) / 2;
            if (!HasHigherPriority(Frontier[ChildIndex], Frontier[ParentIndex], Spatial))
            {
                break;
            }
            Frontier.Swap(ChildIndex, ParentIndex);
            ChildIndex = ParentIndex;
        }
    }

    FFrontierEntry PopFrontier(
        TArray<FFrontierEntry>& Frontier,
        const FDWCEditorSpatialData& Spatial)
    {
        check(!Frontier.IsEmpty());
        const FFrontierEntry Result = Frontier[0];
        const FFrontierEntry Last = Frontier.Pop(EAllowShrinking::No);
        if (Frontier.IsEmpty())
        {
            return Result;
        }

        Frontier[0] = Last;
        int32 ParentIndex = 0;
        while (true)
        {
            const int32 LeftIndex = ParentIndex * 2 + 1;
            if (!Frontier.IsValidIndex(LeftIndex))
            {
                break;
            }
            const int32 RightIndex = LeftIndex + 1;
            int32 BestChildIndex = LeftIndex;
            if (Frontier.IsValidIndex(RightIndex) &&
                HasHigherPriority(Frontier[RightIndex], Frontier[LeftIndex], Spatial))
            {
                BestChildIndex = RightIndex;
            }
            if (!HasHigherPriority(Frontier[BestChildIndex], Frontier[ParentIndex], Spatial))
            {
                break;
            }
            Frontier.Swap(BestChildIndex, ParentIndex);
            ParentIndex = BestChildIndex;
        }
        return Result;
    }

    uint64 MakeSurfacePatchTriangleLookupKey(const int32 MaterialSlotIndex, const int32 TriangleID)
    {
        return (static_cast<uint64>(static_cast<uint32>(MaterialSlotIndex)) << 32) |
            static_cast<uint32>(TriangleID);
    }

    float Cross2D(const FVector2f& A, const FVector2f& B)
    {
        return A.X * B.Y - A.Y * B.X;
    }

    float PointSegmentDistanceSquared(
        const FVector2f& Point,
        const FVector2f& Start,
        const FVector2f& End)
    {
        const FVector2f Segment = End - Start;
        const float LengthSquared = Segment.SizeSquared();
        if (LengthSquared <= UE_SMALL_NUMBER)
        {
            return (Point - Start).SizeSquared();
        }
        const float T = FMath::Clamp(FVector2f::DotProduct(Point - Start, Segment) / LengthSquared, 0.0f, 1.0f);
        return (Point - (Start + Segment * T)).SizeSquared();
    }

    bool IsPointInsideTriangle(
        const FVector2f& Point,
        const FVector2f& A,
        const FVector2f& B,
        const FVector2f& C)
    {
        const float C0 = Cross2D(B - A, Point - A);
        const float C1 = Cross2D(C - B, Point - B);
        const float C2 = Cross2D(A - C, Point - C);
        const bool bHasNegative = C0 < -ProjectionTolerance || C1 < -ProjectionTolerance ||
            C2 < -ProjectionTolerance;
        const bool bHasPositive = C0 > ProjectionTolerance || C1 > ProjectionTolerance ||
            C2 > ProjectionTolerance;
        return !(bHasNegative && bHasPositive);
    }

    bool IntersectsUnitFootprint(
        const FVector2f PhysicalCoordinates[3],
        const FVector2f& EffectiveExtent)
    {
        FVector2f Coordinates[3];
        for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
        {
            Coordinates[CornerIndex] = PhysicalCoordinates[CornerIndex] / EffectiveExtent;
            if (Coordinates[CornerIndex].SizeSquared() <= 1.0f + ProjectionTolerance)
            {
                return true;
            }
        }
        if (IsPointInsideTriangle(
                FVector2f::ZeroVector,
                Coordinates[0],
                Coordinates[1],
                Coordinates[2]))
        {
            return true;
        }
        for (int32 EdgeIndex = 0; EdgeIndex < 3; ++EdgeIndex)
        {
            if (PointSegmentDistanceSquared(
                    FVector2f::ZeroVector,
                    Coordinates[EdgeIndex],
                    Coordinates[(EdgeIndex + 1) % 3]) <= 1.0f + ProjectionTolerance)
            {
                return true;
            }
        }
        return false;
    }

    bool FindSharedCorners(
        const FDWCEditorSpatialTriangle& CurrentTriangle,
        const int32 CurrentEdgeIndex,
        const FDWCEditorSpatialTriangle& AdjacentTriangle,
        int32& OutAdjacentStartCorner,
        int32& OutAdjacentEndCorner,
        int32& OutAdjacentThirdCorner)
    {
        const int32 CurrentStartCorner = CurrentEdgeIndex;
        const int32 CurrentEndCorner = (CurrentEdgeIndex + 1) % 3;
        const int64 StartVertexID = CurrentTriangle.TopologyVertexIDs[CurrentStartCorner];
        const int64 EndVertexID = CurrentTriangle.TopologyVertexIDs[CurrentEndCorner];
        if (StartVertexID == INDEX_NONE || EndVertexID == INDEX_NONE)
        {
            return false;
        }

        OutAdjacentStartCorner = INDEX_NONE;
        OutAdjacentEndCorner = INDEX_NONE;
        OutAdjacentThirdCorner = INDEX_NONE;
        for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
        {
            const int64 VertexID = AdjacentTriangle.TopologyVertexIDs[CornerIndex];
            if (VertexID == StartVertexID)
            {
                OutAdjacentStartCorner = CornerIndex;
            }
            else if (VertexID == EndVertexID)
            {
                OutAdjacentEndCorner = CornerIndex;
            }
            else
            {
                OutAdjacentThirdCorner = CornerIndex;
            }
        }
        return OutAdjacentStartCorner != INDEX_NONE &&
            OutAdjacentEndCorner != INDEX_NONE &&
            OutAdjacentThirdCorner != INDEX_NONE;
    }

    bool UnfoldAdjacentTriangle(
        const FDWCEditorSpatialTriangle& CurrentTriangle,
        const FTriangleProjectionState& CurrentState,
        const int32 CurrentEdgeIndex,
        const FDWCEditorSpatialTriangle& AdjacentTriangle,
        FTriangleProjectionState& OutState)
    {
        int32 AdjacentStartCorner = INDEX_NONE;
        int32 AdjacentEndCorner = INDEX_NONE;
        int32 AdjacentThirdCorner = INDEX_NONE;
        if (!FindSharedCorners(
                CurrentTriangle,
                CurrentEdgeIndex,
                AdjacentTriangle,
                AdjacentStartCorner,
                AdjacentEndCorner,
                AdjacentThirdCorner))
        {
            return false;
        }

        const int32 CurrentStartCorner = CurrentEdgeIndex;
        const int32 CurrentEndCorner = (CurrentEdgeIndex + 1) % 3;
        const int32 CurrentThirdCorner = (CurrentEdgeIndex + 2) % 3;
        const FVector2f Start2D = CurrentState.Coordinates[CurrentStartCorner];
        const FVector2f End2D = CurrentState.Coordinates[CurrentEndCorner];
        const FVector2f Edge2D = End2D - Start2D;
        const float EdgeLength2D = Edge2D.Size();
        if (EdgeLength2D <= UE_SMALL_NUMBER)
        {
            return false;
        }

        const FVector3f& Start3D = AdjacentTriangle.LocalPositions[AdjacentStartCorner];
        const FVector3f& End3D = AdjacentTriangle.LocalPositions[AdjacentEndCorner];
        const FVector3f& Third3D = AdjacentTriangle.LocalPositions[AdjacentThirdCorner];
        const float EdgeLength3D = FVector3f::Distance(Start3D, End3D);
        const float StartToThird = FVector3f::Distance(Start3D, Third3D);
        const float EndToThird = FVector3f::Distance(End3D, Third3D);
        if (EdgeLength3D <= UE_SMALL_NUMBER ||
            StartToThird <= UE_SMALL_NUMBER || EndToThird <= UE_SMALL_NUMBER)
        {
            return false;
        }

        const float Along =
            (StartToThird * StartToThird + EdgeLength3D * EdgeLength3D - EndToThird * EndToThird) /
            (2.0f * EdgeLength3D);
        const float HeightSquared = StartToThird * StartToThird - Along * Along;
        if (HeightSquared < -ProjectionTolerance)
        {
            return false;
        }

        const FVector2f EdgeDirection = Edge2D / EdgeLength2D;
        const FVector2f Perpendicular(-EdgeDirection.Y, EdgeDirection.X);
        const FVector2f EdgeBase = Start2D + EdgeDirection * Along;
        const float Height = FMath::Sqrt(FMath::Max(0.0f, HeightSquared));
        const float CurrentSide = Cross2D(Edge2D, CurrentState.Coordinates[CurrentThirdCorner] - Start2D);
        const FVector2f Third2D = EdgeBase + Perpendicular * (CurrentSide >= 0.0f ? -Height : Height);

        OutState = FTriangleProjectionState();
        OutState.Coordinates[AdjacentStartCorner] = Start2D;
        OutState.Coordinates[AdjacentEndCorner] = End2D;
        OutState.Coordinates[AdjacentThirdCorner] = Third2D;
        OutState.bValid = true;
        return true;
    }

    bool BuildTangentTransforms(
        const FDWCEditorSpatialTriangle& Triangle,
        const FVector2f Coordinates[3],
        FVector2f OutAxisU[3],
        FVector2f OutAxisV[3])
    {
        const FVector2f D1 = Coordinates[1] - Coordinates[0];
        const FVector2f D2 = Coordinates[2] - Coordinates[0];
        const float Determinant = Cross2D(D1, D2);
        if (FMath::Abs(Determinant) <= UE_SMALL_NUMBER)
        {
            return false;
        }

        const FVector3f E1 = Triangle.LocalPositions[1] - Triangle.LocalPositions[0];
        const FVector3f E2 = Triangle.LocalPositions[2] - Triangle.LocalPositions[0];
        const FVector3f PhysicalU =
            (E1 * D2.Y - E2 * D1.Y) / Determinant;
        const FVector3f PhysicalV =
            (-E1 * D2.X + E2 * D1.X) / Determinant;
        const FVector3f SurfaceNormal = FVector3f::CrossProduct(E1, E2).GetSafeNormal();
        FVector3f SafeU = (PhysicalU - SurfaceNormal *
            FVector3f::DotProduct(PhysicalU, SurfaceNormal)).GetSafeNormal();
        FVector3f SafeV = PhysicalV - SurfaceNormal *
            FVector3f::DotProduct(PhysicalV, SurfaceNormal);
        SafeV = (SafeV - SafeU * FVector3f::DotProduct(SafeV, SafeU)).GetSafeNormal();
        if (SurfaceNormal.IsNearlyZero() || SafeU.IsNearlyZero() || SafeV.IsNearlyZero())
        {
            return false;
        }

        for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
        {
            const FVector3f Tangent = Triangle.LocalTangents[CornerIndex].GetSafeNormal();
            const FVector3f Bitangent = Triangle.LocalBitangents[CornerIndex].GetSafeNormal();
            if (Tangent.IsNearlyZero() || Bitangent.IsNearlyZero())
            {
                return false;
            }

            OutAxisU[CornerIndex] = FVector2f(
                FVector3f::DotProduct(SafeU, Tangent),
                FVector3f::DotProduct(SafeU, Bitangent));
            OutAxisV[CornerIndex] = FVector2f(
                FVector3f::DotProduct(SafeV, Tangent),
                FVector3f::DotProduct(SafeV, Bitangent));
            if (OutAxisU[CornerIndex].IsNearlyZero() ||
                OutAxisV[CornerIndex].IsNearlyZero())
            {
                return false;
            }
        }
        return true;
    }

    void SetFailure(
        FDWCEditorSurfacePatchProjectionResult& Result,
        const EDWCEditorSurfacePatchProjectionStatus Status,
        const TCHAR* Message)
    {
        Result.Status = Status;
        Result.Error = Message;
        Result.Fragments.Reset();
        Result.AffectedUVIslandIDs.Reset();
    }

    struct FDiagnosticEdgeEndpoint
    {
        FIntVector Position = FIntVector::ZeroValue;
        FIntPoint UV = FIntPoint::ZeroValue;

        bool operator==(const FDiagnosticEdgeEndpoint& Other) const
        {
            return Position == Other.Position && UV == Other.UV;
        }
    };

    struct FDiagnosticSurfaceEdgeKey
    {
        FDiagnosticEdgeEndpoint A;
        FDiagnosticEdgeEndpoint B;
        int32 UVIslandID = INDEX_NONE;

        bool operator==(const FDiagnosticSurfaceEdgeKey& Other) const
        {
            return A == Other.A && B == Other.B && UVIslandID == Other.UVIslandID;
        }

        friend uint32 GetTypeHash(const FDiagnosticSurfaceEdgeKey& Key)
        {
            uint32 Hash = ::GetTypeHash(Key.A.Position.X);
            Hash = HashCombine(Hash, ::GetTypeHash(Key.A.Position.Y));
            Hash = HashCombine(Hash, ::GetTypeHash(Key.A.Position.Z));
            Hash = HashCombine(Hash, ::GetTypeHash(Key.A.UV.X));
            Hash = HashCombine(Hash, ::GetTypeHash(Key.A.UV.Y));
            Hash = HashCombine(Hash, ::GetTypeHash(Key.B.Position.X));
            Hash = HashCombine(Hash, ::GetTypeHash(Key.B.Position.Y));
            Hash = HashCombine(Hash, ::GetTypeHash(Key.B.Position.Z));
            Hash = HashCombine(Hash, ::GetTypeHash(Key.B.UV.X));
            Hash = HashCombine(Hash, ::GetTypeHash(Key.B.UV.Y));
            return HashCombine(Hash, ::GetTypeHash(Key.UVIslandID));
        }
    };

    bool EndpointLess(const FDiagnosticEdgeEndpoint& A, const FDiagnosticEdgeEndpoint& B)
    {
        if (A.Position.X != B.Position.X) return A.Position.X < B.Position.X;
        if (A.Position.Y != B.Position.Y) return A.Position.Y < B.Position.Y;
        if (A.Position.Z != B.Position.Z) return A.Position.Z < B.Position.Z;
        if (A.UV.X != B.UV.X) return A.UV.X < B.UV.X;
        return A.UV.Y < B.UV.Y;
    }

    FDiagnosticSurfaceEdgeKey MakeDiagnosticSurfaceEdgeKey(
        const FDWCEditorSpatialTriangle& Triangle,
        const int32 EdgeIndex)
    {
        const int32 EndCorner = (EdgeIndex + 1) % 3;
        auto MakeEndpoint = [&Triangle](const int32 Corner)
        {
            FDiagnosticEdgeEndpoint Endpoint;
            Endpoint.Position = FIntVector(
                FMath::RoundToInt(Triangle.LocalPositions[Corner].X * 1000.0f),
                FMath::RoundToInt(Triangle.LocalPositions[Corner].Y * 1000.0f),
                FMath::RoundToInt(Triangle.LocalPositions[Corner].Z * 1000.0f));
            Endpoint.UV = FIntPoint(
                FMath::RoundToInt(Triangle.UVs[Corner].X * 100000.0f),
                FMath::RoundToInt(Triangle.UVs[Corner].Y * 100000.0f));
            return Endpoint;
        };
        FDiagnosticSurfaceEdgeKey Key;
        Key.A = MakeEndpoint(EdgeIndex);
        Key.B = MakeEndpoint(EndCorner);
        Key.UVIslandID = Triangle.UVIslandID;
        if (EndpointLess(Key.B, Key.A))
        {
            Swap(Key.A, Key.B);
        }
        return Key;
    }

    bool FindFragmentCoordinate(
        const FDWCEditorSpatialTriangle& Triangle,
        const FDWCEditorSurfacePatchFragment& Fragment,
        const int64 TopologyVertexID,
        FVector2f& OutCoordinate)
    {
        for (int32 Corner = 0; Corner < 3; ++Corner)
        {
            if (Triangle.TopologyVertexIDs[Corner] == TopologyVertexID)
            {
                OutCoordinate = Fragment.PatchCoordinates[Corner];
                return true;
            }
        }
        return false;
    }

    void AnalyzeProjectionContinuity(
        FDWCEditorSurfacePatchProjectionResult& Result,
        const FDWCEditorSpatialData& Spatial)
    {
        FDWCEditorSurfacePatchProjectionDiagnostics& Diagnostics = Result.Diagnostics;
        if (!Diagnostics.bDetailed)
        {
            return;
        }
        const double StartSeconds = FPlatformTime::Seconds();
        Diagnostics.RegularEdgeCount = 0;
        Diagnostics.UVSeamEdgeCount = 0;
        Diagnostics.BoundaryEdgeCount = 0;
        Diagnostics.BlockedEdgeCount = 0;
        Diagnostics.InternalBoundaryEdgeCount = 0;
        Diagnostics.InternalBlockedEdgeCount = 0;
        Diagnostics.SharedEdgeComparisonCount = 0;
        Diagnostics.DiscontinuousSharedEdgeCount = 0;
        Diagnostics.DegenerateFragmentCount = 0;
        Diagnostics.FlippedFragmentCount = 0;
        Diagnostics.MaxSharedCoordinateError = 0.0f;
        Diagnostics.AverageSharedCoordinateError = 0.0f;
        Diagnostics.EmittedFragmentCount = Result.Fragments.Num();

        TMap<int32, int32> FragmentIndexByTriangle;
        FragmentIndexByTriangle.Reserve(Result.Fragments.Num());
        for (int32 FragmentIndex = 0; FragmentIndex < Result.Fragments.Num(); ++FragmentIndex)
        {
            FragmentIndexByTriangle.Add(Result.Fragments[FragmentIndex].TriangleIndex, FragmentIndex);
            const FDWCEditorSurfacePatchFragment& Fragment = Result.Fragments[FragmentIndex];
            const float TargetArea = Cross2D(
                Fragment.TargetUVs[1] - Fragment.TargetUVs[0],
                Fragment.TargetUVs[2] - Fragment.TargetUVs[0]);
            const float PatchArea = Cross2D(
                Fragment.PatchCoordinates[1] - Fragment.PatchCoordinates[0],
                Fragment.PatchCoordinates[2] - Fragment.PatchCoordinates[0]);
            if (FMath::Abs(TargetArea) <= 1.0e-8f || FMath::Abs(PatchArea) <= 1.0e-8f)
            {
                ++Diagnostics.DegenerateFragmentCount;
            }
            else if (TargetArea * PatchArea < 0.0f)
            {
                ++Diagnostics.FlippedFragmentCount;
            }
        }

        struct FBoundaryReference
        {
            EDWCEditorSpatialEdgeType Type = EDWCEditorSpatialEdgeType::Boundary;
        };
        TMap<FDiagnosticSurfaceEdgeKey, TArray<FBoundaryReference, TInlineAllocator<2>>> BoundaryReferences;
        double SharedErrorSum = 0.0;
        for (const FDWCEditorSurfacePatchFragment& Fragment : Result.Fragments)
        {
            if (!Spatial.Triangles.IsValidIndex(Fragment.TriangleIndex))
            {
                continue;
            }
            const FDWCEditorSpatialTriangle& Triangle = Spatial.Triangles[Fragment.TriangleIndex];
            for (int32 Edge = 0; Edge < 3; ++Edge)
            {
                const EDWCEditorSpatialEdgeType EdgeType = Triangle.EdgeTypes[Edge];
                const int32 Adjacent = Triangle.AdjacentTriangleIndices[Edge];
                const int32* AdjacentFragmentIndex = FragmentIndexByTriangle.Find(Adjacent);
                const bool bPaired = AdjacentFragmentIndex != nullptr;
                if (EdgeType == EDWCEditorSpatialEdgeType::Boundary ||
                    EdgeType == EDWCEditorSpatialEdgeType::Blocked)
                {
                    FBoundaryReference& Reference = BoundaryReferences.FindOrAdd(
                        MakeDiagnosticSurfaceEdgeKey(Triangle, Edge)).AddDefaulted_GetRef();
                    Reference.Type = EdgeType;
                }
                if (bPaired && Fragment.TriangleIndex > Adjacent)
                {
                    continue;
                }
                switch (EdgeType)
                {
                    case EDWCEditorSpatialEdgeType::Regular: ++Diagnostics.RegularEdgeCount; break;
                    case EDWCEditorSpatialEdgeType::UVSeam: ++Diagnostics.UVSeamEdgeCount; break;
                    case EDWCEditorSpatialEdgeType::Boundary: ++Diagnostics.BoundaryEdgeCount; break;
                    case EDWCEditorSpatialEdgeType::Blocked: ++Diagnostics.BlockedEdgeCount; break;
                }
                if (EdgeType != EDWCEditorSpatialEdgeType::Regular || !bPaired ||
                    !Spatial.Triangles.IsValidIndex(Adjacent))
                {
                    continue;
                }

                const FDWCEditorSpatialTriangle& AdjacentTriangle = Spatial.Triangles[Adjacent];
                const FDWCEditorSurfacePatchFragment& AdjacentFragment =
                    Result.Fragments[*AdjacentFragmentIndex];
                float EdgeError = 0.0f;
                bool bCompared = true;
                for (const int32 Corner : {Edge, (Edge + 1) % 3})
                {
                    FVector2f AdjacentCoordinate;
                    if (!FindFragmentCoordinate(
                            AdjacentTriangle,
                            AdjacentFragment,
                            Triangle.TopologyVertexIDs[Corner],
                            AdjacentCoordinate))
                    {
                        bCompared = false;
                        break;
                    }
                    EdgeError = FMath::Max(
                        EdgeError,
                        FVector2f::Distance(Fragment.PatchCoordinates[Corner], AdjacentCoordinate));
                }
                if (bCompared)
                {
                    ++Diagnostics.SharedEdgeComparisonCount;
                    SharedErrorSum += EdgeError;
                    Diagnostics.MaxSharedCoordinateError =
                        FMath::Max(Diagnostics.MaxSharedCoordinateError, EdgeError);
                    if (EdgeError > 1.0e-3f)
                    {
                        ++Diagnostics.DiscontinuousSharedEdgeCount;
                    }
                }
            }
        }

        for (const auto& Pair : BoundaryReferences)
        {
            if (Pair.Value.Num() != 2)
            {
                continue;
            }
            const bool bBlocked = Pair.Value[0].Type == EDWCEditorSpatialEdgeType::Blocked ||
                Pair.Value[1].Type == EDWCEditorSpatialEdgeType::Blocked;
            if (bBlocked)
            {
                ++Diagnostics.InternalBlockedEdgeCount;
            }
            else
            {
                ++Diagnostics.InternalBoundaryEdgeCount;
            }
        }
        if (Diagnostics.SharedEdgeComparisonCount > 0)
        {
            Diagnostics.AverageSharedCoordinateError = static_cast<float>(
                SharedErrorSum / Diagnostics.SharedEdgeComparisonCount);
        }
        Diagnostics.ContinuityValidationMilliseconds =
            (FPlatformTime::Seconds() - StartSeconds) * 1000.0;
    }

    void BuildIslandChartShadowDiagnostics(
        const FDWCEditorSurfacePatchProjectionRequest& Request,
        const FVector2f& EffectiveExtent,
        FDWCEditorSurfacePatchProjectionResult& Result,
        const FDWCEditorCancellationToken* CancellationToken)
    {
        FDWCEditorSurfacePatchProjectionDiagnostics& Diagnostics = Result.Diagnostics;
        if (!Diagnostics.bDetailed || Request.bUseSurfaceDecalProjection ||
            !Request.SpatialHandle.IsValid())
        {
            return;
        }

        FDWCEditorIslandLocalChartRequest ChartRequest;
        ChartRequest.SpatialHandle = Request.SpatialHandle;
        ChartRequest.MaterialSlotIndex = Request.MaterialSlotIndex;
        ChartRequest.AnchorTriangleID = Request.AnchorTriangleID;
        ChartRequest.AnchorBarycentric = Request.AnchorBarycentric;
        ChartRequest.SurfaceFrameU = Request.SurfaceFrameU;
        ChartRequest.SurfaceFrameV = Request.SurfaceFrameV;
        ChartRequest.GeodesicRadiusLocal = EffectiveExtent.Size();
        ChartRequest.MaxVisitedTriangles = Request.MaxVisitedTriangles;
        ChartRequest.MaxWorkingSetBytes = Request.MaxWorkingSetBytes;
        ChartRequest.MaxResultBytes = Request.MaxResultBytes;

        const uint64 LookupKey = MakeSurfacePatchTriangleLookupKey(
            Request.MaterialSlotIndex, Request.AnchorTriangleID);
        if (const int32* AnchorIndex = Request.SpatialHandle->TriangleLookup.Find(LookupKey))
        {
            if (Request.SpatialHandle->Triangles.IsValidIndex(*AnchorIndex))
            {
                const FDWCEditorSpatialTriangle& Anchor =
                    Request.SpatialHandle->Triangles[*AnchorIndex];
                ChartRequest.NeighborhoodMarginLocal = FMath::Max3(
                    FVector3f::Distance(Anchor.LocalPositions[0], Anchor.LocalPositions[1]),
                    FVector3f::Distance(Anchor.LocalPositions[1], Anchor.LocalPositions[2]),
                    FVector3f::Distance(Anchor.LocalPositions[2], Anchor.LocalPositions[0]));
            }
        }

        Diagnostics.bIslandChartBuildAttempted = true;
        const FDWCEditorIslandLocalChartResult ChartResult =
            FDWCEditorIslandLocalGeodesicChartBuilder::Build(ChartRequest, CancellationToken);
        Diagnostics.IslandChartStatus = static_cast<uint8>(ChartResult.Status);
        Diagnostics.bIslandChartBuildSucceeded = ChartResult.IsSuccess();
        if (!ChartResult.IsSuccess())
        {
            return;
        }
        const FDWCEditorIslandLocalGeodesicChart& Chart = *ChartResult.Chart;
        Diagnostics.IslandChartVertexCount = Chart.Vertices.Num();
        Diagnostics.IslandChartTriangleCount = Chart.Triangles.Num();
        Diagnostics.IslandChartLoopMismatchCount =
            Chart.Diagnostics.DiscontinuousLoopClosureCount;
        Diagnostics.IslandChartMaxLoopResidual = Chart.Diagnostics.MaxLoopClosureResidual;
        Diagnostics.IslandChartBuildMilliseconds = Chart.Diagnostics.TotalMilliseconds;
        Diagnostics.IslandChartPeakWorkingSetBytes = Chart.Diagnostics.PeakWorkingSetBytes;
        Diagnostics.IslandChartResultBytes = Chart.Diagnostics.ResultBytes;
    }

    float SmoothProjectionWeight(const float Value, const float Limit, const float Softness)
    {
        if (Value >= Limit || Limit <= UE_SMALL_NUMBER)
        {
            return 0.0f;
        }
        const float FadeStart = Limit * (1.0f - FMath::Clamp(Softness, 0.0f, 1.0f));
        if (Value <= FadeStart || FMath::IsNearlyEqual(FadeStart, Limit))
        {
            return 1.0f;
        }
        const float T = FMath::Clamp((Value - FadeStart) / (Limit - FadeStart), 0.0f, 1.0f);
        return 1.0f - T * T * (3.0f - 2.0f * T);
    }

    FDWCEditorSurfacePatchProjectionResult ProjectSurfaceDecal(
        const FDWCEditorSurfacePatchProjectionRequest& Request,
        const FDWCEditorCancellationToken* CancellationToken)
    {
        FDWCEditorSurfacePatchProjectionResult Result;
        const double ProjectionStartSeconds = Request.bCollectDetailedDiagnostics
            ? FPlatformTime::Seconds()
            : 0.0;
        Result.Diagnostics.bDetailed = Request.bCollectDetailedDiagnostics;
        const FDWCEditorSpatialHandle& Spatial = Request.SpatialHandle;
        FVector3f AnchorBarycentric;
        const FVector2f EffectiveExtent(
            Request.SurfaceHalfExtentLocal.X * FMath::Abs(Request.Scale.X),
            Request.SurfaceHalfExtentLocal.Y * FMath::Abs(Request.Scale.Y));
        if (!Spatial.IsValid() || Request.MaterialSlotIndex == INDEX_NONE ||
            Request.AnchorTriangleID == INDEX_NONE ||
            !FDWCEditorSpatialQueryService::NormalizeSurfaceAnchor(
                Request.AnchorBarycentric, AnchorBarycentric) ||
            EffectiveExtent.X <= UE_SMALL_NUMBER || EffectiveExtent.Y <= UE_SMALL_NUMBER ||
            Request.ProjectionDepthLocal <= UE_SMALL_NUMBER ||
            Request.MaxSurfaceAngleDegrees <= 0.0f || Request.MaxSurfaceAngleDegrees >= 90.0f)
        {
            SetFailure(Result, EDWCEditorSurfacePatchProjectionStatus::InvalidRequest,
                TEXT("The surface decal request has an invalid anchor, footprint, depth, or angle."));
            return Result;
        }

        const int32* AnchorIndex = Spatial->TriangleLookup.Find(
            MakeSurfacePatchTriangleLookupKey(Request.MaterialSlotIndex, Request.AnchorTriangleID));
        if (AnchorIndex == nullptr || !Spatial->Triangles.IsValidIndex(*AnchorIndex))
        {
            SetFailure(Result, EDWCEditorSurfacePatchProjectionStatus::AnchorNotFound,
                TEXT("The surface decal anchor triangle was not found."));
            return Result;
        }

        const FDWCEditorSpatialTriangle& AnchorTriangle = Spatial->Triangles[*AnchorIndex];
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
            SetFailure(Result, EDWCEditorSurfacePatchProjectionStatus::DegenerateSurface,
                TEXT("The surface decal anchor cannot provide a stable frame."));
            return Result;
        }
        const float CosRotation = FMath::Cos(Request.RotationRadians);
        const float SinRotation = FMath::Sin(Request.RotationRadians);
        const FVector3f PatchU = FrameU * CosRotation + FrameV * SinRotation;
        const FVector3f PatchV = FrameV * CosRotation - FrameU * SinRotation;
        const float MaxAngleRadians = FMath::DegreesToRadians(Request.MaxSurfaceAngleDegrees);

        TArray<int32> Frontier;
        Frontier.Add(*AnchorIndex);
        TSet<int32> Visited;
        Visited.Reserve(256);
        TSet<int32> AffectedIslands;
        const int32 VisitLimit = Request.MaxVisitedTriangles > 0
            ? FMath::Min(Request.MaxVisitedTriangles, Spatial->Triangles.Num())
            : Spatial->Triangles.Num();

        while (!Frontier.IsEmpty())
        {
            if (CancellationToken != nullptr && CancellationToken->IsCanceled())
            {
                SetFailure(Result, EDWCEditorSurfacePatchProjectionStatus::Canceled,
                    TEXT("The surface decal projection was canceled."));
                return Result;
            }
            const int32 TriangleIndex = Frontier.Pop(EAllowShrinking::No);
            if (Visited.Contains(TriangleIndex) || !Spatial->Triangles.IsValidIndex(TriangleIndex))
            {
                continue;
            }
            if (Visited.Num() >= VisitLimit)
            {
                SetFailure(Result, EDWCEditorSurfacePatchProjectionStatus::TraversalBudgetExceeded,
                    TEXT("The surface decal projection exceeded its triangle budget."));
                return Result;
            }
            Visited.Add(TriangleIndex);
            ++Result.VisitedTriangleCount;
            const FDWCEditorSpatialTriangle& Triangle = Spatial->Triangles[TriangleIndex];
            if (Triangle.MaterialSlotIndex != Request.MaterialSlotIndex)
            {
                continue;
            }

            FVector2f PatchCoordinates[3];
            float Depths[3];
            float Influences[3];
            bool bWithinTraversalVolume = false;
            for (int32 Corner = 0; Corner < 3; ++Corner)
            {
                const FVector3f Delta = Triangle.LocalPositions[Corner] - AnchorPosition;
                PatchCoordinates[Corner] = FVector2f(
                    FVector3f::DotProduct(Delta, PatchU) / EffectiveExtent.X,
                    FVector3f::DotProduct(Delta, PatchV) / EffectiveExtent.Y);
                Depths[Corner] = FMath::Abs(FVector3f::DotProduct(Delta, AnchorNormal));
                const float NormalDot = FMath::Clamp(
                    FVector3f::DotProduct(Triangle.LocalNormals[Corner].GetSafeNormal(), AnchorNormal),
                    -1.0f,
                    1.0f);
                const float Angle = FMath::Acos(NormalDot);
                Influences[Corner] =
                    SmoothProjectionWeight(
                        Depths[Corner], Request.ProjectionDepthLocal, Request.ProjectionDepthSoftness) *
                    SmoothProjectionWeight(
                        Angle, MaxAngleRadians, Request.ProjectionAngleSoftness);
                bWithinTraversalVolume = bWithinTraversalVolume ||
                    (PatchCoordinates[Corner].SizeSquared() <= 1.21f &&
                     Depths[Corner] <= Request.ProjectionDepthLocal * 1.1f);
            }

            const FVector2f PhysicalCoordinates[3] = {
                PatchCoordinates[0] * EffectiveExtent,
                PatchCoordinates[1] * EffectiveExtent,
                PatchCoordinates[2] * EffectiveExtent
            };
            const bool bIntersectsFootprint = IntersectsUnitFootprint(PhysicalCoordinates, EffectiveExtent);
            const bool bHasInfluence = Influences[0] > UE_SMALL_NUMBER ||
                Influences[1] > UE_SMALL_NUMBER || Influences[2] > UE_SMALL_NUMBER;
            if (bIntersectsFootprint && bHasInfluence)
            {
                FDWCEditorSurfacePatchFragment& Fragment = Result.Fragments.AddDefaulted_GetRef();
                Fragment.TriangleIndex = TriangleIndex;
                Fragment.TriangleID = Triangle.TriangleID;
                Fragment.UVIslandID = Triangle.UVIslandID;
                for (int32 Corner = 0; Corner < 3; ++Corner)
                {
                    Fragment.TargetUVs[Corner] = Triangle.UVs[Corner];
                    Fragment.PatchCoordinates[Corner] = PatchCoordinates[Corner];
                    Fragment.ProjectionInfluence[Corner] = Influences[Corner];
                    Fragment.TargetUVBounds += Triangle.UVs[Corner];
                    const FVector3f Tangent = Triangle.LocalTangents[Corner].GetSafeNormal();
                    const FVector3f Bitangent = Triangle.LocalBitangents[Corner].GetSafeNormal();
                    Fragment.PatchAxisUInTargetTangent[Corner] = FVector2f(
                        FVector3f::DotProduct(PatchU, Tangent),
                        FVector3f::DotProduct(PatchU, Bitangent));
                    Fragment.PatchAxisVInTargetTangent[Corner] = FVector2f(
                        FVector3f::DotProduct(PatchV, Tangent),
                        FVector3f::DotProduct(PatchV, Bitangent));
                }
                AffectedIslands.Add(Triangle.UVIslandID);
                if (Result.GetAllocatedSizeBytes() > Request.MaxResultBytes)
                {
                    SetFailure(Result, EDWCEditorSurfacePatchProjectionStatus::ResultBudgetExceeded,
                        TEXT("The surface decal fragment result exceeded its memory budget."));
                    return Result;
                }
            }

            if (!bWithinTraversalVolume && !bIntersectsFootprint)
            {
                continue;
            }
            for (int32 Edge = 0; Edge < 3; ++Edge)
            {
                const int32 Adjacent = Triangle.AdjacentTriangleIndices[Edge];
                const EDWCEditorSpatialEdgeType EdgeType = Triangle.EdgeTypes[Edge];
                if ((EdgeType == EDWCEditorSpatialEdgeType::Regular ||
                     (Request.bAllowUVSeamTraversal &&
                      EdgeType == EDWCEditorSpatialEdgeType::UVSeam)) &&
                    Spatial->Triangles.IsValidIndex(Adjacent) && !Visited.Contains(Adjacent) &&
                    Spatial->Triangles[Adjacent].MaterialSlotIndex == Request.MaterialSlotIndex)
                {
                    Frontier.Add(Adjacent);
                    if (EdgeType == EDWCEditorSpatialEdgeType::UVSeam)
                    {
                        ++Result.TraversedSeamCount;
                    }
                }
            }
            Result.PeakWorkingSetBytes = FMath::Max<uint64>(
                Result.PeakWorkingSetBytes,
                Visited.GetAllocatedSize() + Frontier.GetAllocatedSize() + AffectedIslands.GetAllocatedSize());
            if (Result.PeakWorkingSetBytes > Request.MaxWorkingSetBytes)
            {
                SetFailure(Result, EDWCEditorSurfacePatchProjectionStatus::TraversalBudgetExceeded,
                    TEXT("The surface decal traversal exceeded its memory budget."));
                return Result;
            }
        }

        for (const int32 Island : AffectedIslands)
        {
            Result.AffectedUVIslandIDs.Add(Island);
        }
        Result.AffectedUVIslandIDs.Sort();
        Result.Status = EDWCEditorSurfacePatchProjectionStatus::Succeeded;
        if (Result.Diagnostics.bDetailed)
        {
            Result.Diagnostics.CandidateTriangleCount = Visited.Num();
            AnalyzeProjectionContinuity(Result, *Spatial);
            Result.Diagnostics.ProjectionMilliseconds =
                (FPlatformTime::Seconds() - ProjectionStartSeconds) * 1000.0;
        }
        return Result;
    }
}

namespace DWCEditorSurfacePatchProjectorPrivate
{
FDWCEditorSurfacePatchProjectionResult ProjectLegacyTriangleUnfolding(
    const FDWCEditorSurfacePatchProjectionRequest& Request,
    const FDWCEditorCancellationToken* CancellationToken)
{
    using namespace DWCEditorSurfacePatchProjectorPrivate;

    if (Request.bUseSurfaceDecalProjection)
    {
        return ProjectSurfaceDecal(Request, CancellationToken);
    }

    FDWCEditorSurfacePatchProjectionResult Result;
    const double ProjectionStartSeconds = Request.bCollectDetailedDiagnostics
        ? FPlatformTime::Seconds()
        : 0.0;
    Result.Diagnostics.bDetailed = Request.bCollectDetailedDiagnostics;
    const FDWCEditorSpatialHandle& Spatial = Request.SpatialHandle;
    if (!Spatial.IsValid())
    {
        SetFailure(Result, EDWCEditorSurfacePatchProjectionStatus::InvalidSpatialHandle,
            TEXT("The surface patch projection has no valid spatial payload."));
        return Result;
    }

    FVector3f AnchorBarycentric;
    const FVector2f EffectiveExtent(
        Request.SurfaceHalfExtentLocal.X * FMath::Abs(Request.Scale.X),
        Request.SurfaceHalfExtentLocal.Y * FMath::Abs(Request.Scale.Y));
    if (Request.MaterialSlotIndex == INDEX_NONE || Request.AnchorTriangleID == INDEX_NONE ||
        !FDWCEditorSpatialQueryService::NormalizeSurfaceAnchor(
            Request.AnchorBarycentric, AnchorBarycentric) ||
        !FMath::IsFinite(EffectiveExtent.X) || !FMath::IsFinite(EffectiveExtent.Y) ||
        EffectiveExtent.X <= UE_SMALL_NUMBER || EffectiveExtent.Y <= UE_SMALL_NUMBER ||
        !FMath::IsFinite(Request.RotationRadians))
    {
        SetFailure(Result, EDWCEditorSurfacePatchProjectionStatus::InvalidRequest,
            TEXT("The surface patch projection request has an invalid anchor or footprint."));
        return Result;
    }

    const int32* AnchorTriangleIndex = Spatial->TriangleLookup.Find(
        MakeSurfacePatchTriangleLookupKey(Request.MaterialSlotIndex, Request.AnchorTriangleID));
    if (AnchorTriangleIndex == nullptr || !Spatial->Triangles.IsValidIndex(*AnchorTriangleIndex))
    {
        SetFailure(Result, EDWCEditorSurfacePatchProjectionStatus::AnchorNotFound,
            TEXT("The anchored triangle is not present in the selected material-slot topology."));
        return Result;
    }

    const int32 TriangleCount = Spatial->Triangles.Num();
    const int32 VisitLimit = Request.MaxVisitedTriangles > 0
        ? FMath::Min(Request.MaxVisitedTriangles, TriangleCount)
        : TriangleCount;
    const uint64 InitialWorkingSetBytes = EstimateSparseWorkingSetBytes(1, 0, 1, 0);
    Result.PeakWorkingSetBytes = InitialWorkingSetBytes;
    if (VisitLimit <= 0 || InitialWorkingSetBytes > Request.MaxWorkingSetBytes)
    {
        SetFailure(Result, EDWCEditorSurfacePatchProjectionStatus::TraversalBudgetExceeded,
            TEXT("The surface patch projection working set exceeds its memory budget."));
        return Result;
    }

    const FDWCEditorSpatialTriangle& AnchorTriangle = Spatial->Triangles[*AnchorTriangleIndex];
    const FVector3f AnchorPosition =
        AnchorTriangle.LocalPositions[0] * AnchorBarycentric.X +
        AnchorTriangle.LocalPositions[1] * AnchorBarycentric.Y +
        AnchorTriangle.LocalPositions[2] * AnchorBarycentric.Z;
    const FVector3f SurfaceNormal = (
        AnchorTriangle.LocalNormals[0] * AnchorBarycentric.X +
        AnchorTriangle.LocalNormals[1] * AnchorBarycentric.Y +
        AnchorTriangle.LocalNormals[2] * AnchorBarycentric.Z).GetSafeNormal(
            UE_SMALL_NUMBER,
            AnchorTriangle.LocalNormal);
    FVector3f SurfaceU;
    FVector3f SurfaceV;
    if (!FDWCEditorSpatialQueryService::BuildStableSurfaceFrame(
            SurfaceNormal,
            Request.SurfaceFrameU,
            Request.SurfaceFrameV,
            SurfaceU,
            SurfaceV))
    {
        SetFailure(Result, EDWCEditorSurfacePatchProjectionStatus::DegenerateSurface,
            TEXT("The anchor triangle cannot provide a stable surface frame."));
        return Result;
    }

    const float CosRotation = FMath::Cos(Request.RotationRadians);
    const float SinRotation = FMath::Sin(Request.RotationRadians);
    const FVector3f PatchU = SurfaceU * CosRotation + SurfaceV * SinRotation;
    const FVector3f PatchV = SurfaceV * CosRotation - SurfaceU * SinRotation;

    // A patch normally touches a small neighborhood. Sparse state avoids allocating
    // arrays proportional to the complete mesh for every interactive hover request.
    TMap<int32, FTriangleProjectionState> States;
    TSet<int32> Finalized;
    const int32 InitialReserve = FMath::Max(
        1,
        FMath::Min3(
            VisitLimit,
            256,
            static_cast<int32>(Request.MaxWorkingSetBytes /
                FMath::Max<uint64>(InitialWorkingSetBytes, 1))));
    States.Reserve(InitialReserve);
    Finalized.Reserve(InitialReserve);
    TArray<FFrontierEntry> Frontier;
    Frontier.Reserve(InitialReserve);
    TSet<int32> AffectedIslandSet;
    AffectedIslandSet.Reserve(8);
    double CandidatePathErrorSum = 0.0;

    FTriangleProjectionState& AnchorState = States.Add(*AnchorTriangleIndex);
    for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
    {
        const FVector3f Delta = AnchorTriangle.LocalPositions[CornerIndex] - AnchorPosition;
        AnchorState.Coordinates[CornerIndex] = FVector2f(
            FVector3f::DotProduct(Delta, PatchU),
            FVector3f::DotProduct(Delta, PatchV));
    }
    if (FMath::Abs(Cross2D(
            AnchorState.Coordinates[1] - AnchorState.Coordinates[0],
            AnchorState.Coordinates[2] - AnchorState.Coordinates[0])) <= UE_SMALL_NUMBER)
    {
        SetFailure(Result, EDWCEditorSurfacePatchProjectionStatus::DegenerateSurface,
            TEXT("The anchor triangle collapses in the patch projection frame."));
        return Result;
    }
    AnchorState.Cost = 0.0f;
    AnchorState.bValid = true;
    PushFrontier(Frontier, { *AnchorTriangleIndex, 0.0f }, *Spatial);

    while (!Frontier.IsEmpty())
    {
        if (CancellationToken != nullptr && CancellationToken->IsCanceled())
        {
            SetFailure(Result, EDWCEditorSurfacePatchProjectionStatus::Canceled,
                TEXT("The surface patch projection was canceled."));
            return Result;
        }

        const FFrontierEntry Entry = PopFrontier(Frontier, *Spatial);
        const FTriangleProjectionState* CurrentState = States.Find(Entry.TriangleIndex);
        if (CurrentState == nullptr || Finalized.Contains(Entry.TriangleIndex) ||
            Entry.Cost > CurrentState->Cost + ProjectionTolerance)
        {
            continue;
        }
        if (Result.VisitedTriangleCount >= VisitLimit)
        {
            SetFailure(Result, EDWCEditorSurfacePatchProjectionStatus::TraversalBudgetExceeded,
                TEXT("The surface patch projection exceeded its triangle traversal budget."));
            return Result;
        }

        Finalized.Add(Entry.TriangleIndex);
        ++Result.VisitedTriangleCount;
        const uint64 FinalizedWorkingSetBytes = EstimateSparseWorkingSetBytes(
            States.Num(), Finalized.Num(), Frontier.Num(), AffectedIslandSet.Num());
        Result.PeakWorkingSetBytes = FMath::Max(
            Result.PeakWorkingSetBytes, FinalizedWorkingSetBytes);
        if (FinalizedWorkingSetBytes > Request.MaxWorkingSetBytes)
        {
            SetFailure(Result, EDWCEditorSurfacePatchProjectionStatus::TraversalBudgetExceeded,
                TEXT("The surface patch projection finalized-state set exceeds its memory budget."));
            return Result;
        }
        const FDWCEditorSpatialTriangle& Triangle = Spatial->Triangles[Entry.TriangleIndex];
        const FTriangleProjectionState State = *CurrentState;
        if (Triangle.MaterialSlotIndex != Request.MaterialSlotIndex)
        {
            continue;
        }

        if (IntersectsUnitFootprint(State.Coordinates, EffectiveExtent))
        {
            const uint64 ProspectiveResultBytes =
                static_cast<uint64>(Result.Fragments.Num() + 1) *
                    sizeof(FDWCEditorSurfacePatchFragment) +
                static_cast<uint64>(AffectedIslandSet.Num() + 1) * sizeof(int32);
            if (ProspectiveResultBytes > Request.MaxResultBytes)
            {
                SetFailure(Result, EDWCEditorSurfacePatchProjectionStatus::ResultBudgetExceeded,
                    TEXT("The projected surface fragment result exceeds its memory budget."));
                return Result;
            }

            FDWCEditorSurfacePatchFragment& Fragment = Result.Fragments.AddDefaulted_GetRef();
            Fragment.TriangleIndex = Entry.TriangleIndex;
            Fragment.TriangleID = Triangle.TriangleID;
            Fragment.UVIslandID = Triangle.UVIslandID;
            for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
            {
                Fragment.TargetUVs[CornerIndex] = Triangle.UVs[CornerIndex];
                Fragment.PatchCoordinates[CornerIndex] =
                    State.Coordinates[CornerIndex] / EffectiveExtent;
                Fragment.TargetUVBounds += Triangle.UVs[CornerIndex];
            }
            if (!BuildTangentTransforms(
                    Triangle,
                    State.Coordinates,
                    Fragment.PatchAxisUInTargetTangent,
                    Fragment.PatchAxisVInTargetTangent))
            {
                SetFailure(Result, EDWCEditorSurfacePatchProjectionStatus::DegenerateSurface,
                    TEXT("A projected triangle cannot provide a stable tangent transform."));
                return Result;
            }
            AffectedIslandSet.Add(Fragment.UVIslandID);
            const uint64 ResultBytes = Result.GetAllocatedSizeBytes();
            if (ResultBytes > Request.MaxResultBytes)
            {
                SetFailure(Result, EDWCEditorSurfacePatchProjectionStatus::ResultBudgetExceeded,
                    TEXT("The allocated surface projection result exceeds its memory budget."));
                return Result;
            }
        }

        for (int32 EdgeIndex = 0; EdgeIndex < 3; ++EdgeIndex)
        {
            const EDWCEditorSpatialEdgeType EdgeType = Triangle.EdgeTypes[EdgeIndex];
            const int32 AdjacentTriangleIndex = Triangle.AdjacentTriangleIndices[EdgeIndex];
            const bool bTraversableEdge = EdgeType == EDWCEditorSpatialEdgeType::Regular ||
                (Request.bAllowUVSeamTraversal &&
                 EdgeType == EDWCEditorSpatialEdgeType::UVSeam);
            if (!bTraversableEdge ||
                !Spatial->Triangles.IsValidIndex(AdjacentTriangleIndex) ||
                Finalized.Contains(AdjacentTriangleIndex) ||
                Spatial->Triangles[AdjacentTriangleIndex].MaterialSlotIndex != Request.MaterialSlotIndex)
            {
                continue;
            }

            const float EdgeDistance = FMath::Sqrt(PointSegmentDistanceSquared(
                FVector2f::ZeroVector,
                State.Coordinates[EdgeIndex] / EffectiveExtent,
                State.Coordinates[(EdgeIndex + 1) % 3] / EffectiveExtent));
            if (EdgeDistance > 1.0f + ProjectionTolerance)
            {
                continue;
            }

            FTriangleProjectionState CandidateState;
            if (!UnfoldAdjacentTriangle(
                    Triangle,
                    State,
                    EdgeIndex,
                    Spatial->Triangles[AdjacentTriangleIndex],
                    CandidateState))
            {
                if (Result.Diagnostics.bDetailed)
                {
                    ++Result.Diagnostics.FailedUnfoldEdgeCount;
                }
                continue;
            }
            CandidateState.Cost = FMath::Max(State.Cost, EdgeDistance);
            FTriangleProjectionState* ExistingState = States.Find(AdjacentTriangleIndex);
            if (Result.Diagnostics.bDetailed && ExistingState != nullptr)
            {
                float CandidatePathError = 0.0f;
                for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
                {
                    CandidatePathError = FMath::Max(
                        CandidatePathError,
                        FVector2f::Distance(
                            ExistingState->Coordinates[CornerIndex],
                            CandidateState.Coordinates[CornerIndex]));
                }
                ++Result.Diagnostics.CandidatePathComparisonCount;
                CandidatePathErrorSum += CandidatePathError;
                Result.Diagnostics.MaxCandidatePathError = FMath::Max(
                    Result.Diagnostics.MaxCandidatePathError,
                    CandidatePathError);
                if (CandidatePathError > 1.0e-3f)
                {
                    ++Result.Diagnostics.DiscontinuousCandidatePathCount;
                }
            }
            if (ExistingState == nullptr ||
                CandidateState.Cost < ExistingState->Cost - ProjectionTolerance)
            {
                const int32 ProspectiveStateCount = States.Num() +
                    (ExistingState == nullptr ? 1 : 0);
                const uint64 ProspectiveWorkingSetBytes = EstimateSparseWorkingSetBytes(
                    ProspectiveStateCount,
                    Finalized.Num(),
                    Frontier.Num() + 1,
                    AffectedIslandSet.Num());
                Result.PeakWorkingSetBytes = FMath::Max(
                    Result.PeakWorkingSetBytes,
                    ProspectiveWorkingSetBytes);
                if (ProspectiveWorkingSetBytes > Request.MaxWorkingSetBytes)
                {
                    SetFailure(Result, EDWCEditorSurfacePatchProjectionStatus::TraversalBudgetExceeded,
                        TEXT("The surface patch projection sparse working set exceeds its memory budget."));
                    return Result;
                }
                if (ExistingState == nullptr)
                {
                    States.Add(AdjacentTriangleIndex, CandidateState);
                }
                else
                {
                    *ExistingState = CandidateState;
                }
                PushFrontier(Frontier, { AdjacentTriangleIndex, CandidateState.Cost }, *Spatial);
                if (EdgeType == EDWCEditorSpatialEdgeType::UVSeam)
                {
                    ++Result.TraversedSeamCount;
                }
            }
        }
    }

    Result.AffectedUVIslandIDs.Reserve(AffectedIslandSet.Num());
    for (const int32 IslandID : AffectedIslandSet)
    {
        Result.AffectedUVIslandIDs.Add(IslandID);
    }
    Result.AffectedUVIslandIDs.Sort();
    Result.Status = EDWCEditorSurfacePatchProjectionStatus::Succeeded;
    Result.Error.Reset();
    if (Result.Diagnostics.bDetailed)
    {
        Result.Diagnostics.CandidateTriangleCount = States.Num();
        if (Result.Diagnostics.CandidatePathComparisonCount > 0)
        {
            Result.Diagnostics.AverageCandidatePathError = static_cast<float>(
                CandidatePathErrorSum / Result.Diagnostics.CandidatePathComparisonCount);
        }
        AnalyzeProjectionContinuity(Result, *Spatial);
        Result.Diagnostics.ProjectionMilliseconds =
            (FPlatformTime::Seconds() - ProjectionStartSeconds) * 1000.0;
        BuildIslandChartShadowDiagnostics(
            Request, EffectiveExtent, Result, CancellationToken);
    }
    return Result;
}
}

bool FDWCEditorSurfacePatchProjector::ValidateProjectionModeContract(
    const FDWCEditorSurfacePatchProjectionRequest& Request,
    FString* OutError)
{
    if (OutError != nullptr)
    {
        OutError->Reset();
    }
    if (Request.bUseSurfaceDecalProjection != Request.bAllowUVSeamTraversal)
    {
        if (OutError != nullptr)
        {
            *OutError = Request.bUseSurfaceDecalProjection
                ? TEXT("Surface Decal projection must explicitly allow UV seam traversal.")
                : TEXT("Non UV Seam projection cannot allow UV seam traversal.");
        }
        return false;
    }
    return true;
}

bool FDWCEditorSurfacePatchProjector::BuildIslandLocalChartRequest(
    const FDWCEditorSurfacePatchProjectionRequest& Request,
    FDWCEditorIslandLocalChartRequest& OutChartRequest,
    FString* OutError)
{
    using namespace DWCEditorSurfacePatchProjectorPrivate;
    if (OutError != nullptr)
    {
        OutError->Reset();
    }
    OutChartRequest = FDWCEditorIslandLocalChartRequest();
    if (!ValidateProjectionModeContract(Request, OutError) ||
        Request.bUseSurfaceDecalProjection)
    {
        if (OutError != nullptr && OutError->IsEmpty())
        {
            *OutError = TEXT("Surface Decal projection does not use an island-local chart.");
        }
        return false;
    }
    if (!Request.SpatialHandle.IsValid())
    {
        if (OutError != nullptr)
        {
            *OutError = TEXT("The surface patch projection has no valid spatial payload.");
        }
        return false;
    }

    FVector3f AnchorBarycentric;
    const FVector2f EffectiveExtent(
        Request.SurfaceHalfExtentLocal.X * FMath::Abs(Request.Scale.X),
        Request.SurfaceHalfExtentLocal.Y * FMath::Abs(Request.Scale.Y));
    if (Request.MaterialSlotIndex == INDEX_NONE || Request.AnchorTriangleID == INDEX_NONE ||
        !FDWCEditorSpatialQueryService::NormalizeSurfaceAnchor(
            Request.AnchorBarycentric, AnchorBarycentric) ||
        !FMath::IsFinite(EffectiveExtent.X) || !FMath::IsFinite(EffectiveExtent.Y) ||
        EffectiveExtent.X <= UE_SMALL_NUMBER || EffectiveExtent.Y <= UE_SMALL_NUMBER)
    {
        if (OutError != nullptr)
        {
            *OutError = TEXT("The surface patch projection request has an invalid anchor or footprint.");
        }
        return false;
    }

    const uint64 LookupKey = MakeSurfacePatchTriangleLookupKey(
        Request.MaterialSlotIndex, Request.AnchorTriangleID);
    const int32* AnchorIndex = Request.SpatialHandle->TriangleLookup.Find(LookupKey);
    if (AnchorIndex == nullptr || !Request.SpatialHandle->Triangles.IsValidIndex(*AnchorIndex))
    {
        if (OutError != nullptr)
        {
            *OutError = TEXT("The anchored triangle is not present in the selected material-slot topology.");
        }
        return false;
    }

    const FDWCEditorSpatialTriangle& Anchor = Request.SpatialHandle->Triangles[*AnchorIndex];
    OutChartRequest.SpatialHandle = Request.SpatialHandle;
    OutChartRequest.MaterialSlotIndex = Request.MaterialSlotIndex;
    OutChartRequest.AnchorTriangleID = Request.AnchorTriangleID;
    OutChartRequest.AnchorBarycentric = AnchorBarycentric;
    OutChartRequest.SurfaceFrameU = Request.SurfaceFrameU;
    OutChartRequest.SurfaceFrameV = Request.SurfaceFrameV;
    OutChartRequest.GeodesicRadiusLocal = EffectiveExtent.Size();
    OutChartRequest.NeighborhoodMarginLocal = FMath::Max3(
        FVector3f::Distance(Anchor.LocalPositions[0], Anchor.LocalPositions[1]),
        FVector3f::Distance(Anchor.LocalPositions[1], Anchor.LocalPositions[2]),
        FVector3f::Distance(Anchor.LocalPositions[2], Anchor.LocalPositions[0]));
    OutChartRequest.MaxVisitedTriangles = Request.MaxVisitedTriangles;
    OutChartRequest.MaxWorkingSetBytes = Request.MaxWorkingSetBytes;
    OutChartRequest.MaxResultBytes = Request.MaxResultBytes;
    return true;
}

FDWCEditorSurfacePatchProjectionResult FDWCEditorSurfacePatchProjector::ProjectFromIslandLocalChart(
    const FDWCEditorSurfacePatchProjectionRequest& Request,
    const FDWCEditorIslandLocalChartHandle& Chart,
    const FDWCEditorCancellationToken* CancellationToken)
{
    using namespace DWCEditorSurfacePatchProjectorPrivate;
    FDWCEditorSurfacePatchProjectionResult Result;
    const double ProjectionStartSeconds = Request.bCollectDetailedDiagnostics
        ? FPlatformTime::Seconds()
        : 0.0;
    Result.Diagnostics.bDetailed = Request.bCollectDetailedDiagnostics;
    FString ModeError;
    if (!ValidateProjectionModeContract(Request, &ModeError) ||
        Request.bUseSurfaceDecalProjection)
    {
        SetFailure(Result, EDWCEditorSurfacePatchProjectionStatus::InvalidRequest,
            ModeError.IsEmpty()
                ? TEXT("Surface Decal projection cannot consume an island-local chart.")
                : *ModeError);
        return Result;
    }
    if (!Request.SpatialHandle.IsValid() || !Chart.IsValid())
    {
        SetFailure(Result, EDWCEditorSurfacePatchProjectionStatus::InvalidSpatialHandle,
            TEXT("The Non UV Seam projection has no valid island-local chart."));
        return Result;
    }
    if (Chart->MaterialSlotIndex != Request.MaterialSlotIndex ||
        Chart->AnchorTriangleID != Request.AnchorTriangleID)
    {
        SetFailure(Result, EDWCEditorSurfacePatchProjectionStatus::InvalidRequest,
            TEXT("The island-local chart does not match the surface patch anchor."));
        return Result;
    }

    const FVector2f EffectiveExtent(
        Request.SurfaceHalfExtentLocal.X * FMath::Abs(Request.Scale.X),
        Request.SurfaceHalfExtentLocal.Y * FMath::Abs(Request.Scale.Y));
    if (!FMath::IsFinite(EffectiveExtent.X) || !FMath::IsFinite(EffectiveExtent.Y) ||
        EffectiveExtent.X <= UE_SMALL_NUMBER || EffectiveExtent.Y <= UE_SMALL_NUMBER ||
        !FMath::IsFinite(Request.RotationRadians))
    {
        SetFailure(Result, EDWCEditorSurfacePatchProjectionStatus::InvalidRequest,
            TEXT("The surface patch projection request has an invalid footprint or rotation."));
        return Result;
    }

    const float CosRotation = FMath::Cos(Request.RotationRadians);
    const float SinRotation = FMath::Sin(Request.RotationRadians);
    TSet<int32> AffectedIslandSet;
    AffectedIslandSet.Reserve(1);
    Result.VisitedTriangleCount = Chart->Triangles.Num();
    Result.PeakWorkingSetBytes = Chart->Diagnostics.PeakWorkingSetBytes;
    Result.Fragments.Reserve(FMath::Min(Chart->Triangles.Num(), 256));

    for (const FDWCEditorIslandLocalChartTriangle& ChartTriangle : Chart->Triangles)
    {
        if (CancellationToken != nullptr && CancellationToken->IsCanceled())
        {
            SetFailure(Result, EDWCEditorSurfacePatchProjectionStatus::Canceled,
                TEXT("The surface patch projection was canceled."));
            return Result;
        }
        if (!Request.SpatialHandle->Triangles.IsValidIndex(ChartTriangle.SpatialTriangleIndex))
        {
            continue;
        }

        FVector2f RotatedCoordinates[3];
        for (int32 Corner = 0; Corner < 3; ++Corner)
        {
            if (!Chart->Vertices.IsValidIndex(ChartTriangle.ChartVertexIndices[Corner]))
            {
                SetFailure(Result, EDWCEditorSurfacePatchProjectionStatus::DegenerateSurface,
                    TEXT("The island-local chart contains an invalid shared vertex reference."));
                return Result;
            }
            const FVector2f Coordinate =
                Chart->Vertices[ChartTriangle.ChartVertexIndices[Corner]].ChartCoordinate;
            RotatedCoordinates[Corner] = FVector2f(
                Coordinate.X * CosRotation + Coordinate.Y * SinRotation,
                Coordinate.Y * CosRotation - Coordinate.X * SinRotation);
        }
        if (!IntersectsUnitFootprint(RotatedCoordinates, EffectiveExtent))
        {
            continue;
        }

        const uint64 ProspectiveResultBytes =
            static_cast<uint64>(Result.Fragments.Num() + 1) *
                sizeof(FDWCEditorSurfacePatchFragment) + sizeof(int32);
        if (ProspectiveResultBytes > Request.MaxResultBytes)
        {
            SetFailure(Result, EDWCEditorSurfacePatchProjectionStatus::ResultBudgetExceeded,
                TEXT("The projected surface fragment result exceeds its memory budget."));
            return Result;
        }

        const FDWCEditorSpatialTriangle& Triangle =
            Request.SpatialHandle->Triangles[ChartTriangle.SpatialTriangleIndex];
        FDWCEditorSurfacePatchFragment& Fragment = Result.Fragments.AddDefaulted_GetRef();
        Fragment.TriangleIndex = ChartTriangle.SpatialTriangleIndex;
        Fragment.TriangleID = ChartTriangle.TriangleID;
        Fragment.UVIslandID = Chart->UVIslandID;
        for (int32 Corner = 0; Corner < 3; ++Corner)
        {
            Fragment.TargetUVs[Corner] = ChartTriangle.TargetUVs[Corner];
            Fragment.PatchCoordinates[Corner] = RotatedCoordinates[Corner] / EffectiveExtent;
            Fragment.TargetUVBounds += ChartTriangle.TargetUVs[Corner];
        }
        if (!BuildTangentTransforms(
                Triangle,
                RotatedCoordinates,
                Fragment.PatchAxisUInTargetTangent,
                Fragment.PatchAxisVInTargetTangent))
        {
            SetFailure(Result, EDWCEditorSurfacePatchProjectionStatus::DegenerateSurface,
                TEXT("A shared-chart triangle cannot provide a stable tangent transform."));
            return Result;
        }
        AffectedIslandSet.Add(Fragment.UVIslandID);
    }

    if (Result.Fragments.IsEmpty())
    {
        SetFailure(Result, EDWCEditorSurfacePatchProjectionStatus::InvalidRequest,
            TEXT("The island-local chart did not intersect the patch footprint."));
        return Result;
    }
    Result.AffectedUVIslandIDs = AffectedIslandSet.Array();
    Result.AffectedUVIslandIDs.Sort();
    Result.bTouchesUVSeam = Chart->Diagnostics.UVSeamEdgeCount > 0;
    Result.Status = EDWCEditorSurfacePatchProjectionStatus::Succeeded;
    if (Result.Diagnostics.bDetailed)
    {
        Result.Diagnostics.CandidateTriangleCount = Chart->Triangles.Num();
        Result.Diagnostics.bIslandChartBuildAttempted = true;
        Result.Diagnostics.bIslandChartBuildSucceeded = true;
        Result.Diagnostics.IslandChartStatus =
            static_cast<uint8>(EDWCEditorIslandLocalChartStatus::Succeeded);
        Result.Diagnostics.IslandChartVertexCount = Chart->Vertices.Num();
        Result.Diagnostics.IslandChartTriangleCount = Chart->Triangles.Num();
        Result.Diagnostics.IslandChartLoopMismatchCount =
            Chart->Diagnostics.DiscontinuousLoopClosureCount;
        Result.Diagnostics.IslandChartMaxLoopResidual =
            Chart->Diagnostics.MaxLoopClosureResidual;
        Result.Diagnostics.IslandChartBuildMilliseconds =
            Chart->Diagnostics.TotalMilliseconds;
        Result.Diagnostics.IslandChartPeakWorkingSetBytes =
            Chart->Diagnostics.PeakWorkingSetBytes;
        Result.Diagnostics.IslandChartResultBytes = Chart->Diagnostics.ResultBytes;
        AnalyzeProjectionContinuity(Result, *Request.SpatialHandle);
        Result.Diagnostics.ProjectionMilliseconds =
            (FPlatformTime::Seconds() - ProjectionStartSeconds) * 1000.0;
    }
    return Result;
}

FDWCEditorSurfacePatchProjectionResult FDWCEditorSurfacePatchProjector::Project(
    const FDWCEditorSurfacePatchProjectionRequest& Request,
    const FDWCEditorCancellationToken* CancellationToken)
{
    using namespace DWCEditorSurfacePatchProjectorPrivate;
    FString ModeError;
    if (!ValidateProjectionModeContract(Request, &ModeError))
    {
        FDWCEditorSurfacePatchProjectionResult Result;
        Result.Diagnostics.bDetailed = Request.bCollectDetailedDiagnostics;
        SetFailure(Result, EDWCEditorSurfacePatchProjectionStatus::InvalidRequest, *ModeError);
        return Result;
    }
    if (Request.bUseSurfaceDecalProjection)
    {
        return ProjectSurfaceDecal(Request, CancellationToken);
    }

    FDWCEditorIslandLocalChartRequest ChartRequest;
    FString RequestError;
    if (!BuildIslandLocalChartRequest(Request, ChartRequest, &RequestError))
    {
        FDWCEditorSurfacePatchProjectionResult Result;
        Result.Diagnostics.bDetailed = Request.bCollectDetailedDiagnostics;
        SetFailure(Result,
            Request.SpatialHandle.IsValid()
                ? EDWCEditorSurfacePatchProjectionStatus::InvalidRequest
                : EDWCEditorSurfacePatchProjectionStatus::InvalidSpatialHandle,
            *RequestError);
        return Result;
    }
    const FDWCEditorIslandLocalChartResult ChartResult =
        FDWCEditorIslandLocalGeodesicChartBuilder::Build(ChartRequest, CancellationToken);
    if (!ChartResult.IsSuccess())
    {
        FDWCEditorSurfacePatchProjectionResult Result;
        Result.Diagnostics.bDetailed = Request.bCollectDetailedDiagnostics;
        Result.Diagnostics.bIslandChartBuildAttempted = true;
        Result.Diagnostics.bIslandChartBuildSucceeded = false;
        Result.Diagnostics.IslandChartStatus = static_cast<uint8>(ChartResult.Status);
        const EDWCEditorSurfacePatchProjectionStatus Status =
            ChartResult.Status == EDWCEditorIslandLocalChartStatus::Canceled
                ? EDWCEditorSurfacePatchProjectionStatus::Canceled
                : (ChartResult.Status == EDWCEditorIslandLocalChartStatus::TraversalBudgetExceeded
                    ? EDWCEditorSurfacePatchProjectionStatus::TraversalBudgetExceeded
                    : (ChartResult.Status == EDWCEditorIslandLocalChartStatus::ResultBudgetExceeded
                        ? EDWCEditorSurfacePatchProjectionStatus::ResultBudgetExceeded
                        : EDWCEditorSurfacePatchProjectionStatus::DegenerateSurface));
        SetFailure(Result, Status,
            ChartResult.Error.IsEmpty()
                ? TEXT("The island-local chart build failed.")
                : *ChartResult.Error);
        return Result;
    }
    return ProjectFromIslandLocalChart(Request, ChartResult.Chart, CancellationToken);
}

void FDWCEditorSurfacePatchProjector::AnalyzeContinuityForDiagnostics(
    FDWCEditorSurfacePatchProjectionResult& Result,
    const FDWCEditorSpatialData& SpatialData)
{
    DWCEditorSurfacePatchProjectorPrivate::AnalyzeProjectionContinuity(Result, SpatialData);
}
