#include "DWCDataUVGenerator.h"

#include "DWCDataUVChartBuilder.h"
#include "DWCDataUVGenerationTypes.h"
#include "DWCDataUVPacker.h"
#include "DWCDataUVSeamSplitter.h"
#include "DWCDataUVValidator.h"

#include "Algo/Sort.h"
#include "Engine/SkeletalMesh.h"
#include "HAL/PlatformTime.h"
#include "MeshDescription.h"
#include "RenderResource.h"
#include "RenderingThread.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "SkeletalMeshAttributes.h"
#include "WetClothing/Foundation/UV/DWCUVGeometry.h"

namespace DWCDataUVGeneratorInternal
{
    // Data UV padding is authored in texels at the minimum/reference data-texture resolution,
    // then converted once to normalized UV space for the resolution-independent packer.
    static constexpr int32 DataUVReferenceResolution = 256;
    static constexpr int32 ChartPaddingTexels = 2;
    static constexpr int32 BorderPaddingTexels = 2;
    static constexpr double ChartPaddingUV =
        static_cast<double>(ChartPaddingTexels) / static_cast<double>(DataUVReferenceResolution);
    static constexpr double BorderPaddingUV =
        static_cast<double>(BorderPaddingTexels) / static_cast<double>(DataUVReferenceResolution);
    static constexpr double TransferDegenerateTriangleAreaTolerance = 1.0e-10;
    static constexpr double VisibleExclusionNoteRatioThreshold = 0.0001; // 0.01%
    static constexpr double VisibleExclusionFailureRatioThreshold = 0.005; // 0.5%
    static constexpr double ConnectedVisibleExclusionFailureRatioThreshold = 0.0025; // 0.25%

    struct FExcludedVisibleTriangle
    {
        int32 MaterialSlotIndex = INDEX_NONE;
        int32 GeneratorTriangleIndex = INDEX_NONE;
        int32 MeshTriangleID = INDEX_NONE;
        FVertexID Vertices[3];
        double SurfaceArea = 0.0;
        bool bPackedDegenerate = false;
    };

    struct FMeshEdgeKey
    {
        int32 A = INDEX_NONE;
        int32 B = INDEX_NONE;

        friend bool operator==(const FMeshEdgeKey& Left, const FMeshEdgeKey& Right)
        {
            return Left.A == Right.A && Left.B == Right.B;
        }

        friend uint32 GetTypeHash(const FMeshEdgeKey& Key)
        {
            return HashCombine(::GetTypeHash(Key.A), ::GetTypeHash(Key.B));
        }
    };

    static FMeshEdgeKey MakeMeshEdgeKey(const FVertexID A, const FVertexID B)
    {
        const int32 ValueA = A.GetValue();
        const int32 ValueB = B.GetValue();
        return ValueA <= ValueB
            ? FMeshEdgeKey{ValueA, ValueB}
            : FMeshEdgeKey{ValueB, ValueA};
    }

    static double ComputeTriangleSurfaceArea3D(const FDWCDataUVTriangle& Triangle)
    {
        return 0.5 * FDWCUVGeometry::ComputeTriangleDoubleArea3D(
            Triangle.Positions[0], Triangle.Positions[1], Triangle.Positions[2]);
    }

    static double ComputeLargestConnectedExcludedArea(
        const TArray<FExcludedVisibleTriangle>& ExcludedTriangles,
        const int32 MaterialSlotIndex)
    {
        TArray<int32> LocalIndices;
        for (int32 Index = 0; Index < ExcludedTriangles.Num(); ++Index)
        {
            if (ExcludedTriangles[Index].MaterialSlotIndex == MaterialSlotIndex)
            {
                LocalIndices.Add(Index);
            }
        }
        if (LocalIndices.IsEmpty())
        {
            return 0.0;
        }

        TArray<int32> Parent;
        Parent.SetNumUninitialized(LocalIndices.Num());
        for (int32 LocalIndex = 0; LocalIndex < Parent.Num(); ++LocalIndex)
        {
            Parent[LocalIndex] = LocalIndex;
        }

        auto FindRoot = [&Parent](int32 Index)
        {
            int32 Root = Index;
            while (Parent[Root] != Root)
            {
                Root = Parent[Root];
            }
            while (Parent[Index] != Index)
            {
                const int32 Next = Parent[Index];
                Parent[Index] = Root;
                Index = Next;
            }
            return Root;
        };

        auto Union = [&Parent, &FindRoot](const int32 A, const int32 B)
        {
            const int32 RootA = FindRoot(A);
            const int32 RootB = FindRoot(B);
            if (RootA != RootB)
            {
                Parent[RootB] = RootA;
            }
        };

        TMap<FMeshEdgeKey, int32> FirstTriangleByEdge;
        for (int32 LocalIndex = 0; LocalIndex < LocalIndices.Num(); ++LocalIndex)
        {
            const FExcludedVisibleTriangle& Triangle = ExcludedTriangles[LocalIndices[LocalIndex]];
            for (int32 EdgeIndex = 0; EdgeIndex < 3; ++EdgeIndex)
            {
                const FMeshEdgeKey Edge = MakeMeshEdgeKey(
                    Triangle.Vertices[EdgeIndex],
                    Triangle.Vertices[(EdgeIndex + 1) % 3]);
                if (const int32* ExistingLocalIndex = FirstTriangleByEdge.Find(Edge))
                {
                    Union(LocalIndex, *ExistingLocalIndex);
                }
                else
                {
                    FirstTriangleByEdge.Add(Edge, LocalIndex);
                }
            }
        }

        TMap<int32, double> AreaByRoot;
        for (int32 LocalIndex = 0; LocalIndex < LocalIndices.Num(); ++LocalIndex)
        {
            AreaByRoot.FindOrAdd(FindRoot(LocalIndex)) +=
                ExcludedTriangles[LocalIndices[LocalIndex]].SurfaceArea;
        }

        double LargestArea = 0.0;
        for (const TPair<int32, double>& Pair : AreaByRoot)
        {
            LargestArea = FMath::Max(LargestArea, Pair.Value);
        }
        return LargestArea;
    }

    static void SetFailure(FDWCDataUVGenerationResult& Result, const FString& Message)
    {
        Result.bSucceeded = false;
        Result.Message = Message;
    }

