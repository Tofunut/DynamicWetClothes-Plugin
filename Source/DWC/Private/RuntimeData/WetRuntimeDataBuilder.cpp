// Fill out your copyright notice in the Description page of Project Settings.

#include "RuntimeData/WetRuntimeDataBuilder.h"

#include "RuntimeData/WetPrecomputedSimulationDataBridge.h"
#include "RuntimeData/WetBoneOptimizationCacheBuilder.h"
#include "RuntimeData/WetClothingRuntimeData.h"
#include "RuntimeData/WetNeighborGraphBuilder.h"

#include "Runtime/Engine/Classes/Components/SkeletalMeshComponent.h"

#include "RuntimeData/Build/WetMeshAnalysis.h"
#include "WetInputSystem/Sampling/WetClothingMeshSampler.h"
#include "WetInputSystem/WetInputStage.h"
#include "WetRendering/WetRenderStage.h"
#include "WetSimulation/AbsorbedWetness/AbsorbedWetnessSimulationState.h"
#include "Runtime/Engine/Classes/Engine/SkeletalMesh.h"
#include "Runtime/Engine/Public/Rendering/SkeletalMeshLODRenderData.h"
#include "Runtime/Engine/Public/Rendering/SkeletalMeshRenderData.h"
#include "DataAssets/WetClothingAsset.h"
#include "DataAssets/WetnessProfile.h"

namespace
{
    bool ResolveWetPartSourceProfileParameters(
        const FWetClothingWetPartEntry& WetPartEntry,
        FWetnessProfileParameters&           OutParameters)
    {
        if (!WetPartEntry.ProfileAssignment.SourceProfile.IsValid())
        {
            return false;
        }

        const UWetnessProfile* SourceProfile =
            Cast<UWetnessProfile>(WetPartEntry.ProfileAssignment.SourceProfile.TryLoad());
        if (SourceProfile == nullptr)
        {
            return false;
        }

        OutParameters = SourceProfile->GetParameters();
        return true;
    }

    bool DoesWetPartEntryMatchReceiverScope(
        const FWetRuntimeDataBuildArgs& Receiver,
        const FWetClothingWetPartEntry& WetPartEntry)
    {
        return WetPartEntry.ComponentPath == Receiver.ComponentPath;
    }
} // namespace

void FWetRuntimeDataBuilder::InitializeAbsorbedWetnessData(FWetRuntimeDataBuildArgs& Receiver)
{
    FSkeletalMeshLODRenderData* LODData = nullptr;
    if (!GetLODRenderData(Receiver.TargetSkeletalMesh, Receiver.LODIndex, LODData))
    {
        return;
    }

    const int32 VertexCount = LODData->GetNumVertices();

    Receiver.SimulationState->AbsorbedWetnessPerVertex.SetNumZeroed(VertexCount);
    Receiver.SimulationState->UpdatingPendingWetnessAmounts.SetNumZeroed(VertexCount);
    Receiver.SimulationState->WetnessDryHoldTimePerVertex.SetNumZeroed(VertexCount);
    Receiver.SimulationState->UpdatingPendingWetnessVertexIndexQueue.Reset();
    Receiver.SimulationState->CurrentPendingWetnessVertexIndexQueue.Reset();
    Receiver.SimulationState->CurrentPendingWetnessAmounts.Reset();
    Receiver.SimulationState->bPendingWetnessQueued.Init(false, VertexCount);
    Receiver.CachedWetVertexColors->Init(FLinearColor::Black, VertexCount);
    Receiver.SimulationState->DirtyWetVertexIndices.Reset();

    Receiver.TargetSkeletalMesh->SetVertexColorOverride_LinearColor(0, *Receiver.CachedWetVertexColors);
    Receiver.TargetSkeletalMesh->MarkRenderStateDirty();
}

