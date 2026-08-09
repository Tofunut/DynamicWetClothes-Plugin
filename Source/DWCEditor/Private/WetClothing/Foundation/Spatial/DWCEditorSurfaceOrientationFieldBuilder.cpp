//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Foundation/Spatial/DWCEditorSurfaceOrientationFieldBuilder.h"

#include "Algo/Sort.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSurfacePatchProjectionVersion.h"

namespace
{
    enum class EOrientationTriangleClass : uint8
    {
        Stable,
        Blend,
        Fallback
    };

    struct FOrientationCandidateState
    {
        EOrientationTriangleClass Classification = EOrientationTriangleClass::Stable;
        int32 ComponentIndex = INDEX_NONE;
        bool bSolved = false;
        float Distance = TNumericLimits<float>::Max();
        int32 SeedTriangleID = MAX_int32;
        int32 ParentTriangleID = MAX_int32;
        int32 ParentEdgeIndex = MAX_int32;
        FVector3f DirectionV = FVector3f::ZeroVector;
    };

    struct FOrientationComponent
    {
        TArray<int32> TriangleIndices;
        bool bHasStableSeed = false;
    };

    struct FOrientationQueueNode
    {
        float Distance = 0.0f;
        int32 SeedTriangleID = MAX_int32;
        int32 ParentTriangleID = MAX_int32;
        int32 ParentEdgeIndex = MAX_int32;
        int32 TriangleIndex = INDEX_NONE;
        FVector3f DirectionV = FVector3f::ZeroVector;
    };

    struct FOrientationCornerKey
    {
        int32 ComponentIndex = INDEX_NONE;
        int64 TopologyVertexID = INDEX_NONE;

        bool operator==(const FOrientationCornerKey& Other) const
        {
            return ComponentIndex == Other.ComponentIndex &&
                TopologyVertexID == Other.TopologyVertexID;
        }
    };

    uint32 GetTypeHash(const FOrientationCornerKey& Key)
    {
        const uint64 VertexBits = static_cast<uint64>(Key.TopologyVertexID);
        const uint32 VertexHash = static_cast<uint32>(VertexBits) ^
            static_cast<uint32>(VertexBits >> 32);
        return HashCombine(::GetTypeHash(Key.ComponentIndex), VertexHash);
    }

    struct FOrientationCornerAccumulator
    {
        FVector3f Sum = FVector3f::ZeroVector;
        FVector3f Reference = FVector3f::ZeroVector;
        int32 Count = 0;
    };

    bool IsFiniteOrientationVector(const FVector3f& Value)
    {
        return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
    }

    bool IsTraversableOrientationEdge(const EDWCEditorSpatialEdgeType EdgeType)
    {
        return EdgeType == EDWCEditorSpatialEdgeType::Regular ||
            EdgeType == EDWCEditorSpatialEdgeType::UVSeam;
    }

    FVector3f GetOrientationTriangleNormal(
        const FDWCEditorSpatialTriangle& Triangle,
        bool& bOutDegraded)
    {
        FVector3f Normal = Triangle.LocalNormal.GetSafeNormal();
        if (!IsFiniteOrientationVector(Normal) || Normal.IsNearlyZero())
        {
            Normal = (
                Triangle.LocalNormals[0] +
                Triangle.LocalNormals[1] +
                Triangle.LocalNormals[2]).GetSafeNormal();
            bOutDegraded = true;
        }
        if (!IsFiniteOrientationVector(Normal) || Normal.IsNearlyZero())
        {
            Normal = FVector3f(0.0f, 0.0f, 1.0f);
            bOutDegraded = true;
        }
        return Normal;
    }

    FVector3f ProjectOrientationDirection(
        const FVector3f& Direction,
        const FVector3f& Normal)
    {
        return (Direction - Normal * FVector3f::DotProduct(Direction, Normal)).GetSafeNormal();
    }

