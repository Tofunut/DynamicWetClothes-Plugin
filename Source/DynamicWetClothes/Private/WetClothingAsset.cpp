#include "WetClothingAsset.h"

#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"

namespace
{
    static constexpr double UVQuantizeScale = 100000.0;

    struct FRuntimeQuantizedUV
    {
        int64 U = 0;
        int64 V = 0;

        FRuntimeQuantizedUV() = default;

        explicit FRuntimeQuantizedUV(const FVector2D& InUV)
        {
            U = FMath::RoundToInt64(InUV.X * UVQuantizeScale);
            V = FMath::RoundToInt64(InUV.Y * UVQuantizeScale);
        }

        bool operator==(const FRuntimeQuantizedUV& Other) const
        {
            return U == Other.U && V == Other.V;
        }
    };

    uint32 HashInt64(int64 Value)
    {
        const uint64 UnsignedValue = static_cast<uint64>(Value);
        const uint32 Low = static_cast<uint32>(UnsignedValue & 0xFFFFFFFFull);
        const uint32 High = static_cast<uint32>((UnsignedValue >> 32) & 0xFFFFFFFFull);
        return HashCombine(::GetTypeHash(Low), ::GetTypeHash(High));
    }

    uint32 GetTypeHash(const FRuntimeQuantizedUV& Value)
    {
        return HashCombine(HashInt64(Value.U), HashInt64(Value.V));
    }

    bool LessUV(const FRuntimeQuantizedUV& A, const FRuntimeQuantizedUV& B)
    {
        return A.U != B.U ? A.U < B.U : A.V < B.V;
    }

    struct FRuntimeUVEdgeKey
    {
        FRuntimeQuantizedUV A;
        FRuntimeQuantizedUV B;

        FRuntimeUVEdgeKey() = default;

        FRuntimeUVEdgeKey(const FVector2D& InA, const FVector2D& InB)
        {
            FRuntimeQuantizedUV QuantizedA(InA);
            FRuntimeQuantizedUV QuantizedB(InB);

            if (LessUV(QuantizedB, QuantizedA))
            {
                A = QuantizedB;
                B = QuantizedA;
            }
            else
            {
                A = QuantizedA;
                B = QuantizedB;
            }
        }

        bool operator==(const FRuntimeUVEdgeKey& Other) const
        {
            return A == Other.A && B == Other.B;
        }
    };

    uint32 GetTypeHash(const FRuntimeUVEdgeKey& Key)
    {
        return HashCombine(GetTypeHash(Key.A), GetTypeHash(Key.B));
    }

    struct FRuntimeScopeKey
    {
        int32 MaterialSlotIndex = INDEX_NONE;
        int32 UVChannelIndex = INDEX_NONE;

        bool operator==(const FRuntimeScopeKey& Other) const
        {
            return MaterialSlotIndex == Other.MaterialSlotIndex && UVChannelIndex == Other.UVChannelIndex;
        }
    };

    uint32 GetTypeHash(const FRuntimeScopeKey& Key)
    {
        return HashCombine(::GetTypeHash(Key.MaterialSlotIndex), ::GetTypeHash(Key.UVChannelIndex));
    }

    struct FRuntimeTriangle
    {
        int32     VertexIndices[3] = { INDEX_NONE, INDEX_NONE, INDEX_NONE };
        FVector2D UVs[3];
    };

    struct FRuntimeIsland
    {
        int32       IslandID = INDEX_NONE;
        TSet<int32> VertexIndices;
    };

    void SetError(FString* OutErrorMessage, const TCHAR* InMessage)
    {
        if (OutErrorMessage != nullptr)
        {
            *OutErrorMessage = InMessage;
        }
    }

    int32 FindParent(TArray<int32>& Parents, int32 Index)
    {
        if (Parents[Index] == Index)
        {
            return Index;
        }

        Parents[Index] = FindParent(Parents, Parents[Index]);
        return Parents[Index];
    }

    void UnionParents(TArray<int32>& Parents, int32 A, int32 B)
    {
        const int32 RootA = FindParent(Parents, A);
        const int32 RootB = FindParent(Parents, B);

        if (RootA != RootB)
        {
            Parents[RootB] = RootA;
        }
    }

    FString MakeMeshBuildSignature(const USkeletalMesh* SkeletalMesh, const FSkeletalMeshLODRenderData& LODData, int32 LODIndex)
    {
        TArray<uint32> IndexBuffer;
        LODData.MultiSizeIndexContainer.GetIndexBuffer(IndexBuffer);

        return FString::Printf(
            TEXT("%s|LOD=%d|Vertices=%d|Indices=%d|Materials=%d"),
            *GetPathNameSafe(SkeletalMesh),
            LODIndex,
            LODData.GetNumVertices(),
            IndexBuffer.Num(),
            SkeletalMesh != nullptr ? SkeletalMesh->GetMaterials().Num() : 0);
    }

