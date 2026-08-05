/*
 *  선택된 UV Island의 경계선을 두께 있는 3D Selection Overlay Mesh 데이터로 변환합니다.
 */

#include "WetClothingSelectionOverlayMeshBuilder.h"

#include "WetClothing/Foundation/MeshAnalysis/WetClothingAssetMeshAnalyzer.h"
#include "WetClothing/Modes/Part/Viewport/DWCOverlayNormal.h"

namespace
{
    struct FSelectionOverlayQuantizedLocalVertex
    {
        int32 X = 0;
        int32 Y = 0;
        int32 Z = 0;

        bool operator==(const FSelectionOverlayQuantizedLocalVertex& Other) const
        {
            return X == Other.X && Y == Other.Y && Z == Other.Z;
        }
    };

    uint32 MakeSelectionOverlayQuantizedLocalVertexHash(const FSelectionOverlayQuantizedLocalVertex& Vertex)
    {
        return HashCombine(HashCombine(::GetTypeHash(Vertex.X), ::GetTypeHash(Vertex.Y)), ::GetTypeHash(Vertex.Z));
    }

    uint32 GetTypeHash(const FSelectionOverlayQuantizedLocalVertex& Vertex)
    {
        return MakeSelectionOverlayQuantizedLocalVertexHash(Vertex);
    }

    bool operator<(const FSelectionOverlayQuantizedLocalVertex& A, const FSelectionOverlayQuantizedLocalVertex& B)
    {
        if (A.X != B.X)
        {
            return A.X < B.X;
        }

        if (A.Y != B.Y)
        {
            return A.Y < B.Y;
        }

        return A.Z < B.Z;
    }

    struct FSelectionOverlayQuantizedLocalEdge
    {
        FSelectionOverlayQuantizedLocalVertex A;
        FSelectionOverlayQuantizedLocalVertex B;

        bool operator==(const FSelectionOverlayQuantizedLocalEdge& Other) const
        {
            return A == Other.A && B == Other.B;
        }
    };

    uint32 MakeSelectionOverlayQuantizedLocalEdgeHash(const FSelectionOverlayQuantizedLocalEdge& Edge)
    {
        return HashCombine(GetTypeHash(Edge.A), GetTypeHash(Edge.B));
    }

    uint32 GetTypeHash(const FSelectionOverlayQuantizedLocalEdge& Edge)
    {
        return MakeSelectionOverlayQuantizedLocalEdgeHash(Edge);
    }

    struct FWetClothingSelectionOverlayEdge
    {
        FVector LocalStart = FVector::ZeroVector;
        FVector LocalEnd = FVector::ZeroVector;
        FVector LocalNormal = FVector::UpVector;
    };

    struct FEdgeAccumulatorWithNormal
    {
        int32   Count = 0;
        FVector Start = FVector::ZeroVector;
        FVector End = FVector::ZeroVector;
        FVector NormalSum = FVector::ZeroVector;
    };

    FSelectionOverlayQuantizedLocalVertex MakeSelectionOverlayQuantizedLocalVertex(const FVector& Position)
    {
        constexpr double QuantizeScale = 1000.0;

        return FSelectionOverlayQuantizedLocalVertex{
            static_cast<int32>(FMath::RoundToInt(Position.X * QuantizeScale)),
            static_cast<int32>(FMath::RoundToInt(Position.Y * QuantizeScale)),
            static_cast<int32>(FMath::RoundToInt(Position.Z * QuantizeScale))
        };
    }

    FSelectionOverlayQuantizedLocalEdge MakeSelectionOverlayQuantizedLocalEdge(const FVector& Start, const FVector& End)
    {
        FSelectionOverlayQuantizedLocalVertex QuantizedStart = MakeSelectionOverlayQuantizedLocalVertex(Start);
        FSelectionOverlayQuantizedLocalVertex QuantizedEnd = MakeSelectionOverlayQuantizedLocalVertex(End);

        if (QuantizedEnd < QuantizedStart)
        {
            Swap(QuantizedStart, QuantizedEnd);
        }

        return FSelectionOverlayQuantizedLocalEdge{ QuantizedStart, QuantizedEnd };
    }