    FVector3f ChooseLeastAlignedOrientationAxis(const FVector3f& Normal)
    {
        const FVector3f Axes[] = {
            FVector3f(1.0f, 0.0f, 0.0f),
            FVector3f(0.0f, 1.0f, 0.0f),
            FVector3f(0.0f, 0.0f, 1.0f)
        };
        int32 BestAxisIndex = 0;
        float BestAlignment = FMath::Abs(FVector3f::DotProduct(Normal, Axes[0]));
        for (int32 AxisIndex = 1; AxisIndex < UE_ARRAY_COUNT(Axes); ++AxisIndex)
        {
            const float Alignment = FMath::Abs(FVector3f::DotProduct(Normal, Axes[AxisIndex]));
            if (Alignment < BestAlignment)
            {
                BestAlignment = Alignment;
                BestAxisIndex = AxisIndex;
            }
        }
        return Axes[BestAxisIndex];
    }

    FVector3f BuildOrientationDirection(
        const FVector3f& PreferredAxis,
        const FVector3f& Normal)
    {
        FVector3f Direction = ProjectOrientationDirection(PreferredAxis, Normal);
        if (Direction.IsNearlyZero())
        {
            Direction = ProjectOrientationDirection(ChooseLeastAlignedOrientationAxis(Normal), Normal);
        }
        return Direction;
    }

    FVector3f TransportOrientationDirection(
        const FVector3f& Direction,
        const FVector3f& SourceNormal,
        const FVector3f& TargetNormal)
    {
        const FVector3f FromNormal = SourceNormal.GetSafeNormal();
        const FVector3f ToNormal = TargetNormal.GetSafeNormal();
        const FVector3f SourceDirection = ProjectOrientationDirection(Direction, FromNormal);
        if (FromNormal.IsNearlyZero() || ToNormal.IsNearlyZero() || SourceDirection.IsNearlyZero())
        {
            return FVector3f::ZeroVector;
        }

        const float CosAngle = FMath::Clamp(
            FVector3f::DotProduct(FromNormal, ToNormal),
            -1.0f,
            1.0f);
        const FVector3f RotationVector = FVector3f::CrossProduct(FromNormal, ToNormal);
        const float RotationVectorSizeSq = RotationVector.SizeSquared();
        FVector3f Transported = SourceDirection;
        if (RotationVectorSizeSq > UE_SMALL_NUMBER)
        {
            Transported = SourceDirection +
                FVector3f::CrossProduct(RotationVector, SourceDirection) +
                FVector3f::CrossProduct(
                    RotationVector,
                    FVector3f::CrossProduct(RotationVector, SourceDirection)) *
                    ((1.0f - CosAngle) / RotationVectorSizeSq);
        }
        else if (CosAngle < 0.0f)
        {
            Transported = ProjectOrientationDirection(SourceDirection, ToNormal);
        }

        Transported = ProjectOrientationDirection(Transported, ToNormal);
        if (Transported.IsNearlyZero())
        {
            Transported = BuildOrientationDirection(SourceDirection, ToNormal);
        }
        return Transported;
    }

    FVector3f GetOrientationTriangleCenter(const FDWCEditorSpatialTriangle& Triangle)
    {
        return (Triangle.LocalPositions[0] + Triangle.LocalPositions[1] +
            Triangle.LocalPositions[2]) / 3.0f;
    }

    float GetOrientationEdgeDistance(
        const FDWCEditorSpatialTriangle& Source,
        const FDWCEditorSpatialTriangle& Target)
    {
        return FMath::Max(
            (GetOrientationTriangleCenter(Target) - GetOrientationTriangleCenter(Source)).Size(),
            UE_KINDA_SMALL_NUMBER);
    }

    bool IsOrientationQueueNodeEarlier(
        const FOrientationQueueNode& A,
        const FOrientationQueueNode& B)
    {
        if (!FMath::IsNearlyEqual(A.Distance, B.Distance, UE_KINDA_SMALL_NUMBER))
        {
            return A.Distance < B.Distance;
        }
        if (A.SeedTriangleID != B.SeedTriangleID)
        {
            return A.SeedTriangleID < B.SeedTriangleID;
        }
        if (A.ParentTriangleID != B.ParentTriangleID)
        {
            return A.ParentTriangleID < B.ParentTriangleID;
        }
        if (A.ParentEdgeIndex != B.ParentEdgeIndex)
        {
            return A.ParentEdgeIndex < B.ParentEdgeIndex;
        }
        return A.TriangleIndex < B.TriangleIndex;
    }

