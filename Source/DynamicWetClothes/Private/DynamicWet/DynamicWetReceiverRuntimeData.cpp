// Fill out your copyright notice in the Description page of Project Settings.

#include "DynamicWet/DynamicWetReceiverRuntimeData.h"

#include "Runtime/Engine/Classes/Components/SkeletalMeshComponent.h"
#include "DynamicWet/DynamicWetReceiverContext.h"
#include "DynamicWet/DynamicWetMeshAnalysis.h"
#include "DynamicWet/DynamicWetReceiverMeshSampler.h"
#include "DynamicWet/DynamicWetReceiverRenderApplier.h"
#include "DynamicWet/DynamicWetReceiverSimulationState.h"
#include "Runtime/Engine/Classes/Engine/SkeletalMesh.h"
#include "Runtime/Engine/Public/Rendering/SkeletalMeshLODRenderData.h"
#include "Runtime/Engine/Public/Rendering/SkeletalMeshRenderData.h"
#include "WetClothingProfile.h"
#include "WetnessProfile.h"
#include "Runtime/Engine/Public/RawIndexBuffer.h"


void FDynamicWetReceiverRuntimeData::ResetWetPartData()
{
    VertexWetPartIDs.Reset();
    VertexWetnessProfileParameters.Reset();
    VertexWetPartDebugColors.Reset();
}

void FDynamicWetReceiverRuntimeData::ResetNeighborGraph()
{
    NeighborGraph.Reset();
}

void FDynamicWetReceiverRuntimeDataBuilder::InitializeWetnessData(FDynamicWetReceiverContext& Receiver)
{
    FSkeletalMeshLODRenderData* LODData = nullptr;
    if (!GetLODRenderData(Receiver, 0, LODData))
    {
        return;
    }

    const int32 VertexCount = LODData->GetNumVertices();

    Receiver.SimulationState.WetnessPerVertex.SetNumZeroed(VertexCount);
    Receiver.SimulationState.Updating_Pending_Wetness_Amounts.SetNumZeroed(VertexCount);
    Receiver.SimulationState.WetnessDryHoldTimePerVertex.SetNumZeroed(VertexCount);
    Receiver.SimulationState.Updating_Pending_Wetness_Vertex_IndexQueue.Reset();
    Receiver.SimulationState.Current_Pending_Wetness_Vertex_IndexQueue.Reset();
    Receiver.SimulationState.Current_Pending_Wetness_Amounts.Reset();
    Receiver.SimulationState.bPendingWetnessQueued.Init(false, VertexCount);
    Receiver.RenderApplier.CachedWetVertexColors.Init(FLinearColor::Black, VertexCount);
    Receiver.SimulationState.DirtyWetVertexIndices.Reset();

    Receiver.TargetSkeletalMesh->SetVertexColorOverride_LinearColor(0, Receiver.RenderApplier.CachedWetVertexColors);
    Receiver.TargetSkeletalMesh->MarkRenderStateDirty();
}