void FWetRuntimeDataBuilder::InitializeWetPartVertexData(FWetRuntimeDataBuildArgs& Receiver)
{
    FSkeletalMeshLODRenderData* LODData = nullptr;
    if (!GetLODRenderData(Receiver.TargetSkeletalMesh, Receiver.LODIndex, LODData))
    {
        return;
    }

    const int32 VertexCount = LODData->GetNumVertices();
    Receiver.RuntimeData->VertexWetPartIDs.Init(INDEX_NONE, VertexCount);
    Receiver.RuntimeData->VertexWetnessProfileParameters.SetNum(VertexCount);
    Receiver.RuntimeData->VertexWetPartDebugColors.Init(Receiver.UnassignedWetPartDebugColor, VertexCount);

    FWetnessProfileParameters DefaultParameters;
    if (const UWetnessProfile* WetnessProfile = Receiver.GetActiveWetnessProfile())
    {
        DefaultParameters = WetnessProfile->GetParameters();
    }

    for (FWetnessProfileParameters& VertexParameters : Receiver.RuntimeData->VertexWetnessProfileParameters)
    {
        VertexParameters = DefaultParameters;
    }

    if (!Receiver.WetClothingAsset)
    {
        return;
    }

    USkeletalMesh* SkeletalMesh = Receiver.TargetSkeletalMesh ? Receiver.TargetSkeletalMesh->GetSkeletalMeshAsset() : nullptr;
    if (Receiver.ComponentPath.IsEmpty() && Receiver.WetClothingAsset->TargetMesh && Receiver.WetClothingAsset->TargetMesh != SkeletalMesh)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("DynamicWetClothesComponent: WetClothingAsset TargetMesh does not match the receiver mesh on %s."),
            *GetNameSafe(Receiver.OwnerForLogs));
    }

    TMap<int32, FWetnessProfileParameters> ResolvedWetPartParametersByEntryIndex;
    for (int32 EntryIndex = 0; EntryIndex < Receiver.WetClothingAsset->PartData.EditableWetPartData.WetPartEntries.Num(); ++EntryIndex)
    {
        const FWetClothingWetPartEntry& WetPartEntry = Receiver.WetClothingAsset->PartData.EditableWetPartData.WetPartEntries[EntryIndex];
        if (!DoesWetPartEntryMatchReceiverScope(Receiver, WetPartEntry))
        {
            continue;
        }

        FWetnessProfileParameters ResolvedParameters;
        if (ResolveWetPartSourceProfileParameters(WetPartEntry, ResolvedParameters))
        {
            ResolvedWetPartParametersByEntryIndex.Add(EntryIndex, ResolvedParameters);
        }
    }

    TMap<FIntPoint, TArray<int32>> WetPartEntryIndicesByScope;
    for (int32 EntryIndex = 0; EntryIndex < Receiver.WetClothingAsset->PartData.EditableWetPartData.WetPartEntries.Num(); ++EntryIndex)
    {
        const FWetClothingWetPartEntry& WetPartEntry = Receiver.WetClothingAsset->PartData.EditableWetPartData.WetPartEntries[EntryIndex];
        if (!DoesWetPartEntryMatchReceiverScope(Receiver, WetPartEntry) ||
            WetPartEntry.MaterialSlotIndex == INDEX_NONE ||
            WetPartEntry.UVChannelIndex < 0 ||
            WetPartEntry.AssignedUVIslandIDs.Num() == 0 ||
            !ResolvedWetPartParametersByEntryIndex.Contains(EntryIndex))
        {
            continue;
        }

        const FIntPoint WetPartScopeKey(WetPartEntry.MaterialSlotIndex, WetPartEntry.UVChannelIndex);
        WetPartEntryIndicesByScope.FindOrAdd(WetPartScopeKey).Add(EntryIndex);
    }

    for (const TPair<FIntPoint, TArray<int32>>& ScopePair : WetPartEntryIndicesByScope)
    {
        const int32 MaterialSlotIndex = ScopePair.Key.X;
        const int32 UVChannelIndex = ScopePair.Key.Y;

        TArray<FWetUVIsland> Islands;
        if (!FWetMeshAnalysis::BuildMaterialSlotUVIslands(
                SkeletalMesh,
                0,
                UVChannelIndex,
                MaterialSlotIndex,
                Islands))
        {
            continue;
        }

        TArray<FWetVertexIslandMembership> VertexMembership;
        if (!FWetMeshAnalysis::BuildVertexIslandMembership(
                Islands,
                VertexCount,
                UVChannelIndex,
                VertexMembership))
        {
            continue;
        }

        TMap<int32, int32> UVIslandToWetPartEntryIndex;
        for (const int32 WetPartEntryIndex : ScopePair.Value)
        {
            if (!Receiver.WetClothingAsset->PartData.EditableWetPartData.WetPartEntries.IsValidIndex(WetPartEntryIndex))
            {
                continue;
            }

            const FWetClothingWetPartEntry& WetPartEntry = Receiver.WetClothingAsset->PartData.EditableWetPartData.WetPartEntries[WetPartEntryIndex];
            for (const int32 UVIslandID : WetPartEntry.AssignedUVIslandIDs)
            {
                UVIslandToWetPartEntryIndex.Add(UVIslandID, WetPartEntryIndex);
            }
        }

        for (int32 VertexIndex = 0; VertexIndex < VertexMembership.Num(); ++VertexIndex)
        {
            const FWetVertexIslandMembership& Membership = VertexMembership[VertexIndex];
            if (Membership.UVIslandID == INDEX_NONE)
            {
                continue;
            }

            const int32* WetPartEntryIndex = UVIslandToWetPartEntryIndex.Find(Membership.UVIslandID);
            if (WetPartEntryIndex == nullptr ||
                !Receiver.WetClothingAsset->PartData.EditableWetPartData.WetPartEntries.IsValidIndex(*WetPartEntryIndex) ||
                !Receiver.RuntimeData->VertexWetPartIDs.IsValidIndex(VertexIndex) ||
                !Receiver.RuntimeData->VertexWetnessProfileParameters.IsValidIndex(VertexIndex))
            {
                continue;
            }

            const FWetClothingWetPartEntry& WetPartEntry = Receiver.WetClothingAsset->PartData.EditableWetPartData.WetPartEntries[*WetPartEntryIndex];
            Receiver.RuntimeData->VertexWetPartIDs[VertexIndex] = WetPartEntry.WetPartID;
            Receiver.RuntimeData->VertexWetnessProfileParameters[VertexIndex] =
                ResolvedWetPartParametersByEntryIndex[*WetPartEntryIndex];
            Receiver.RuntimeData->VertexWetPartDebugColors[VertexIndex] = WetPartEntry.Color;
        }
    }
}

