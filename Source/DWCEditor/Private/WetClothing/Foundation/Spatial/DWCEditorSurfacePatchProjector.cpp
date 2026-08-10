//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Foundation/Spatial/DWCEditorSurfacePatchProjector.h"

#include "WetClothing/Foundation/Jobs/DWCEditorCancellationToken.h"
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

    bool IntersectsCircularFootprint(
        const FVector2f Coordinates[3],
        const float Radius)
    {
        const float RadiusSquared = Radius * Radius;
        for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
        {
            if (Coordinates[CornerIndex].SizeSquared() <= RadiusSquared + ProjectionTolerance)
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
                    Coordinates[(EdgeIndex + 1) % 3]) <= RadiusSquared + ProjectionTolerance)
            {
                return true;
            }
        }
        return false;
    }

    bool IntersectsUnitFootprint(
        const FVector2f PhysicalCoordinates[3],
        const FVector2f& EffectiveExtent)
    {
        const FVector2f Coordinates[3] = {
            PhysicalCoordinates[0] / EffectiveExtent,
            PhysicalCoordinates[1] / EffectiveExtent,
            PhysicalCoordinates[2] / EffectiveExtent
        };
        return IntersectsCircularFootprint(Coordinates, 1.0f);
    }

    bool IntersectsSymmetricDepthInterval(
        const float SignedDepths[3],
        const float Limit)
    {
        const float MinDepth = FMath::Min3(SignedDepths[0], SignedDepths[1], SignedDepths[2]);
        const float MaxDepth = FMath::Max3(SignedDepths[0], SignedDepths[1], SignedDepths[2]);
        return MinDepth <= Limit + ProjectionTolerance &&
            MaxDepth >= -Limit - ProjectionTolerance;
    }

    bool BuildProjectedPatchAxes(
        const FVector3f& PatchU,
        const FVector3f& PatchV,
        const FVector3f& SurfaceNormal,
        const FVector3f& TargetTangent,
        const FVector3f& TargetBitangent,
        FVector2f& OutAxisU,
        FVector2f& OutAxisV)
    {
        const FVector3f Normal = SurfaceNormal.GetSafeNormal();
        if (Normal.IsNearlyZero())
        {
            return false;
        }

        FVector3f SurfaceU = PatchU - Normal * FVector3f::DotProduct(PatchU, Normal);
        FVector3f SurfaceVHint = PatchV - Normal * FVector3f::DotProduct(PatchV, Normal);
        if (!SurfaceU.Normalize())
        {
            if (!SurfaceVHint.Normalize())
            {
                return false;
            }
            SurfaceU = FVector3f::CrossProduct(SurfaceVHint, Normal).GetSafeNormal();
        }
        FVector3f SurfaceV = SurfaceVHint -
            SurfaceU * FVector3f::DotProduct(SurfaceVHint, SurfaceU);
        if (!SurfaceV.Normalize())
        {
            SurfaceV = FVector3f::CrossProduct(Normal, SurfaceU).GetSafeNormal();
        }
        if (SurfaceV.IsNearlyZero())
        {
            return false;
        }

        const FVector3f Tangent = TargetTangent.GetSafeNormal();
        const FVector3f Bitangent = TargetBitangent.GetSafeNormal();
        if (Tangent.IsNearlyZero() || Bitangent.IsNearlyZero())
        {
            return false;
        }
        OutAxisU = FVector2f(
            FVector3f::DotProduct(SurfaceU, Tangent),
            FVector3f::DotProduct(SurfaceU, Bitangent));
        OutAxisV = FVector2f(
            FVector3f::DotProduct(SurfaceV, Tangent),
            FVector3f::DotProduct(SurfaceV, Bitangent));
        return !OutAxisU.IsNearlyZero() && !OutAxisV.IsNearlyZero();
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
        const bool bAnchorIslandOnly =
            Request.BoundaryPolicy ==
                EDWCEditorSurfacePatchBoundaryPolicy::AnchorUVIslandOnly;
        const int32 AnchorUVIslandID = AnchorTriangle.UVIslandID;
        if (bAnchorIslandOnly && AnchorUVIslandID == INDEX_NONE)
        {
            SetFailure(Result, EDWCEditorSurfacePatchProjectionStatus::InvalidRequest,
                TEXT("The anchor-island decal request has no valid anchor UV island."));
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
            SetFailure(Result, EDWCEditorSurfacePatchProjectionStatus::DegenerateSurface,
                TEXT("The surface decal anchor cannot provide a stable frame."));
            return Result;
        }
        const float CosRotation = FMath::Cos(Request.RotationRadians);
        const float SinRotation = FMath::Sin(Request.RotationRadians);
        const FVector3f PatchU = FrameU * CosRotation + FrameV * SinRotation;
        const FVector3f PatchV = FrameV * CosRotation - FrameU * SinRotation;
        struct FSharedProjectorVertexSample
        {
            FVector3f LocalPosition = FVector3f::ZeroVector;
            FVector2f PatchCoordinate = FVector2f::ZeroVector;
            float SignedDepth = 0.0f;
        };

        TArray<int32> Frontier;
        Frontier.Add(*AnchorIndex);
        TSet<int32> Visited;
        Visited.Reserve(256);
        TSet<int32> AffectedIslands;
        TMap<int64, FSharedProjectorVertexSample> SharedProjectorVertices;
        SharedProjectorVertices.Reserve(256);
        const auto UpdateWorkingSet = [&]()
        {
            Result.PeakWorkingSetBytes = FMath::Max<uint64>(
                Result.PeakWorkingSetBytes,
                Visited.GetAllocatedSize() + Frontier.GetAllocatedSize() +
                    AffectedIslands.GetAllocatedSize() + SharedProjectorVertices.GetAllocatedSize());
            if (Result.PeakWorkingSetBytes > Request.MaxWorkingSetBytes)
            {
                SetFailure(Result, EDWCEditorSurfacePatchProjectionStatus::TraversalBudgetExceeded,
                    TEXT("The surface decal traversal exceeded its memory budget."));
                return false;
            }
            return true;
        };
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
            if (Triangle.MaterialSlotIndex != Request.MaterialSlotIndex ||
                (bAnchorIslandOnly && Triangle.UVIslandID != AnchorUVIslandID))
            {
                continue;
            }

            FVector2f PatchCoordinates[3];
            float SignedDepths[3];
            bool bHasCornerInsideFootprint = false;
            bool bHasCornerInsideDepth = false;
            for (int32 Corner = 0; Corner < 3; ++Corner)
            {
                const FVector3f Delta = Triangle.LocalPositions[Corner] - AnchorPosition;
                const FVector2f ComputedCoordinate(
                    FVector3f::DotProduct(Delta, PatchU) / EffectiveExtent.X,
                    FVector3f::DotProduct(Delta, PatchV) / EffectiveExtent.Y);
                const float ComputedSignedDepth =
                    FVector3f::DotProduct(Delta, AnchorNormal);
                const int64 TopologyVertexID = Triangle.TopologyVertexIDs[Corner];
                if (TopologyVertexID != INDEX_NONE)
                {
                    if (const FSharedProjectorVertexSample* SharedSample =
                            SharedProjectorVertices.Find(TopologyVertexID))
                    {
                        PatchCoordinates[Corner] = SharedSample->PatchCoordinate;
                        SignedDepths[Corner] = SharedSample->SignedDepth;
                        if (Result.Diagnostics.bDetailed)
                        {
                            const float PositionError = FVector3f::Distance(
                                Triangle.LocalPositions[Corner], SharedSample->LocalPosition);
                            Result.Diagnostics.MaxSharedProjectorVertexError = FMath::Max(
                                Result.Diagnostics.MaxSharedProjectorVertexError,
                                PositionError);
                            if (PositionError > ProjectionTolerance)
                            {
                                ++Result.Diagnostics.SharedProjectorVertexMismatchCount;
                            }
                        }
                    }
                    else
                    {
                        FSharedProjectorVertexSample& NewSharedSample =
                            SharedProjectorVertices.Add(TopologyVertexID);
                        NewSharedSample.LocalPosition = Triangle.LocalPositions[Corner];
                        NewSharedSample.PatchCoordinate = ComputedCoordinate;
                        NewSharedSample.SignedDepth = ComputedSignedDepth;
                        PatchCoordinates[Corner] = ComputedCoordinate;
                        SignedDepths[Corner] = ComputedSignedDepth;
                    }
                }
                else
                {
                    PatchCoordinates[Corner] = ComputedCoordinate;
                    SignedDepths[Corner] = ComputedSignedDepth;
                }
                bHasCornerInsideFootprint = bHasCornerInsideFootprint ||
                    PatchCoordinates[Corner].SizeSquared() <= 1.0f + ProjectionTolerance;
                bHasCornerInsideDepth = bHasCornerInsideDepth ||
                    FMath::Abs(SignedDepths[Corner]) <=
                        Request.ProjectionDepthLocal + ProjectionTolerance;
            }

            const bool bIntersectsFootprint =
                IntersectsCircularFootprint(PatchCoordinates, 1.0f);
            const bool bIntersectsDepth = IntersectsSymmetricDepthInterval(
                SignedDepths, Request.ProjectionDepthLocal);
            const bool bIntersectsTraversalFootprint =
                IntersectsCircularFootprint(PatchCoordinates, 1.1f);
            const bool bIntersectsTraversalDepth = IntersectsSymmetricDepthInterval(
                SignedDepths, Request.ProjectionDepthLocal * 1.1f);
            if (Result.Diagnostics.bDetailed)
            {
                if (bIntersectsFootprint && !bHasCornerInsideFootprint)
                {
                    ++Result.Diagnostics.InteriorFootprintCandidateCount;
                }
                if (bIntersectsDepth && !bHasCornerInsideDepth)
                {
                    ++Result.Diagnostics.InteriorDepthCandidateCount;
                }
            }
            if (bIntersectsFootprint && bIntersectsDepth)
            {
                FDWCEditorSurfacePatchFragment& Fragment = Result.Fragments.AddDefaulted_GetRef();
                Fragment.TriangleIndex = TriangleIndex;
                Fragment.TriangleID = Triangle.TriangleID;
                Fragment.UVIslandID = Triangle.UVIslandID;
                for (int32 Corner = 0; Corner < 3; ++Corner)
                {
                    Fragment.TargetUVs[Corner] = Triangle.UVs[Corner];
                    Fragment.PatchCoordinates[Corner] = PatchCoordinates[Corner];
                    Fragment.SignedProjectionDepth[Corner] = SignedDepths[Corner];
                    Fragment.TargetUVBounds += Triangle.UVs[Corner];
                    const FVector3f SurfaceNormal =
                        Triangle.LocalNormals[Corner].GetSafeNormal(
                            UE_SMALL_NUMBER, Triangle.LocalNormal);
                    Fragment.SurfaceNormalInProjectorSpace[Corner] = FVector3f(
                        FVector3f::DotProduct(SurfaceNormal, PatchU),
                        FVector3f::DotProduct(SurfaceNormal, PatchV),
                        FVector3f::DotProduct(SurfaceNormal, AnchorNormal));
                    if (!BuildProjectedPatchAxes(
                            PatchU,
                            PatchV,
                            SurfaceNormal,
                            Triangle.LocalTangents[Corner],
                            Triangle.LocalBitangents[Corner],
                            Fragment.PatchAxisUInTargetTangent[Corner],
                            Fragment.PatchAxisVInTargetTangent[Corner]))
                    {
                        ++Result.Diagnostics.DegenerateTangentFrameCount;
                        SetFailure(
                            Result,
                            EDWCEditorSurfacePatchProjectionStatus::DegenerateSurface,
                            TEXT("The surface decal cannot derive a valid target tangent frame."));
                        return Result;
                    }
                }
                AffectedIslands.Add(Triangle.UVIslandID);
                if (Result.GetAllocatedSizeBytes() > Request.MaxResultBytes)
                {
                    SetFailure(Result, EDWCEditorSurfacePatchProjectionStatus::ResultBudgetExceeded,
                        TEXT("The surface decal fragment result exceeded its memory budget."));
                    return Result;
                }
            }

            if (!UpdateWorkingSet())
            {
                return Result;
            }
            if (!bIntersectsTraversalFootprint || !bIntersectsTraversalDepth)
            {
                continue;
            }
            for (int32 Edge = 0; Edge < 3; ++Edge)
            {
                const int32 Adjacent = Triangle.AdjacentTriangleIndices[Edge];
                const EDWCEditorSpatialEdgeType EdgeType = Triangle.EdgeTypes[Edge];
                const bool bTraversableEdge =
                    EdgeType == EDWCEditorSpatialEdgeType::Regular ||
                    (Request.BoundaryPolicy ==
                         EDWCEditorSurfacePatchBoundaryPolicy::CrossUVSeams &&
                     EdgeType == EDWCEditorSpatialEdgeType::UVSeam);
                const bool bValidAdjacent = Spatial->Triangles.IsValidIndex(Adjacent);
                const bool bAllowedAdjacentIsland = !bValidAdjacent || !bAnchorIslandOnly ||
                    Spatial->Triangles[Adjacent].UVIslandID == AnchorUVIslandID;
                if (bTraversableEdge && bValidAdjacent && bAllowedAdjacentIsland &&
                    !Visited.Contains(Adjacent) &&
                    Spatial->Triangles[Adjacent].MaterialSlotIndex == Request.MaterialSlotIndex)
                {
                    Frontier.Add(Adjacent);
                    if (EdgeType == EDWCEditorSpatialEdgeType::UVSeam)
                    {
                        ++Result.TraversedSeamCount;
                    }
                }
            }
            if (!UpdateWorkingSet())
            {
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

bool FDWCEditorSurfacePatchProjector::ValidateProjectionContract(
    const FDWCEditorSurfacePatchProjectionRequest& Request,
    FString* OutError)
{
    if (OutError != nullptr)
    {
        OutError->Reset();
    }

    const bool bKnownBoundaryPolicy =
        Request.BoundaryPolicy ==
            EDWCEditorSurfacePatchBoundaryPolicy::AnchorUVIslandOnly ||
        Request.BoundaryPolicy ==
            EDWCEditorSurfacePatchBoundaryPolicy::CrossUVSeams;
    if (!bKnownBoundaryPolicy)
    {
        if (OutError != nullptr)
        {
            *OutError = TEXT("The surface patch boundary policy is invalid.");
        }
        return false;
    }

    return true;
}

FDWCEditorSurfacePatchProjectionMemoryEstimate
FDWCEditorSurfacePatchProjector::EstimateAdmissionMemory(
    const FDWCEditorSurfacePatchProjectionRequest& Request)
{
    FDWCEditorSurfacePatchProjectionMemoryEstimate Estimate;
    if (!ValidateProjectionContract(Request, nullptr) || !Request.SpatialHandle.IsValid())
    {
        return Estimate;
    }

    const int32 SpatialTriangleCount = Request.SpatialHandle->Triangles.Num();
    Estimate.TriangleUpperBound = Request.MaxVisitedTriangles > 0
        ? FMath::Min(Request.MaxVisitedTriangles, SpatialTriangleCount)
        : SpatialTriangleCount;
    if (Estimate.TriangleUpperBound <= 0)
    {
        return Estimate;
    }

    const uint64 TriangleCount = static_cast<uint64>(Estimate.TriangleUpperBound);
    const auto SaturatingMultiply = [](const uint64 A, const uint64 B)
    {
        return A != 0 && B > MAX_uint64 / A ? MAX_uint64 : A * B;
    };
    const auto AddClamped = [](const uint64 A, const uint64 B, const uint64 Limit)
    {
        const uint64 Sum = B > MAX_uint64 - A ? MAX_uint64 : A + B;
        return FMath::Min(Sum, Limit);
    };

    const uint64 FragmentCapacityUpperBound = SaturatingMultiply(TriangleCount, 2ull);
    const uint64 FragmentResultBytes = SaturatingMultiply(
        FragmentCapacityUpperBound,
        sizeof(FDWCEditorSurfacePatchFragment) + sizeof(int32));
    Estimate.ResultBytes = AddClamped(
        sizeof(FDWCEditorSurfacePatchProjectionGeometry),
        FragmentResultBytes,
        Request.MaxResultBytes);

    // Visited/frontier/island containers and the shared physical-vertex
    // projector samples coexist during decal traversal.
    constexpr uint64 DecalTraversalBytesPerTriangle = 320ull;
    Estimate.WorkingSetBytes = AddClamped(
        4096ull,
        SaturatingMultiply(TriangleCount, DecalTraversalBytesPerTriangle),
        Request.MaxWorkingSetBytes);
    return Estimate;
}

FDWCEditorSurfacePatchProjectionResult FDWCEditorSurfacePatchProjector::Project(
    const FDWCEditorSurfacePatchProjectionRequest& Request,
    const FDWCEditorCancellationToken* CancellationToken)
{
    using namespace DWCEditorSurfacePatchProjectorPrivate;
    FString ContractError;
    if (!ValidateProjectionContract(Request, &ContractError))
    {
        FDWCEditorSurfacePatchProjectionResult Result;
        SetFailure(Result, EDWCEditorSurfacePatchProjectionStatus::InvalidRequest,
            *ContractError);
        return Result;
    }
    return ProjectSurfaceDecal(Request, CancellationToken);
}

void FDWCEditorSurfacePatchProjector::AnalyzeContinuityForDiagnostics(
    FDWCEditorSurfacePatchProjectionResult& Result,
    const FDWCEditorSpatialData& SpatialData)
{
    DWCEditorSurfacePatchProjectorPrivate::AnalyzeProjectionContinuity(Result, SpatialData);
}
