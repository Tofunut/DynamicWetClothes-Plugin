// Fill out your copyright notice in the Description page of Project Settings.

#include "RuntimeState/WetRuntimeDataBuilder.h"

#include "RuntimeState/WetPrecomputedSimulationDataBridge.h"
#include "RuntimeState/WetClothingRuntimeData.h"

#include "Runtime/Engine/Classes/Components/SkeletalMeshComponent.h"

#include "WetInputSystem/Sampling/WetClothingMeshSampler.h"
#include "WetInputSystem/WetInputStage.h"
#include "WetRendering/WetRenderStage.h"
#include "WetRendering/WetVertexColorBuffer.h"
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
    Receiver.SimulationState->CurrentPendingWetnessReadIndex = 0;
    Receiver.SimulationState->bPendingWetnessQueued.Init(false, VertexCount);
    Receiver.CachedWetVertexColors->Init(FLinearColor::Black, VertexCount);
    Receiver.SimulationState->DirtyWetVertexIndices.Reset();

    FWetVertexColorBuffer::ApplyVertexColorOverride(
        *Receiver.TargetSkeletalMesh,
        Receiver.LODIndex,
        *Receiver.CachedWetVertexColors);
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
            Receiver.RuntimeData->NeighborRanges,
            Receiver.RuntimeData->FlatNeighborIndices,
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
        Receiver.SimulationState->CurrentPendingWetnessReadIndex = 0;
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
        Receiver.SimulationState->CurrentPendingWetnessVertexIndexQueue.Reset();
        Receiver.SimulationState->CurrentPendingWetnessAmounts.Reset();
        Receiver.SimulationState->CurrentPendingWetnessReadIndex = 0;
    }

    if (Receiver.SimulationState->WetnessDryHoldTimePerVertex.Num() != VertexCount)
    {
        Receiver.SimulationState->WetnessDryHoldTimePerVertex.SetNumZeroed(VertexCount);
    }

    if (Receiver.SimulationState->bPendingWetnessQueued.Num() != VertexCount)
    {
        Receiver.SimulationState->bPendingWetnessQueued.Init(false, VertexCount);
        Receiver.SimulationState->UpdatingPendingWetnessVertexIndexQueue.Reset();
        Receiver.SimulationState->CurrentPendingWetnessVertexIndexQueue.Reset();
        Receiver.SimulationState->CurrentPendingWetnessAmounts.Reset();
        Receiver.SimulationState->CurrentPendingWetnessReadIndex = 0;
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
            Receiver.SimulationState->CurrentPendingWetnessReadIndex = 0;
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
            Receiver.SimulationState->CurrentPendingWetnessVertexIndexQueue.Reset();
            Receiver.SimulationState->CurrentPendingWetnessAmounts.Reset();
            Receiver.SimulationState->CurrentPendingWetnessReadIndex = 0;
        }
        if (Receiver.SimulationState->WetnessDryHoldTimePerVertex.Num() != VertexCount)
        {
            Receiver.SimulationState->WetnessDryHoldTimePerVertex.SetNumZeroed(VertexCount);
        }
        if (Receiver.SimulationState->bPendingWetnessQueued.Num() != VertexCount)
        {
            Receiver.SimulationState->bPendingWetnessQueued.Init(false, VertexCount);
            Receiver.SimulationState->UpdatingPendingWetnessVertexIndexQueue.Reset();
            Receiver.SimulationState->CurrentPendingWetnessVertexIndexQueue.Reset();
            Receiver.SimulationState->CurrentPendingWetnessAmounts.Reset();
            Receiver.SimulationState->CurrentPendingWetnessReadIndex = 0;
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
    if (!Receiver.RuntimeData)
    {
        return false;
    }

    Receiver.RuntimeData->ResetBoneOptimizationCache();

    auto SetFallbackReason = [&Receiver](const FString& Reason)
    {
        if (Receiver.RuntimeData)
        {
            Receiver.RuntimeData->BoneOptimizationCacheFallbackReason = Reason;
        }
    };

    if (!Receiver.TargetSkeletalMesh)
    {
        SetFallbackReason(TEXT("The target SkeletalMeshComponent is unavailable."));
        return false;
    }

    USkeletalMesh* SkeletalMesh = Receiver.TargetSkeletalMesh->GetSkeletalMeshAsset();
    if (!SkeletalMesh)
    {
        SetFallbackReason(TEXT("The target SkeletalMesh asset is unavailable."));
        return false;
    }

    if (!Receiver.bUsePrecomputedBoneOptimizationCache)
    {
        SetFallbackReason(TEXT("Precomputed bone optimization cache usage is disabled."));
        return false;
    }

    if (!Receiver.WetClothingAsset)
    {
        SetFallbackReason(TEXT("No WetClothingAsset is assigned, so no precomputed bone cache is registered."));
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
        SetFallbackReason(
            PrecomputedCacheErrorMessage.IsEmpty()
                ? FString(TEXT("The precomputed bone optimization cache is unavailable or invalid."))
                : PrecomputedCacheErrorMessage);
        return false;
    }

    const FWetBonePrimaryVertexCache& PrimaryCache =
        Receiver.RuntimeData->BoneOptimizationCache.PrimaryVertexCache;
    if (PrimaryCache.SourceMesh != SkeletalMesh ||
        PrimaryCache.LODIndex != LODIndex ||
        PrimaryCache.BoneCount <= 0 ||
        PrimaryCache.VertexCount <= 0 ||
        PrimaryCache.BoneStartOffsets.Num() != PrimaryCache.BoneCount + 1)
    {
        Receiver.RuntimeData->ResetBoneOptimizationCache();
        SetFallbackReason(TEXT("The copied precomputed bone cache contains no valid LOD primary-bone data."));
        return false;
    }

    Receiver.RuntimeData->bHasBoneOptimizationCache = true;
    Receiver.RuntimeData->BoneOptimizationCacheFallbackReason.Reset();
    return true;
}