void FDynamicWetReceiverRuntimeDataBuilder::InitializeWetPartVertexData(FDynamicWetReceiverContext& Receiver)
{
    FSkeletalMeshLODRenderData* LODData = nullptr;
    if (!GetLODRenderData(Receiver, 0, LODData))
    {
        return;
    }

    const int32 VertexCount = LODData->GetNumVertices();
    Receiver.RuntimeData.VertexWetPartIDs.Init(INDEX_NONE, VertexCount);
    Receiver.RuntimeData.VertexWetnessProfileParameters.SetNum(VertexCount);
    Receiver.RuntimeData.VertexWetPartDebugColors.Init(Receiver.UnassignedWetPartDebugColor, VertexCount);

    FWetnessProfileParameters DefaultParameters;
    if (const UWetnessProfile* MaterialPreset = Receiver.GetActiveMaterialProfile())
    {
        DefaultParameters = MaterialPreset->GetParameters();
    }

    for (FWetnessProfileParameters& VertexParameters : Receiver.RuntimeData.VertexWetnessProfileParameters)
    {
        VertexParameters = DefaultParameters;
    }

    if (!Receiver.WetClothingProfile)
    {
        return;
    }

    if (InitializeWetPartVertexDataFromBakedProfile(Receiver, VertexCount, DefaultParameters))
    {
        return;
    }

    USkeletalMesh* SkeletalMesh = Receiver.TargetSkeletalMesh ? Receiver.TargetSkeletalMesh->GetSkeletalMeshAsset() : nullptr;
    if (Receiver.WetClothingProfile->TargetMesh && Receiver.WetClothingProfile->TargetMesh != SkeletalMesh)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("DynamicWetReceiverComponent: WetClothingProfile TargetMesh does not match the receiver mesh on %s."),
            *GetNameSafe(Receiver.OwnerForLogs));
    }

    TMap<FIntPoint, TArray<int32>> WetPartEntryIndicesByScope;
    for (int32 EntryIndex = 0; EntryIndex < Receiver.WetClothingProfile->WetPartEntries.Num(); ++EntryIndex)
    {
        const FWetClothingProfileWetPartEntry& WetPartEntry = Receiver.WetClothingProfile->WetPartEntries[EntryIndex];
        if (WetPartEntry.MaterialSlotIndex == INDEX_NONE ||
            WetPartEntry.UVChannelIndex < 0 ||
            WetPartEntry.AssignedIslandIDs.Num() == 0)
        {
            continue;
        }

        const FIntPoint CacheKey(WetPartEntry.MaterialSlotIndex, WetPartEntry.UVChannelIndex);
        WetPartEntryIndicesByScope.FindOrAdd(CacheKey).Add(EntryIndex);
    }

    for (const TPair<FIntPoint, TArray<int32>>& ScopePair : WetPartEntryIndicesByScope)
    {
        const int32 MaterialSlotIndex = ScopePair.Key.X;
        const int32 UVChannelIndex = ScopePair.Key.Y;

        TArray<FDynamicWetUVIsland> Islands;
        if (!FDynamicWetMeshAnalysis::BuildMaterialSlotUVIslands(
                SkeletalMesh,
                0,
                UVChannelIndex,
                MaterialSlotIndex,
                Islands))
        {
            continue;
        }

        TArray<FDynamicWetVertexIslandMembership> VertexMembership;
        if (!FDynamicWetMeshAnalysis::BuildVertexIslandMembership(
                Islands,
                VertexCount,
                UVChannelIndex,
                VertexMembership))
        {
            continue;
        }

        TMap<int32, int32> IslandToWetPartEntryIndex;
        for (const int32 WetPartEntryIndex : ScopePair.Value)
        {
            if (!Receiver.WetClothingProfile->WetPartEntries.IsValidIndex(WetPartEntryIndex))
            {
                continue;
            }

            const FWetClothingProfileWetPartEntry& WetPartEntry = Receiver.WetClothingProfile->WetPartEntries[WetPartEntryIndex];
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
                !Receiver.WetClothingProfile->WetPartEntries.IsValidIndex(*WetPartEntryIndex) ||
                !Receiver.RuntimeData.VertexWetPartIDs.IsValidIndex(VertexIndex) ||
                !Receiver.RuntimeData.VertexWetnessProfileParameters.IsValidIndex(VertexIndex))
            {
                continue;
            }

            const FWetClothingProfileWetPartEntry& WetPartEntry = Receiver.WetClothingProfile->WetPartEntries[*WetPartEntryIndex];
            Receiver.RuntimeData.VertexWetPartIDs[VertexIndex] = WetPartEntry.WetPartID;
            Receiver.RuntimeData.VertexWetnessProfileParameters[VertexIndex] = WetPartEntry.ProfileAssignment.Parameters;
            Receiver.RuntimeData.VertexWetPartDebugColors[VertexIndex] = WetPartEntry.Color;
        }
    }
}