    void PushOrientationQueueNode(
        TArray<FOrientationQueueNode>& Heap,
        const FOrientationQueueNode& Node)
    {
        int32 ChildIndex = Heap.Add(Node);
        while (ChildIndex > 0)
        {
            const int32 ParentIndex = (ChildIndex - 1) / 2;
            if (!IsOrientationQueueNodeEarlier(Heap[ChildIndex], Heap[ParentIndex]))
            {
                break;
            }
            Swap(Heap[ChildIndex], Heap[ParentIndex]);
            ChildIndex = ParentIndex;
        }
    }

    bool PopOrientationQueueNode(
        TArray<FOrientationQueueNode>& Heap,
        FOrientationQueueNode& OutNode)
    {
        if (Heap.IsEmpty())
        {
            return false;
        }
        OutNode = Heap[0];
        if (Heap.Num() == 1)
        {
            Heap.Pop(EAllowShrinking::No);
            return true;
        }

        Heap[0] = Heap.Pop(EAllowShrinking::No);
        int32 ParentIndex = 0;
        for (;;)
        {
            const int32 LeftIndex = ParentIndex * 2 + 1;
            if (!Heap.IsValidIndex(LeftIndex))
            {
                break;
            }
            const int32 RightIndex = LeftIndex + 1;
            int32 EarlierChildIndex = LeftIndex;
            if (Heap.IsValidIndex(RightIndex) &&
                IsOrientationQueueNodeEarlier(Heap[RightIndex], Heap[LeftIndex]))
            {
                EarlierChildIndex = RightIndex;
            }
            if (!IsOrientationQueueNodeEarlier(Heap[EarlierChildIndex], Heap[ParentIndex]))
            {
                break;
            }
            Swap(Heap[EarlierChildIndex], Heap[ParentIndex]);
            ParentIndex = EarlierChildIndex;
        }
        return true;
    }

    bool IsBetterOrientationSolution(
        const FOrientationQueueNode& Candidate,
        const FOrientationCandidateState& Existing)
    {
        if (!Existing.bSolved || Candidate.Distance < Existing.Distance - UE_KINDA_SMALL_NUMBER)
        {
            return true;
        }
        if (!FMath::IsNearlyEqual(Candidate.Distance, Existing.Distance, UE_KINDA_SMALL_NUMBER))
        {
            return false;
        }
        if (Candidate.SeedTriangleID != Existing.SeedTriangleID)
        {
            return Candidate.SeedTriangleID < Existing.SeedTriangleID;
        }
        if (Candidate.ParentTriangleID != Existing.ParentTriangleID)
        {
            return Candidate.ParentTriangleID < Existing.ParentTriangleID;
        }
        return Candidate.ParentEdgeIndex < Existing.ParentEdgeIndex;
    }

    bool MatchesOrientationSolution(
        const FOrientationQueueNode& Node,
        const FOrientationCandidateState& State)
    {
        return State.bSolved &&
            FMath::IsNearlyEqual(Node.Distance, State.Distance, UE_KINDA_SMALL_NUMBER) &&
            Node.SeedTriangleID == State.SeedTriangleID &&
            Node.ParentTriangleID == State.ParentTriangleID &&
            Node.ParentEdgeIndex == State.ParentEdgeIndex;
    }

    void AssignOrientationSolution(
        const FOrientationQueueNode& Node,
        FOrientationCandidateState& State)
    {
        State.bSolved = true;
        State.Distance = Node.Distance;
        State.SeedTriangleID = Node.SeedTriangleID;
        State.ParentTriangleID = Node.ParentTriangleID;
        State.ParentEdgeIndex = Node.ParentEdgeIndex;
        State.DirectionV = Node.DirectionV;
    }

    float AngleDegreesBetweenOrientationDirections(
        const FVector3f& A,
        const FVector3f& B)
    {
        return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
            FVector3f::DotProduct(A.GetSafeNormal(), B.GetSafeNormal()),
            -1.0f,
            1.0f)));
    }
}