bool FWetRuntimeDataBuilder::InitializeWetPartVertexDataFromBakedProfile(
    FWetRuntimeDataBuildArgs&        Receiver,
    const int32                      VertexCount,
    const FWetnessProfileParameters& DefaultParameters)
{
    if (!Receiver.WetClothingAsset || !Receiver.TargetSkeletalMesh)
    {
        return false;
    }

    const USkeletalMesh* SkeletalMesh = Receiver.TargetSkeletalMesh->GetSkeletalMeshAsset();
    if (!Receiver.WetClothingAsset->IsPrecomputedSimulationDataValidForMesh(SkeletalMesh, 0))
    {
        if (Receiver.WetClothingAsset->GetPrecomputedSimulationData().bIsValid)
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("DynamicWetClothesComponent: WetClothingAsset precomputed simulation data is stale for %s. Falling back to runtime UV analysis."),
                *GetNameSafe(Receiver.OwnerForLogs));
        }
        return false;
    }

    const FWetClothingPrecomputedSimulationData& PrecomputedData = Receiver.WetClothingAsset->GetPrecomputedSimulationData();
    if (PrecomputedData.VertexCount != VertexCount || PrecomputedData.Vertices.Num() != VertexCount)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("DynamicWetClothesComponent: WetClothingAsset precomputed simulation data vertex count mismatch on %s. Falling back to runtime UV analysis."),
            *GetNameSafe(Receiver.OwnerForLogs));
        return false;
    }

    TMap<int32, FWetnessProfileParameters> ResolvedWetPartParametersByEntryIndex;
    for (int32 EntryIndex = 0; EntryIndex < Receiver.WetClothingAsset->PartData.EditableWetPartData.WetPartEntries.Num(); ++EntryIndex)
    {
        const FWetClothingWetPartEntry& WetPartEntry = Receiver.WetClothingAsset->PartData.EditableWetPartData.WetPartEntries[EntryIndex];
        if (!DoesWetPartEntryMatchReceiverScope(Receiver, WetPartEntry))
        {
            continue;
        }

        FWetnessProfileParameters ResolvedParameters;
        if (ResolveWetPartSourceProfileParameters(WetPartEntry, ResolvedParameters))
        {
            ResolvedWetPartParametersByEntryIndex.Add(EntryIndex, ResolvedParameters);
        }
    }

    for (int32 VertexIndex = 0; VertexIndex < PrecomputedData.Vertices.Num(); ++VertexIndex)
    {
        const FWetClothingPrecomputedVertexData& PrecomputedVertex = PrecomputedData.Vertices[VertexIndex];
        if (!Receiver.RuntimeData->VertexWetPartIDs.IsValidIndex(VertexIndex) ||
            !Receiver.RuntimeData->VertexWetnessProfileParameters.IsValidIndex(VertexIndex) ||
            !Receiver.RuntimeData->VertexWetPartDebugColors.IsValidIndex(VertexIndex))
        {
            continue;
        }

        if (Receiver.WetClothingAsset->PartData.EditableWetPartData.WetPartEntries.IsValidIndex(PrecomputedVertex.WetPartEntryIndex) &&
            ResolvedWetPartParametersByEntryIndex.Contains(PrecomputedVertex.WetPartEntryIndex))
        {
            const FWetClothingWetPartEntry& WetPartEntry =
                Receiver.WetClothingAsset->PartData.EditableWetPartData.WetPartEntries[PrecomputedVertex.WetPartEntryIndex];
            if (!DoesWetPartEntryMatchReceiverScope(Receiver, WetPartEntry))
            {
                continue;
            }
            Receiver.RuntimeData->VertexWetPartIDs[VertexIndex] = PrecomputedVertex.WetPartID;
            Receiver.RuntimeData->VertexWetnessProfileParameters[VertexIndex] =
                ResolvedWetPartParametersByEntryIndex[PrecomputedVertex.WetPartEntryIndex];
            Receiver.RuntimeData->VertexWetPartDebugColors[VertexIndex] = WetPartEntry.Color;
        }
    }

    return true;
}