bool FDynamicWetReceiverRuntimeDataBuilder::InitializeWetPartVertexDataFromBakedProfile(
    FDynamicWetReceiverContext& Receiver,
    const int32 VertexCount,
    const FWetnessProfileParameters& DefaultParameters)
{
    if (!Receiver.WetClothingProfile || !Receiver.TargetSkeletalMesh)
    {
        return false;
    }

    const USkeletalMesh* SkeletalMesh = Receiver.TargetSkeletalMesh->GetSkeletalMeshAsset();
    if (!Receiver.WetClothingProfile->IsRuntimeDataValidForMesh(SkeletalMesh, 0))
    {
        if (Receiver.WetClothingProfile->GetBakedRuntimeData().bIsValid)
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("DynamicWetReceiverComponent: WetClothingProfile runtime data is stale for %s. Falling back to runtime UV analysis."),
                *GetNameSafe(Receiver.OwnerForLogs));
        }
        return false;
    }

    const FWetClothingProfileBakedRuntimeData& BakedData = Receiver.WetClothingProfile->GetBakedRuntimeData();
    if (BakedData.VertexCount != VertexCount || BakedData.Vertices.Num() != VertexCount)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("DynamicWetReceiverComponent: WetClothingProfile runtime data vertex count mismatch on %s. Falling back to runtime UV analysis."),
            *GetNameSafe(Receiver.OwnerForLogs));
        return false;
    }

    for (int32 VertexIndex = 0; VertexIndex < BakedData.Vertices.Num(); ++VertexIndex)
    {
        const FWetClothingProfileBakedVertexData& BakedVertex = BakedData.Vertices[VertexIndex];
        if (!Receiver.RuntimeData.VertexWetPartIDs.IsValidIndex(VertexIndex) ||
            !Receiver.RuntimeData.VertexWetnessProfileParameters.IsValidIndex(VertexIndex) ||
            !Receiver.RuntimeData.VertexWetPartDebugColors.IsValidIndex(VertexIndex))
        {
            continue;
        }

        Receiver.RuntimeData.VertexWetPartIDs[VertexIndex] = BakedVertex.WetPartID;
        Receiver.RuntimeData.VertexWetnessProfileParameters[VertexIndex] = DefaultParameters;
        Receiver.RuntimeData.VertexWetPartDebugColors[VertexIndex] = Receiver.UnassignedWetPartDebugColor;

        if (Receiver.WetClothingProfile->WetPartEntries.IsValidIndex(BakedVertex.WetPartEntryIndex))
        {
            const FWetClothingProfileWetPartEntry& WetPartEntry =
                Receiver.WetClothingProfile->WetPartEntries[BakedVertex.WetPartEntryIndex];
            Receiver.RuntimeData.VertexWetnessProfileParameters[VertexIndex] = WetPartEntry.ProfileAssignment.Parameters;
            Receiver.RuntimeData.VertexWetPartDebugColors[VertexIndex] = WetPartEntry.Color;
        }
    }

    return true;
}

void FDynamicWetReceiverRuntimeDataBuilder::BuildNeighborGraph(FDynamicWetReceiverContext& Receiver)
{
    FSkeletalMeshLODRenderData* LODData = nullptr;
    if (!GetLODRenderData(Receiver, 0, LODData))
    {
        return;
    }

    const int32 VertexCount = LODData->GetNumVertices();

    if (Receiver.RuntimeData.NeighborGraph.Num() != VertexCount)
    {
        Receiver.RuntimeData.NeighborGraph.Empty();
        Receiver.RuntimeData.NeighborGraph.SetNum(VertexCount);
    }

    if (BuildNeighborGraphFromBakedProfile(Receiver, VertexCount))
    {
        return;
    }

    for (FDynamicWetReceiverVertexNeighbors& VertexNeighbors : Receiver.RuntimeData.NeighborGraph)
    {
        VertexNeighbors.Neighbors.Reset();
    }

    const FRawStaticIndexBuffer16or32Interface* IndexBuffer =
        LODData->MultiSizeIndexContainer.GetIndexBuffer();

    if (!IndexBuffer)
    {
        return;
    }

    const int32 IndexCount = IndexBuffer->Num();
    for (int32 Index = 0; Index + 2 < IndexCount; Index += 3)
    {
        const int32 V0 = IndexBuffer->Get(Index);
        const int32 V1 = IndexBuffer->Get(Index + 1);
        const int32 V2 = IndexBuffer->Get(Index + 2);

        AddNeighbor(Receiver, V0, V1);
        AddNeighbor(Receiver, V1, V0);

        AddNeighbor(Receiver, V1, V2);
        AddNeighbor(Receiver, V2, V1);

        AddNeighbor(Receiver, V2, V0);
        AddNeighbor(Receiver, V0, V2);
    }
}

