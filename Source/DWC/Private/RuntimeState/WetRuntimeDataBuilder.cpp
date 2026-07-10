// Fill out your copyright notice in the Description page of Project Settings.

#include "RuntimeState/WetRuntimeDataBuilder.h"

#include "RuntimeState/WetPrecomputedSimulationDataBridge.h"
#include "RuntimeState/WetClothingRuntimeData.h"

#include "Runtime/Engine/Classes/Components/SkeletalMeshComponent.h"

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
    bool ResolveWetPartProfileParameters(
        const FWetClothingWetPartEntry& WetPartEntry,
        FWetnessProfileParameters&           OutParameters)
    {
        if (WetPartEntry.ProfileAssignment.SourceProfile.IsValid())
        {
            const UWetnessProfile* SourceProfile =
                Cast<UWetnessProfile>(WetPartEntry.ProfileAssignment.SourceProfile.TryLoad());
            if (SourceProfile != nullptr)
            {
                OutParameters = SourceProfile->GetParameters();
                return true;
            }
        }

        OutParameters = WetPartEntry.ProfileAssignment.Parameters;
        return true;
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

bool FWetRuntimeDataBuilder::InitializeWetPartVertexData(FWetRuntimeDataBuildArgs& Receiver)
{
    FSkeletalMeshLODRenderData* LODData = nullptr;
    if (!GetLODRenderData(Receiver.TargetSkeletalMesh, Receiver.LODIndex, LODData))
    {
        return false;
    }

    const int32 VertexCount = LODData->GetNumVertices();
    Receiver.RuntimeData->VertexWetPartIDs.Init(INDEX_NONE, VertexCount);
    Receiver.RuntimeData->VertexWettableFlags.Init(false, VertexCount);
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

    if (!Receiver.WetClothingAsset || !Receiver.TargetSkeletalMesh)
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("DynamicWetClothesComponent: Missing WetClothingAsset or target mesh on %s. Wet simulation disabled."),
            *GetNameSafe(Receiver.OwnerForLogs));
        return false;
    }

    USkeletalMesh* SkeletalMesh = Receiver.TargetSkeletalMesh->GetSkeletalMeshAsset();
    if (Receiver.WetClothingAsset->TargetMesh && Receiver.WetClothingAsset->TargetMesh != SkeletalMesh)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("DynamicWetClothesComponent: WetClothingAsset TargetMesh does not match the receiver mesh on %s."),
            *GetNameSafe(Receiver.OwnerForLogs));
    }

    if (!Receiver.bUsePrecomputedSimulationData)
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("DynamicWetClothesComponent: Runtime UV analysis fallback was removed. Precomputed simulation data is required for %s."),
            *GetNameSafe(Receiver.OwnerForLogs));
        return false;
    }

    if (!InitializeWetPartVertexDataFromPrecomputedData(Receiver, VertexCount, DefaultParameters))
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("DynamicWetClothesComponent: Failed to initialize wet part vertex data from precomputed simulation data on %s. Open the Wet Clothing Asset and save it to update runtime-ready data."),
            *GetNameSafe(Receiver.OwnerForLogs));
        return false;
    }

    return true;
}

