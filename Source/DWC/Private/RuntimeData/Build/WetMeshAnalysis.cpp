
#include "RuntimeData/Build/WetMeshAnalysis.h"
#include "Runtime/Engine/Classes/Engine/SkeletalMesh.h"
#include "Runtime/Engine/Public/Rendering/SkeletalMeshLODRenderData.h"
#include "Runtime/Engine/Public/Rendering/SkeletalMeshRenderData.h"
#include "Utility/DWCError.h"

namespace DynamicWetMeshAnalysisInternal
{
    uint32 HashInt64(const int64 Value)
    {
        const uint64 UnsignedValue = static_cast<uint64>(Value);
        return HashCombine(
            ::GetTypeHash(static_cast<uint32>(UnsignedValue & 0xFFFFFFFFull)),
            ::GetTypeHash(static_cast<uint32>((UnsignedValue >> 32) & 0xFFFFFFFFull)));
    }

    bool LessUV(const FWetQuantizedUV& A, const FWetQuantizedUV& B)
    {
        return A.U != B.U ? A.U < B.U : A.V < B.V;
    }

    int32 FindParent(TArray<int32>& Parents, const int32 Index)
    {
        if (Parents[Index] == Index)
        {
            return Index;
        }

        Parents[Index] = FindParent(Parents, Parents[Index]);
        return Parents[Index];
    }

    void UnionParents(TArray<int32>& Parents, const int32 A, const int32 B)
    {
        const int32 RootA = FindParent(Parents, A);
        const int32 RootB = FindParent(Parents, B);

        if (RootA != RootB)
        {
            Parents[RootB] = RootA;
        }
    }

    double ComputeTriangleArea(const FVector2D& A, const FVector2D& B, const FVector2D& C)
    {
        return FMath::Abs((B.X - A.X) * (C.Y - A.Y) - (B.Y - A.Y) * (C.X - A.X)) * 0.5;
    }