bool FDWCEditorSurfaceOrientationFieldBuilder::Build(
    const TConstArrayView<FDWCEditorSpatialTriangle> Triangles,
    const FDWCEditorSurfaceOrientationPolicy& Policy,
    FDWCEditorSurfaceOrientationField& OutField,
    FString* OutWarning)
{
    TRACE_CPUPROFILER_EVENT_SCOPE(FDWCEditorSurfaceOrientationFieldBuilder_Build);
    if (OutWarning != nullptr)
    {
        OutWarning->Reset();
    }

    FDWCEditorSurfaceOrientationField BuiltField;
    if (!Policy.IsValid() || Policy.BuildSignature() == 0)
    {
        if (OutWarning != nullptr)
        {
            *OutWarning = TEXT("The surface orientation policy is invalid.");
        }
        OutField.Reset();
        return false;
    }

    BuiltField.BuildStatus = EDWCEditorSurfaceOrientationFieldBuildStatus::Ready;
    BuiltField.PolicySignature = Policy.BuildSignature();
    BuiltField.FieldLayoutVersion = DWCEditorSurfaceOrientationVersion::FieldLayout;
    if (Triangles.IsEmpty())
    {
        OutField = MoveTemp(BuiltField);
        return true;
    }

    bool bDegraded = false;
    TArray<FVector3f> TriangleNormals;
    TriangleNormals.Reserve(Triangles.Num());
    TArray<FOrientationCandidateState> States;
    States.SetNum(Triangles.Num());
    for (int32 TriangleIndex = 0; TriangleIndex < Triangles.Num(); ++TriangleIndex)
    {
        const FVector3f Normal = GetOrientationTriangleNormal(Triangles[TriangleIndex], bDegraded);
        TriangleNormals.Add(Normal);
        const float Quality = FVector3f::CrossProduct(Policy.PrimaryAxis, Normal).Size();
        if (Quality >= Policy.FallbackBeginQuality)
        {
            States[TriangleIndex].Classification = EOrientationTriangleClass::Stable;
            ++BuiltField.Diagnostics.StableTriangleCount;
        }
        else if (Quality <= Policy.FallbackFullQuality)
        {
            States[TriangleIndex].Classification = EOrientationTriangleClass::Fallback;
            ++BuiltField.Diagnostics.FallbackTriangleCount;
        }
        else
        {
            States[TriangleIndex].Classification = EOrientationTriangleClass::Blend;
            ++BuiltField.Diagnostics.BlendTriangleCount;
        }
    }

    TArray<FOrientationComponent> Components;
    TArray<int32> PendingTriangles;
    for (int32 StartTriangleIndex = 0; StartTriangleIndex < Triangles.Num(); ++StartTriangleIndex)
    {
        if (States[StartTriangleIndex].Classification == EOrientationTriangleClass::Stable ||
            States[StartTriangleIndex].ComponentIndex != INDEX_NONE)
        {
            continue;
        }

        const int32 ComponentIndex = Components.AddDefaulted();
        PendingTriangles.Reset();
        PendingTriangles.Add(StartTriangleIndex);
        States[StartTriangleIndex].ComponentIndex = ComponentIndex;
        while (!PendingTriangles.IsEmpty())
        {
            const int32 TriangleIndex = PendingTriangles.Pop(EAllowShrinking::No);
            Components[ComponentIndex].TriangleIndices.Add(TriangleIndex);
            const FDWCEditorSpatialTriangle& Triangle = Triangles[TriangleIndex];
            for (int32 EdgeIndex = 0; EdgeIndex < 3; ++EdgeIndex)
            {
                if (!IsTraversableOrientationEdge(Triangle.EdgeTypes[EdgeIndex]))
                {
                    continue;
                }
                const int32 AdjacentIndex = Triangle.AdjacentTriangleIndices[EdgeIndex];
                if (!Triangles.IsValidIndex(AdjacentIndex))
                {
                    bDegraded = true;
                    continue;
                }
                if (States[AdjacentIndex].Classification == EOrientationTriangleClass::Stable ||
                    States[AdjacentIndex].ComponentIndex != INDEX_NONE)
                {
                    continue;
                }
                States[AdjacentIndex].ComponentIndex = ComponentIndex;
                PendingTriangles.Add(AdjacentIndex);
            }
        }
        Components[ComponentIndex].TriangleIndices.Sort([&Triangles](const int32 A, const int32 B)
        {
            if (Triangles[A].TriangleID != Triangles[B].TriangleID)
            {
                return Triangles[A].TriangleID < Triangles[B].TriangleID;
            }
            return A < B;
        });
    }

    TArray<FOrientationQueueNode> Queue;
    for (int32 ComponentIndex = 0; ComponentIndex < Components.Num(); ++ComponentIndex)
    {
        FOrientationComponent& Component = Components[ComponentIndex];
        Queue.Reset();
        for (const int32 TriangleIndex : Component.TriangleIndices)
        {
            const FDWCEditorSpatialTriangle& Triangle = Triangles[TriangleIndex];
            for (int32 EdgeIndex = 0; EdgeIndex < 3; ++EdgeIndex)
            {
                if (!IsTraversableOrientationEdge(Triangle.EdgeTypes[EdgeIndex]))
                {
                    continue;
                }
                const int32 AdjacentIndex = Triangle.AdjacentTriangleIndices[EdgeIndex];
                if (!Triangles.IsValidIndex(AdjacentIndex) ||
                    States[AdjacentIndex].Classification != EOrientationTriangleClass::Stable)
                {
                    continue;
                }

                Component.bHasStableSeed = true;
                const FVector3f SeedDirection = BuildOrientationDirection(
                    Policy.PrimaryAxis,
                    TriangleNormals[AdjacentIndex]);
                FOrientationQueueNode Node;
                Node.Distance = GetOrientationEdgeDistance(
                    Triangles[AdjacentIndex],
                    Triangle);
                Node.SeedTriangleID = Triangles[AdjacentIndex].TriangleID;
                Node.ParentTriangleID = Triangles[AdjacentIndex].TriangleID;
                Node.ParentEdgeIndex = EdgeIndex;
                Node.TriangleIndex = TriangleIndex;
                Node.DirectionV = TransportOrientationDirection(
                    SeedDirection,
                    TriangleNormals[AdjacentIndex],
                    TriangleNormals[TriangleIndex]);
                if (!Node.DirectionV.IsNearlyZero() &&
                    IsBetterOrientationSolution(Node, States[TriangleIndex]))
                {
                    AssignOrientationSolution(Node, States[TriangleIndex]);
                    PushOrientationQueueNode(Queue, Node);
                }
            }
        }

        if (!Component.bHasStableSeed)
        {
            ++BuiltField.Diagnostics.FullyDegenerateComponentCount;
            const int32 RootTriangleIndex = Component.TriangleIndices[0];
            FOrientationQueueNode RootNode;
            RootNode.SeedTriangleID = Triangles[RootTriangleIndex].TriangleID;
            RootNode.ParentTriangleID = Triangles[RootTriangleIndex].TriangleID;
            RootNode.ParentEdgeIndex = INDEX_NONE;
            RootNode.TriangleIndex = RootTriangleIndex;
            RootNode.DirectionV = BuildOrientationDirection(
                Policy.SecondaryAxis,
                TriangleNormals[RootTriangleIndex]);
            AssignOrientationSolution(RootNode, States[RootTriangleIndex]);
            PushOrientationQueueNode(Queue, RootNode);
        }

        FOrientationQueueNode CurrentNode;
        while (PopOrientationQueueNode(Queue, CurrentNode))
        {
            if (!States.IsValidIndex(CurrentNode.TriangleIndex) ||
                !MatchesOrientationSolution(CurrentNode, States[CurrentNode.TriangleIndex]))
            {
                continue;
            }
            const FDWCEditorSpatialTriangle& Triangle = Triangles[CurrentNode.TriangleIndex];
            for (int32 EdgeIndex = 0; EdgeIndex < 3; ++EdgeIndex)
            {
                if (!IsTraversableOrientationEdge(Triangle.EdgeTypes[EdgeIndex]))
                {
                    continue;
                }
                const int32 AdjacentIndex = Triangle.AdjacentTriangleIndices[EdgeIndex];
                if (!Triangles.IsValidIndex(AdjacentIndex) ||
                    States[AdjacentIndex].ComponentIndex != ComponentIndex)
                {
                    continue;
                }

                FOrientationQueueNode NextNode;
                NextNode.Distance = CurrentNode.Distance + GetOrientationEdgeDistance(
                    Triangle,
                    Triangles[AdjacentIndex]);
                NextNode.SeedTriangleID = CurrentNode.SeedTriangleID;
                NextNode.ParentTriangleID = Triangle.TriangleID;
                NextNode.ParentEdgeIndex = EdgeIndex;
                NextNode.TriangleIndex = AdjacentIndex;
                NextNode.DirectionV = TransportOrientationDirection(
                    CurrentNode.DirectionV,
                    TriangleNormals[CurrentNode.TriangleIndex],
                    TriangleNormals[AdjacentIndex]);
                if (!NextNode.DirectionV.IsNearlyZero() &&
                    IsBetterOrientationSolution(NextNode, States[AdjacentIndex]))
                {
                    AssignOrientationSolution(NextNode, States[AdjacentIndex]);
                    PushOrientationQueueNode(Queue, NextNode);
                }
            }
        }

        for (const int32 TriangleIndex : Component.TriangleIndices)
        {
            if (!States[TriangleIndex].bSolved || States[TriangleIndex].DirectionV.IsNearlyZero())
            {
                States[TriangleIndex].DirectionV = BuildOrientationDirection(
                    Policy.SecondaryAxis,
                    TriangleNormals[TriangleIndex]);
                States[TriangleIndex].bSolved = !States[TriangleIndex].DirectionV.IsNearlyZero();
                bDegraded = true;
            }
        }
    }

    TMap<FOrientationCornerKey, FOrientationCornerAccumulator> CornerAccumulators;
    CornerAccumulators.Reserve(
        BuiltField.Diagnostics.BlendTriangleCount * 3 +
        BuiltField.Diagnostics.FallbackTriangleCount * 3);
    for (int32 TriangleIndex = 0; TriangleIndex < Triangles.Num(); ++TriangleIndex)
    {
        if (States[TriangleIndex].Classification == EOrientationTriangleClass::Stable)
        {
            continue;
        }
        const FDWCEditorSpatialTriangle& Triangle = Triangles[TriangleIndex];
        for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
        {
            FVector3f CornerNormal = Triangle.LocalNormals[CornerIndex].GetSafeNormal(
                UE_SMALL_NUMBER,
                TriangleNormals[TriangleIndex]);
            FVector3f CornerDirection = TransportOrientationDirection(
                States[TriangleIndex].DirectionV,
                TriangleNormals[TriangleIndex],
                CornerNormal);
            if (CornerDirection.IsNearlyZero())
            {
                CornerDirection = BuildOrientationDirection(Policy.SecondaryAxis, CornerNormal);
                bDegraded = true;
            }

            int64 TopologyVertexID = Triangle.TopologyVertexIDs[CornerIndex];
            if (TopologyVertexID == INDEX_NONE)
            {
                TopologyVertexID = -2 - static_cast<int64>(TriangleIndex) * 3 - CornerIndex;
                bDegraded = true;
            }
            FOrientationCornerKey Key;
            Key.ComponentIndex = States[TriangleIndex].ComponentIndex;
            Key.TopologyVertexID = TopologyVertexID;
            FOrientationCornerAccumulator& Accumulator = CornerAccumulators.FindOrAdd(Key);
            if (Accumulator.Count == 0)
            {
                Accumulator.Reference = CornerDirection;
            }
            else if (FVector3f::DotProduct(CornerDirection, Accumulator.Reference) < 0.0f)
            {
                CornerDirection *= -1.0f;
            }
            Accumulator.Sum += CornerDirection;
            ++Accumulator.Count;
        }
    }

    BuiltField.EntryIndexByTriangle.Init(INDEX_NONE, Triangles.Num());
    BuiltField.Entries.Reserve(
        BuiltField.Diagnostics.BlendTriangleCount + BuiltField.Diagnostics.FallbackTriangleCount);
    for (int32 TriangleIndex = 0; TriangleIndex < Triangles.Num(); ++TriangleIndex)
    {
        if (States[TriangleIndex].Classification == EOrientationTriangleClass::Stable)
        {
            continue;
        }
        const FDWCEditorSpatialTriangle& Triangle = Triangles[TriangleIndex];
        FDWCEditorSurfaceOrientationFieldEntry& Entry = BuiltField.Entries.AddDefaulted_GetRef();
        Entry.TriangleIndex = TriangleIndex;
        BuiltField.EntryIndexByTriangle[TriangleIndex] = BuiltField.Entries.Num() - 1;
        for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
        {
            int64 TopologyVertexID = Triangle.TopologyVertexIDs[CornerIndex];
            if (TopologyVertexID == INDEX_NONE)
            {
                TopologyVertexID = -2 - static_cast<int64>(TriangleIndex) * 3 - CornerIndex;
            }
            const FOrientationCornerKey Key{
                States[TriangleIndex].ComponentIndex,
                TopologyVertexID
            };
            const FOrientationCornerAccumulator* Accumulator = CornerAccumulators.Find(Key);
            FVector3f Direction = Accumulator != nullptr
                ? Accumulator->Sum.GetSafeNormal()
                : FVector3f::ZeroVector;
            const FVector3f CornerNormal = Triangle.LocalNormals[CornerIndex].GetSafeNormal(
                UE_SMALL_NUMBER,
                TriangleNormals[TriangleIndex]);
            Direction = ProjectOrientationDirection(Direction, CornerNormal);
            if (Direction.IsNearlyZero())
            {
                Direction = BuildOrientationDirection(Policy.SecondaryAxis, CornerNormal);
                bDegraded = true;
            }
            Entry.CornerFallbackV[CornerIndex] = FPackedNormal(Direction);
        }
    }

    for (int32 TriangleIndex = 0; TriangleIndex < Triangles.Num(); ++TriangleIndex)
    {
        if (States[TriangleIndex].Classification == EOrientationTriangleClass::Stable)
        {
            continue;
        }
        const FDWCEditorSpatialTriangle& Triangle = Triangles[TriangleIndex];
        for (int32 EdgeIndex = 0; EdgeIndex < 3; ++EdgeIndex)
        {
            const int32 AdjacentIndex = Triangle.AdjacentTriangleIndices[EdgeIndex];
            if (Triangle.EdgeTypes[EdgeIndex] == EDWCEditorSpatialEdgeType::UVSeam &&
                Triangles.IsValidIndex(AdjacentIndex) &&
                (States[AdjacentIndex].Classification == EOrientationTriangleClass::Stable ||
                 TriangleIndex < AdjacentIndex))
            {
                ++BuiltField.Diagnostics.CrossedUVSeamEdgeCount;
            }
            if (!Triangles.IsValidIndex(AdjacentIndex) || TriangleIndex >= AdjacentIndex ||
                States[AdjacentIndex].Classification == EOrientationTriangleClass::Stable ||
                States[AdjacentIndex].ComponentIndex != States[TriangleIndex].ComponentIndex)
            {
                continue;
            }
            const FVector3f TransportedDirection = TransportOrientationDirection(
                States[TriangleIndex].DirectionV,
                TriangleNormals[TriangleIndex],
                TriangleNormals[AdjacentIndex]);
            if (!TransportedDirection.IsNearlyZero())
            {
                BuiltField.Diagnostics.MaxAdjacentDirectionAngleDegrees = FMath::Max(
                    BuiltField.Diagnostics.MaxAdjacentDirectionAngleDegrees,
                    AngleDegreesBetweenOrientationDirections(
                        TransportedDirection,
                        States[AdjacentIndex].DirectionV));
            }
        }
    }

    if (BuiltField.Entries.IsEmpty())
    {
        BuiltField.EntryIndexByTriangle.Empty();
    }
    BuiltField.Diagnostics.FallbackComponentCount = Components.Num();
    BuiltField.BuildStatus = bDegraded
        ? EDWCEditorSurfaceOrientationFieldBuildStatus::Degraded
        : EDWCEditorSurfaceOrientationFieldBuildStatus::Ready;

    FString ContractError;
    if (!BuiltField.ValidateContract(Triangles.Num(), &ContractError))
    {
        if (OutWarning != nullptr)
        {
            *OutWarning = FString::Printf(
                TEXT("The surface orientation field contract is invalid: %s"),
                *ContractError);
        }
        OutField.Reset();
        return false;
    }
    if (bDegraded && OutWarning != nullptr)
    {
        *OutWarning = TEXT(
            "The surface orientation field used deterministic fallbacks for malformed topology or normals.");
    }
    OutField = MoveTemp(BuiltField);
    return true;
}