void FWetRuntimeDataBuilder::BuildNeighborGraph(FWetRuntimeDataBuildArgs& Receiver)
{
    FSkeletalMeshLODRenderData* LODData = nullptr;
    if (!GetLODRenderData(Receiver.TargetSkeletalMesh, Receiver.LODIndex, LODData))
    {
        return;
    }

    const int32 VertexCount = LODData->GetNumVertices();
    Receiver.RuntimeData->ResetNeighborGraph();

    USkeletalMesh* SkeletalMesh = Receiver.TargetSkeletalMesh ? Receiver.TargetSkeletalMesh->GetSkeletalMeshAsset() : nullptr;
    if (Receiver.bUsePrecomputedSimulationData && Receiver.WetClothingAsset)
    {
        FString BakedGraphErrorMessage;
        if (FWetPrecomputedSimulationDataBridge::TryCopyPrecomputedNeighborGraph(
                Receiver.WetClothingAsset,
                SkeletalMesh,
                Receiver.LODIndex,
                VertexCount,
                Receiver.RuntimeData->NeighborGraph,
                &BakedGraphErrorMessage))
        {
            Receiver.RuntimeData->bHasNeighborGraph = true;
            return;
        }

        if (!BakedGraphErrorMessage.IsEmpty())
        {
            UE_LOG(
                LogTemp,
                Verbose,
                TEXT("DynamicWetClothesComponent: Precomputed neighbor graph was not used on %s. %s"),
                *GetNameSafe(Receiver.OwnerForLogs),
                *BakedGraphErrorMessage);
        }
    }

    if (!Receiver.bAllowRuntimeFallbackBuild)
    {
        return;
    }

    UE_LOG(
        LogTemp,
        Log,
        TEXT("DynamicWetClothesComponent: Building neighbor graph at runtime fallback on %s."),
        *GetNameSafe(Receiver.OwnerForLogs));

    FString ErrorMessage;
    if (FWetNeighborGraphBuilder::BuildRuntimeGraph(
            *LODData,
            Receiver.CoincidentVertexNeighborTolerance,
            Receiver.RuntimeData->NeighborGraph,
            &ErrorMessage))
    {
        Receiver.RuntimeData->bHasNeighborGraph = true;
    }
    else
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("DynamicWetClothesComponent: Failed to build neighbor graph on %s. %s"),
            *GetNameSafe(Receiver.OwnerForLogs),
            *ErrorMessage);
    }
}

