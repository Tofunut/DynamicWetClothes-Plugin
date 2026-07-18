#include "WetClothingAssetMeshAnalyzer.h"

#include "DataAssets/WetClothingAsset.h"
#include "Utility/DWCDataUVBufferView.h"
#include "Engine/SkeletalMesh.h"
#include "DataAssets/WetClothingAssetSetupData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Utility/DWCError.h"

namespace WetClothingAssetMeshAnalyzerInternal
{
    static constexpr double PositionQuantizeScale = 1000.0;
    static constexpr double UVQuantizeScale = 100000.0;

    struct FQuantizedUV
    {
        int64 U = 0;
        int64 V = 0;

        FQuantizedUV() = default;

        explicit FQuantizedUV(const FVector2D& InUV)
        {
            U = FMath::RoundToInt64(InUV.X * UVQuantizeScale);
            V = FMath::RoundToInt64(InUV.Y * UVQuantizeScale);
        }

        bool operator==(const FQuantizedUV& Other) const
        {
            return U == Other.U && V == Other.V;
        }
    };

    static uint32 HashInt64(int64 Value)
    {
        const uint64 UnsignedValue = static_cast<uint64>(Value);
        const uint32 Low = static_cast<uint32>(UnsignedValue & 0xFFFFFFFFull);
        const uint32 High = static_cast<uint32>((UnsignedValue >> 32) & 0xFFFFFFFFull);
        return HashCombine(::GetTypeHash(Low), ::GetTypeHash(High));
    }

    static uint32 GetTypeHash(const FQuantizedUV& Value)
    {
        return HashCombine(HashInt64(Value.U), HashInt64(Value.V));
    }

    static bool LessUV(const FQuantizedUV& A, const FQuantizedUV& B)
    {
        if (A.U != B.U)
        {
            return A.U < B.U;
        }

        return A.V < B.V;
    }

    struct FQuantizedPosition
    {
        int64 X = 0;
        int64 Y = 0;
        int64 Z = 0;

        FQuantizedPosition() = default;

        explicit FQuantizedPosition(const FVector& InPosition)
        {
            X = FMath::RoundToInt64(InPosition.X * PositionQuantizeScale);
            Y = FMath::RoundToInt64(InPosition.Y * PositionQuantizeScale);
            Z = FMath::RoundToInt64(InPosition.Z * PositionQuantizeScale);
        }

        bool operator==(const FQuantizedPosition& Other) const
        {
            return X == Other.X && Y == Other.Y && Z == Other.Z;
        }
    };

    static uint32 GetTypeHash(const FQuantizedPosition& Value)
    {
        return HashCombine(HashCombine(HashInt64(Value.X), HashInt64(Value.Y)), HashInt64(Value.Z));
    }

    struct FUVEdgeEndpointKey
    {
        FQuantizedPosition Position;
        FQuantizedUV UV;

        bool operator==(const FUVEdgeEndpointKey& Other) const
        {
            return Position == Other.Position && UV == Other.UV;
        }
    };

    static uint32 GetTypeHash(const FUVEdgeEndpointKey& Value)
    {
        return HashCombine(GetTypeHash(Value.Position), GetTypeHash(Value.UV));
    }

    static bool LessEndpoint(const FUVEdgeEndpointKey& A, const FUVEdgeEndpointKey& B)
    {
        if (A.Position.X != B.Position.X) return A.Position.X < B.Position.X;
        if (A.Position.Y != B.Position.Y) return A.Position.Y < B.Position.Y;
        if (A.Position.Z != B.Position.Z) return A.Position.Z < B.Position.Z;
        return LessUV(A.UV, B.UV);
    }

    struct FUVEdgeKey
    {
        FUVEdgeEndpointKey A;
        FUVEdgeEndpointKey B;

        FUVEdgeKey() = default;

        FUVEdgeKey(
            const FVector& InPositionA,
            const FVector2D& InUVA,
            const FVector& InPositionB,
            const FVector2D& InUVB)
        {
            FUVEdgeEndpointKey EndpointA{FQuantizedPosition(InPositionA), FQuantizedUV(InUVA)};
            FUVEdgeEndpointKey EndpointB{FQuantizedPosition(InPositionB), FQuantizedUV(InUVB)};

            if (LessEndpoint(EndpointB, EndpointA))
            {
                A = EndpointB;
                B = EndpointA;
            }
            else
            {
                A = EndpointA;
                B = EndpointB;
            }
        }