    bool BuildRawTriangles(
        const USkeletalMesh* SkeletalMesh,
        const FSkeletalMeshLODRenderData& LODData,
        const TArray<uint32>& IndexBuffer,
        int32 UVChannelIndex,
        int32 MaterialSlotIndex,
        TArray<FRuntimeTriangle>& OutTriangles,
        FString* OutErrorMessage)
    {
        OutTriangles.Reset();

        if (SkeletalMesh == nullptr || !SkeletalMesh->GetMaterials().IsValidIndex(MaterialSlotIndex))
        {
            SetError(OutErrorMessage, TEXT("A wet part references an invalid material slot."));
            return false;
        }

        const int32 NumUVChannels = static_cast<int32>(LODData.GetNumTexCoords());
        if (UVChannelIndex < 0 || UVChannelIndex >= NumUVChannels)
        {
            SetError(OutErrorMessage, TEXT("A wet part references a UV channel that is not available on the mesh."));
            return false;
        }

        const int32 VertexCount = LODData.GetNumVertices();

        for (const FSkelMeshRenderSection& Section : LODData.RenderSections)
        {
            if (!Section.IsValid() || Section.MaterialIndex != MaterialSlotIndex)
            {
                continue;
            }

            const int32 FirstIndex = static_cast<int32>(Section.BaseIndex);
            const int32 LastIndex = FMath::Min(FirstIndex + static_cast<int32>(Section.NumTriangles * 3), IndexBuffer.Num());

            for (int32 TriangleIndex = FirstIndex; TriangleIndex + 2 < LastIndex; TriangleIndex += 3)
            {
                const uint32 Index0 = IndexBuffer[TriangleIndex];
                const uint32 Index1 = IndexBuffer[TriangleIndex + 1];
                const uint32 Index2 = IndexBuffer[TriangleIndex + 2];

                if (Index0 >= static_cast<uint32>(VertexCount) ||
                    Index1 >= static_cast<uint32>(VertexCount) ||
                    Index2 >= static_cast<uint32>(VertexCount))
                {
                    continue;
                }

                FRuntimeTriangle Triangle;
                Triangle.VertexIndices[0] = static_cast<int32>(Index0);
                Triangle.VertexIndices[1] = static_cast<int32>(Index1);
                Triangle.VertexIndices[2] = static_cast<int32>(Index2);
                Triangle.UVs[0] = FVector2D(LODData.StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV(Index0, UVChannelIndex));
                Triangle.UVs[1] = FVector2D(LODData.StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV(Index1, UVChannelIndex));
                Triangle.UVs[2] = FVector2D(LODData.StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV(Index2, UVChannelIndex));
                OutTriangles.Add(Triangle);
            }
        }

        SetError(OutErrorMessage, TEXT(""));
        return true;
    }

    void BuildIslands(const TArray<FRuntimeTriangle>& RawTriangles, TArray<FRuntimeIsland>& OutIslands)
    {
        OutIslands.Reset();

        if (RawTriangles.Num() == 0)
        {
            return;
        }

        TArray<int32> Parents;
        Parents.SetNum(RawTriangles.Num());

        for (int32 TriangleIndex = 0; TriangleIndex < RawTriangles.Num(); ++TriangleIndex)
        {
            Parents[TriangleIndex] = TriangleIndex;
        }

        TMap<FRuntimeUVEdgeKey, TArray<int32>> EdgeToTriangles;
        for (int32 TriangleIndex = 0; TriangleIndex < RawTriangles.Num(); ++TriangleIndex)
        {
            const FRuntimeTriangle& Triangle = RawTriangles[TriangleIndex];
            EdgeToTriangles.FindOrAdd(FRuntimeUVEdgeKey(Triangle.UVs[0], Triangle.UVs[1])).Add(TriangleIndex);
            EdgeToTriangles.FindOrAdd(FRuntimeUVEdgeKey(Triangle.UVs[1], Triangle.UVs[2])).Add(TriangleIndex);
            EdgeToTriangles.FindOrAdd(FRuntimeUVEdgeKey(Triangle.UVs[2], Triangle.UVs[0])).Add(TriangleIndex);
        }

        for (const TPair<FRuntimeUVEdgeKey, TArray<int32>>& Pair : EdgeToTriangles)
        {
            const TArray<int32>& ConnectedTriangles = Pair.Value;
            if (ConnectedTriangles.Num() <= 1)
            {
                continue;
            }

            const int32 FirstTriangle = ConnectedTriangles[0];
            for (int32 ConnectedIndex = 1; ConnectedIndex < ConnectedTriangles.Num(); ++ConnectedIndex)
            {
                UnionParents(Parents, FirstTriangle, ConnectedTriangles[ConnectedIndex]);
            }
        }

        TMap<int32, int32> RootToIslandArrayIndex;
        for (int32 TriangleIndex = 0; TriangleIndex < RawTriangles.Num(); ++TriangleIndex)
        {
            const int32 Root = FindParent(Parents, TriangleIndex);
            int32* ExistingIslandIndex = RootToIslandArrayIndex.Find(Root);

            if (ExistingIslandIndex == nullptr)
            {
                FRuntimeIsland Island;
                Island.IslandID = OutIslands.Num();

                const int32 NewIslandIndex = OutIslands.Add(MoveTemp(Island));
                RootToIslandArrayIndex.Add(Root, NewIslandIndex);
                ExistingIslandIndex = RootToIslandArrayIndex.Find(Root);
            }

            FRuntimeIsland& Island = OutIslands[*ExistingIslandIndex];
            const FRuntimeTriangle& Triangle = RawTriangles[TriangleIndex];
            Island.VertexIndices.Add(Triangle.VertexIndices[0]);
            Island.VertexIndices.Add(Triangle.VertexIndices[1]);
            Island.VertexIndices.Add(Triangle.VertexIndices[2]);
        }
    }