    bool BuildRawTrianglesFromSkeletalMesh(
        const USkeletalMesh*    SkeletalMesh,
        const int32             LODIndex,
        const int32             UVChannelIndex,
        const int32             MaterialSlotIndex,
        TArray<FWetUVTriangle>& OutTriangles,
        FString*                OutErrorMessage)
    {
        OutTriangles.Reset();

        if (SkeletalMesh == nullptr)
        {
            FWetMeshAnalysis::SetError(OutErrorMessage, TEXT("No TargetMesh is assigned."));
            return false;
        }

        const FSkeletalMeshRenderData* RenderData = SkeletalMesh->GetResourceForRendering();
        if (RenderData == nullptr)
        {
            FWetMeshAnalysis::SetError(OutErrorMessage, TEXT("TargetMesh render data is unavailable."));
            return false;
        }

        if (!RenderData->LODRenderData.IsValidIndex(LODIndex))
        {
            FWetMeshAnalysis::SetError(OutErrorMessage, TEXT("LOD render data is unavailable."));
            return false;
        }

        if (!SkeletalMesh->GetMaterials().IsValidIndex(MaterialSlotIndex))
        {
            FWetMeshAnalysis::SetError(OutErrorMessage, TEXT("The selected material slot is no longer valid."));
            return false;
        }

        const FSkeletalMeshLODRenderData& LODData = RenderData->LODRenderData[LODIndex];
        const int32                       NumUVChannels = static_cast<int32>(LODData.GetNumTexCoords());

        if (NumUVChannels <= 0)
        {
            FWetMeshAnalysis::SetError(OutErrorMessage, TEXT("The TargetMesh does not contain any UV channels."));
            return false;
        }

        if (UVChannelIndex < 0 || UVChannelIndex >= NumUVChannels)
        {
            FWetMeshAnalysis::SetError(OutErrorMessage, TEXT("The selected UV channel is not available on this mesh."));
            return false;
        }

        TArray<uint32> IndexBuffer;
        LODData.MultiSizeIndexContainer.GetIndexBuffer(IndexBuffer);

        if (IndexBuffer.Num() == 0)
        {
            FWetMeshAnalysis::SetError(OutErrorMessage, TEXT("The TargetMesh index buffer is empty."));
            return false;
        }

        const int32 VertexCount = LODData.GetNumVertices();
        int32       TriangleID = 0;

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

                FWetUVTriangle Triangle;
                Triangle.TriangleID = TriangleID++;
                Triangle.MaterialSlotIndex = MaterialSlotIndex;
                Triangle.VertexIndices[0] = static_cast<int32>(Index0);
                Triangle.VertexIndices[1] = static_cast<int32>(Index1);
                Triangle.VertexIndices[2] = static_cast<int32>(Index2);
                Triangle.UVs[0] = FVector2D(LODData.StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV(Index0, UVChannelIndex));
                Triangle.UVs[1] = FVector2D(LODData.StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV(Index1, UVChannelIndex));
                Triangle.UVs[2] = FVector2D(LODData.StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV(Index2, UVChannelIndex));
                Triangle.LocalPositions[0] = FVector(LODData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(Index0));
                Triangle.LocalPositions[1] = FVector(LODData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(Index1));
                Triangle.LocalPositions[2] = FVector(LODData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(Index2));
                OutTriangles.Add(Triangle);
            }
        }

        FWetMeshAnalysis::SetError(OutErrorMessage, TEXT(""));
        return true;
    }

    void BuildIslandsFromRawTriangles(
        const TArray<FWetUVTriangle>& RawTriangles,
        TArray<FWetUVIsland>&         OutIslands)
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

        TMap<FWetUVEdgeKey, TArray<int32>> EdgeToTriangles;
        for (int32 TriangleIndex = 0; TriangleIndex < RawTriangles.Num(); ++TriangleIndex)
        {
            const FWetUVTriangle& Triangle = RawTriangles[TriangleIndex];
            EdgeToTriangles.FindOrAdd(FWetUVEdgeKey(Triangle.UVs[0], Triangle.UVs[1])).Add(TriangleIndex);
            EdgeToTriangles.FindOrAdd(FWetUVEdgeKey(Triangle.UVs[1], Triangle.UVs[2])).Add(TriangleIndex);
            EdgeToTriangles.FindOrAdd(FWetUVEdgeKey(Triangle.UVs[2], Triangle.UVs[0])).Add(TriangleIndex);
        }

        for (const TPair<FWetUVEdgeKey, TArray<int32>>& Pair : EdgeToTriangles)
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
            int32*      ExistingIslandIndex = RootToIslandArrayIndex.Find(Root);

            if (ExistingIslandIndex == nullptr)
            {
                FWetUVIsland Island;
                Island.MaterialSlotIndex = RawTriangles[TriangleIndex].MaterialSlotIndex;
                Island.UVIslandID = OutIslands.Num();
                Island.UVBounds = FBox2D(ForceInit);

                const int32 NewIslandIndex = OutIslands.Add(Island);
                RootToIslandArrayIndex.Add(Root, NewIslandIndex);
                ExistingIslandIndex = RootToIslandArrayIndex.Find(Root);
            }

            FWetUVIsland&  Island = OutIslands[*ExistingIslandIndex];
            FWetUVTriangle Triangle = RawTriangles[TriangleIndex];
            Triangle.UVIslandID = Island.UVIslandID;

            Island.TriangleCount++;
            Island.TriangleIDs.Add(Triangle.TriangleID);
            Island.UVTriangles.Add(Triangle);
            Island.UVBounds += Triangle.UVs[0];
            Island.UVBounds += Triangle.UVs[1];
            Island.UVBounds += Triangle.UVs[2];
            Island.UVArea += ComputeTriangleArea(Triangle.UVs[0], Triangle.UVs[1], Triangle.UVs[2]);
        }

        OutIslands.Sort(
            [](const FWetUVIsland& A, const FWetUVIsland& B)
            {
                return A.UVIslandID < B.UVIslandID;
            });
    }
} // namespace DynamicWetMeshAnalysisInternal

FWetQuantizedUV::FWetQuantizedUV(const FVector2D& UV)
    : U(FMath::RoundToInt64(UV.X * FWetMeshAnalysis::UVQuantizeScale)), V(FMath::RoundToInt64(UV.Y * FWetMeshAnalysis::UVQuantizeScale))
{
}

bool FWetQuantizedUV::operator==(const FWetQuantizedUV& Other) const
{
    return U == Other.U && V == Other.V;
}

bool FWetUVEdgeKey::operator==(const FWetUVEdgeKey& Other) const
{
    return A == Other.A && B == Other.B;
}

uint32 GetTypeHash(const FWetQuantizedUV& Value)
{
    return HashCombine(
        DynamicWetMeshAnalysisInternal::HashInt64(Value.U),
        DynamicWetMeshAnalysisInternal::HashInt64(Value.V));
}