bool FWetRuntimeDataBuilder::BuildNeighborGraphFromBakedProfile(
    FWetRuntimeDataBuildArgs& Receiver,
    const int32               VertexCount)
{
    const USkeletalMesh* SkeletalMesh = Receiver.TargetSkeletalMesh ? Receiver.TargetSkeletalMesh->GetSkeletalMeshAsset() : nullptr;
    Receiver.RuntimeData->ResetNeighborGraph();
    if (!FWetPrecomputedSimulationDataBridge::TryCopyPrecomputedNeighborGraph(
            Receiver.WetClothingAsset,
            SkeletalMesh,
            Receiver.LODIndex,
            VertexCount,
            Receiver.RuntimeData->NeighborGraph,
            nullptr))
    {
        return false;
    }

    Receiver.RuntimeData->bHasNeighborGraph = true;
    return true;
}

void FWetRuntimeDataBuilder::AddNeighbor(
    FWetClothingRuntimeData& RuntimeData,
    int32                    VertexIndex,
    int32                    NeighborIndex)
{
    if (!RuntimeData.NeighborGraph.IsValidIndex(VertexIndex))
    {
        return;
    }

    if (!RuntimeData.NeighborGraph.IsValidIndex(NeighborIndex))
    {
        return;
    }

    if (VertexIndex == NeighborIndex)
    {
        return;
    }

    TArray<int32>& Neighbors = RuntimeData.NeighborGraph[VertexIndex].Neighbors;

    if (!Neighbors.Contains(NeighborIndex))
    {
        Neighbors.Add(NeighborIndex);
    }
}

void FWetRuntimeDataBuilder::EnsureWetnessBufferSize(FWetRuntimeDataBuildArgs& Receiver, const int32 VertexCount)
{
    if (VertexCount <= 0)
    {
        Receiver.SimulationState->AbsorbedWetnessPerVertex.Reset();
        Receiver.RuntimeData->VertexWetPartIDs.Reset();
        Receiver.RuntimeData->VertexWetnessProfileParameters.Reset();
        Receiver.RuntimeData->VertexWetPartDebugColors.Reset();
        Receiver.SimulationState->UpdatingPendingWetnessAmounts.Reset();
        Receiver.SimulationState->WetnessDryHoldTimePerVertex.Reset();
        Receiver.SimulationState->UpdatingPendingWetnessVertexIndexQueue.Reset();
        Receiver.SimulationState->CurrentPendingWetnessVertexIndexQueue.Reset();
        Receiver.SimulationState->CurrentPendingWetnessAmounts.Reset();
        Receiver.SimulationState->bPendingWetnessQueued.Reset();
        return;
    }

    if (Receiver.SimulationState->AbsorbedWetnessPerVertex.Num() != VertexCount)
    {
        Receiver.SimulationState->AbsorbedWetnessPerVertex.SetNumZeroed(VertexCount);
    }

    if (Receiver.RuntimeData->VertexWetPartIDs.Num() != VertexCount ||
        Receiver.RuntimeData->VertexWetnessProfileParameters.Num() != VertexCount ||
        Receiver.RuntimeData->VertexWetPartDebugColors.Num() != VertexCount)
    {
        InitializeWetPartVertexData(Receiver);
    }

    if (Receiver.SimulationState->UpdatingPendingWetnessAmounts.Num() != VertexCount)
    {
        Receiver.SimulationState->UpdatingPendingWetnessAmounts.SetNumZeroed(VertexCount);
        Receiver.SimulationState->UpdatingPendingWetnessVertexIndexQueue.Reset();
    }

    if (Receiver.SimulationState->WetnessDryHoldTimePerVertex.Num() != VertexCount)
    {
        Receiver.SimulationState->WetnessDryHoldTimePerVertex.SetNumZeroed(VertexCount);
    }

    if (Receiver.SimulationState->bPendingWetnessQueued.Num() != VertexCount)
    {
        Receiver.SimulationState->bPendingWetnessQueued.Init(false, VertexCount);
        Receiver.SimulationState->UpdatingPendingWetnessVertexIndexQueue.Reset();
    }
}