        bool operator==(const FUVEdgeKey& Other) const
        {
            return A == Other.A && B == Other.B;
        }
    };

    static uint32 GetTypeHash(const FUVEdgeKey& Key)
    {
        return HashCombine(GetTypeHash(Key.A), GetTypeHash(Key.B));
    }

    struct FRawTriangle
    {
        int32     TriangleID = INDEX_NONE;
        int32     MaterialSlotIndex = INDEX_NONE;
        FVector2D UVs[3];
        FVector   LocalPositions[3];
    };

    static int32 FindParent(TArray<int32>& Parents, int32 Index)
    {
        if (Parents[Index] == Index)
        {
            return Index;
        }

        Parents[Index] = FindParent(Parents, Parents[Index]);
        return Parents[Index];
    }

    static void UnionParents(TArray<int32>& Parents, int32 A, int32 B)
    {
        const int32 RootA = FindParent(Parents, A);
        const int32 RootB = FindParent(Parents, B);

        if (RootA != RootB)
        {
            Parents[RootB] = RootA;
        }
    }

    static double ComputeTriangleArea(const FVector2D& A, const FVector2D& B, const FVector2D& C)
    {
        return FMath::Abs((B.X - A.X) * (C.Y - A.Y) - (B.Y - A.Y) * (C.X - A.X)) * 0.5;
    }

    static bool BuildRawTrianglesFromSkeletalMesh(
        const USkeletalMesh*  SkeletalMesh,
        int32                 LODIndex,
        int32                 UVChannelIndex,
        int32                 MaterialSlotIndex,
        TArray<FRawTriangle>& OutTriangles,
        FString*              OutErrorMessage)
    {
        OutTriangles.Reset();

        if (SkeletalMesh == nullptr)
        {
            FWetClothingAssetMeshAnalyzer::SetError(OutErrorMessage, TEXT("No DWC Data UV is assigned."));
            return false;
        }

        const FSkeletalMeshRenderData* RenderData = SkeletalMesh->GetResourceForRendering();
        if (RenderData == nullptr)
        {
            FWetClothingAssetMeshAnalyzer::SetError(OutErrorMessage, TEXT("DWC Data UV render data is unavailable."));
            return false;
        }

        if (!RenderData->LODRenderData.IsValidIndex(LODIndex))
        {
            FWetClothingAssetMeshAnalyzer::SetError(OutErrorMessage, TEXT("LOD 0 render data is unavailable."));
            return false;
        }

        if (!SkeletalMesh->GetMaterials().IsValidIndex(MaterialSlotIndex))
        {
            FWetClothingAssetMeshAnalyzer::SetError(OutErrorMessage, TEXT("The selected material slot is no longer valid."));
            return false;
        }

        const FSkeletalMeshLODRenderData& LODData = RenderData->LODRenderData[LODIndex];
        const int32                       NumUVChannels = static_cast<int32>(LODData.GetNumTexCoords());

        if (NumUVChannels <= 0)
        {
            FWetClothingAssetMeshAnalyzer::SetError(OutErrorMessage, TEXT("The DWC Data UV does not contain any UV channels in LOD 0."));
            return false;
        }

        if (UVChannelIndex < 0 || UVChannelIndex >= NumUVChannels)
        {
            FWetClothingAssetMeshAnalyzer::SetError(OutErrorMessage, TEXT("The selected UV channel is not available on this mesh."));
            return false;
        }

        TArray<uint32> IndexBuffer;
        LODData.MultiSizeIndexContainer.GetIndexBuffer(IndexBuffer);

        if (IndexBuffer.Num() == 0)
        {
            FWetClothingAssetMeshAnalyzer::SetError(OutErrorMessage, TEXT("The DWC Data UV index buffer is empty."));
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

                FRawTriangle Triangle;
                Triangle.TriangleID = TriangleIndex / 3;
                Triangle.MaterialSlotIndex = MaterialSlotIndex;
                Triangle.UVs[0] = FVector2D(LODData.StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV(Index0, UVChannelIndex));
                Triangle.UVs[1] = FVector2D(LODData.StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV(Index1, UVChannelIndex));
                Triangle.UVs[2] = FVector2D(LODData.StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV(Index2, UVChannelIndex));
                Triangle.LocalPositions[0] = FVector(LODData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(Index0));
                Triangle.LocalPositions[1] = FVector(LODData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(Index1));
                Triangle.LocalPositions[2] = FVector(LODData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(Index2));
                OutTriangles.Add(Triangle);
            }
        }