bool FWetRuntimeDataBuilder::InitializeWetPartVertexDataFromPrecomputedData(
    FWetRuntimeDataBuildArgs&        Receiver,
    const int32                      VertexCount,
    const FWetnessProfileParameters& DefaultParameters)
{
    if (!Receiver.WetClothingAsset || !Receiver.TargetSkeletalMesh)
    {
        return false;
    }

    const USkeletalMesh* SkeletalMesh = Receiver.TargetSkeletalMesh->GetSkeletalMeshAsset();
    if (!Receiver.WetClothingAsset->IsPrecomputedSimulationDataValidForMesh(SkeletalMesh, Receiver.LODIndex))
    {
        if (Receiver.WetClothingAsset->GetPrecomputedSimulationData().bIsValid)
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("DynamicWetClothesComponent: WetClothingAsset precomputed simulation data is stale for %s."),
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
            TEXT("DynamicWetClothesComponent: WetClothingAsset precomputed simulation data vertex count mismatch on %s."),
            *GetNameSafe(Receiver.OwnerForLogs));
        return false;
    }

    TMap<int32, FWetnessProfileParameters> ResolvedWetPartParametersByEntryIndex;
    for (int32 EntryIndex = 0; EntryIndex < Receiver.WetClothingAsset->PartData.EditableWetPartData.WetPartEntries.Num(); ++EntryIndex)
    {
        const FWetClothingWetPartEntry& WetPartEntry = Receiver.WetClothingAsset->PartData.EditableWetPartData.WetPartEntries[EntryIndex];

        FWetnessProfileParameters ResolvedParameters;
        if (ResolveWetPartProfileParameters(WetPartEntry, ResolvedParameters))
        {
            ResolvedWetPartParametersByEntryIndex.Add(EntryIndex, ResolvedParameters);
        }
    }

    int32 PrecomputedWettableVertexCount = 0;
    int32 RuntimeWettableVertexCount = 0;

    for (int32 VertexIndex = 0; VertexIndex < PrecomputedData.Vertices.Num(); ++VertexIndex)
    {
        const FWetClothingPrecomputedVertexData& PrecomputedVertex = PrecomputedData.Vertices[VertexIndex];
        if (!Receiver.RuntimeData->VertexWetPartIDs.IsValidIndex(VertexIndex) ||
            !Receiver.RuntimeData->VertexWettableFlags.IsValidIndex(VertexIndex) ||
            !Receiver.RuntimeData->VertexWetnessProfileParameters.IsValidIndex(VertexIndex) ||
            !Receiver.RuntimeData->VertexWetPartDebugColors.IsValidIndex(VertexIndex))
        {
            continue;
        }

        if (PrecomputedVertex.bIsWettable)
        {
            ++PrecomputedWettableVertexCount;
        }

        if (PrecomputedVertex.bIsWettable &&
            Receiver.WetClothingAsset->PartData.EditableWetPartData.WetPartEntries.IsValidIndex(PrecomputedVertex.WetPartEntryIndex) &&
            ResolvedWetPartParametersByEntryIndex.Contains(PrecomputedVertex.WetPartEntryIndex))
        {
            const FWetClothingWetPartEntry& WetPartEntry =
                Receiver.WetClothingAsset->PartData.EditableWetPartData.WetPartEntries[PrecomputedVertex.WetPartEntryIndex];
            Receiver.RuntimeData->VertexWettableFlags[VertexIndex] = true;
            Receiver.RuntimeData->VertexWetPartIDs[VertexIndex] = PrecomputedVertex.WetPartID;
            Receiver.RuntimeData->VertexWetnessProfileParameters[VertexIndex] =
                ResolvedWetPartParametersByEntryIndex[PrecomputedVertex.WetPartEntryIndex];
            Receiver.RuntimeData->VertexWetPartDebugColors[VertexIndex] = WetPartEntry.Color;
            ++RuntimeWettableVertexCount;
        }
    }

    if (PrecomputedWettableVertexCount == 0)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("DynamicWetClothesComponent: Precomputed simulation data contains no wettable vertices on %s. Check wettable material slots and wet part assignments."),
            *GetNameSafe(Receiver.OwnerForLogs));
    }
    else if (RuntimeWettableVertexCount == 0)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("DynamicWetClothesComponent: Runtime initialized 0 wettable vertices from %d precomputed wettable vertices on %s."),
            PrecomputedWettableVertexCount,
            *GetNameSafe(Receiver.OwnerForLogs));
    }

    return true;
}

bool FWetRuntimeDataBuilder::InitializeNeighborGraphFromPrecomputedData(FWetRuntimeDataBuildArgs& Receiver)
{
    FSkeletalMeshLODRenderData* LODData = nullptr;
    if (!GetLODRenderData(Receiver.TargetSkeletalMesh, Receiver.LODIndex, LODData))
    {
        return false;
    }

    const int32 VertexCount = LODData->GetNumVertices();
    Receiver.RuntimeData->ResetNeighborGraph();

    const USkeletalMesh* SkeletalMesh = Receiver.TargetSkeletalMesh ? Receiver.TargetSkeletalMesh->GetSkeletalMeshAsset() : nullptr;
    FString ErrorMessage;
    if (!FWetPrecomputedSimulationDataBridge::TryCopyPrecomputedNeighborGraph(
            Receiver.WetClothingAsset,
            SkeletalMesh,
            Receiver.LODIndex,
            VertexCount,
            Receiver.RuntimeData->NeighborGraph,
            &ErrorMessage))
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("DynamicWetClothesComponent: Failed to initialize neighbor graph from precomputed simulation data on %s. %s Open the Wet Clothing Asset and save it to update runtime-ready data."),
            *GetNameSafe(Receiver.OwnerForLogs),
            *ErrorMessage);
        return false;
    }

    Receiver.RuntimeData->bHasNeighborGraph = true;
    return true;
}

