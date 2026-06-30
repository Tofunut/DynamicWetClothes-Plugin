#include "WetClothingProfile.h"

#include "DynamicWet/DynamicWetMeshAnalysis.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"

namespace WetClothingProfileRuntimeData
{
    void SetError(FString* OutErrorMessage, const TCHAR* Message)
    {
        if (OutErrorMessage != nullptr)
        {
            *OutErrorMessage = Message;
        }
    }

    bool GetLODData(
        const USkeletalMesh* SkeletalMesh,
        const int32 LODIndex,
        const FSkeletalMeshLODRenderData*& OutLODData,
        FString* OutErrorMessage)
    {
        OutLODData = nullptr;

        if (SkeletalMesh == nullptr)
        {
            SetError(OutErrorMessage, TEXT("No TargetMesh is assigned."));
            return false;
        }

        const FSkeletalMeshRenderData* RenderData = SkeletalMesh->GetResourceForRendering();
        if (RenderData == nullptr || !RenderData->LODRenderData.IsValidIndex(LODIndex))
        {
            SetError(OutErrorMessage, TEXT("TargetMesh render data is unavailable."));
            return false;
        }

        OutLODData = &RenderData->LODRenderData[LODIndex];
        return true;
    }

    FString BuildMeshSignature(const USkeletalMesh* SkeletalMesh, const int32 LODIndex)
    {
        const FSkeletalMeshLODRenderData* LODData = nullptr;
        if (!GetLODData(SkeletalMesh, LODIndex, LODData, nullptr) || LODData == nullptr)
        {
            return FString();
        }

        const FRawStaticIndexBuffer16or32Interface* IndexBuffer =
            LODData->MultiSizeIndexContainer.GetIndexBuffer();

        return FString::Printf(
            TEXT("%s|LOD=%d|Vertices=%d|Indices=%d|Sections=%d|Materials=%d|UVs=%d"),
            *SkeletalMesh->GetPathName(),
            LODIndex,
            LODData->GetNumVertices(),
            IndexBuffer != nullptr ? IndexBuffer->Num() : 0,
            LODData->RenderSections.Num(),
            SkeletalMesh->GetMaterials().Num(),
            static_cast<int32>(LODData->GetNumTexCoords()));
    }

    void AddNeighbor(
        TArray<FWetClothingProfileBakedVertexNeighbors>& NeighborGraph,
        const int32 VertexIndex,
        const int32 NeighborIndex)
    {
        if (!NeighborGraph.IsValidIndex(VertexIndex) ||
            !NeighborGraph.IsValidIndex(NeighborIndex) ||
            VertexIndex == NeighborIndex)
        {
            return;
        }

        TArray<int32>& Neighbors = NeighborGraph[VertexIndex].Neighbors;
        if (!Neighbors.Contains(NeighborIndex))
        {
            Neighbors.Add(NeighborIndex);
        }
    }

    bool BuildNeighborGraph(
        const FSkeletalMeshLODRenderData& LODData,
        TArray<FWetClothingProfileBakedVertexNeighbors>& OutNeighborGraph)
    {
        const int32 VertexCount = LODData.GetNumVertices();
        OutNeighborGraph.Reset();
        OutNeighborGraph.SetNum(VertexCount);

        const FRawStaticIndexBuffer16or32Interface* IndexBuffer =
            LODData.MultiSizeIndexContainer.GetIndexBuffer();

        if (IndexBuffer == nullptr)
        {
            return false;
        }

        const int32 IndexCount = IndexBuffer->Num();
        for (int32 Index = 0; Index + 2 < IndexCount; Index += 3)
        {
            const int32 V0 = IndexBuffer->Get(Index);
            const int32 V1 = IndexBuffer->Get(Index + 1);
            const int32 V2 = IndexBuffer->Get(Index + 2);

            AddNeighbor(OutNeighborGraph, V0, V1);
            AddNeighbor(OutNeighborGraph, V1, V0);

            AddNeighbor(OutNeighborGraph, V1, V2);
            AddNeighbor(OutNeighborGraph, V2, V1);

            AddNeighbor(OutNeighborGraph, V2, V0);
            AddNeighbor(OutNeighborGraph, V0, V2);
        }

        for (FWetClothingProfileBakedVertexNeighbors& VertexNeighbors : OutNeighborGraph)
        {
            VertexNeighbors.Neighbors.Sort();
        }

        return true;
    }
} // namespace WetClothingProfileRuntimeData

void UWetClothingProfile::ClearRuntimeData()
{
    BakedRuntimeData = FWetClothingProfileBakedRuntimeData();
}

bool UWetClothingProfile::IsRuntimeDataValidForMesh(const USkeletalMesh* SkeletalMesh, const int32 LODIndex) const
{
    if (!BakedRuntimeData.bIsValid ||
        BakedRuntimeData.LODIndex != LODIndex ||
        SkeletalMesh == nullptr)
    {
        return false;
    }

    return BakedRuntimeData.MeshBuildSignature == WetClothingProfileRuntimeData::BuildMeshSignature(SkeletalMesh, LODIndex);
}