bool FWetRuntimeDataBuilder::ResolveSpecificBonesToLoopThrough(
    const FWetClothingRuntimeData& RuntimeData,
    const USkeletalMeshComponent*  TargetSkeletalMesh,
    const FName                    HitBoneName,
    TArray<int32>&                 OutBoneIndices,
    FString*                       OutFallbackReason,
    const bool                     bRequireFullVertexTraversal) const
{
    OutBoneIndices.Reset();

    auto Fail = [OutFallbackReason](const FString& Reason)
    {
        if (OutFallbackReason)
        {
            *OutFallbackReason = Reason;
        }
        return false;
    };

    if (OutFallbackReason)
    {
        OutFallbackReason->Reset();
    }

    if (bRequireFullVertexTraversal)
    {
        return Fail(TEXT("The current request requires complete vertex information."));
    }

    if (HitBoneName.IsNone())
    {
        return Fail(TEXT("HitResult did not provide a HitBone/BoneName."));
    }

    if (!TargetSkeletalMesh)
    {
        return Fail(TEXT("The target SkeletalMeshComponent is unavailable."));
    }

    const USkeletalMesh* SkeletalMesh = TargetSkeletalMesh->GetSkeletalMeshAsset();
    if (!SkeletalMesh)
    {
        return Fail(TEXT("The target SkeletalMesh asset is unavailable."));
    }

    if (!RuntimeData.bHasBoneOptimizationCache)
    {
        return Fail(
            RuntimeData.BoneOptimizationCacheFallbackReason.IsEmpty()
                ? FString(TEXT("No usable precomputed bone optimization cache is registered."))
                : RuntimeData.BoneOptimizationCacheFallbackReason);
    }

    const FWetBonePrimaryVertexCache& PrimaryCache = RuntimeData.BoneOptimizationCache.PrimaryVertexCache;
    const int32                       BoneCount = SkeletalMesh->GetRefSkeleton().GetNum();
    if (PrimaryCache.SourceMesh != SkeletalMesh ||
        PrimaryCache.BoneCount != BoneCount ||
        PrimaryCache.VertexCount <= 0 ||
        PrimaryCache.BoneStartOffsets.Num() != BoneCount + 1 ||
        PrimaryCache.FlatVertexIndices.IsEmpty())
    {
        return Fail(TEXT("The precomputed bone cache is empty, stale, or belongs to a different SkeletalMesh."));
    }

    const int32 HitBoneIndex = SkeletalMesh->GetRefSkeleton().FindBoneIndex(HitBoneName);
    if (HitBoneIndex == INDEX_NONE)
    {
        return Fail(FString::Printf(
            TEXT("HitBone '%s' does not exist in the target reference skeleton."),
            *HitBoneName.ToString()));
    }

    TSet<int32> UniqueBoneIndices;
    auto AddBoneIndex = [&UniqueBoneIndices, &OutBoneIndices, BoneCount](const int32 BoneIndex)
    {
        if (BoneIndex < 0 || BoneIndex >= BoneCount || UniqueBoneIndices.Contains(BoneIndex))
        {
            return;
        }

        UniqueBoneIndices.Add(BoneIndex);
        OutBoneIndices.Add(BoneIndex);
    };

    AddBoneIndex(HitBoneIndex);

    if (const FWetResolvedBoneIncludeRule* IncludeRule =
            RuntimeData.BoneOptimizationCache.ResolvedIncludeRules.FindByPredicate(
                [HitBoneIndex](const FWetResolvedBoneIncludeRule& Candidate)
                {
                    return Candidate.TargetBoneIndex == HitBoneIndex;
                }))
    {
        for (const int32 IncludedBoneIndex : IncludeRule->IncludedBoneIndices)
        {
            AddBoneIndex(IncludedBoneIndex);
        }
    }

    if (OutBoneIndices.IsEmpty())
    {
        return Fail(TEXT("The resolved specific-bone candidate list is empty."));
    }

    if (OutBoneIndices.Num() >= BoneCount)
    {
        OutBoneIndices.Reset();
        return Fail(TEXT("The resolved specific-bone candidates cover the entire reference skeleton."));
    }

    return true;
}