FWetUVEdgeKey::FWetUVEdgeKey(const FVector2D& InA, const FVector2D& InB)
{
    FWetQuantizedUV QuantizedA(InA);
    FWetQuantizedUV QuantizedB(InB);

    if (DynamicWetMeshAnalysisInternal::LessUV(QuantizedB, QuantizedA))
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

uint32 GetTypeHash(const FWetUVEdgeKey& Key)
{
    return HashCombine(GetTypeHash(Key.A), GetTypeHash(Key.B));
}

void FWetMeshAnalysis::SetError(FString* OutErrorMessage, const TCHAR* InMessage)
{
    DWC::Error::SetMessage(OutErrorMessage, InMessage);
}

int32 FWetMeshAnalysis::GetNumUVChannels(const USkeletalMesh* SkeletalMesh, const int32 LODIndex)
{
    if (SkeletalMesh == nullptr)
    {
        return 0;
    }

    const FSkeletalMeshRenderData* RenderData = SkeletalMesh->GetResourceForRendering();
    if (RenderData == nullptr || !RenderData->LODRenderData.IsValidIndex(LODIndex))
    {
        return 0;
    }

    return static_cast<int32>(RenderData->LODRenderData[LODIndex].GetNumTexCoords());
}

bool FWetMeshAnalysis::BuildMaterialSlotUVIslands(
    const USkeletalMesh*  SkeletalMesh,
    const int32           LODIndex,
    const int32           UVChannelIndex,
    const int32           MaterialSlotIndex,
    TArray<FWetUVIsland>& OutIslands,
    FString*              OutErrorMessage)
{
    OutIslands.Reset();

    TArray<FWetUVTriangle> RawTriangles;
    const bool             bBuiltTriangles = DynamicWetMeshAnalysisInternal::BuildRawTrianglesFromSkeletalMesh(
        SkeletalMesh,
        LODIndex,
        UVChannelIndex,
        MaterialSlotIndex,
        RawTriangles,
        OutErrorMessage);

    if (!bBuiltTriangles)
    {
        return false;
    }

    DynamicWetMeshAnalysisInternal::BuildIslandsFromRawTriangles(RawTriangles, OutIslands);
    SetError(OutErrorMessage, TEXT(""));
    return true;
}

bool FWetMeshAnalysis::BuildUVIslandVertexMap(
    const TArray<FWetUVIsland>& Islands,
    TMap<int32, TArray<int32>>& OutIslandVertices)
{
    OutIslandVertices.Reset();

    TMap<int32, TSet<int32>> IslandVertexSets;
    for (const FWetUVIsland& Island : Islands)
    {
        TSet<int32>& VertexSet = IslandVertexSets.FindOrAdd(Island.UVIslandID);
        for (const FWetUVTriangle& Triangle : Island.UVTriangles)
        {
            for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
            {
                if (Triangle.VertexIndices[CornerIndex] != INDEX_NONE)
                {
                    VertexSet.Add(Triangle.VertexIndices[CornerIndex]);
                }
            }
        }
    }

    for (const TPair<int32, TSet<int32>>& Pair : IslandVertexSets)
    {
        TArray<int32>& IslandVertices = OutIslandVertices.FindOrAdd(Pair.Key);
        IslandVertices.Reserve(Pair.Value.Num());
        for (const int32 VertexIndex : Pair.Value)
        {
            IslandVertices.Add(VertexIndex);
        }
        IslandVertices.Sort();
    }

    return true;
}

bool FWetMeshAnalysis::BuildVertexIslandMembership(
    const TArray<FWetUVIsland>&         Islands,
    const int32                         VertexCount,
    const int32                         UVChannelIndex,
    TArray<FWetVertexIslandMembership>& OutVertexMembership)
{
    if (VertexCount < 0)
    {
        OutVertexMembership.Reset();
        return false;
    }

    OutVertexMembership.SetNum(VertexCount);

    for (FWetVertexIslandMembership& Membership : OutVertexMembership)
    {
        Membership = FWetVertexIslandMembership();
    }

    for (const FWetUVIsland& Island : Islands)
    {
        for (const FWetUVTriangle& Triangle : Island.UVTriangles)
        {
            for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
            {
                const int32 VertexIndex = Triangle.VertexIndices[CornerIndex];
                if (!OutVertexMembership.IsValidIndex(VertexIndex))
                {
                    continue;
                }

                FWetVertexIslandMembership& Membership = OutVertexMembership[VertexIndex];
                Membership.MaterialSlotIndex = Island.MaterialSlotIndex;
                Membership.UVChannelIndex = UVChannelIndex;
                Membership.UVIslandID = Island.UVIslandID;
            }
        }
    }

    return true;
}