bool UWetClothingProfile::RebuildRuntimeData(FString* OutErrorMessage, const int32 LODIndex)
{
    ClearRuntimeData();

    const FSkeletalMeshLODRenderData* LODData = nullptr;
    if (!WetClothingProfileRuntimeData::GetLODData(TargetMesh, LODIndex, LODData, OutErrorMessage) || LODData == nullptr)
    {
        return false;
    }

    const int32 VertexCount = LODData->GetNumVertices();
    BakedRuntimeData.LODIndex = LODIndex;
    BakedRuntimeData.VertexCount = VertexCount;
    BakedRuntimeData.MeshBuildSignature = WetClothingProfileRuntimeData::BuildMeshSignature(TargetMesh, LODIndex);
    BakedRuntimeData.Vertices.SetNum(VertexCount);

    for (FWetClothingProfileBakedVertexData& VertexData : BakedRuntimeData.Vertices)
    {
        VertexData = FWetClothingProfileBakedVertexData();
    }

    TMap<FIntPoint, TArray<int32>> WetPartEntryIndicesByScope;
    for (int32 EntryIndex = 0; EntryIndex < WetPartEntries.Num(); ++EntryIndex)
    {
        const FWetClothingProfileWetPartEntry& WetPartEntry = WetPartEntries[EntryIndex];
        if (WetPartEntry.MaterialSlotIndex == INDEX_NONE ||
            WetPartEntry.UVChannelIndex < 0 ||
            WetPartEntry.AssignedIslandIDs.Num() == 0)
        {
            continue;
        }

        WetPartEntryIndicesByScope.FindOrAdd(FIntPoint(WetPartEntry.MaterialSlotIndex, WetPartEntry.UVChannelIndex)).Add(EntryIndex);
    }

    for (const TPair<FIntPoint, TArray<int32>>& ScopePair : WetPartEntryIndicesByScope)
    {
        const int32 MaterialSlotIndex = ScopePair.Key.X;
        const int32 UVChannelIndex = ScopePair.Key.Y;

        TArray<FDynamicWetUVIsland> Islands;
        FString ErrorMessage;
        if (!FDynamicWetMeshAnalysis::BuildMaterialSlotUVIslands(
                TargetMesh,
                LODIndex,
                UVChannelIndex,
                MaterialSlotIndex,
                Islands,
                &ErrorMessage))
        {
            WetClothingProfileRuntimeData::SetError(OutErrorMessage, *ErrorMessage);
            return false;
        }

        TArray<FDynamicWetVertexIslandMembership> VertexMembership;
        if (!FDynamicWetMeshAnalysis::BuildVertexIslandMembership(
                Islands,
                VertexCount,
                UVChannelIndex,
                VertexMembership))
        {
            WetClothingProfileRuntimeData::SetError(OutErrorMessage, TEXT("Failed to build vertex island membership."));
            return false;
        }

        TMap<int32, int32> IslandToWetPartEntryIndex;
        for (const int32 WetPartEntryIndex : ScopePair.Value)
        {
            if (!WetPartEntries.IsValidIndex(WetPartEntryIndex))
            {
                continue;
            }

            const FWetClothingProfileWetPartEntry& WetPartEntry = WetPartEntries[WetPartEntryIndex];
            for (const int32 IslandID : WetPartEntry.AssignedIslandIDs)
            {
                IslandToWetPartEntryIndex.Add(IslandID, WetPartEntryIndex);
            }
        }

        for (int32 VertexIndex = 0; VertexIndex < VertexMembership.Num(); ++VertexIndex)
        {
            const FDynamicWetVertexIslandMembership& Membership = VertexMembership[VertexIndex];
            if (Membership.IslandID == INDEX_NONE)
            {
                continue;
            }

            const int32* WetPartEntryIndex = IslandToWetPartEntryIndex.Find(Membership.IslandID);
            if (WetPartEntryIndex == nullptr ||
                !WetPartEntries.IsValidIndex(*WetPartEntryIndex) ||
                !BakedRuntimeData.Vertices.IsValidIndex(VertexIndex))
            {
                continue;
            }

            const FWetClothingProfileWetPartEntry& WetPartEntry = WetPartEntries[*WetPartEntryIndex];
            FWetClothingProfileBakedVertexData& VertexData = BakedRuntimeData.Vertices[VertexIndex];
            VertexData.WetPartID = WetPartEntry.WetPartID;
            VertexData.WetPartEntryIndex = *WetPartEntryIndex;
            VertexData.MaterialSlotIndex = Membership.MaterialSlotIndex;
            VertexData.UVChannelIndex = Membership.UVChannelIndex;
            VertexData.IslandID = Membership.IslandID;
        }
    }

    if (!WetClothingProfileRuntimeData::BuildNeighborGraph(*LODData, BakedRuntimeData.NeighborGraph))
    {
        WetClothingProfileRuntimeData::SetError(OutErrorMessage, TEXT("Failed to build runtime neighbor graph."));
        ClearRuntimeData();
        return false;
    }

    BakedRuntimeData.bIsValid =
        BakedRuntimeData.Vertices.Num() == VertexCount &&
        BakedRuntimeData.NeighborGraph.Num() == VertexCount;

    WetClothingProfileRuntimeData::SetError(OutErrorMessage, TEXT(""));
    return BakedRuntimeData.bIsValid;
}