    static FDWCDataUVSlotWarning& FindOrAddSlotWarning(
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

    template <typename ElementIDType>
    static bool IsValidElementID(ElementIDType ElementID)
    {
        return ElementID.GetValue() != INDEX_NONE;
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

    struct FDataUVTransferSourceTriangle
    {
        int32 MaterialSlotIndex = INDEX_NONE;
        FVector Positions[3];
        FVector2f DataUVs[3];
        FBox Bounds = FBox(ForceInit);
        FVector Centroid = FVector::ZeroVector;
    };

    struct FDataUVTransferBVHNode
    {
        FBox Bounds = FBox(ForceInit);
        int32 LeftChildIndex = INDEX_NONE;
        int32 RightChildIndex = INDEX_NONE;
        int32 FirstTriangleIndex = 0;
        int32 TriangleCount = 0;

        bool IsLeaf() const
        {
            return LeftChildIndex == INDEX_NONE;
        }
    };

    struct FDataUVTransferBVH
    {
        TArray<int32> TriangleIndices;
        TArray<FDataUVTransferBVHNode> Nodes;
    };

    static bool ComputeBarycentric3D(
        const FVector& Point,
        const FVector& A,
        const FVector& B,
        const FVector& C,
        FVector3d& OutBarycentric)
    {
        const FVector V0 = B - A;
        const FVector V1 = C - A;
        const FVector V2 = Point - A;
        const double D00 = FVector::DotProduct(V0, V0);
        const double D01 = FVector::DotProduct(V0, V1);
        const double D11 = FVector::DotProduct(V1, V1);
        const double D20 = FVector::DotProduct(V2, V0);
        const double D21 = FVector::DotProduct(V2, V1);
        const double Denominator = D00 * D11 - D01 * D01;
        if (FMath::Abs(Denominator) <= SMALL_NUMBER)
        {
            return false;
        }

        const double WeightB = (D11 * D20 - D01 * D21) / Denominator;
        const double WeightC = (D00 * D21 - D01 * D20) / Denominator;
        const double WeightA = 1.0 - WeightB - WeightC;
        OutBarycentric = FVector3d(
            FMath::Clamp(WeightA, 0.0, 1.0),
            FMath::Clamp(WeightB, 0.0, 1.0),
            FMath::Clamp(WeightC, 0.0, 1.0));

        const double Sum = OutBarycentric.X + OutBarycentric.Y + OutBarycentric.Z;
        if (Sum > SMALL_NUMBER)
        {
            OutBarycentric /= Sum;
        }
        return true;
    }

    static double GetBoxSurfaceArea(const FBox& Bounds)
    {
        if (!Bounds.IsValid)
        {
            return 0.0;
        }

        const FVector Extent = Bounds.GetSize();
        return 2.0 * (
            Extent.X * Extent.Y +
            Extent.Y * Extent.Z +
            Extent.Z * Extent.X);
    }

    static double GetSquaredDistanceToBox(const FVector& Point, const FBox& Bounds)
    {
        if (!Bounds.IsValid)
        {
            return TNumericLimits<double>::Max();
        }

        double DistanceSquared = 0.0;
        for (int32 Axis = 0; Axis < 3; ++Axis)
        {
            const double Value = Point[Axis];
            if (Value < Bounds.Min[Axis])
            {
                DistanceSquared += FMath::Square(Bounds.Min[Axis] - Value);
            }
            else if (Value > Bounds.Max[Axis])
            {
                DistanceSquared += FMath::Square(Value - Bounds.Max[Axis]);
            }
        }
        return DistanceSquared;
    }

    static int32 GetLongestAxis(const FBox& Bounds)
    {
        const FVector Size = Bounds.GetSize();
        if (Size.Y > Size.X && Size.Y >= Size.Z)
        {
            return 1;
        }
        return Size.Z > Size.X && Size.Z > Size.Y ? 2 : 0;
    }

    static int32 BuildTransferBVHRecursive(
        const TArray<FDataUVTransferSourceTriangle>& Triangles,
        FDataUVTransferBVH& BVH,
        const int32 FirstTriangleIndex,
        const int32 TriangleCount)
    {
        static constexpr int32 LeafTriangleCount = 8;
        static constexpr int32 BinCount = 16;

        FDataUVTransferBVHNode Node;
        Node.FirstTriangleIndex = FirstTriangleIndex;
        Node.TriangleCount = TriangleCount;
        FBox CentroidBounds(ForceInit);
        for (int32 Offset = 0; Offset < TriangleCount; ++Offset)
        {
            const FDataUVTransferSourceTriangle& Triangle = Triangles[BVH.TriangleIndices[FirstTriangleIndex + Offset]];
            Node.Bounds += Triangle.Bounds;
            CentroidBounds += Triangle.Centroid;
        }

        const int32 NodeIndex = BVH.Nodes.Add(Node);
        if (TriangleCount <= LeafTriangleCount || !CentroidBounds.IsValid)
        {
            return NodeIndex;
        }

        int32 BestAxis = INDEX_NONE;
        int32 BestSplitBin = INDEX_NONE;
        double BestCost = TNumericLimits<double>::Max();
        const double ParentArea = FMath::Max(GetBoxSurfaceArea(Node.Bounds), SMALL_NUMBER);

        for (int32 Axis = 0; Axis < 3; ++Axis)
        {
            const double AxisMin = CentroidBounds.Min[Axis];
            const double AxisMax = CentroidBounds.Max[Axis];
            const double AxisExtent = AxisMax - AxisMin;
            if (AxisExtent <= KINDA_SMALL_NUMBER)
            {
                continue;
            }

            FBox BinBounds[BinCount];
            int32 BinTriangleCounts[BinCount] = {};
            for (int32 BinIndex = 0; BinIndex < BinCount; ++BinIndex)
            {
                BinBounds[BinIndex] = FBox(ForceInit);
            }

            for (int32 Offset = 0; Offset < TriangleCount; ++Offset)
            {
                const FDataUVTransferSourceTriangle& Triangle = Triangles[BVH.TriangleIndices[FirstTriangleIndex + Offset]];
                const int32 BinIndex = FMath::Clamp(
                    FMath::FloorToInt(((Triangle.Centroid[Axis] - AxisMin) / AxisExtent) * static_cast<double>(BinCount)),
                    0,
                    BinCount - 1);
                BinBounds[BinIndex] += Triangle.Bounds;
                ++BinTriangleCounts[BinIndex];
            }

            FBox LeftBounds[BinCount - 1];
            FBox RightBounds[BinCount - 1];
            int32 LeftCounts[BinCount - 1] = {};
            int32 RightCounts[BinCount - 1] = {};
            FBox RunningBounds(ForceInit);
            int32 RunningCount = 0;
            for (int32 BinIndex = 0; BinIndex < BinCount - 1; ++BinIndex)
            {
                RunningBounds += BinBounds[BinIndex];
                RunningCount += BinTriangleCounts[BinIndex];
                LeftBounds[BinIndex] = RunningBounds;
                LeftCounts[BinIndex] = RunningCount;
            }

            RunningBounds = FBox(ForceInit);
            RunningCount = 0;
            for (int32 BinIndex = BinCount - 1; BinIndex > 0; --BinIndex)
            {
                RunningBounds += BinBounds[BinIndex];
                RunningCount += BinTriangleCounts[BinIndex];
                RightBounds[BinIndex - 1] = RunningBounds;
                RightCounts[BinIndex - 1] = RunningCount;
            }

            for (int32 SplitBin = 0; SplitBin < BinCount - 1; ++SplitBin)
            {
                if (LeftCounts[SplitBin] <= 0 || RightCounts[SplitBin] <= 0)
                {
                    continue;
                }

                const double Cost =
                    (GetBoxSurfaceArea(LeftBounds[SplitBin]) / ParentArea) * static_cast<double>(LeftCounts[SplitBin]) +
                    (GetBoxSurfaceArea(RightBounds[SplitBin]) / ParentArea) * static_cast<double>(RightCounts[SplitBin]);
                if (Cost < BestCost)
                {
                    BestCost = Cost;
                    BestAxis = Axis;
                    BestSplitBin = SplitBin;
                }
            }
        }

        int32 SplitOffset = INDEX_NONE;
        if (BestAxis != INDEX_NONE)
        {
            const double AxisMin = CentroidBounds.Min[BestAxis];
            const double AxisExtent = CentroidBounds.Max[BestAxis] - AxisMin;
            int32 Left = FirstTriangleIndex;
            int32 Right = FirstTriangleIndex + TriangleCount - 1;
            while (Left <= Right)
            {
                const FDataUVTransferSourceTriangle& Triangle = Triangles[BVH.TriangleIndices[Left]];
                const int32 BinIndex = FMath::Clamp(
                    FMath::FloorToInt(((Triangle.Centroid[BestAxis] - AxisMin) / AxisExtent) * static_cast<double>(BinCount)),
                    0,
                    BinCount - 1);
                if (BinIndex <= BestSplitBin)
                {
                    ++Left;
                }
                else
                {
                    Swap(BVH.TriangleIndices[Left], BVH.TriangleIndices[Right]);
                    --Right;
                }
            }
            SplitOffset = Left - FirstTriangleIndex;
        }

        if (SplitOffset <= 0 || SplitOffset >= TriangleCount)
        {
            const int32 Axis = GetLongestAxis(CentroidBounds);
            Algo::Sort(
                MakeArrayView(BVH.TriangleIndices.GetData() + FirstTriangleIndex, TriangleCount),
                [&Triangles, Axis](const int32 A, const int32 B)
                {
                    return Triangles[A].Centroid[Axis] < Triangles[B].Centroid[Axis];
                });
            SplitOffset = TriangleCount / 2;
        }

        const int32 LeftChildIndex = BuildTransferBVHRecursive(Triangles, BVH, FirstTriangleIndex, SplitOffset);
        const int32 RightChildIndex = BuildTransferBVHRecursive(Triangles, BVH, FirstTriangleIndex + SplitOffset, TriangleCount - SplitOffset);
        BVH.Nodes[NodeIndex].LeftChildIndex = LeftChildIndex;
        BVH.Nodes[NodeIndex].RightChildIndex = RightChildIndex;
        BVH.Nodes[NodeIndex].TriangleCount = 0;
        return NodeIndex;
    }

    static FDataUVTransferBVH BuildTransferBVH(const TArray<FDataUVTransferSourceTriangle>& Triangles)
    {
        FDataUVTransferBVH BVH;
        BVH.TriangleIndices.Reserve(Triangles.Num());
        for (int32 TriangleIndex = 0; TriangleIndex < Triangles.Num(); ++TriangleIndex)
        {
            BVH.TriangleIndices.Add(TriangleIndex);
        }
        BVH.Nodes.Reserve(FMath::Max(1, Triangles.Num() * 2));
        if (!Triangles.IsEmpty())
        {
            BuildTransferBVHRecursive(Triangles, BVH, 0, Triangles.Num());
        }
        return BVH;
    }

    static bool EnsureRenderVertexBufferUVChannel(
        FStaticMeshVertexBuffer& VertexBuffer,
        const int32 RequiredUVChannelIndex)
    {
        if (RequiredUVChannelIndex < 0 || RequiredUVChannelIndex >= 8)
        {
            return false;
        }

        const uint32 RequiredNumTexCoords = static_cast<uint32>(RequiredUVChannelIndex + 1);
        if (VertexBuffer.GetNumTexCoords() >= RequiredNumTexCoords)
        {
            return true;
        }

        if (!VertexBuffer.GetAllowCPUAccess())
        {
            return false;
        }

        const int32 VertexCount = static_cast<int32>(VertexBuffer.GetNumVertices());
        const int32 OldNumTexCoords = static_cast<int32>(VertexBuffer.GetNumTexCoords());
        const bool bUseFullPrecisionUVs = VertexBuffer.GetUseFullPrecisionUVs();
        const bool bUseHighPrecisionTangentBasis = VertexBuffer.GetUseHighPrecisionTangentBasis();

        TArray<FVector3f> TangentX;
        TArray<FVector3f> TangentY;
        TArray<FVector3f> TangentZ;
        TArray<FVector2f> OldUVs;
        TangentX.SetNum(VertexCount);
        TangentY.SetNum(VertexCount);
        TangentZ.SetNum(VertexCount);
        OldUVs.SetNum(VertexCount * OldNumTexCoords);
        for (int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
        {
            const FVector4f SourceTangentX = VertexBuffer.VertexTangentX(VertexIndex);
            const FVector4f SourceTangentZ = VertexBuffer.VertexTangentZ(VertexIndex);
            TangentX[VertexIndex] = FVector3f(SourceTangentX.X, SourceTangentX.Y, SourceTangentX.Z);
            TangentY[VertexIndex] = VertexBuffer.VertexTangentY(VertexIndex);
            TangentZ[VertexIndex] = FVector3f(SourceTangentZ.X, SourceTangentZ.Y, SourceTangentZ.Z);
            for (int32 UVIndex = 0; UVIndex < OldNumTexCoords; ++UVIndex)
            {
                OldUVs[VertexIndex * OldNumTexCoords + UVIndex] = VertexBuffer.GetVertexUV(VertexIndex, UVIndex);
            }
        }

        BeginReleaseResource(&VertexBuffer);
        FlushRenderingCommands();
        VertexBuffer.CleanUp();
        VertexBuffer.SetUseFullPrecisionUVs(bUseFullPrecisionUVs);
        VertexBuffer.SetUseHighPrecisionTangentBasis(bUseHighPrecisionTangentBasis);
        VertexBuffer.Init(VertexCount, RequiredNumTexCoords, true);
        FlushRenderingCommands();
        for (int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
        {
            VertexBuffer.SetVertexTangents(VertexIndex, TangentX[VertexIndex], TangentY[VertexIndex], TangentZ[VertexIndex]);
            for (uint32 UVIndex = 0; UVIndex < RequiredNumTexCoords; ++UVIndex)
            {
                const FVector2f UV = static_cast<int32>(UVIndex) < OldNumTexCoords
                    ? OldUVs[VertexIndex * OldNumTexCoords + static_cast<int32>(UVIndex)]
                    : FVector2f(0.0f, 0.0f);
                VertexBuffer.SetVertexUV(VertexIndex, UVIndex, UV);
            }
        }
        BeginInitResource(&VertexBuffer);
        FlushRenderingCommands();
        return true;
    }

    static const FDataUVTransferSourceTriangle* FindClosestTransferSourceTriangle(
        const TArray<FDataUVTransferSourceTriangle>& SourceTriangles,
        const FDataUVTransferBVH& BVH,
        const FVector& TargetPosition,
        const int32 TargetMaterialSlotIndex,
        const bool bRequireMaterialMatch,
        FVector3d& OutBarycentric)
    {
        const FDataUVTransferSourceTriangle* BestTriangle = nullptr;
        FVector BestClosestPoint = FVector::ZeroVector;
        double BestDistanceSquared = TNumericLimits<double>::Max();

        TArray<int32, TInlineAllocator<64>> NodeStack;
        if (!BVH.Nodes.IsEmpty())
        {
            NodeStack.Push(0);
        }

        while (!NodeStack.IsEmpty())
        {
            const FDataUVTransferBVHNode& Node = BVH.Nodes[NodeStack.Pop(EAllowShrinking::No)];
            if (GetSquaredDistanceToBox(TargetPosition, Node.Bounds) > BestDistanceSquared)
            {
                continue;
            }

            if (Node.IsLeaf())
            {
                for (int32 Offset = 0; Offset < Node.TriangleCount; ++Offset)
                {
                    const FDataUVTransferSourceTriangle& SourceTriangle =
                        SourceTriangles[BVH.TriangleIndices[Node.FirstTriangleIndex + Offset]];
                    if (bRequireMaterialMatch && SourceTriangle.MaterialSlotIndex != TargetMaterialSlotIndex)
                    {
                        continue;
                    }

                    const FVector ClosestPoint = FMath::ClosestPointOnTriangleToPoint(
                        TargetPosition,
                        SourceTriangle.Positions[0],
                        SourceTriangle.Positions[1],
                        SourceTriangle.Positions[2]);
                    const double DistanceSquared = FVector::DistSquared(TargetPosition, ClosestPoint);
                    if (DistanceSquared < BestDistanceSquared)
                    {
                        BestDistanceSquared = DistanceSquared;
                        BestClosestPoint = ClosestPoint;
                        BestTriangle = &SourceTriangle;
                    }
                }
                continue;
            }

            const double LeftDistance = BVH.Nodes.IsValidIndex(Node.LeftChildIndex)
                ? GetSquaredDistanceToBox(TargetPosition, BVH.Nodes[Node.LeftChildIndex].Bounds)
                : TNumericLimits<double>::Max();
            const double RightDistance = BVH.Nodes.IsValidIndex(Node.RightChildIndex)
                ? GetSquaredDistanceToBox(TargetPosition, BVH.Nodes[Node.RightChildIndex].Bounds)
                : TNumericLimits<double>::Max();

            if (LeftDistance < RightDistance)
            {
                if (RightDistance <= BestDistanceSquared)
                {
                    NodeStack.Push(Node.RightChildIndex);
                }
                if (LeftDistance <= BestDistanceSquared)
                {
                    NodeStack.Push(Node.LeftChildIndex);
                }
            }
            else
            {
                if (LeftDistance <= BestDistanceSquared)
                {
                    NodeStack.Push(Node.LeftChildIndex);
                }
                if (RightDistance <= BestDistanceSquared)
                {
                    NodeStack.Push(Node.RightChildIndex);
                }
            }
        }

        return BestTriangle != nullptr &&
               ComputeBarycentric3D(
                   BestClosestPoint,
                   BestTriangle->Positions[0],
                   BestTriangle->Positions[1],
                   BestTriangle->Positions[2],
                   OutBarycentric)
            ? BestTriangle
            : nullptr;
    }


} // namespace DWCDataUVGeneratorInternal

FDWCDataUVGenerationResult FDWCDataUVGenerator::GenerateForSkeletalMesh(
    USkeletalMesh* SkeletalMesh,
    int32 LODIndex,
    int32 SourceUVChannelIndex,
    int32 PreferredUVChannelIndex,
    bool bAllowOverwriteExistingChannel,
    int32 TargetMaterialSlotIndex,
    const TSet<int32>* TargetMaterialSlotIndices)
{
    using namespace DWCDataUVGeneratorInternal;

    FDWCDataUVGenerationResult Result;
    const double GenerationStartTime = FPlatformTime::Seconds();

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

    FSkeletalMeshAttributes Attributes(*MeshDescription);
    Attributes.Register(true);

    auto VertexPositions = Attributes.GetVertexPositions();
    auto VertexInstanceUVs = Attributes.GetVertexInstanceUVs();

    const int32 ExistingUVChannelCount = VertexInstanceUVs.GetNumChannels();
    const int32 SafeSourceUVChannelIndex = FMath::Clamp(SourceUVChannelIndex, 0, 7);
    if (SafeSourceUVChannelIndex >= ExistingUVChannelCount)
    {
        SetFailure(Result, FString::Printf(
            TEXT("Source UV Channel %d does not exist. A DWC UV Channel needs an existing material UV channel to preserve material-slot UV islands."),
            SafeSourceUVChannelIndex));
        return Result;
    }

    const int32 SafePreferredUVChannelIndex = FMath::Clamp(PreferredUVChannelIndex, 0, 7);

    int32 NewUVChannelIndex = INDEX_NONE;
    bool bOverwritingExistingChannel = false;
    bool bAppendedBecausePreferredChannelWasOccupied = false;

    if (SafePreferredUVChannelIndex >= ExistingUVChannelCount)
    {
        // Reserve the channel index logically, but do not mutate MeshDescription until
        // chart packing and fixed-resolution texel validation have succeeded.
        NewUVChannelIndex = SafePreferredUVChannelIndex;
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
                TEXT("UV Channel %d already exists and is not marked as generated by DWC. The target mesh also already has 8 UV channels, so a new safe DWC UV Channel cannot be appended."),
                SafePreferredUVChannelIndex));
            return Result;
        }

        NewUVChannelIndex = ExistingUVChannelCount;
        bAppendedBecausePreferredChannelWasOccupied = true;
    }

    TArray<FDWCDataUVTriangle> Triangles;
    TMap<int32, TArray<int32>> SlotToTriangleIndices;
    TMap<int32, int32> MatchingTriangleCountBySlot;
    TMap<int32, int32> VisibleTriangleCountBySlot;
    TMap<int32, double> TotalValid3DSurfaceAreaBySlot;
    TMap<int32, int32> Degenerate3DTriangleCountBySlot;
    TMap<int32, int32> DegenerateUVTriangleCountBySlot;
    TMap<int32, int32> InvalidUVTriangleCountBySlot;
    TArray<FExcludedVisibleTriangle> ExcludedVisibleTriangles;
    TSet<int32> ExcludedTriangleIndices;
    TSet<int32> ExcludedVertexInstanceIDs;

    for (const FTriangleID TriangleID : MeshDescription->Triangles().GetElementIDs())
    {
        const int32 MaterialSlotIndex = ResolveMaterialSlotIndex(SkeletalMesh, *MeshDescription, Attributes, TriangleID);
        if (MaterialSlotIndex == INDEX_NONE)
        {
            continue;
        }

        if (TargetMaterialSlotIndices != nullptr)
        {
            if (!TargetMaterialSlotIndices->Contains(MaterialSlotIndex))
            {
                continue;
            }
        }
        else if (TargetMaterialSlotIndex != INDEX_NONE && MaterialSlotIndex != TargetMaterialSlotIndex)
        {
            continue;
        }

        ++MatchingTriangleCountBySlot.FindOrAdd(MaterialSlotIndex);

        const auto VertexInstances = MeshDescription->GetTriangleVertexInstances(TriangleID);
        if (VertexInstances.Num() < 3)
        {
            continue;
        }

        FDWCDataUVTriangle Triangle;
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

        const int32 TriangleArrayIndex = Triangles.Add(Triangle);
        const double TriangleSurfaceArea = ComputeTriangleSurfaceArea3D(Triangle);
        if (TriangleSurfaceArea <= 0.5e-10)
        {
            ++Result.Degenerate3DTriangleCount;
            ++Degenerate3DTriangleCountBySlot.FindOrAdd(MaterialSlotIndex);
            ExcludedTriangleIndices.Add(TriangleArrayIndex);
            for (const FVertexInstanceID VertexInstanceID : Triangle.VertexInstances)
            {
                ExcludedVertexInstanceIDs.Add(VertexInstanceID.GetValue());
            }
            continue;
        }
        TotalValid3DSurfaceAreaBySlot.FindOrAdd(MaterialSlotIndex) += TriangleSurfaceArea;
        ++VisibleTriangleCountBySlot.FindOrAdd(MaterialSlotIndex);

        const bool bSourceUVIsFinite =
            FDWCUVGeometry::IsFiniteReasonableUV(Triangle.SourceUVs[0]) &&
            FDWCUVGeometry::IsFiniteReasonableUV(Triangle.SourceUVs[1]) &&
            FDWCUVGeometry::IsFiniteReasonableUV(Triangle.SourceUVs[2]);
        if (!bSourceUVIsFinite)
        {
            ++Result.InvalidSourceUVTriangleCount;
            ++InvalidUVTriangleCountBySlot.FindOrAdd(MaterialSlotIndex);
            FExcludedVisibleTriangle& ExcludedTriangle = ExcludedVisibleTriangles.AddDefaulted_GetRef();
            ExcludedTriangle.MaterialSlotIndex = MaterialSlotIndex;
            ExcludedTriangle.GeneratorTriangleIndex = TriangleArrayIndex;
            ExcludedTriangle.MeshTriangleID = TriangleID.GetValue();
            ExcludedTriangle.SurfaceArea = TriangleSurfaceArea;
            ExcludedTriangleIndices.Add(TriangleArrayIndex);
            for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
            {
                ExcludedTriangle.Vertices[CornerIndex] = Triangle.Vertices[CornerIndex];
            }
            for (const FVertexInstanceID VertexInstanceID : Triangle.VertexInstances)
            {
                ExcludedVertexInstanceIDs.Add(VertexInstanceID.GetValue());
            }
            continue;
        }

        // Degenerate UV triangles are filtered before connectivity/overlap analysis.
        // Point/line UV triangles would otherwise create false conflicts and cannot be rasterized.
        if (FDWCUVGeometry::ComputeTriangleArea2D(
                Triangle.SourceUVs[0], Triangle.SourceUVs[1], Triangle.SourceUVs[2]) <= 1.0e-12)
        {
            ++Result.DegenerateSourceUVTriangleCount;
            ++DegenerateUVTriangleCountBySlot.FindOrAdd(MaterialSlotIndex);
            FExcludedVisibleTriangle& ExcludedTriangle = ExcludedVisibleTriangles.AddDefaulted_GetRef();
            ExcludedTriangle.MaterialSlotIndex = MaterialSlotIndex;
            ExcludedTriangle.GeneratorTriangleIndex = TriangleArrayIndex;
            ExcludedTriangle.MeshTriangleID = TriangleID.GetValue();
            ExcludedTriangle.SurfaceArea = TriangleSurfaceArea;
            ExcludedTriangleIndices.Add(TriangleArrayIndex);
            for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
            {
                ExcludedTriangle.Vertices[CornerIndex] = Triangle.Vertices[CornerIndex];
            }
            for (const FVertexInstanceID VertexInstanceID : Triangle.VertexInstances)
            {
                ExcludedVertexInstanceIDs.Add(VertexInstanceID.GetValue());
            }
            continue;
        }

        SlotToTriangleIndices.FindOrAdd(MaterialSlotIndex).Add(TriangleArrayIndex);
    }

    for (const TPair<int32, int32>& Pair : Degenerate3DTriangleCountBySlot)
    {
        if (Pair.Value > 0)
        {
            FindOrAddSlotWarning(Result.SlotWarnings, Pair.Key).Degenerate3DTriangleCount += Pair.Value;
        }
    }
    for (const TPair<int32, int32>& Pair : DegenerateUVTriangleCountBySlot)
    {
        if (Pair.Value > 0)
        {
            FindOrAddSlotWarning(Result.SlotWarnings, Pair.Key).DegenerateSourceUVTriangleCount += Pair.Value;
        }
    }
    for (const TPair<int32, int32>& Pair : InvalidUVTriangleCountBySlot)
    {
        if (Pair.Value > 0)
        {
            FindOrAddSlotWarning(Result.SlotWarnings, Pair.Key).InvalidSourceUVTriangleCount += Pair.Value;
        }
    }

    if (SlotToTriangleIndices.IsEmpty())
    {
        if (TargetMaterialSlotIndices == nullptr && TargetMaterialSlotIndex != INDEX_NONE)
        {
            const int32 MatchingTriangleCount = MatchingTriangleCountBySlot.FindRef(TargetMaterialSlotIndex);
            const int32 VisibleTriangleCount = VisibleTriangleCountBySlot.FindRef(TargetMaterialSlotIndex);
            Result.MaterialSlotIndex = TargetMaterialSlotIndex;
            Result.UVChannelIndex = NewUVChannelIndex;

            if (MatchingTriangleCount == 0)
            {
                Result.bSucceeded = true;
                Result.bTargetSlotNotPresent = true;
                Result.Message = FString::Printf(
                    TEXT("This material slot is not used by LOD%d."),
                    LODIndex);
                return Result;
            }

            if (VisibleTriangleCount == 0)
            {
                Result.bSucceeded = true;
                Result.bTargetSlotNotPresent = true;
                Result.Message = FString::Printf(
                    TEXT("All triangles in this material slot have zero 3D surface area at LOD%d."),
                    LODIndex);
                return Result;
            }

            Result.FailedMaterialSlotIndices.Add(TargetMaterialSlotIndex);
            SetFailure(Result, TEXT("Visible triangles exist, but all source UV triangles are invalid or degenerate."));
            return Result;
        }

        if (TargetMaterialSlotIndices != nullptr)
        {
            for (const int32 MaterialSlotIndex : *TargetMaterialSlotIndices)
            {
                Result.FailedMaterialSlotIndices.Add(MaterialSlotIndex);
            }
            SetFailure(Result, TEXT("The selected Wettable material slots do not contain valid source UV triangles."));
        }
        else
        {
            SetFailure(Result, TEXT("The target mesh does not contain triangles that can be unwrapped."));
        }
        return Result;
    }
    const double TriangleReadEndTime = FPlatformTime::Seconds();

    TArray<FDWCDataUVChart> OriginalUVIslands;
    FDWCDataUVChartBuilder::BuildOriginalUVIslands(
        Triangles,
        SlotToTriangleIndices,
        OriginalUVIslands);
    const double OriginalIslandBuildEndTime = FPlatformTime::Seconds();

    if (OriginalUVIslands.Num() == 0)
    {
        if (TargetMaterialSlotIndices != nullptr)
        {
            for (const int32 MaterialSlotIndex : *TargetMaterialSlotIndices)
            {
                Result.FailedMaterialSlotIndices.Add(MaterialSlotIndex);
            }
        }
        else if (TargetMaterialSlotIndex != INDEX_NONE)
        {
            Result.FailedMaterialSlotIndices.Add(TargetMaterialSlotIndex);
        }
        SetFailure(Result, TEXT("No valid Original-UV islands could be generated after degenerate triangles were excluded."));
        return Result;
    }

    Result.OriginalUVIslandCount = OriginalUVIslands.Num();

    TArray<FDWCDataUVChart> DataUVCharts;
    FDWCDataUVChartBuildFailure ChartBuildFailure;
    const bool bChartsBuilt = FDWCDataUVChartBuilder::BuildNonOverlappingCharts(
        Triangles,
        OriginalUVIslands,
        DataUVCharts,
        Result.SplitOriginalUVIslandCount,
        Result.SelfOverlapPairCount,
        Result.SlotWarnings,
        &ChartBuildFailure);
    const double ChartBuildEndTime = FPlatformTime::Seconds();

    if (!bChartsBuilt)
    {
        if (ChartBuildFailure.bIsValid)
        {
            Result.FailedMaterialSlotIndices.Add(ChartBuildFailure.MaterialSlotIndex);
            SetFailure(Result, FString::Printf(
                TEXT("Material Slot %d contains a physical Source UV shell whose internal self-overlap analysis exceeds the supported limit after %lld exact triangle-pair tests within a shell containing %d triangle(s)."),
                ChartBuildFailure.MaterialSlotIndex,
                static_cast<long long>(ChartBuildFailure.TestedCandidatePairCount),
                ChartBuildFailure.SourceTriangleCount));
        }
        else
        {
            SetFailure(Result, TEXT("Source UV overlap analysis failed before non-overlapping DWC UV Channel charts could be generated."));
        }
        return Result;
    }

    if (DataUVCharts.Num() == 0)
    {
        if (TargetMaterialSlotIndices != nullptr)
        {
            for (const int32 MaterialSlotIndex : *TargetMaterialSlotIndices)
            {
                Result.FailedMaterialSlotIndices.Add(MaterialSlotIndex);
            }
        }
        else if (TargetMaterialSlotIndex != INDEX_NONE)
        {
            Result.FailedMaterialSlotIndices.Add(TargetMaterialSlotIndex);
        }
        SetFailure(Result, TEXT("Original-UV islands were found, but no non-overlapping DWC UV Channel charts could be generated."));
        return Result;
    }

    // Validate the final chart layout with synthetic per-corner IDs before changing
    // MeshDescription topology. This prevents a failed pack or overlap check from
    // leaving even transient chart-boundary VertexInstance splits in the edited mesh.
    TArray<FDWCDataUVTriangle> PackingTriangles = Triangles;
    for (int32 TriangleIndex = 0; TriangleIndex < PackingTriangles.Num(); ++TriangleIndex)
    {
        for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
        {
            PackingTriangles[TriangleIndex].VertexInstances[CornerIndex] =
                FVertexInstanceID(TriangleIndex * 3 + CornerIndex);
        }
    }

    TArray<FDWCDataUVChart> ValidatedCharts = DataUVCharts;
    TMap<int32, FVector2f> PackedUVBySyntheticCorner;
    int32 PackingFailedMaterialSlotIndex = INDEX_NONE;
    int32 PackingFailedChartCount = 0;
    if (!FDWCDataUVPacker::Pack(
            PackingTriangles,
            ValidatedCharts,
            ChartPaddingUV,
            BorderPaddingUV,
            PackedUVBySyntheticCorner,
            PackingFailedMaterialSlotIndex,
            &PackingFailedChartCount))
    {
        if (PackingFailedMaterialSlotIndex != INDEX_NONE)
        {
            Result.FailedMaterialSlotIndices.Add(PackingFailedMaterialSlotIndex);
        }
        SetFailure(Result, FString::Printf(
            TEXT("Material Slot %d generated %d DWC UV packing chart(s), which cannot be packed into the 0-1 UV space while preserving %d texels of padding around each chart."),
            PackingFailedMaterialSlotIndex,
            PackingFailedChartCount,
            ChartPaddingTexels));
        return Result;
    }

    TSet<int32> ProblemMaterialSlots;
    FString PackedValidationError;
    FDWCDataUVValidationFailure ValidationFailure;
    TArray<FDWCDataUVValidationExclusion> PackedDegenerateExclusions;
    if (!FDWCDataUVValidator::Validate(
            PackingTriangles,
            ValidatedCharts,
            PackedUVBySyntheticCorner,
            DataUVReferenceResolution,
            ProblemMaterialSlots,
            PackedValidationError,
            &ValidationFailure,
            &PackedDegenerateExclusions))
    {
        for (const int32 MaterialSlotIndex : ProblemMaterialSlots)
        {
            Result.FailedMaterialSlotIndices.Add(MaterialSlotIndex);
        }
        Result.ValidationFailure = MoveTemp(ValidationFailure);
        SetFailure(Result, FString::Printf(
            TEXT("DWC UV Channel generation failed final non-overlap validation: %s"),
            *PackedValidationError));
        return Result;
    }

    TSet<int32> PackedDegenerateTriangleIndices;
    for (const FDWCDataUVValidationExclusion& Exclusion : PackedDegenerateExclusions)
    {
        if (!Triangles.IsValidIndex(Exclusion.GeneratorTriangleIndex) ||
            PackedDegenerateTriangleIndices.Contains(Exclusion.GeneratorTriangleIndex))
        {
            continue;
        }

        PackedDegenerateTriangleIndices.Add(Exclusion.GeneratorTriangleIndex);
        ExcludedTriangleIndices.Add(Exclusion.GeneratorTriangleIndex);
        const FDWCDataUVTriangle& Triangle = Triangles[Exclusion.GeneratorTriangleIndex];
        ++Result.PackedDegenerateTriangleCount;
        FDWCDataUVSlotWarning& SlotDiagnostic = FindOrAddSlotWarning(
            Result.SlotWarnings,
            Triangle.MaterialSlotIndex);
        ++SlotDiagnostic.PackedDegenerateTriangleCount;

        FExcludedVisibleTriangle& ExcludedTriangle = ExcludedVisibleTriangles.AddDefaulted_GetRef();
        ExcludedTriangle.MaterialSlotIndex = Triangle.MaterialSlotIndex;
        ExcludedTriangle.GeneratorTriangleIndex = Exclusion.GeneratorTriangleIndex;
        ExcludedTriangle.MeshTriangleID = Triangle.TriangleID.GetValue();
        ExcludedTriangle.SurfaceArea = ComputeTriangleSurfaceArea3D(Triangle);
        ExcludedTriangle.bPackedDegenerate = true;
        for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
        {
            ExcludedTriangle.Vertices[CornerIndex] = Triangle.Vertices[CornerIndex];
            ExcludedVertexInstanceIDs.Add(Triangle.VertexInstances[CornerIndex].GetValue());
        }
    }

    TMap<int32, int32> ExcludedVisibleTriangleCountBySlot;
    TMap<int32, double> ExcludedVisibleAreaBySlot;
    for (const FExcludedVisibleTriangle& ExcludedTriangle : ExcludedVisibleTriangles)
    {
        ++ExcludedVisibleTriangleCountBySlot.FindOrAdd(ExcludedTriangle.MaterialSlotIndex);
        ExcludedVisibleAreaBySlot.FindOrAdd(ExcludedTriangle.MaterialSlotIndex) += ExcludedTriangle.SurfaceArea;
    }

    TSet<int32> DiagnosticSlotIndices;
    for (const FDWCDataUVSlotWarning& SlotDiagnostic : Result.SlotWarnings)
    {
        DiagnosticSlotIndices.Add(SlotDiagnostic.MaterialSlotIndex);
    }
    for (const TPair<int32, double>& Pair : TotalValid3DSurfaceAreaBySlot)
    {
        DiagnosticSlotIndices.Add(Pair.Key);
    }

    TArray<FString> ExclusionFailureMessages;
    for (const int32 MaterialSlotIndex : DiagnosticSlotIndices)
    {
        FDWCDataUVSlotWarning& SlotDiagnostic = FindOrAddSlotWarning(Result.SlotWarnings, MaterialSlotIndex);
        SlotDiagnostic.TotalValid3DSurfaceArea = TotalValid3DSurfaceAreaBySlot.FindRef(MaterialSlotIndex);
        SlotDiagnostic.ExcludedVisibleTriangleCount = ExcludedVisibleTriangleCountBySlot.FindRef(MaterialSlotIndex);
        SlotDiagnostic.ExcludedVisible3DSurfaceArea = ExcludedVisibleAreaBySlot.FindRef(MaterialSlotIndex);
        SlotDiagnostic.ExcludedVisible3DSurfaceRatio = SlotDiagnostic.TotalValid3DSurfaceArea > SMALL_NUMBER
            ? SlotDiagnostic.ExcludedVisible3DSurfaceArea / SlotDiagnostic.TotalValid3DSurfaceArea
            : 0.0;
        SlotDiagnostic.LargestConnectedExcluded3DSurfaceArea = ComputeLargestConnectedExcludedArea(
            ExcludedVisibleTriangles,
            MaterialSlotIndex);
        SlotDiagnostic.LargestConnectedExcluded3DSurfaceRatio = SlotDiagnostic.TotalValid3DSurfaceArea > SMALL_NUMBER
            ? SlotDiagnostic.LargestConnectedExcluded3DSurfaceArea / SlotDiagnostic.TotalValid3DSurfaceArea
            : 0.0;

        EDWCDataUVResultSeverity Severity = EDWCDataUVResultSeverity::Ready;
        if (SlotDiagnostic.Degenerate3DTriangleCount > 0 ||
            SlotDiagnostic.SplitOriginalUVIslandCount > 0 ||
            SlotDiagnostic.SelfOverlapPairCount > 0 ||
            SlotDiagnostic.BudgetFallbackIslandCount > 0)
        {
            Severity = EDWCDataUVResultSeverity::ReadyWithNotes;
        }

        if (SlotDiagnostic.ExcludedVisibleTriangleCount > 0)
        {
            const bool bPackedDegenerateWasExcluded = SlotDiagnostic.PackedDegenerateTriangleCount > 0;
            Severity = bPackedDegenerateWasExcluded ||
                       SlotDiagnostic.ExcludedVisible3DSurfaceRatio > VisibleExclusionNoteRatioThreshold
                ? EDWCDataUVResultSeverity::ReadyWithWarnings
                : DWCDataUVResultSeverity::Max(Severity, EDWCDataUVResultSeverity::ReadyWithNotes);
        }

        if (SlotDiagnostic.InvalidSourceUVTriangleCount > 0)
        {
            Severity = EDWCDataUVResultSeverity::Failed;
            Result.FailedMaterialSlotIndices.Add(MaterialSlotIndex);
            ExclusionFailureMessages.Add(FString::Printf(
                TEXT("Material Slot %d contains %d source triangle(s) with non-finite UV coordinates. NaN or Inf UV data cannot be used safely."),
                MaterialSlotIndex,
                SlotDiagnostic.InvalidSourceUVTriangleCount));
        }
        else
        {
            const bool bTotalExcludedSurfaceLimitExceeded =
                SlotDiagnostic.ExcludedVisible3DSurfaceRatio > VisibleExclusionFailureRatioThreshold;
            const bool bConnectedExcludedRegionLimitExceeded =
                SlotDiagnostic.LargestConnectedExcluded3DSurfaceRatio > ConnectedVisibleExclusionFailureRatioThreshold;

            if (bTotalExcludedSurfaceLimitExceeded || bConnectedExcludedRegionLimitExceeded)
            {
                Severity = EDWCDataUVResultSeverity::Failed;
                Result.FailedMaterialSlotIndices.Add(MaterialSlotIndex);

                if (bTotalExcludedSurfaceLimitExceeded && bConnectedExcludedRegionLimitExceeded)
                {
                    ExclusionFailureMessages.Add(FString::Printf(
                        TEXT("Material Slot %d excluded %.4f%% of its visible 3D surface, exceeding the %.2f%% total limit. Its largest connected excluded region is %.4f%%, also exceeding the %.2f%% connected-region limit."),
                        MaterialSlotIndex,
                        SlotDiagnostic.ExcludedVisible3DSurfaceRatio * 100.0,
                        VisibleExclusionFailureRatioThreshold * 100.0,
                        SlotDiagnostic.LargestConnectedExcluded3DSurfaceRatio * 100.0,
                        ConnectedVisibleExclusionFailureRatioThreshold * 100.0));
                }
                else if (bTotalExcludedSurfaceLimitExceeded)
                {
                    ExclusionFailureMessages.Add(FString::Printf(
                        TEXT("Material Slot %d excluded %.4f%% of its visible 3D surface, exceeding the %.2f%% total limit. Its largest connected excluded region is %.4f%% (within the %.2f%% connected-region limit)."),
                        MaterialSlotIndex,
                        SlotDiagnostic.ExcludedVisible3DSurfaceRatio * 100.0,
                        VisibleExclusionFailureRatioThreshold * 100.0,
                        SlotDiagnostic.LargestConnectedExcluded3DSurfaceRatio * 100.0,
                        ConnectedVisibleExclusionFailureRatioThreshold * 100.0));
                }
                else
                {
                    ExclusionFailureMessages.Add(FString::Printf(
                        TEXT("Material Slot %d's largest connected excluded region is %.4f%%, exceeding the %.2f%% connected-region limit. Total excluded visible surface is %.4f%% (within the %.2f%% total limit)."),
                        MaterialSlotIndex,
                        SlotDiagnostic.LargestConnectedExcluded3DSurfaceRatio * 100.0,
                        ConnectedVisibleExclusionFailureRatioThreshold * 100.0,
                        SlotDiagnostic.ExcludedVisible3DSurfaceRatio * 100.0,
                        VisibleExclusionFailureRatioThreshold * 100.0));
                }
            }
        }
        SlotDiagnostic.ResultSeverity = Severity;
        Result.ResultSeverity = DWCDataUVResultSeverity::Max(Result.ResultSeverity, Severity);
    }

    if (!ExclusionFailureMessages.IsEmpty())
    {
        SetFailure(Result, FString::Join(ExclusionFailureMessages, TEXT("\n")));
        return Result;
    }

    if (!PackedDegenerateTriangleIndices.IsEmpty())
    {
        for (FDWCDataUVChart& Chart : ValidatedCharts)
        {
            Chart.TriangleIndices.RemoveAll(
                [&PackedDegenerateTriangleIndices](const int32 TriangleIndex)
                {
                    return PackedDegenerateTriangleIndices.Contains(TriangleIndex);
                });
        }
        ValidatedCharts.RemoveAll(
            [](const FDWCDataUVChart& Chart)
            {
                return Chart.TriangleIndices.IsEmpty();
            });
    }

    Result.ExcludedVisibleTriangleCount = ExcludedVisibleTriangles.Num();
    for (const FDWCDataUVSlotWarning& SlotDiagnostic : Result.SlotWarnings)
    {
        Result.ExcludedVisible3DSurfaceArea += SlotDiagnostic.ExcludedVisible3DSurfaceArea;
        Result.ExcludedVisible3DSurfaceRatio = FMath::Max(
            Result.ExcludedVisible3DSurfaceRatio,
            SlotDiagnostic.ExcludedVisible3DSurfaceRatio);
        Result.LargestConnectedExcluded3DSurfaceArea = FMath::Max(
            Result.LargestConnectedExcluded3DSurfaceArea,
            SlotDiagnostic.LargestConnectedExcluded3DSurfaceArea);
        Result.LargestConnectedExcluded3DSurfaceRatio = FMath::Max(
            Result.LargestConnectedExcluded3DSurfaceRatio,
            SlotDiagnostic.LargestConnectedExcluded3DSurfaceRatio);
    }
    const double FinalPackAndValidateEndTime = FPlatformTime::Seconds();

    DataUVCharts = MoveTemp(ValidatedCharts);

    // Excluded triangles are not DWC simulation/rasterization inputs, but may still share
    // MeshDescription VertexInstances with neighboring valid triangles. Add one topology-only
    // chart per material slot so the seam splitter gives all excluded triangles independent
    // corners that can safely be cleared without touching valid DWC UVs.
    TArray<FDWCDataUVChart> SeamCharts = DataUVCharts;
    if (!ExcludedTriangleIndices.IsEmpty())
    {
        TMap<int32, TArray<int32>> ExcludedTrianglesByMaterial;
        for (const int32 TriangleIndex : ExcludedTriangleIndices)
        {
            if (Triangles.IsValidIndex(TriangleIndex))
            {
                ExcludedTrianglesByMaterial.FindOrAdd(Triangles[TriangleIndex].MaterialSlotIndex).Add(TriangleIndex);
            }
        }
        for (TPair<int32, TArray<int32>>& Pair : ExcludedTrianglesByMaterial)
        {
            FDWCDataUVChart& ExcludedChart = SeamCharts.AddDefaulted_GetRef();
            ExcludedChart.MaterialSlotIndex = Pair.Key;
            ExcludedChart.TriangleIndices = MoveTemp(Pair.Value);
        }
    }

    // Only a fully validated chart layout may modify the Prepared Mesh. Begin the
    // transaction immediately before creating real render-corner seams.
    SkeletalMesh->Modify();
    const FDWCDataUVSeamSplitResult SeamSplitResult = FDWCDataUVSeamSplitter::SplitChartBoundaries(
        *MeshDescription,
        Triangles,
        SeamCharts);
    if (!SeamSplitResult.bSucceeded)
    {
        SetFailure(Result, FString::Printf(
            TEXT("DWC UV Channel chart-boundary seam generation failed: %s"),
            *SeamSplitResult.Message));
        return Result;
    }
    Result.ChartBoundarySplitVertexInstanceCount = SeamSplitResult.SplitVertexInstanceCount;
    const double SeamSplitEndTime = FPlatformTime::Seconds();

    // Resolve the validated per-corner UVs onto the final chart-specific VertexInstances.
    TMap<int32, FVector2f> PackedUVByVertexInstance;
    for (int32 TriangleIndex = 0; TriangleIndex < Triangles.Num(); ++TriangleIndex)
    {
        if (ExcludedTriangleIndices.Contains(TriangleIndex))
        {
            continue;
        }
        for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
        {
            const int32 SyntheticCornerIndex = TriangleIndex * 3 + CornerIndex;
            const FVector2f* PackedUV = PackedUVBySyntheticCorner.Find(SyntheticCornerIndex);
            if (PackedUV == nullptr)
            {
                Result.FailedMaterialSlotIndices.Add(PackingTriangles[TriangleIndex].MaterialSlotIndex);
                SetFailure(Result, FString::Printf(
                    TEXT("DWC UV Channel packing omitted triangle %d corner %d."),
                    TriangleIndex,
                    CornerIndex));
                return Result;
            }

            const int32 VertexInstanceIndex = Triangles[TriangleIndex].VertexInstances[CornerIndex].GetValue();
            if (const FVector2f* ExistingUV = PackedUVByVertexInstance.Find(VertexInstanceIndex))
            {
                if (!FMath::IsNearlyEqual(ExistingUV->X, PackedUV->X, 1.0e-6f) ||
                    !FMath::IsNearlyEqual(ExistingUV->Y, PackedUV->Y, 1.0e-6f))
                {
                    Result.FailedMaterialSlotIndices.Add(Triangles[TriangleIndex].MaterialSlotIndex);
                    SetFailure(Result, FString::Printf(
                        TEXT("A final DWC UV Channel VertexInstance received conflicting packed coordinates in material slot %d."),
                        Triangles[TriangleIndex].MaterialSlotIndex));
                    return Result;
                }
            }
            else
            {
                PackedUVByVertexInstance.Add(VertexInstanceIndex, *PackedUV);
            }
        }
    }

    // Topology edits may reallocate MeshDescription attribute storage. Reacquire the
    // writable UV reference and reassert the destination channel before committing.
    auto WritableVertexInstanceUVs = Attributes.GetVertexInstanceUVs();
    if (WritableVertexInstanceUVs.GetNumChannels() <= NewUVChannelIndex)
    {
        WritableVertexInstanceUVs.SetNumChannels(NewUVChannelIndex + 1);
    }

    // Batch generation owns the whole DWC channel. Clear every non-target corner so an
    // overwritten preferred channel cannot leave unrelated source UV values looking like
    // valid DWC data on Non-wettable slots.
    if (TargetMaterialSlotIndices != nullptr)
    {
        for (const FVertexInstanceID VertexInstanceID : MeshDescription->VertexInstances().GetElementIDs())
        {
            WritableVertexInstanceUVs.Set(VertexInstanceID, NewUVChannelIndex, FVector2f::ZeroVector);
        }
    }

    for (const TPair<int32, FVector2f>& Pair : PackedUVByVertexInstance)
    {
        const FVertexInstanceID VertexInstanceID(Pair.Key);
        if (IsValidElementID(VertexInstanceID))
        {
            WritableVertexInstanceUVs.Set(VertexInstanceID, NewUVChannelIndex, Pair.Value);
        }
    }

    // The topology-only excluded charts now own separate VertexInstances. Clear them
    // unconditionally so later GPU/runtime builders cannot interpret any excluded triangle
    // as a valid DWC UV triangle.
    for (const int32 TriangleIndex : ExcludedTriangleIndices)
    {
        if (!Triangles.IsValidIndex(TriangleIndex))
        {
            continue;
        }
        for (const FVertexInstanceID VertexInstanceID : Triangles[TriangleIndex].VertexInstances)
        {
            if (IsValidElementID(VertexInstanceID))
            {
                WritableVertexInstanceUVs.Set(VertexInstanceID, NewUVChannelIndex, FVector2f::ZeroVector);
            }
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
            WritableVertexInstanceUVs.Set(VertexInstanceID, NewUVChannelIndex, FVector2f(0.0f, 0.0f));
        }
    }

    SkeletalMesh->CommitMeshDescription(LODIndex);
    SkeletalMesh->PostEditChange();
    SkeletalMesh->MarkPackageDirty();

    Result.bSucceeded = true;
    Result.UVChannelIndex = NewUVChannelIndex;
    Result.MaterialSlotIndex = TargetMaterialSlotIndex;
    Result.DataUVChartCount = DataUVCharts.Num();

    const FString TargetLabel = TargetMaterialSlotIndices != nullptr
                                    ? FString::Printf(TEXT("%d selected Wettable material slot(s)"), TargetMaterialSlotIndices->Num())
                                    : TargetMaterialSlotIndex != INDEX_NONE
                                        ? FString::Printf(TEXT("Material Slot %d"), TargetMaterialSlotIndex)
                                        : FString(TEXT("all material slots"));
    if (bOverwritingExistingChannel)
    {
        Result.Message = FString::Printf(
            TEXT("Regenerated %s in DWC-owned DWC UV Channel %d with %d packed DWC UV Channel chart(s)."),
            *TargetLabel,
            NewUVChannelIndex,
            DataUVCharts.Num());
    }
    else if (bAppendedBecausePreferredChannelWasOccupied)
    {
        Result.Message = FString::Printf(
            TEXT("Preferred UV Channel %d already existed and was not marked as DWC-generated, so created safe DWC UV Channel %d and generated %s with %d packed DWC UV Channel chart(s)."),
            SafePreferredUVChannelIndex,
            NewUVChannelIndex,
            *TargetLabel,
            DataUVCharts.Num());
    }
    else
    {
        Result.Message = FString::Printf(
            TEXT("Created DWC UV Channel %d and generated %s with %d packed DWC UV Channel chart(s), creating %d chart-boundary VertexInstance seam(s)."),
            NewUVChannelIndex,
            *TargetLabel,
            DataUVCharts.Num(),
            Result.ChartBoundarySplitVertexInstanceCount);
    }

    if (Result.HasWarnings())
    {
        Result.Message += FString::Printf(
            TEXT(" Warnings: excluded %d degenerate source-UV triangle(s); excluded %d invalid source-UV triangle(s); separated %d overlapping Source-UV triangle pair(s), splitting %d physical Source UV shell(s)."),
            Result.DegenerateSourceUVTriangleCount,
            Result.InvalidSourceUVTriangleCount,
            Result.SelfOverlapPairCount,
            Result.SplitOriginalUVIslandCount);
    }

    Result.TriangleReadMilliseconds = (TriangleReadEndTime - GenerationStartTime) * 1000.0;
    Result.OriginalIslandBuildMilliseconds = (OriginalIslandBuildEndTime - TriangleReadEndTime) * 1000.0;
    Result.ChartBuildMilliseconds = (ChartBuildEndTime - OriginalIslandBuildEndTime) * 1000.0;
    Result.PackAndValidateMilliseconds = (FinalPackAndValidateEndTime - ChartBuildEndTime) * 1000.0;
    Result.SeamSplitMilliseconds = (SeamSplitEndTime - FinalPackAndValidateEndTime) * 1000.0;
    Result.Message += FString::Printf(
        TEXT(" Timing (ms): triangle read %.1f, Original UV islands %.1f, overlap/chart split %.1f, pack/validate %.1f, seam split %.1f."),
        Result.TriangleReadMilliseconds,
        Result.OriginalIslandBuildMilliseconds,
        Result.ChartBuildMilliseconds,
        Result.PackAndValidateMilliseconds,
        Result.SeamSplitMilliseconds);

    return Result;
}

FDWCDataUVGenerationResult FDWCDataUVGenerator::TransferFromSourceLOD(
    USkeletalMesh* SkeletalMesh,
    const int32 SourceLODIndex,
    const int32 TargetLODIndex,
    const int32 DataUVChannelIndex,
    const bool bAllowOverwriteExistingChannel,
    const int32 TargetMaterialSlotIndex)
{
    using namespace DWCDataUVGeneratorInternal;

    FDWCDataUVGenerationResult Result;
    if (SkeletalMesh == nullptr)
    {
        SetFailure(Result, TEXT("No skeletal mesh is assigned."));
        return Result;
    }

    if (SourceLODIndex == TargetLODIndex)
    {
        SetFailure(Result, TEXT("Source and target LOD are the same; use full DWC UV Channel generation for the canonical LOD."));
        return Result;
    }

    if (DataUVChannelIndex < 0 || DataUVChannelIndex >= 8)
    {
        SetFailure(Result, FString::Printf(TEXT("DWC UV Channel %d is outside the supported 0-7 UV channel range."), DataUVChannelIndex));
        return Result;
    }

    FSkeletalMeshRenderData* RenderData = SkeletalMesh->GetResourceForRendering();
    if (RenderData == nullptr ||
        !RenderData->LODRenderData.IsValidIndex(SourceLODIndex) ||
        !RenderData->LODRenderData.IsValidIndex(TargetLODIndex))
    {
        SetFailure(Result, TEXT("Source or target LOD render data is unavailable."));
        return Result;
    }

    FSkeletalMeshLODRenderData& SourceLODData = RenderData->LODRenderData[SourceLODIndex];
    FSkeletalMeshLODRenderData& TargetLODData = RenderData->LODRenderData[TargetLODIndex];
    FStaticMeshVertexBuffer& SourceVertexBuffer = SourceLODData.StaticVertexBuffers.StaticMeshVertexBuffer;
    FStaticMeshVertexBuffer& TargetVertexBuffer = TargetLODData.StaticVertexBuffers.StaticMeshVertexBuffer;
    if (SourceVertexBuffer.GetNumTexCoords() <= static_cast<uint32>(DataUVChannelIndex))
    {
        SetFailure(Result, FString::Printf(TEXT("Source LOD%d render data does not contain DWC UV Channel %d."), SourceLODIndex, DataUVChannelIndex));
        return Result;
    }
    if (TargetVertexBuffer.GetNumTexCoords() <= static_cast<uint32>(DataUVChannelIndex))
    {
        if (!EnsureRenderVertexBufferUVChannel(TargetVertexBuffer, DataUVChannelIndex))
        {
            SetFailure(Result, FString::Printf(TEXT("Target LOD%d render data could not allocate DWC UV Channel %d."), TargetLODIndex, DataUVChannelIndex));
            return Result;
        }
    }

    TArray<uint32> SourceIndexBuffer;
    SourceLODData.MultiSizeIndexContainer.GetIndexBuffer(SourceIndexBuffer);
    if (SourceIndexBuffer.IsEmpty())
    {
        SetFailure(Result, FString::Printf(TEXT("Source LOD%d index buffer is empty."), SourceLODIndex));
        return Result;
    }

    TArray<FDataUVTransferSourceTriangle> SourceTriangles;
    SourceTriangles.Reserve(SourceIndexBuffer.Num() / 3);
    const int32 SourceVertexCount = static_cast<int32>(SourceLODData.GetNumVertices());
    for (const FSkelMeshRenderSection& Section : SourceLODData.RenderSections)
    {
        if (!Section.IsValid())
        {
            continue;
        }

        if (TargetMaterialSlotIndex != INDEX_NONE && Section.MaterialIndex != TargetMaterialSlotIndex)
        {
            continue;
        }

        const int32 FirstIndex = static_cast<int32>(Section.BaseIndex);
        const int32 LastIndex = FMath::Min(
            FirstIndex + static_cast<int32>(Section.NumTriangles * 3),
            SourceIndexBuffer.Num());
        for (int32 IndexOffset = FirstIndex; IndexOffset + 2 < LastIndex; IndexOffset += 3)
        {
            const int32 VertexIndices[3] =
            {
                static_cast<int32>(SourceIndexBuffer[IndexOffset]),
                static_cast<int32>(SourceIndexBuffer[IndexOffset + 1]),
                static_cast<int32>(SourceIndexBuffer[IndexOffset + 2])
            };
            if (VertexIndices[0] < 0 || VertexIndices[0] >= SourceVertexCount ||
                VertexIndices[1] < 0 || VertexIndices[1] >= SourceVertexCount ||
                VertexIndices[2] < 0 || VertexIndices[2] >= SourceVertexCount)
            {
                continue;
            }

            FDataUVTransferSourceTriangle SourceTriangle;
            SourceTriangle.MaterialSlotIndex = Section.MaterialIndex;
            for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
            {
                const int32 VertexIndex = VertexIndices[CornerIndex];
                SourceTriangle.Positions[CornerIndex] =
                    FVector(SourceLODData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(VertexIndex));
                SourceTriangle.DataUVs[CornerIndex] = SourceVertexBuffer.GetVertexUV(VertexIndex, DataUVChannelIndex);
                SourceTriangle.Bounds += SourceTriangle.Positions[CornerIndex];
            }
            SourceTriangle.Centroid =
                (SourceTriangle.Positions[0] + SourceTriangle.Positions[1] + SourceTriangle.Positions[2]) / 3.0;

            if (FDWCUVGeometry::ComputeTriangleDoubleArea3D(
                    SourceTriangle.Positions[0],
                    SourceTriangle.Positions[1],
                    SourceTriangle.Positions[2]) <= TransferDegenerateTriangleAreaTolerance)
            {
                ++Result.Degenerate3DTriangleCount;
                continue;
            }

            SourceTriangles.Add(SourceTriangle);
        }
    }

    if (SourceTriangles.IsEmpty())
    {
        SetFailure(Result, FString::Printf(TEXT("Source LOD%d has no valid triangles for DWC UV Channel transfer."), SourceLODIndex));
        return Result;
    }

    const FDataUVTransferBVH SourceBVH = BuildTransferBVH(SourceTriangles);
    TArray<uint32> TargetIndexBuffer;
    TargetLODData.MultiSizeIndexContainer.GetIndexBuffer(TargetIndexBuffer);
    if (TargetIndexBuffer.IsEmpty())
    {
        SetFailure(Result, FString::Printf(TEXT("Target LOD%d index buffer is empty."), TargetLODIndex));
        return Result;
    }

    int32 WrittenVertexCount = 0;
    int32 FailedVertexCount = 0;
    TSet<int32> WrittenVertices;
    const int32 TargetVertexCount = static_cast<int32>(TargetLODData.GetNumVertices());
    TArray<FVector2f> TransferredUVs;
    TransferredUVs.SetNum(TargetVertexCount);
    for (int32 VertexIndex = 0; VertexIndex < TargetVertexCount; ++VertexIndex)
    {
        TransferredUVs[VertexIndex] = TargetVertexBuffer.GetVertexUV(VertexIndex, DataUVChannelIndex);
    }

    for (const FSkelMeshRenderSection& Section : TargetLODData.RenderSections)
    {
        if (!Section.IsValid())
        {
            continue;
        }

        if (TargetMaterialSlotIndex != INDEX_NONE && Section.MaterialIndex != TargetMaterialSlotIndex)
        {
            continue;
        }

        const int32 FirstIndex = static_cast<int32>(Section.BaseIndex);
        const int32 LastIndex = FMath::Min(
            FirstIndex + static_cast<int32>(Section.NumTriangles * 3),
            TargetIndexBuffer.Num());
        for (int32 IndexOffset = FirstIndex; IndexOffset + 2 < LastIndex; IndexOffset += 3)
        {
            const int32 TargetVertexIndices[3] =
            {
                static_cast<int32>(TargetIndexBuffer[IndexOffset]),
                static_cast<int32>(TargetIndexBuffer[IndexOffset + 1]),
                static_cast<int32>(TargetIndexBuffer[IndexOffset + 2])
            };
            if (TargetVertexIndices[0] < 0 || TargetVertexIndices[0] >= TargetVertexCount ||
                TargetVertexIndices[1] < 0 || TargetVertexIndices[1] >= TargetVertexCount ||
                TargetVertexIndices[2] < 0 || TargetVertexIndices[2] >= TargetVertexCount)
            {
                continue;
            }

            const FVector TargetPositions[3] =
            {
                FVector(TargetLODData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(TargetVertexIndices[0])),
                FVector(TargetLODData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(TargetVertexIndices[1])),
                FVector(TargetLODData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(TargetVertexIndices[2]))
            };
            const FVector TargetCentroid = (TargetPositions[0] + TargetPositions[1] + TargetPositions[2]) / 3.0;
            FVector3d SourceCentroidBarycentric = FVector3d::ZeroVector;
            const FDataUVTransferSourceTriangle* SourceTriangle = FindClosestTransferSourceTriangle(
                SourceTriangles,
                SourceBVH,
                TargetCentroid,
                Section.MaterialIndex,
                true,
                SourceCentroidBarycentric);
            if (SourceTriangle == nullptr)
            {
                SourceTriangle = FindClosestTransferSourceTriangle(
                    SourceTriangles,
                    SourceBVH,
                    TargetCentroid,
                    Section.MaterialIndex,
                    false,
                    SourceCentroidBarycentric);
            }
            if (SourceTriangle == nullptr)
            {
                for (const int32 TargetVertexIndex : TargetVertexIndices)
                {
                    TransferredUVs[TargetVertexIndex] = FVector2f(0.0f, 0.0f);
                    if (!WrittenVertices.Contains(TargetVertexIndex))
                    {
                        WrittenVertices.Add(TargetVertexIndex);
                        ++WrittenVertexCount;
                    }
                    ++FailedVertexCount;
                }
                continue;
            }

            for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
            {
                FVector3d Barycentric = FVector3d::ZeroVector;
                const FVector ClosestSourcePoint = FMath::ClosestPointOnTriangleToPoint(
                    TargetPositions[CornerIndex],
                    SourceTriangle->Positions[0],
                    SourceTriangle->Positions[1],
                    SourceTriangle->Positions[2]);
                if (!ComputeBarycentric3D(
                        ClosestSourcePoint,
                        SourceTriangle->Positions[0],
                        SourceTriangle->Positions[1],
                        SourceTriangle->Positions[2],
                        Barycentric))
                {
                    Barycentric = SourceCentroidBarycentric;
                }

                const int32 TargetVertexIndex = TargetVertexIndices[CornerIndex];
                TransferredUVs[TargetVertexIndex] =
                    SourceTriangle->DataUVs[0] * static_cast<float>(Barycentric.X) +
                    SourceTriangle->DataUVs[1] * static_cast<float>(Barycentric.Y) +
                    SourceTriangle->DataUVs[2] * static_cast<float>(Barycentric.Z);
                if (!WrittenVertices.Contains(TargetVertexIndex))
                {
                    WrittenVertices.Add(TargetVertexIndex);
                    ++WrittenVertexCount;
                }
            }
        }
    }

    if (WrittenVertexCount <= 0)
    {
        SetFailure(Result, FString::Printf(TEXT("Target LOD%d has no render vertices that could receive transferred DWC UV Channels."), TargetLODIndex));
        return Result;
    }

    SkeletalMesh->Modify();
    for (const int32 VertexIndex : WrittenVertices)
    {
        TargetVertexBuffer.SetVertexUV(VertexIndex, DataUVChannelIndex, TransferredUVs[VertexIndex]);
    }
    BeginUpdateResourceRHI(&TargetVertexBuffer);
    SkeletalMesh->PostEditChange();
    SkeletalMesh->MarkPackageDirty();

    Result.bSucceeded = true;
    Result.UVChannelIndex = DataUVChannelIndex;
    Result.MaterialSlotIndex = TargetMaterialSlotIndex;
    Result.RenderVertexCount = WrittenVertexCount;
    Result.InvalidSourceUVTriangleCount = FailedVertexCount;
    Result.Message = FString::Printf(
        TEXT("Transferred DWC UV Channel %d from LOD%d to LOD%d render data for %d render vertex/vertices."),
        DataUVChannelIndex,
        SourceLODIndex,
        TargetLODIndex,
        WrittenVertexCount);
    if (FailedVertexCount > 0)
    {
        Result.Message += FString::Printf(TEXT(" %d render vertex/vertices fell back to zero UV."), FailedVertexCount);
    }
    return Result;
}