void FWetRuntimeDataBuilder::EnsureWetnessBufferSize(FWetInputStageArgs& Receiver, const int32 VertexCount)
{
    if (VertexCount <= 0)
    {
        if (Receiver.SimulationState)
        {
            Receiver.SimulationState->AbsorbedWetnessPerVertex.Reset();
            Receiver.SimulationState->UpdatingPendingWetnessAmounts.Reset();
            Receiver.SimulationState->WetnessDryHoldTimePerVertex.Reset();
            Receiver.SimulationState->UpdatingPendingWetnessVertexIndexQueue.Reset();
            Receiver.SimulationState->CurrentPendingWetnessVertexIndexQueue.Reset();
            Receiver.SimulationState->CurrentPendingWetnessAmounts.Reset();
            Receiver.SimulationState->bPendingWetnessQueued.Reset();
        }
        if (Receiver.RuntimeData)
        {
            Receiver.RuntimeData->VertexWetPartIDs.Reset();
            Receiver.RuntimeData->VertexWetnessProfileParameters.Reset();
            Receiver.RuntimeData->VertexWetPartDebugColors.Reset();
        }
        return;
    }

    if (Receiver.SimulationState)
    {
        if (Receiver.SimulationState->AbsorbedWetnessPerVertex.Num() != VertexCount)
        {
            Receiver.SimulationState->AbsorbedWetnessPerVertex.SetNumZeroed(VertexCount);
        }
        if (Receiver.SimulationState->UpdatingPendingWetnessAmounts.Num() != VertexCount)
        {
            Receiver.SimulationState->UpdatingPendingWetnessAmounts.SetNumZeroed(VertexCount);
            Receiver.SimulationState->UpdatingPendingWetnessVertexIndexQueue.Reset();
        }
        if (Receiver.SimulationState->WetnessDryHoldTimePerVertex.Num() != VertexCount)
        {
            Receiver.SimulationState->WetnessDryHoldTimePerVertex.SetNumZeroed(VertexCount);
        }
        if (Receiver.SimulationState->bPendingWetnessQueued.Num() != VertexCount)
        {
            Receiver.SimulationState->bPendingWetnessQueued.Init(false, VertexCount);
            Receiver.SimulationState->UpdatingPendingWetnessVertexIndexQueue.Reset();
        }
    }

    if (Receiver.RuntimeData)
    {
        if (Receiver.RuntimeData->VertexWetPartIDs.Num() != VertexCount)
        {
            Receiver.RuntimeData->VertexWetPartIDs.Init(INDEX_NONE, VertexCount);
        }
        if (Receiver.RuntimeData->VertexWetnessProfileParameters.Num() != VertexCount)
        {
            Receiver.RuntimeData->VertexWetnessProfileParameters.SetNum(VertexCount);
        }
        if (Receiver.RuntimeData->VertexWetPartDebugColors.Num() != VertexCount)
        {
            Receiver.RuntimeData->VertexWetPartDebugColors.Init(FLinearColor(0.25f, 0.25f, 0.25f, 1.0f), VertexCount);
        }
    }
}

bool FWetRuntimeDataBuilder::GetLODRenderData(
    const USkeletalMeshComponent* TargetSkeletalMesh,
    int32                         LODIndex,
    FSkeletalMeshLODRenderData*&  OutLODData) const
{
    OutLODData = nullptr;

    if (!TargetSkeletalMesh)
    {
        return false;
    }

    USkeletalMesh* SkeletalMesh = TargetSkeletalMesh->GetSkeletalMeshAsset();
    if (!SkeletalMesh)
    {
        UE_LOG(LogTemp, Warning, TEXT("DynamicWetClothesComponent: SkeletalMeshAsset reference is null."));
        return false;
    }

    FSkeletalMeshRenderData* RenderData = SkeletalMesh->GetResourceForRendering();
    if (!RenderData || !RenderData->LODRenderData.IsValidIndex(LODIndex))
    {
        UE_LOG(LogTemp, Warning, TEXT("DynamicWetClothesComponent: RenderData reference is null."));
        return false;
    }

    OutLODData = &RenderData->LODRenderData[LODIndex];
    return true;
}