bool FDynamicWetReceiverRuntimeDataBuilder::BuildNeighborGraphFromBakedProfile(
    FDynamicWetReceiverContext& Receiver,
    const int32 VertexCount)
{
    if (!Receiver.WetClothingProfile || !Receiver.TargetSkeletalMesh)
    {
        return false;
    }

    const USkeletalMesh* SkeletalMesh = Receiver.TargetSkeletalMesh->GetSkeletalMeshAsset();
    if (!Receiver.WetClothingProfile->IsRuntimeDataValidForMesh(SkeletalMesh, 0))
    {
        return false;
    }

    const FWetClothingProfileBakedRuntimeData& BakedData = Receiver.WetClothingProfile->GetBakedRuntimeData();
    if (BakedData.NeighborGraph.Num() != VertexCount || Receiver.RuntimeData.NeighborGraph.Num() != VertexCount)
    {
        return false;
    }

    for (int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
    {
        Receiver.RuntimeData.NeighborGraph[VertexIndex].Neighbors = BakedData.NeighborGraph[VertexIndex].Neighbors;
    }

    return true;
}

void FDynamicWetReceiverRuntimeDataBuilder::AddNeighbor(
    FDynamicWetReceiverContext& Receiver,
    int32 VertexIndex,
    int32 NeighborIndex)
{
    if (!Receiver.RuntimeData.NeighborGraph.IsValidIndex(VertexIndex))
    {
        return;
    }

    if (!Receiver.RuntimeData.NeighborGraph.IsValidIndex(NeighborIndex))
    {
        return;
    }

    if (VertexIndex == NeighborIndex)
    {
        return;
    }

    TArray<int32>& Neighbors = Receiver.RuntimeData.NeighborGraph[VertexIndex].Neighbors;

    if (!Neighbors.Contains(NeighborIndex))
    {
        Neighbors.Add(NeighborIndex);
    }
}

void FDynamicWetReceiverRuntimeDataBuilder::EnsureWetnessBufferSize(FDynamicWetReceiverContext& Receiver, const int32 VertexCount)
{
    if (VertexCount <= 0)
    {
        Receiver.SimulationState.WetnessPerVertex.Reset();
        Receiver.RuntimeData.VertexWetPartIDs.Reset();
        Receiver.RuntimeData.VertexWetnessProfileParameters.Reset();
        Receiver.RuntimeData.VertexWetPartDebugColors.Reset();
        Receiver.SimulationState.Updating_Pending_Wetness_Amounts.Reset();
        Receiver.SimulationState.WetnessDryHoldTimePerVertex.Reset();
        Receiver.SimulationState.Updating_Pending_Wetness_Vertex_IndexQueue.Reset();
        Receiver.SimulationState.Current_Pending_Wetness_Vertex_IndexQueue.Reset();
        Receiver.SimulationState.Current_Pending_Wetness_Amounts.Reset();
        Receiver.SimulationState.bPendingWetnessQueued.Reset();
        return;
    }

    if (Receiver.SimulationState.WetnessPerVertex.Num() != VertexCount)
    {
        Receiver.SimulationState.WetnessPerVertex.SetNumZeroed(VertexCount);
    }

    if (Receiver.RuntimeData.VertexWetPartIDs.Num() != VertexCount ||
        Receiver.RuntimeData.VertexWetnessProfileParameters.Num() != VertexCount ||
        Receiver.RuntimeData.VertexWetPartDebugColors.Num() != VertexCount)
    {
        InitializeWetPartVertexData(Receiver);
    }

    if (Receiver.SimulationState.Updating_Pending_Wetness_Amounts.Num() != VertexCount)
    {
        Receiver.SimulationState.Updating_Pending_Wetness_Amounts.SetNumZeroed(VertexCount);
        Receiver.SimulationState.Updating_Pending_Wetness_Vertex_IndexQueue.Reset();
    }

    if (Receiver.SimulationState.WetnessDryHoldTimePerVertex.Num() != VertexCount)
    {
        Receiver.SimulationState.WetnessDryHoldTimePerVertex.SetNumZeroed(VertexCount);
    }

    if (Receiver.SimulationState.bPendingWetnessQueued.Num() != VertexCount)
    {
        Receiver.SimulationState.bPendingWetnessQueued.Init(false, VertexCount);
        Receiver.SimulationState.Updating_Pending_Wetness_Vertex_IndexQueue.Reset();
    }
}

bool FDynamicWetReceiverRuntimeDataBuilder::GetLODRenderData(
    const FDynamicWetReceiverContext& Receiver,
    int32 LODIndex,
    FSkeletalMeshLODRenderData*& OutLODData)
{
    OutLODData = nullptr;

    if (!Receiver.TargetSkeletalMesh)
    {
        return false;
    }

    USkeletalMesh* SkeletalMesh = Receiver.TargetSkeletalMesh->GetSkeletalMeshAsset();
    if (!SkeletalMesh)
    {
        UE_LOG(LogTemp, Warning, TEXT("DynamicWetReceiverComponent: SkeletalMeshAsset reference is null."));
        return false;
    }

    FSkeletalMeshRenderData* RenderData = SkeletalMesh->GetResourceForRendering();
    if (!RenderData || !RenderData->LODRenderData.IsValidIndex(LODIndex))
    {
        UE_LOG(LogTemp, Warning, TEXT("DynamicWetReceiverComponent: RenderData reference is null."));
        return false;
    }

    OutLODData = &RenderData->LODRenderData[LODIndex];
    return true;
}

bool FDynamicWetReceiverRuntimeDataBuilder::DoesVertexMatchBoneName(const FDynamicWetReceiverContext& Receiver, const int32 VertexIndex, const FName BoneName)
{
    if (BoneName.IsNone())
    {
        return true;
    }

    if (!Receiver.TargetSkeletalMesh)
    {
        return false;
    }

    const USkeletalMesh* SkeletalMesh = Receiver.TargetSkeletalMesh->GetSkeletalMeshAsset();
    if (!SkeletalMesh)
    {
        return false;
    }

    const int32 BoneIndex = SkeletalMesh->GetRefSkeleton().FindBoneIndex(BoneName);
    if (BoneIndex == INDEX_NONE)
    {
        return false;
    }

    const FSkinWeightVertexBuffer* SkinWeightBuffer =
        Receiver.TargetSkeletalMesh->GetSkinWeightBuffer(0);
    if (!SkinWeightBuffer)
    {
        return false;
    }

    const uint32 MaxInfluences = SkinWeightBuffer->GetMaxBoneInfluences();
    for (uint32 InfluenceIndex = 0; InfluenceIndex < MaxInfluences; ++InfluenceIndex)
    {
        if (SkinWeightBuffer->GetBoneWeight(VertexIndex, InfluenceIndex) == 0)
        {
            continue;
        }

        if (static_cast<int32>(SkinWeightBuffer->GetBoneIndex(VertexIndex, InfluenceIndex)) == BoneIndex)
        {
            return true;
        }
    }

    return false;
}
