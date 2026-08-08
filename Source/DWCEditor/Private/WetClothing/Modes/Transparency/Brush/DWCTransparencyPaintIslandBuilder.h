// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/UV/DWCUVEdgeKey.h"

/**
 * Triangle input for transparency paint clipping.
 *
 * Paint islands require both the mesh-space edge and the UV edge to match.
 * This keeps unrelated shells with coincident UV borders isolated.
 */
struct FDWCTransparencyPaintIslandTriangle
{
    int32     TriangleID = INDEX_NONE;
    int32     MaterialSlotIndex = INDEX_NONE;
    FVector   Positions[3] = { FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector };
    FVector2D UVs[3] = { FVector2D::ZeroVector, FVector2D::ZeroVector, FVector2D::ZeroVector };
};

namespace DWCTransparencyPaintIslandBuilderPrivate
{
    struct FQuantizedPositionUVPoint
    {
        static constexpr double PositionQuantizeScale = 1000.0;

        int64                PositionX = 0;
        int64                PositionY = 0;
        int64                PositionZ = 0;
        FDWCQuantizedUVPoint UV;

        FQuantizedPositionUVPoint() = default;

        FQuantizedPositionUVPoint(const FVector& Position, const FVector2D& InUV)
            : PositionX(FMath::RoundToInt64(Position.X * PositionQuantizeScale)), PositionY(FMath::RoundToInt64(Position.Y * PositionQuantizeScale)), PositionZ(FMath::RoundToInt64(Position.Z * PositionQuantizeScale)), UV(InUV)
        {
        }

        bool operator==(const FQuantizedPositionUVPoint& Other) const
        {
            return PositionX == Other.PositionX &&
                   PositionY == Other.PositionY &&
                   PositionZ == Other.PositionZ &&
                   UV == Other.UV;
        }

        bool operator<(const FQuantizedPositionUVPoint& Other) const
        {
            if (PositionX != Other.PositionX)
                return PositionX < Other.PositionX;
            if (PositionY != Other.PositionY)
                return PositionY < Other.PositionY;
            if (PositionZ != Other.PositionZ)
                return PositionZ < Other.PositionZ;
            return UV < Other.UV;
        }
    };

    inline uint32 HashInt64(const int64 Value)
    {
        const uint64 UnsignedValue = static_cast<uint64>(Value);
        return HashCombine(
            ::GetTypeHash(static_cast<uint32>(UnsignedValue & 0xffffffffull)),
            ::GetTypeHash(static_cast<uint32>(UnsignedValue >> 32)));
    }

    inline uint32 GetTypeHash(const FQuantizedPositionUVPoint& Point)
    {
        uint32 Hash = HashCombine(HashInt64(Point.PositionX), HashInt64(Point.PositionY));
        Hash = HashCombine(Hash, HashInt64(Point.PositionZ));
        return HashCombine(Hash, ::GetTypeHash(Point.UV));
    }

    struct FMeshUVEdge
    {
        FQuantizedPositionUVPoint A;
        FQuantizedPositionUVPoint B;

        FMeshUVEdge() = default;

        FMeshUVEdge(
            const FVector&   PositionA,
            const FVector2D& UVA,
            const FVector&   PositionB,
            const FVector2D& UVB)
            : A(PositionA, UVA), B(PositionB, UVB)
        {
            if (B < A)
            {
                Swap(A, B);
            }
        }

        bool operator==(const FMeshUVEdge& Other) const
        {
            return A == Other.A && B == Other.B;
        }
    };

    inline uint32 GetTypeHash(const FMeshUVEdge& Edge)
    {
        return HashCombine(GetTypeHash(Edge.A), GetTypeHash(Edge.B));
    }

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
} // namespace DWCTransparencyPaintIslandBuilderPrivate

class FDWCTransparencyPaintIslandBuilder
{
  public:
    static void Build(
        const TConstArrayView<FDWCTransparencyPaintIslandTriangle> Triangles,
        TMap<int32, int32>&                                        OutIslandIDByTriangleID)
    {
        using namespace DWCTransparencyPaintIslandBuilderPrivate;

        OutIslandIDByTriangleID.Reset();
        if (Triangles.IsEmpty())
        {
            return;
        }

        TMap<int32, TArray<int32>> TriangleIndicesByMaterialSlot;
        for (int32 TriangleIndex = 0; TriangleIndex < Triangles.Num(); ++TriangleIndex)
        {
            TriangleIndicesByMaterialSlot
                .FindOrAdd(Triangles[TriangleIndex].MaterialSlotIndex)
                .Add(TriangleIndex);
        }

        for (const TPair<int32, TArray<int32>>& SlotPair : TriangleIndicesByMaterialSlot)
        {
            const TArray<int32>&             SlotTriangleIndices = SlotPair.Value;
            FDisjointSet                     DisjointSet(SlotTriangleIndices.Num());
            TMap<FMeshUVEdge, TArray<int32>> EdgeToLocalTriangles;

            for (int32 LocalIndex = 0; LocalIndex < SlotTriangleIndices.Num(); ++LocalIndex)
            {
                const FDWCTransparencyPaintIslandTriangle& Triangle =
                    Triangles[SlotTriangleIndices[LocalIndex]];
                EdgeToLocalTriangles.FindOrAdd(FMeshUVEdge(
                                                   Triangle.Positions[0], Triangle.UVs[0],
                                                   Triangle.Positions[1], Triangle.UVs[1]))
                    .Add(LocalIndex);
                EdgeToLocalTriangles.FindOrAdd(FMeshUVEdge(
                                                   Triangle.Positions[1], Triangle.UVs[1],
                                                   Triangle.Positions[2], Triangle.UVs[2]))
                    .Add(LocalIndex);
                EdgeToLocalTriangles.FindOrAdd(FMeshUVEdge(
                                                   Triangle.Positions[2], Triangle.UVs[2],
                                                   Triangle.Positions[0], Triangle.UVs[0]))
                    .Add(LocalIndex);
            }

            for (const TPair<FMeshUVEdge, TArray<int32>>& EdgePair : EdgeToLocalTriangles)
            {
                const TArray<int32>& ConnectedTriangles = EdgePair.Value;
                for (int32 ConnectedIndex = 1; ConnectedIndex < ConnectedTriangles.Num(); ++ConnectedIndex)
                {
                    DisjointSet.Union(ConnectedTriangles[0], ConnectedTriangles[ConnectedIndex]);
                }
            }

            TMap<int32, int32> IslandIDByRoot;
            int32              NextIslandID = 0;
            for (int32 LocalIndex = 0; LocalIndex < SlotTriangleIndices.Num(); ++LocalIndex)
            {
                const int32 Root = DisjointSet.Find(LocalIndex);
                int32*      IslandID = IslandIDByRoot.Find(Root);
                if (IslandID == nullptr)
                {
                    IslandIDByRoot.Add(Root, NextIslandID++);
                    IslandID = IslandIDByRoot.Find(Root);
                }
                check(IslandID != nullptr);
                OutIslandIDByTriangleID.Add(
                    Triangles[SlotTriangleIndices[LocalIndex]].TriangleID,
                    *IslandID);
            }
        }
    }
};