    FVector MakeSelectionOverlayAnyPerpendicular(const FVector& Direction)
    {
        FVector Perpendicular = FVector::CrossProduct(Direction, FVector::UpVector).GetSafeNormal();
        if (Perpendicular.IsNearlyZero())
        {
            Perpendicular = FVector::CrossProduct(Direction, FVector::RightVector).GetSafeNormal();
        }

        return Perpendicular.IsNearlyZero() ? FVector::ForwardVector : Perpendicular;
    }

    void AddSelectionOverlayBuilderVertex(
        FDWCOverlayMeshData& MeshData,
        const FVector&               Position,
        const FVector&               Normal,
        const FLinearColor&          Color)
    {
        MeshData.Vertices.Add(Position);
        MeshData.Normals.Add(Normal);
        MeshData.UVs.Add(FVector2D::ZeroVector);
        MeshData.VertexColors.Add(Color);
    }

    void AddSelectionOverlayBuilderQuad(
        TArray<int32>& Indices,
        int32          A,
        int32          B,
        int32          C,
        int32          D)
    {
        Indices.Add(A);
        Indices.Add(B);
        Indices.Add(C);
        Indices.Add(C);
        Indices.Add(B);
        Indices.Add(A);

        Indices.Add(A);
        Indices.Add(C);
        Indices.Add(D);
        Indices.Add(D);
        Indices.Add(C);
        Indices.Add(A);
    }

    void AddSelectionOverlayBuilderEdgeMesh(
        FDWCOverlayMeshData&            MeshData,
        const FWetClothingSelectionOverlayEdge& Edge,
        float                                   HalfThickness,
        const FLinearColor&                     Color)
    {
        const FVector EdgeDirection = (Edge.LocalEnd - Edge.LocalStart).GetSafeNormal();
        if (EdgeDirection.IsNearlyZero())
        {
            return;
        }

        FVector Normal = Edge.LocalNormal.GetSafeNormal();
        if (Normal.IsNearlyZero())
        {
            Normal = MakeSelectionOverlayAnyPerpendicular(EdgeDirection);
        }

        FVector Side = FVector::CrossProduct(EdgeDirection, Normal).GetSafeNormal();
        if (Side.IsNearlyZero())
        {
            Side = MakeSelectionOverlayAnyPerpendicular(EdgeDirection);
            Normal = FVector::CrossProduct(Side, EdgeDirection).GetSafeNormal();
        }

        const FVector CenterOffset = Normal * (HalfThickness * 1.5f);
        const FVector Start = Edge.LocalStart + CenterOffset;
        const FVector End = Edge.LocalEnd + CenterOffset;
        const int32   BaseIndex = MeshData.Vertices.Num();

        const FVector Corners[8] = {
            Start + Side * HalfThickness + Normal * HalfThickness,
            Start - Side * HalfThickness + Normal * HalfThickness,
            Start - Side * HalfThickness - Normal * HalfThickness,
            Start + Side * HalfThickness - Normal * HalfThickness,
            End + Side * HalfThickness + Normal * HalfThickness,
            End - Side * HalfThickness + Normal * HalfThickness,
            End - Side * HalfThickness - Normal * HalfThickness,
            End + Side * HalfThickness - Normal * HalfThickness
        };

        for (int32 CornerIndex = 0; CornerIndex < 8; ++CornerIndex)
        {
            FVector VertexNormal = (Corners[CornerIndex] - ((CornerIndex < 4) ? Start : End)).GetSafeNormal();
            if (VertexNormal.IsNearlyZero())
            {
                VertexNormal = Normal;
            }

            AddSelectionOverlayBuilderVertex(MeshData, Corners[CornerIndex], VertexNormal, Color);
        }

        AddSelectionOverlayBuilderQuad(MeshData.Indices, BaseIndex + 0, BaseIndex + 4, BaseIndex + 5, BaseIndex + 1);
        AddSelectionOverlayBuilderQuad(MeshData.Indices, BaseIndex + 1, BaseIndex + 5, BaseIndex + 6, BaseIndex + 2);
        AddSelectionOverlayBuilderQuad(MeshData.Indices, BaseIndex + 2, BaseIndex + 6, BaseIndex + 7, BaseIndex + 3);
        AddSelectionOverlayBuilderQuad(MeshData.Indices, BaseIndex + 3, BaseIndex + 7, BaseIndex + 4, BaseIndex + 0);
        AddSelectionOverlayBuilderQuad(MeshData.Indices, BaseIndex + 0, BaseIndex + 1, BaseIndex + 2, BaseIndex + 3);
        AddSelectionOverlayBuilderQuad(MeshData.Indices, BaseIndex + 4, BaseIndex + 7, BaseIndex + 6, BaseIndex + 5);
    }
} // namespace