        FWetClothingAssetMeshAnalyzer::SetError(OutErrorMessage, TEXT(""));
        return true;
    }

    static bool BuildRawTrianglesFromDataUV(
        const UWetClothingAsset& WetClothingAsset,
        int32                    LODIndex,
        int32                    MaterialSlotIndex,
        TArray<FRawTriangle>&    OutTriangles,
        FString*                 OutErrorMessage)
    {
        OutTriangles.Reset();

        const USkeletalMesh* RuntimeMesh = WetClothingAsset.GetRuntimeSkeletalMesh();
        if (RuntimeMesh == nullptr)
        {
            FWetClothingAssetMeshAnalyzer::SetError(OutErrorMessage, TEXT("No Source Skeletal Mesh is assigned."));
            return false;
        }

        if (!RuntimeMesh->GetMaterials().IsValidIndex(MaterialSlotIndex))
        {
            FWetClothingAssetMeshAnalyzer::SetError(OutErrorMessage, TEXT("The selected material slot is no longer valid."));
            return false;
        }

        const FDWCDataUVLODMetadata* DataUVMetadata = WetClothingAsset.FindDataUVMetadataForLOD(LODIndex);
        if (DataUVMetadata == nullptr || !DataUVMetadata->bIsValid)
        {
            FWetClothingAssetMeshAnalyzer::SetError(OutErrorMessage, TEXT("Generated DWC Data UV metadata is missing for the requested LOD."));
            return false;
        }

        const FSkeletalMeshRenderData* RenderData = RuntimeMesh->GetResourceForRendering();
        if (RenderData == nullptr || !RenderData->LODRenderData.IsValidIndex(LODIndex))
        {
            FWetClothingAssetMeshAnalyzer::SetError(OutErrorMessage, TEXT("Requested Source Mesh LOD render data is unavailable."));
            return false;
        }

        const FSkeletalMeshLODRenderData& LODData = RenderData->LODRenderData[LODIndex];
        const int32 VertexCount = static_cast<int32>(LODData.GetNumVertices());
        FDWCDataUVBufferView DataUVView;
        FString DataUVError;
        if (!DataUVView.Initialize(RuntimeMesh, LODIndex, WetClothingAsset.GetDWCDataUVChannelIndex(), &DataUVError) ||
            VertexCount <= 0 ||
            DataUVMetadata->RenderVertexCount != VertexCount ||
            DataUVView.NumVertices() != VertexCount)
        {
            FWetClothingAssetMeshAnalyzer::SetError(OutErrorMessage, *FString::Printf(TEXT("Generated DWC Data UV does not match render vertices. %s"), *DataUVError));
            return false;
        }

        const FString CurrentSignature = UWetClothingAsset::BuildMeshContentSignature(
            RuntimeMesh,
            LODIndex,
            WetClothingAsset.GetDWCDataUVChannelIndex());
        if (CurrentSignature.IsEmpty() || DataUVMetadata->DataUVOutputSignature != CurrentSignature)
        {
            FWetClothingAssetMeshAnalyzer::SetError(OutErrorMessage, TEXT("Generated DWC Data UV metadata is out of date."));
            return false;
        }

        TArray<uint32> IndexBuffer;
        LODData.MultiSizeIndexContainer.GetIndexBuffer(IndexBuffer);
        if (IndexBuffer.IsEmpty())
        {
            FWetClothingAssetMeshAnalyzer::SetError(OutErrorMessage, TEXT("Source Mesh index buffer is empty."));
            return false;
        }

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

                FRawTriangle Triangle;
                Triangle.TriangleID = TriangleIndex / 3;
                Triangle.MaterialSlotIndex = MaterialSlotIndex;
                Triangle.UVs[0] = FVector2D(DataUVView.GetUV(Index0));
                Triangle.UVs[1] = FVector2D(DataUVView.GetUV(Index1));
                Triangle.UVs[2] = FVector2D(DataUVView.GetUV(Index2));
                Triangle.LocalPositions[0] = FVector(LODData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(Index0));
                Triangle.LocalPositions[1] = FVector(LODData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(Index1));
                Triangle.LocalPositions[2] = FVector(LODData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(Index2));
                OutTriangles.Add(Triangle);
            }
        }

        FWetClothingAssetMeshAnalyzer::SetError(OutErrorMessage, TEXT(""));
        return true;
    }

    static void BuildIslandsFromRawTriangles(
        const TArray<FRawTriangle>&        RawTriangles,
        TArray<FWetClothingAssetUVIsland>& OutIslands)
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

        TMap<FUVEdgeKey, TArray<int32>> EdgeToTriangles;
        for (int32 TriangleIndex = 0; TriangleIndex < RawTriangles.Num(); ++TriangleIndex)
        {
            const FRawTriangle& Triangle = RawTriangles[TriangleIndex];
            EdgeToTriangles.FindOrAdd(FUVEdgeKey(
                Triangle.LocalPositions[0], Triangle.UVs[0],
                Triangle.LocalPositions[1], Triangle.UVs[1])).Add(TriangleIndex);
            EdgeToTriangles.FindOrAdd(FUVEdgeKey(
                Triangle.LocalPositions[1], Triangle.UVs[1],
                Triangle.LocalPositions[2], Triangle.UVs[2])).Add(TriangleIndex);
            EdgeToTriangles.FindOrAdd(FUVEdgeKey(
                Triangle.LocalPositions[2], Triangle.UVs[2],
                Triangle.LocalPositions[0], Triangle.UVs[0])).Add(TriangleIndex);
        }

        for (const TPair<FUVEdgeKey, TArray<int32>>& Pair : EdgeToTriangles)
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
                FWetClothingAssetUVIsland Island;
                Island.MaterialSlotIndex = RawTriangles[TriangleIndex].MaterialSlotIndex;
                Island.UVIslandID = OutIslands.Num();
                Island.UVBounds = FBox2D(ForceInit);

                const int32 NewIslandIndex = OutIslands.Add(Island);
                RootToIslandArrayIndex.Add(Root, NewIslandIndex);
                ExistingIslandIndex = RootToIslandArrayIndex.Find(Root);
            }

            FWetClothingAssetUVIsland& Island = OutIslands[*ExistingIslandIndex];
            const FRawTriangle&        RawTriangle = RawTriangles[TriangleIndex];

            FWetClothingAssetUVTriangle Triangle;
            Triangle.TriangleID = RawTriangle.TriangleID;
            Triangle.MaterialSlotIndex = RawTriangle.MaterialSlotIndex;
            Triangle.UVIslandID = Island.UVIslandID;
            Triangle.UVs[0] = RawTriangle.UVs[0];
            Triangle.UVs[1] = RawTriangle.UVs[1];
            Triangle.UVs[2] = RawTriangle.UVs[2];
            Triangle.LocalPositions[0] = RawTriangle.LocalPositions[0];
            Triangle.LocalPositions[1] = RawTriangle.LocalPositions[1];
            Triangle.LocalPositions[2] = RawTriangle.LocalPositions[2];

            Island.TriangleCount++;
            Island.TriangleIDs.Add(Triangle.TriangleID);
            Island.UVTriangles.Add(Triangle);
            Island.UVBounds += Triangle.UVs[0];
            Island.UVBounds += Triangle.UVs[1];
            Island.UVBounds += Triangle.UVs[2];
            Island.UVArea += ComputeTriangleArea(Triangle.UVs[0], Triangle.UVs[1], Triangle.UVs[2]);
        }

        OutIslands.Sort(
            [](const FWetClothingAssetUVIsland& A, const FWetClothingAssetUVIsland& B)
            {
                return A.UVIslandID < B.UVIslandID;
            });
    }
} // namespace WetClothingAssetMeshAnalyzerInternal