void FWetRuntimeDataBuilder::EnsureWetnessBufferSize(FWetRuntimeDataBuildArgs& Receiver, const int32 VertexCount)
{
    if (VertexCount <= 0)
    {
        Receiver.SimulationState->AbsorbedWetnessPerVertex.Reset();
        Receiver.RuntimeData->VertexWetPartIDs.Reset();
        Receiver.RuntimeData->VertexWettableFlags.Reset();
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
        Receiver.RuntimeData->VertexWettableFlags.Num() != VertexCount ||
        Receiver.RuntimeData->VertexWetnessProfileParameters.Num() != VertexCount ||
        Receiver.RuntimeData->VertexWetPartDebugColors.Num() != VertexCount)
    {
        if (!InitializeWetPartVertexData(Receiver))
        {
            UE_LOG(
                LogTemp,
                Error,
                TEXT("DynamicWetClothesComponent: Failed to refresh precomputed wet part data on %s."),
                *GetNameSafe(Receiver.OwnerForLogs));
        }
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
            Receiver.RuntimeData->VertexWettableFlags.Reset();
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
        if (Receiver.RuntimeData->VertexWettableFlags.Num() != VertexCount)
        {
            Receiver.RuntimeData->VertexWettableFlags.Init(false, VertexCount);
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

bool FWetRuntimeDataBuilder::InitializeBoneOptimizationCacheFromPrecomputedData(
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

    if (!Receiver.bUsePrecomputedBoneOptimizationCache || !Receiver.WetClothingAsset)
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("DynamicWetClothesComponent: Precomputed bone optimization cache is required on %s. Runtime fallback was removed."),
            *GetNameSafe(Receiver.OwnerForLogs));
        return false;
    }

    FString PrecomputedCacheErrorMessage;
    if (!FWetPrecomputedSimulationDataBridge::TryCopyPrecomputedBoneOptimizationCache(
            Receiver.WetClothingAsset,
            SkeletalMesh,
            LODIndex,
            Receiver.RuntimeData->BoneOptimizationCache,
            &PrecomputedCacheErrorMessage))
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("DynamicWetClothesComponent: Failed to initialize bone optimization cache from precomputed simulation data on %s. %s Open the Wet Clothing Asset and save it to update runtime-ready data."),
            *GetNameSafe(Receiver.OwnerForLogs),
            *PrecomputedCacheErrorMessage);
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

    FSkeletalMeshLODRenderData* LODData = nullptr;
    if (!GetLODRenderData(TargetSkeletalMesh, 0, LODData) || !LODData)
    {
        return false;
    }

    const int32 VertexCount = static_cast<int32>(LODData->GetNumVertices());
    if (VertexIndex < 0 || VertexIndex >= VertexCount)
    {
        return false;
    }

    int32 SectionIndex = INDEX_NONE;
    int32 SectionVertexIndex = INDEX_NONE;
    LODData->GetSectionFromVertexIndex(VertexIndex, SectionIndex, SectionVertexIndex);
    if (!LODData->RenderSections.IsValidIndex(SectionIndex) || SectionVertexIndex < 0)
    {
        return false;
    }

    const FSkelMeshRenderSection& Section = LODData->RenderSections[SectionIndex];
    const int32 BufferVertexIndex = Section.GetVertexBufferIndex() + SectionVertexIndex;
    if (BufferVertexIndex < 0 || BufferVertexIndex >= VertexCount)
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
        if (SkinWeightBuffer->GetBoneWeight(BufferVertexIndex, InfluenceIndex) == 0)
        {
            continue;
        }

        const int32 BoneMapIndex = static_cast<int32>(SkinWeightBuffer->GetBoneIndex(BufferVertexIndex, InfluenceIndex));
        if (!Section.BoneMap.IsValidIndex(BoneMapIndex))
        {
            continue;
        }

        if (static_cast<int32>(Section.BoneMap[BoneMapIndex]) == BoneIndex)
        {
            return true;
        }
    }

    return false;
}
