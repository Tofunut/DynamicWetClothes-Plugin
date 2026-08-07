//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Foundation/UV/DWCUVIslandBuilder.h"
#include "WetClothing/Foundation/UV/DWCUVEdgeKey.h"
#include "WetClothing/Foundation/UV/DWCUVGeometry.h"

namespace DWCUVIslandBuilderPrivate
{
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

    void BuildMaterialSlot(
        const TArray<FDWCUVIslandBuildTriangle>& Triangles,
        const TArray<int32>& TriangleInputIndices,
        const int32 MaterialSlotIndex,
        TArray<FDWCOriginalUVIslandBuildResult>& OutIslands)
    {
        if (TriangleInputIndices.IsEmpty())
        {
            return;
        }

        FDisjointSet DisjointSet(TriangleInputIndices.Num());
        TMap<FDWCCanonicalUVEdge, TArray<int32>> EdgeToLocalTriangles;

        for (int32 LocalIndex = 0; LocalIndex < TriangleInputIndices.Num(); ++LocalIndex)
        {
            const FDWCUVIslandBuildTriangle& Triangle = Triangles[TriangleInputIndices[LocalIndex]];
            EdgeToLocalTriangles.FindOrAdd(FDWCCanonicalUVEdge(Triangle.UVs[0], Triangle.UVs[1])).Add(LocalIndex);
            EdgeToLocalTriangles.FindOrAdd(FDWCCanonicalUVEdge(Triangle.UVs[1], Triangle.UVs[2])).Add(LocalIndex);
            EdgeToLocalTriangles.FindOrAdd(FDWCCanonicalUVEdge(Triangle.UVs[2], Triangle.UVs[0])).Add(LocalIndex);
        }

        for (const TPair<FDWCCanonicalUVEdge, TArray<int32>>& Pair : EdgeToLocalTriangles)
        {
            const TArray<int32>& ConnectedTriangles = Pair.Value;
            if (ConnectedTriangles.Num() <= 1)
            {
                continue;
            }

            const int32 FirstTriangle = ConnectedTriangles[0];
            for (int32 ConnectedIndex = 1; ConnectedIndex < ConnectedTriangles.Num(); ++ConnectedIndex)
            {
                DisjointSet.Union(FirstTriangle, ConnectedTriangles[ConnectedIndex]);
            }
        }

        TMap<int32, int32> RootToOutputIndex;
        int32 NextIslandID = 0;
        for (int32 LocalIndex = 0; LocalIndex < TriangleInputIndices.Num(); ++LocalIndex)
        {
            const int32 InputIndex = TriangleInputIndices[LocalIndex];
            const FDWCUVIslandBuildTriangle& Triangle = Triangles[InputIndex];
            const int32 Root = DisjointSet.Find(LocalIndex);

            int32* OutputIndex = RootToOutputIndex.Find(Root);
            if (OutputIndex == nullptr)
            {
                FDWCOriginalUVIslandBuildResult& NewIsland = OutIslands.AddDefaulted_GetRef();
                NewIsland.MaterialSlotIndex = MaterialSlotIndex;
                NewIsland.IslandID = NextIslandID++;
                RootToOutputIndex.Add(Root, OutIslands.Num() - 1);
                OutputIndex = RootToOutputIndex.Find(Root);
                check(OutputIndex != nullptr);
            }

            FDWCOriginalUVIslandBuildResult& Island = OutIslands[*OutputIndex];
            Island.TriangleInputIndices.Add(InputIndex);
            Island.TriangleIDs.Add(Triangle.TriangleID);
            Island.UVBounds += Triangle.UVs[0];
            Island.UVBounds += Triangle.UVs[1];
            Island.UVBounds += Triangle.UVs[2];
            Island.UVArea += FDWCUVGeometry::ComputeTriangleArea2D(Triangle.UVs[0], Triangle.UVs[1], Triangle.UVs[2]);
        }
    }
}

void FDWCUVIslandBuilder::Build(
    const TArray<FDWCUVIslandBuildTriangle>& Triangles,
    TArray<FDWCOriginalUVIslandBuildResult>& OutIslands)
{
    using namespace DWCUVIslandBuilderPrivate;

    OutIslands.Reset();
    if (Triangles.IsEmpty())
    {
        return;
    }

    TMap<int32, TArray<int32>> TriangleIndicesByMaterialSlot;
    for (int32 TriangleIndex = 0; TriangleIndex < Triangles.Num(); ++TriangleIndex)
    {
        TriangleIndicesByMaterialSlot.FindOrAdd(Triangles[TriangleIndex].MaterialSlotIndex).Add(TriangleIndex);
    }

    TArray<int32> MaterialSlotIndices;
    TriangleIndicesByMaterialSlot.GetKeys(MaterialSlotIndices);
    MaterialSlotIndices.Sort();

    for (const int32 MaterialSlotIndex : MaterialSlotIndices)
    {
        BuildMaterialSlot(
            Triangles,
            TriangleIndicesByMaterialSlot.FindChecked(MaterialSlotIndex),
            MaterialSlotIndex,
            OutIslands);
    }
}