bool FWetRuntimeDataBuilder::GetBoneCandidateVertexIndices(
    const FWetClothingRuntimeData& RuntimeData,
    const USkeletalMeshComponent*  TargetSkeletalMesh,
    const FName                    HitBoneName,
    TArray<int32>&                 OutVertexIndices,
    FString*                       OutFallbackReason,
    const bool                     bRequireFullVertexTraversal) const
{
    OutVertexIndices.Reset();

    TArray<int32> ResolvedBoneIndices;
    if (!ResolveSpecificBonesToLoopThrough(
            RuntimeData,
            TargetSkeletalMesh,
            HitBoneName,
            ResolvedBoneIndices,
            OutFallbackReason,
            bRequireFullVertexTraversal))
    {
        return false;
    }

    const FWetBonePrimaryVertexCache& PrimaryCache = RuntimeData.BoneOptimizationCache.PrimaryVertexCache;
    for (const int32 BoneIndex : ResolvedBoneIndices)
    {
        if (!PrimaryCache.BoneStartOffsets.IsValidIndex(BoneIndex + 1))
        {
            OutVertexIndices.Reset();
            if (OutFallbackReason)
            {
                *OutFallbackReason = TEXT("A resolved bone has no valid precomputed vertex range.");
            }
            return false;
        }

        const int32 StartOffset = PrimaryCache.BoneStartOffsets[BoneIndex];
        const int32 EndOffset = PrimaryCache.BoneStartOffsets[BoneIndex + 1];
        if (StartOffset < 0 || EndOffset < StartOffset || EndOffset > PrimaryCache.FlatVertexIndices.Num())
        {
            OutVertexIndices.Reset();
            if (OutFallbackReason)
            {
                *OutFallbackReason = TEXT("A precomputed primary-bone vertex range is corrupt.");
            }
            return false;
        }

        OutVertexIndices.Reserve(OutVertexIndices.Num() + (EndOffset - StartOffset));
        for (int32 Offset = StartOffset; Offset < EndOffset; ++Offset)
        {
            const int32 VertexIndex = PrimaryCache.FlatVertexIndices[Offset];
            if (VertexIndex < 0 || VertexIndex >= PrimaryCache.VertexCount)
            {
                OutVertexIndices.Reset();
                if (OutFallbackReason)
                {
                    *OutFallbackReason = TEXT("The precomputed bone cache contains an invalid vertex index.");
                }
                return false;
            }

            OutVertexIndices.Add(VertexIndex);
        }
    }

    // A valid specific-bone lookup may legitimately resolve to no Influence-0
    // vertices. This is a cache-search miss, not a cache-unavailable condition,
    // so the caller must not retry by traversing the entire LOD.
    return true;
}