bool FWetRuntimeDataBuilder::BuildBoneOptimizationCache(
    FWetRuntimeDataBuildArgs& Receiver,
    const int32               LODIndex)
{
    Receiver.RuntimeData->ResetBoneOptimizationCache();

    if (!Receiver.TargetSkeletalMesh)
    {
        return false;
    }

    USkeletalMesh* SkeletalMesh = Receiver.TargetSkeletalMesh->GetSkeletalMeshAsset();
    if (!SkeletalMesh)
    {
        return false;
    }

    if (Receiver.bUsePrecomputedBoneOptimizationCache && Receiver.WetClothingAsset)
    {
        FString PrecomputedCacheErrorMessage;
        if (FWetPrecomputedSimulationDataBridge::TryCopyPrecomputedBoneOptimizationCache(
                Receiver.WetClothingAsset,
                SkeletalMesh,
                LODIndex,
                Receiver.RuntimeData->BoneOptimizationCache,
                &PrecomputedCacheErrorMessage))
        {
            Receiver.RuntimeData->bHasBoneOptimizationCache = true;
            return true;
        }

        if (!PrecomputedCacheErrorMessage.IsEmpty())
        {
            UE_LOG(
                LogTemp,
                Verbose,
                TEXT("DynamicWetClothesComponent: Precomputed bone optimization cache was not used on %s. %s"),
                *GetNameSafe(Receiver.OwnerForLogs),
                *PrecomputedCacheErrorMessage);
        }
    }

    if (!Receiver.bAllowRuntimeFallbackBuild)
    {
        return false;
    }

    TArray<FWetBoneIncludeRule> IncludeRules;
    FString                     ErrorMessage;
    if (!FWetBoneOptimizationCacheBuilder::Build(
            SkeletalMesh,
            LODIndex,
            IncludeRules,
            Receiver.RuntimeData->BoneOptimizationCache,
            &ErrorMessage))
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("DynamicWetClothesComponent: Failed to build bone optimization cache on %s. %s"),
            *GetNameSafe(Receiver.OwnerForLogs),
            *ErrorMessage);
        return false;
    }

    Receiver.RuntimeData->bHasBoneOptimizationCache = true;
    return true;
}

bool FWetRuntimeDataBuilder::GetBoneCandidateVertexRange(
    const FWetClothingRuntimeData& RuntimeData,
    const USkeletalMeshComponent*  TargetSkeletalMesh,
    const FName                    BoneName,
    int32&                         OutStartOffset,
    int32&                         OutEndOffset) const
{
    OutStartOffset = INDEX_NONE;
    OutEndOffset = INDEX_NONE;

    if (BoneName.IsNone() || !RuntimeData.bHasBoneOptimizationCache || !TargetSkeletalMesh)
    {
        return false;
    }

    const USkeletalMesh* SkeletalMesh = TargetSkeletalMesh->GetSkeletalMeshAsset();
    if (!SkeletalMesh)
    {
        return false;
    }

    const FWetBonePrimaryVertexCache& PrimaryVertexCache =
        RuntimeData.BoneOptimizationCache.PrimaryVertexCache;
    if (PrimaryVertexCache.SourceMesh != SkeletalMesh)
    {
        return false;
    }

    const int32 BoneIndex = SkeletalMesh->GetRefSkeleton().FindBoneIndex(BoneName);
    if (BoneIndex == INDEX_NONE || !PrimaryVertexCache.BoneStartOffsets.IsValidIndex(BoneIndex + 1))
    {
        return false;
    }

    OutStartOffset = PrimaryVertexCache.BoneStartOffsets[BoneIndex];
    OutEndOffset = PrimaryVertexCache.BoneStartOffsets[BoneIndex + 1];
    return OutStartOffset >= 0 && OutEndOffset >= OutStartOffset &&
           OutEndOffset <= PrimaryVertexCache.FlatVertexIndices.Num();
}

bool FWetRuntimeDataBuilder::DoesVertexMatchBoneName(const USkeletalMeshComponent* TargetSkeletalMesh, const int32 VertexIndex, const FName BoneName) const
{
    if (BoneName.IsNone())
    {
        return true;
    }

    if (!TargetSkeletalMesh)
    {
        return false;
    }

    const USkeletalMesh* SkeletalMesh = TargetSkeletalMesh->GetSkeletalMeshAsset();
    if (!SkeletalMesh)
    {
        return false;
    }

    const int32 BoneIndex = SkeletalMesh->GetRefSkeleton().FindBoneIndex(BoneName);
    if (BoneIndex == INDEX_NONE)
    {
        return false;
    }

    const FSkinWeightVertexBuffer* SkinWeightBuffer = TargetSkeletalMesh->GetSkinWeightBuffer(0);
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