    void AddNeighbor(TArray<FWetClothingAssetBakedVertexNeighbors>& NeighborGraph, int32 A, int32 B)
    {
        if (NeighborGraph.IsValidIndex(A) && NeighborGraph.IsValidIndex(B) && A != B)
        {
            NeighborGraph[A].Neighbors.AddUnique(B);
        }
    }

    void BuildNeighborGraph(
        const FSkeletalMeshLODRenderData& LODData,
        const TArray<uint32>& IndexBuffer,
        TArray<FWetClothingAssetBakedVertexNeighbors>& OutNeighborGraph)
    {
        OutNeighborGraph.SetNum(LODData.GetNumVertices());

        for (const FSkelMeshRenderSection& Section : LODData.RenderSections)
        {
            if (!Section.IsValid())
            {
                continue;
            }

            const int32 FirstIndex = static_cast<int32>(Section.BaseIndex);
            const int32 LastIndex = FMath::Min(FirstIndex + static_cast<int32>(Section.NumTriangles * 3), IndexBuffer.Num());

            for (int32 TriangleIndex = FirstIndex; TriangleIndex + 2 < LastIndex; TriangleIndex += 3)
            {
                const int32 Index0 = static_cast<int32>(IndexBuffer[TriangleIndex]);
                const int32 Index1 = static_cast<int32>(IndexBuffer[TriangleIndex + 1]);
                const int32 Index2 = static_cast<int32>(IndexBuffer[TriangleIndex + 2]);

                AddNeighbor(OutNeighborGraph, Index0, Index1);
                AddNeighbor(OutNeighborGraph, Index0, Index2);
                AddNeighbor(OutNeighborGraph, Index1, Index0);
                AddNeighbor(OutNeighborGraph, Index1, Index2);
                AddNeighbor(OutNeighborGraph, Index2, Index0);
                AddNeighbor(OutNeighborGraph, Index2, Index1);
            }
        }
    }
} // namespace

void UWetClothingAsset::ClearRuntimeData()
{
    BakedRuntimeData = FWetClothingAssetBakedRuntimeData();
}

bool UWetClothingAsset::IsRuntimeDataValidForMesh(const USkeletalMesh* SkeletalMesh, int32 LODIndex) const
{
    if (!BakedRuntimeData.bIsValid || SkeletalMesh == nullptr)
    {
        return false;
    }

    const FSkeletalMeshRenderData* RenderData = SkeletalMesh->GetResourceForRendering();
    if (RenderData == nullptr || !RenderData->LODRenderData.IsValidIndex(LODIndex))
    {
        return false;
    }

    const FSkeletalMeshLODRenderData& LODData = RenderData->LODRenderData[LODIndex];
    return BakedRuntimeData.LODIndex == LODIndex &&
           BakedRuntimeData.VertexCount == LODData.GetNumVertices() &&
           BakedRuntimeData.MeshBuildSignature == MakeMeshBuildSignature(SkeletalMesh, LODData, LODIndex);
}