void FWetClothingSelectionOverlayMeshBuilder::BuildMeshData(
    const TArray<FWetClothingAssetUVIsland>& Islands,
    const TSet<int32>&                       HighlightedUVIslandIDs,
    float                                    HalfThickness,
    const FLinearColor&                      Color,
    FDWCOverlayMeshData&             OutMeshData)
{
    OutMeshData.Reset();

    if (HighlightedUVIslandIDs.Num() == 0)
    {
        return;
    }

    TMap<FSelectionOverlayQuantizedLocalEdge, FEdgeAccumulatorWithNormal> EdgeMap;
    auto                                                                  AccumulateEdge = [&EdgeMap](const FVector& Start, const FVector& End, const FVector& TriangleNormal)
    {
        const FSelectionOverlayQuantizedLocalEdge EdgeKey = MakeSelectionOverlayQuantizedLocalEdge(Start, End);
        FEdgeAccumulatorWithNormal&               Accumulator = EdgeMap.FindOrAdd(EdgeKey);
        if (Accumulator.Count == 0)
        {
            Accumulator.Start = Start;
            Accumulator.End = End;
        }
        ++Accumulator.Count;
        Accumulator.NormalSum += TriangleNormal;
    };

    for (const FWetClothingAssetUVIsland& Island : Islands)
    {
        if (!HighlightedUVIslandIDs.Contains(Island.UVIslandID))
        {
            continue;
        }

        for (const FWetClothingAssetUVTriangle& Triangle : Island.UVTriangles)
        {
            const FVector TriangleNormal = DWCOverlayNormal::MakeWetPartOverlayNormal(
                Triangle.LocalPositions[0], Triangle.LocalPositions[1], Triangle.LocalPositions[2]);
            AccumulateEdge(Triangle.LocalPositions[0], Triangle.LocalPositions[1], TriangleNormal);
            AccumulateEdge(Triangle.LocalPositions[1], Triangle.LocalPositions[2], TriangleNormal);
            AccumulateEdge(Triangle.LocalPositions[2], Triangle.LocalPositions[0], TriangleNormal);
        }
    }

    for (const TPair<FSelectionOverlayQuantizedLocalEdge, FEdgeAccumulatorWithNormal>& Pair : EdgeMap)
    {
        // Internal triangle edges occur twice. Keep only the selected islands' boundary.
        if (Pair.Value.Count != 1)
        {
            continue;
        }

        FWetClothingSelectionOverlayEdge SelectionEdge;
        SelectionEdge.LocalStart = Pair.Value.Start;
        SelectionEdge.LocalEnd = Pair.Value.End;
        SelectionEdge.LocalNormal = Pair.Value.NormalSum.GetSafeNormal();
        if (SelectionEdge.LocalNormal.IsNearlyZero())
        {
            SelectionEdge.LocalNormal = FVector::UpVector;
        }

        AddSelectionOverlayBuilderEdgeMesh(OutMeshData, SelectionEdge, HalfThickness, Color);
    }
}