void FWetClothingAssetMeshAnalyzer::SetError(FString* OutErrorMessage, const TCHAR* InMessage)
{
    DWC::Error::SetMessage(OutErrorMessage, InMessage);
}

int32 FWetClothingAssetMeshAnalyzer::GetNumUVChannels(const USkeletalMesh* SkeletalMesh, int32 LODIndex)
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

bool FWetClothingAssetMeshAnalyzer::BuildMaterialSlotUVIslands(
    const USkeletalMesh*               SkeletalMesh,
    int32                              LODIndex,
    int32                              UVChannelIndex,
    int32                              MaterialSlotIndex,
    TArray<FWetClothingAssetUVIsland>& OutIslands,
    FString*                           OutErrorMessage)
{
    OutIslands.Reset();

    TArray<WetClothingAssetMeshAnalyzerInternal::FRawTriangle> RawTriangles;
    const bool                                                 bBuiltTriangles = WetClothingAssetMeshAnalyzerInternal::BuildRawTrianglesFromSkeletalMesh(
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

    WetClothingAssetMeshAnalyzerInternal::BuildIslandsFromRawTriangles(RawTriangles, OutIslands);
    SetError(OutErrorMessage, TEXT(""));
    return true;
}

bool FWetClothingAssetMeshAnalyzer::BuildMaterialSlotDataUVIslands(
    const UWetClothingAsset&           WetClothingAsset,
    const int32                        LODIndex,
    const int32                        MaterialSlotIndex,
    TArray<FWetClothingAssetUVIsland>& OutIslands,
    FString*                           OutErrorMessage)
{
    OutIslands.Reset();

    TArray<WetClothingAssetMeshAnalyzerInternal::FRawTriangle> RawTriangles;
    if (!WetClothingAssetMeshAnalyzerInternal::BuildRawTrianglesFromDataUV(
            WetClothingAsset,
            LODIndex,
            MaterialSlotIndex,
            RawTriangles,
            OutErrorMessage))
    {
        return false;
    }

    WetClothingAssetMeshAnalyzerInternal::BuildIslandsFromRawTriangles(RawTriangles, OutIslands);
    SetError(OutErrorMessage, TEXT(""));
    return true;
}

bool FWetClothingAssetMeshAnalyzer::BuildMaterialSlotUVIslandsFromTopology(
    const USkeletalMesh*                        SkeletalMesh,
    int32                                       LODIndex,
    int32                                       UVChannelIndex,
    int32                                       MaterialSlotIndex,
    const TArray<FDWCOriginalUVIslandTopology>& Topology,
    TArray<FWetClothingAssetUVIsland>&          OutIslands,
    FString*                                    OutErrorMessage)
{
    using namespace WetClothingAssetMeshAnalyzerInternal;

    OutIslands.Reset();

    TArray<FRawTriangle> RawTriangles;
    if (!BuildRawTrianglesFromSkeletalMesh(
            SkeletalMesh,
            LODIndex,
            UVChannelIndex,
            MaterialSlotIndex,
            RawTriangles,
            OutErrorMessage))
    {
        return false;
    }

    TMap<int32, int32> RawIndexByTriangleID;
    RawIndexByTriangleID.Reserve(RawTriangles.Num());
    for (int32 RawIndex = 0; RawIndex < RawTriangles.Num(); ++RawIndex)
    {
        RawIndexByTriangleID.Add(RawTriangles[RawIndex].TriangleID, RawIndex);
    }

    for (const FDWCOriginalUVIslandTopology& TopologyIsland : Topology)
    {
        if (TopologyIsland.MaterialSlotIndex != MaterialSlotIndex)
        {
            continue;
        }

        FWetClothingAssetUVIsland Island;
        Island.MaterialSlotIndex = MaterialSlotIndex;
        Island.UVIslandID = TopologyIsland.IslandID;
        Island.UVBounds = FBox2D(ForceInit);

        for (const int32 TriangleID : TopologyIsland.TriangleIndices)
        {
            const int32* RawIndex = RawIndexByTriangleID.Find(TriangleID);
            if (RawIndex == nullptr || !RawTriangles.IsValidIndex(*RawIndex))
            {
                continue;
            }

            const FRawTriangle& RawTriangle = RawTriangles[*RawIndex];
            FWetClothingAssetUVTriangle Triangle;
            Triangle.TriangleID = RawTriangle.TriangleID;
            Triangle.MaterialSlotIndex = RawTriangle.MaterialSlotIndex;
            Triangle.UVIslandID = Island.UVIslandID;
            for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
            {
                Triangle.UVs[CornerIndex] = RawTriangle.UVs[CornerIndex];
                Triangle.LocalPositions[CornerIndex] = RawTriangle.LocalPositions[CornerIndex];
                Island.UVBounds += Triangle.UVs[CornerIndex];
            }

            Island.TriangleIDs.Add(Triangle.TriangleID);
            Island.UVTriangles.Add(Triangle);
            Island.UVArea += ComputeTriangleArea(Triangle.UVs[0], Triangle.UVs[1], Triangle.UVs[2]);
        }

        Island.TriangleCount = Island.UVTriangles.Num();
        if (Island.TriangleCount > 0)
        {
            OutIslands.Add(MoveTemp(Island));
        }
    }

    OutIslands.Sort([](const FWetClothingAssetUVIsland& A, const FWetClothingAssetUVIsland& B)
    {
        return A.UVIslandID < B.UVIslandID;
    });

    if (OutIslands.IsEmpty() && !RawTriangles.IsEmpty())
    {
        SetError(OutErrorMessage, TEXT("Stored WCA island topology does not match the DWC Data UV."));
        return false;
    }

    SetError(OutErrorMessage, TEXT(""));
    return true;
}