bool UWetClothingAsset::RebuildRuntimeData(FString* OutErrorMessage, int32 LODIndex)
{
    ClearRuntimeData();

    if (TargetMesh == nullptr)
    {
        SetError(OutErrorMessage, TEXT("No TargetMesh is assigned."));
        return false;
    }

    const FSkeletalMeshRenderData* RenderData = TargetMesh->GetResourceForRendering();
    if (RenderData == nullptr)
    {
        SetError(OutErrorMessage, TEXT("TargetMesh render data is unavailable."));
        return false;
    }

    if (!RenderData->LODRenderData.IsValidIndex(LODIndex))
    {
        SetError(OutErrorMessage, TEXT("The requested LOD render data is unavailable."));
        return false;
    }

    const FSkeletalMeshLODRenderData& LODData = RenderData->LODRenderData[LODIndex];
    TArray<uint32> IndexBuffer;
    LODData.MultiSizeIndexContainer.GetIndexBuffer(IndexBuffer);

    if (LODData.GetNumVertices() <= 0 || IndexBuffer.Num() == 0)
    {
        SetError(OutErrorMessage, TEXT("TargetMesh render data is empty."));
        return false;
    }

    BakedRuntimeData.bIsValid = true;
    BakedRuntimeData.LODIndex = LODIndex;
    BakedRuntimeData.VertexCount = LODData.GetNumVertices();
    BakedRuntimeData.MeshBuildSignature = MakeMeshBuildSignature(TargetMesh, LODData, LODIndex);
    BakedRuntimeData.Vertices.SetNum(BakedRuntimeData.VertexCount);
    BuildNeighborGraph(LODData, IndexBuffer, BakedRuntimeData.NeighborGraph);

    TMap<FRuntimeScopeKey, TArray<int32>> EntryIndicesByScope;
    for (int32 EntryIndex = 0; EntryIndex < WetPartEntries.Num(); ++EntryIndex)
    {
        const FWetClothingAssetWetPartEntry& Entry = WetPartEntries[EntryIndex];
        if (Entry.MaterialSlotIndex == INDEX_NONE || Entry.UVChannelIndex == INDEX_NONE)
        {
            continue;
        }

        FRuntimeScopeKey ScopeKey;
        ScopeKey.MaterialSlotIndex = Entry.MaterialSlotIndex;
        ScopeKey.UVChannelIndex = Entry.UVChannelIndex;
        EntryIndicesByScope.FindOrAdd(ScopeKey).Add(EntryIndex);
    }

    for (const TPair<FRuntimeScopeKey, TArray<int32>>& ScopePair : EntryIndicesByScope)
    {
        TArray<FRuntimeTriangle> RawTriangles;
        if (!BuildRawTriangles(
                TargetMesh,
                LODData,
                IndexBuffer,
                ScopePair.Key.UVChannelIndex,
                ScopePair.Key.MaterialSlotIndex,
                RawTriangles,
                OutErrorMessage))
        {
            ClearRuntimeData();
            return false;
        }

        TArray<FRuntimeIsland> Islands;
        BuildIslands(RawTriangles, Islands);

        int32 DefaultEntryIndex = INDEX_NONE;
        TMap<int32, int32> AssignedIslandToEntryIndex;

        for (int32 EntryIndex : ScopePair.Value)
        {
            const FWetClothingAssetWetPartEntry& Entry = WetPartEntries[EntryIndex];
            if (Entry.WetPartID == 0)
            {
                DefaultEntryIndex = EntryIndex;
                continue;
            }

            for (int32 IslandID : Entry.AssignedIslandIDs)
            {
                AssignedIslandToEntryIndex.FindOrAdd(IslandID) = EntryIndex;
            }
        }

        for (const FRuntimeIsland& Island : Islands)
        {
            const int32* AssignedEntryIndex = AssignedIslandToEntryIndex.Find(Island.IslandID);
            const int32 EffectiveEntryIndex = AssignedEntryIndex != nullptr ? *AssignedEntryIndex : DefaultEntryIndex;

            if (!WetPartEntries.IsValidIndex(EffectiveEntryIndex))
            {
                continue;
            }

            const FWetClothingAssetWetPartEntry& Entry = WetPartEntries[EffectiveEntryIndex];
            for (int32 VertexIndex : Island.VertexIndices)
            {
                if (!BakedRuntimeData.Vertices.IsValidIndex(VertexIndex))
                {
                    continue;
                }

                FWetClothingAssetBakedVertexData& VertexData = BakedRuntimeData.Vertices[VertexIndex];
                VertexData.WetPartID = Entry.WetPartID;
                VertexData.WetPartEntryIndex = EffectiveEntryIndex;
                VertexData.MaterialSlotIndex = ScopePair.Key.MaterialSlotIndex;
                VertexData.UVChannelIndex = ScopePair.Key.UVChannelIndex;
                VertexData.IslandID = Island.IslandID;
            }
        }
    }

    SetError(OutErrorMessage, TEXT(""));
    return true;
}
