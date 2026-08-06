// Fill out your copyright notice in the Description page of Project Settings.

#include "RuntimeState/Utils/WetRuntimeDataBuilder.h"

#include "RuntimeState/WetPrecomputedSimulationDataBridge.h"
#include "RuntimeState/WetClothingRuntimeData.h"

#include "Runtime/Engine/Classes/Components/SkeletalMeshComponent.h"

#include "WetInputSystem/Sampling/WetClothingMeshSampler.h"
#include "RuntimeState/Utils/WetInputStage.h"
#include "WetRendering/WetRenderStage.h"
#include "WetRendering/WetVertexColorBuffer.h"
#include "WetSimulation/AbsorbedWetness/AbsorbedWetnessSimulationState.h"
#include "Runtime/Engine/Classes/Engine/SkeletalMesh.h"
#include "Runtime/Engine/Public/Rendering/SkeletalMeshLODRenderData.h"
#include "Runtime/Engine/Public/Rendering/SkeletalMeshRenderData.h"
#include "DataAssets/WetClothingAsset.h"
#include "DataAssets/WetClothingGPUData.h"
#include "DataAssets/WetnessProfile.h"


namespace
{
    FWetnessProfileParameters ResolveRuntimeWetnessProfileParameters(
        const UWetClothingAsset& Asset,
        const int32 ProfileIndex,
        const UObject* OwnerForLogs)
    {
        const FWetClothingEditableWetPartData& EditableWetPartData =
            Asset.Authored.PartData.EditableWetPartData;
        const FWetPartProfileAssignment* ProfileAssignment =
            EditableWetPartData.FindProfile(ProfileIndex);
        if (ProfileAssignment == nullptr)
        {
            return FWetnessProfileParameters();
        }

#if WITH_EDITOR
        if (ProfileAssignment->HasSourceProfile())
        {
            UObject* SourceObject = ProfileAssignment->GetSourceProfilePath().ResolveObject();
            if (SourceObject == nullptr)
            {
                SourceObject = ProfileAssignment->GetSourceProfilePath().TryLoad();
            }

            if (const UWetnessProfile* SourceProfile =
                    Cast<UWetnessProfile>(SourceObject))
            {
                return SourceProfile->GetParameters();
            }
            else
            {
                UE_LOG(
                    LogTemp,
                    Warning,
                    TEXT("DynamicWetClothesComponent: Failed to resolve Wetness Profile '%s' for WCA '%s' on '%s'. Using the WCA snapshot/fallback profile."),
                    *ProfileAssignment->GetSourceProfilePath().ToString(),
                    *GetNameSafe(&Asset),
                    *GetNameSafe(OwnerForLogs));
            }
        }

#endif

        return Asset.Derived.Inline.ResolvedWetnessProfileParameters.IsValidIndex(ProfileIndex)
            ? Asset.Derived.Inline.ResolvedWetnessProfileParameters[ProfileIndex]
            : ProfileAssignment->Parameters;
    }

}
void FWetRuntimeDataBuilder::InitializeAbsorbedWetnessData(FWetRuntimeDataBuildArgs& Receiver)
{
    constexpr int32 RuntimeLODIndex = UWetClothingAsset::RuntimeSimulationLODIndex;
    FSkeletalMeshLODRenderData* LODData = nullptr;
    if (!GetLODRenderData(Receiver.TargetSkeletalMesh, RuntimeLODIndex, LODData))
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
    Receiver.CachedWetVertexColors->Init(FColor::Black, VertexCount);
    Receiver.SimulationState->DirtyWetVertexIndices.Reset();
    Receiver.SimulationState->bDirtyWetVertexQueued.Init(false, VertexCount);

    FWetVertexColorBuffer::ApplyVertexColorOverride(
        *Receiver.TargetSkeletalMesh,
        RuntimeLODIndex,
        *Receiver.CachedWetVertexColors);
}

bool FWetRuntimeDataBuilder::InitializeWetPartVertexData(FWetRuntimeDataBuildArgs& Receiver)
{
    constexpr int32 RuntimeLODIndex = UWetClothingAsset::RuntimeSimulationLODIndex;
    if (Receiver.MutableRuntimeData == nullptr)
    {
        return false;
    }

    FSkeletalMeshLODRenderData* LODData = nullptr;
    if (!GetLODRenderData(Receiver.TargetSkeletalMesh, RuntimeLODIndex, LODData))
    {
        return false;
    }

    const int32 VertexCount = LODData->GetNumVertices();
    Receiver.MutableRuntimeData->VertexWetPartIDs.Init(INDEX_NONE, VertexCount);
    Receiver.MutableRuntimeData->VertexWettableFlags.Init(false, VertexCount);
    Receiver.MutableRuntimeData->VertexAbsorbedWetnessFlags.Init(false, VertexCount);
    Receiver.MutableRuntimeData->WetnessProfileTable.Reset();
    Receiver.MutableRuntimeData->VertexWetnessProfileIndices.Init(
        FWetClothingRuntimeData::InvalidWetnessProfileIndex,
        VertexCount);

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
    if (Receiver.WetClothingAsset->GetDWCSkeletalMesh() && Receiver.WetClothingAsset->GetDWCSkeletalMesh() != SkeletalMesh)
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("DynamicWetClothesComponent: WetClothingAsset DWC Skeletal Mesh does not match the receiver mesh on %s."),
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

    if (!InitializeWetPartVertexDataFromPrecomputedData(Receiver, VertexCount))
    {
        const USkeletalMesh* RuntimeMesh = Receiver.TargetSkeletalMesh != nullptr
                                               ? Receiver.TargetSkeletalMesh->GetSkeletalMeshAsset()
                                               : nullptr;
        const FString ValidationSummary = Receiver.WetClothingAsset != nullptr
                                              ? Receiver.WetClothingAsset->GetPrecomputedSimulationDataValidationSummary(RuntimeMesh)
                                              : FString(TEXT("CPUPrecomputed{asset=null}"));
        UE_LOG(
            LogTemp,
            Error,
            TEXT("DynamicWetClothesComponent: Failed to initialize wet part vertex data from precomputed simulation data on %s. %s Open the Wet Clothing Asset and save it to update runtime-ready data."),
            *GetNameSafe(Receiver.OwnerForLogs),
            *ValidationSummary);
        return false;
    }

    return true;
}

bool FWetRuntimeDataBuilder::InitializeWetPartVertexDataFromPrecomputedData(
    FWetRuntimeDataBuildArgs& Receiver,
    const int32 VertexCount)
{
    if (!Receiver.WetClothingAsset || !Receiver.TargetSkeletalMesh)
    {
        return false;
    }

    const USkeletalMesh* SkeletalMesh = Receiver.TargetSkeletalMesh->GetSkeletalMeshAsset();
    if (!Receiver.bPrecomputedDataAlreadyValidated &&
        !Receiver.WetClothingAsset->IsPrecomputedSimulationDataValidForMesh(SkeletalMesh))
    {
        const FString ValidationSummary =
            Receiver.WetClothingAsset->GetPrecomputedSimulationDataValidationSummary(SkeletalMesh);
        if (Receiver.WetClothingAsset->GetPrecomputedSimulationData().bIsValid)
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("DynamicWetClothesComponent: WetClothingAsset precomputed simulation data is stale for %s. %s"),
                *GetNameSafe(Receiver.OwnerForLogs),
                *ValidationSummary);
        }
        else
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("DynamicWetClothesComponent: WetClothingAsset precomputed simulation data is unavailable for %s. %s"),
                *GetNameSafe(Receiver.OwnerForLogs),
                *ValidationSummary);
        }
        return false;
    }

    const FWetClothingPrecomputedSimulationData& PrecomputedData = Receiver.WetClothingAsset->GetPrecomputedSimulationData();
    if (PrecomputedData.VertexCount != VertexCount || PrecomputedData.Vertices.Num() != VertexCount)
    {
        const FString ValidationSummary =
            Receiver.WetClothingAsset->GetPrecomputedSimulationDataValidationSummary(SkeletalMesh);
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("DynamicWetClothesComponent: WetClothingAsset precomputed simulation data vertex count mismatch on %s. RuntimeVertexCount=%d, PrecomputedVertexCount=%d, PrecomputedVertices=%d. %s"),
            *GetNameSafe(Receiver.OwnerForLogs),
            VertexCount,
            PrecomputedData.VertexCount,
            PrecomputedData.Vertices.Num(),
            *ValidationSummary);
        return false;
    }

    const FWetClothingEditableWetPartData& EditableWetPartData =
        Receiver.WetClothingAsset->Authored.PartData.EditableWetPartData;
    Receiver.MutableRuntimeData->WetnessProfileTable.SetNum(EditableWetPartData.Profiles.Num());
    for (int32 ProfileIndex = 0; ProfileIndex < EditableWetPartData.Profiles.Num(); ++ProfileIndex)
    {
        Receiver.MutableRuntimeData->WetnessProfileTable[ProfileIndex] =
            ResolveRuntimeWetnessProfileParameters(
                *Receiver.WetClothingAsset,
                ProfileIndex,
                Receiver.OwnerForLogs);
    }
    if (Receiver.MutableRuntimeData->WetnessProfileTable.IsEmpty())
    {
        Receiver.MutableRuntimeData->WetnessProfileTable.Add(FWetnessProfileParameters());
    }

    if (Receiver.MutableRuntimeData->WetnessProfileTable.Num() >=
        static_cast<int32>(FWetClothingRuntimeData::InvalidWetnessProfileIndex))
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("DynamicWetClothesComponent: Too many authored wetness profiles for uint16 vertex mapping on %s."),
            *GetNameSafe(Receiver.OwnerForLogs));
        return false;
    }

    int32 PrecomputedWettableVertexCount = 0;
    int32 RuntimeWettableVertexCount = 0;

    for (int32 VertexIndex = 0; VertexIndex < PrecomputedData.Vertices.Num(); ++VertexIndex)
    {
        const FWetClothingPrecomputedVertexData& PrecomputedVertex = PrecomputedData.Vertices[VertexIndex];
        if (!Receiver.MutableRuntimeData->VertexWetPartIDs.IsValidIndex(VertexIndex) ||
            !Receiver.MutableRuntimeData->VertexWettableFlags.IsValidIndex(VertexIndex) ||
            !Receiver.MutableRuntimeData->VertexAbsorbedWetnessFlags.IsValidIndex(VertexIndex) ||
            !Receiver.MutableRuntimeData->VertexWetnessProfileIndices.IsValidIndex(VertexIndex))
        {
            continue;
        }

        if (PrecomputedVertex.IsWettable())
        {
            ++PrecomputedWettableVertexCount;
        }

        const int32 ProfileIndex = PrecomputedVertex.ProfileIndex;
        if (PrecomputedVertex.IsWettable() &&
            EditableWetPartData.Profiles.IsValidIndex(ProfileIndex) &&
            Receiver.MutableRuntimeData->WetnessProfileTable.IsValidIndex(ProfileIndex))
        {
            Receiver.MutableRuntimeData->VertexWettableFlags[VertexIndex] = true;
            Receiver.MutableRuntimeData->VertexWetPartIDs[VertexIndex] = PrecomputedVertex.WetPartID;
            Receiver.MutableRuntimeData->VertexWetnessProfileIndices[VertexIndex] = static_cast<uint16>(ProfileIndex);
            const FWetnessProfileParameters* Profile = Receiver.MutableRuntimeData->GetWetnessProfileParameters(VertexIndex);
            Receiver.MutableRuntimeData->VertexAbsorbedWetnessFlags[VertexIndex] =
                Profile != nullptr && Profile->SupportsAbsorbedWetness();
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


bool FWetRuntimeDataBuilder::InitializeWetPartVertexDataFromGPUData(
    FWetRuntimeDataBuildArgs& Receiver,
    const int32 VertexCount)
{
    constexpr int32 RuntimeLODIndex = UWetClothingAsset::RuntimeSimulationLODIndex;
    if (Receiver.MutableRuntimeData == nullptr ||
        Receiver.WetClothingAsset == nullptr ||
        Receiver.TargetSkeletalMesh == nullptr ||
        VertexCount <= 0)
    {
        return false;
    }

    const USkeletalMesh* SkeletalMesh = Receiver.TargetSkeletalMesh->GetSkeletalMeshAsset();
    if (SkeletalMesh == nullptr ||
        !Receiver.WetClothingAsset->IsGPURuntimeDataValidForMesh(SkeletalMesh, RuntimeLODIndex))
    {
        return false;
    }

    const FDWCGPULODBakeData& GPUData =
        Receiver.WetClothingAsset->GetGPUWetMapRuntimeData(RuntimeLODIndex);
    if (GPUData.Triangles.IsEmpty())
    {
        return false;
    }

    FWetClothingRuntimeData& RuntimeData = *Receiver.MutableRuntimeData;
    RuntimeData.VertexWetPartIDs.Init(INDEX_NONE, VertexCount);
    RuntimeData.VertexWettableFlags.Init(false, VertexCount);
    RuntimeData.VertexAbsorbedWetnessFlags.Init(false, VertexCount);
    RuntimeData.VertexWetnessProfileIndices.Init(
        FWetClothingRuntimeData::InvalidWetnessProfileIndex,
        VertexCount);
    RuntimeData.WetnessProfileTable.Reset();
    RuntimeData.ResetNeighborGraph();
    RuntimeData.ResetBoneOptimizationCache();

    const FWetClothingEditableWetPartData& WetPartData =
        Receiver.WetClothingAsset->Authored.PartData.EditableWetPartData;
    RuntimeData.WetnessProfileTable.SetNum(WetPartData.Profiles.Num());
    for (int32 ProfileIndex = 0; ProfileIndex < WetPartData.Profiles.Num(); ++ProfileIndex)
    {
        RuntimeData.WetnessProfileTable[ProfileIndex] =
            ResolveRuntimeWetnessProfileParameters(
                *Receiver.WetClothingAsset,
                ProfileIndex,
                Receiver.OwnerForLogs);
    }
    if (RuntimeData.WetnessProfileTable.IsEmpty())
    {
        RuntimeData.WetnessProfileTable.Add(FWetnessProfileParameters());
    }
    if (RuntimeData.WetnessProfileTable.Num() >=
        static_cast<int32>(FWetClothingRuntimeData::InvalidWetnessProfileIndex))
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("DynamicWetClothesComponent: Too many authored wetness profiles for GPU vertex mapping on %s."),
            *GetNameSafe(Receiver.OwnerForLogs));
        return false;
    }

    // Match the CPU precompute policy for vertices shared by multiple sections:
    // the lowest material-slot index owns the deterministic vertex binding.
    TArray<int32> AssignedMaterialSlots;
    AssignedMaterialSlots.Init(INDEX_NONE, VertexCount);
    int32 AssignedWettableVertexCount = 0;

    for (const FDWCGPUBakedTriangle& Triangle : GPUData.Triangles)
    {
        if (!Triangle.IsValid())
        {
            continue;
        }

        const FWetClothingAuthoredMaterialSlot* Slot =
            WetPartData.FindMaterialSlot(Triangle.MaterialSlotIndex);
        if (Slot == nullptr || !Slot->bIsWettableSlot)
        {
            continue;
        }

        const FWetClothingWetPartEntry* Part = Slot->WetPartEntries.FindByPredicate(
            [&Triangle](const FWetClothingWetPartEntry& Candidate)
            {
                return Candidate.WetPartID != 0 &&
                       Candidate.AssignedUVIslandIDs.Contains(Triangle.UVIslandID);
            });
        if (Part == nullptr)
        {
            continue;
        }

        const int32 EffectiveProfileIndex = WetPartData.Profiles.IsValidIndex(Part->ProfileIndex)
            ? Part->ProfileIndex
            : 0;
        if (!RuntimeData.WetnessProfileTable.IsValidIndex(EffectiveProfileIndex))
        {
            continue;
        }

        const FWetnessProfileParameters& Profile =
            RuntimeData.WetnessProfileTable[EffectiveProfileIndex];
        const int32 TriangleVertices[3] =
        {
            Triangle.VertexIndices.X,
            Triangle.VertexIndices.Y,
            Triangle.VertexIndices.Z
        };

        for (const int32 VertexIndex : TriangleVertices)
        {
            if (!RuntimeData.VertexWetPartIDs.IsValidIndex(VertexIndex))
            {
                continue;
            }

            const int32 ExistingSlot = AssignedMaterialSlots[VertexIndex];
            if (ExistingSlot != INDEX_NONE && ExistingSlot <= Triangle.MaterialSlotIndex)
            {
                continue;
            }

            if (!RuntimeData.VertexWettableFlags[VertexIndex])
            {
                ++AssignedWettableVertexCount;
            }
            AssignedMaterialSlots[VertexIndex] = Triangle.MaterialSlotIndex;
            RuntimeData.VertexWetPartIDs[VertexIndex] = Part->WetPartID;
            RuntimeData.VertexWettableFlags[VertexIndex] = true;
            RuntimeData.VertexAbsorbedWetnessFlags[VertexIndex] =
                Profile.SupportsAbsorbedWetness();
            RuntimeData.VertexWetnessProfileIndices[VertexIndex] =
                static_cast<uint16>(EffectiveProfileIndex);
        }
    }

    if (AssignedWettableVertexCount <= 0)
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("DynamicWetClothesComponent: GPU runtime data initialized no wettable vertex bindings for WCA '%s' on %s."),
            *GetNameSafe(Receiver.WetClothingAsset),
            *GetNameSafe(Receiver.OwnerForLogs));
        return false;
    }

    return true;
}

bool FWetRuntimeDataBuilder::InitializeNeighborGraphFromPrecomputedData(FWetRuntimeDataBuildArgs& Receiver)
{
    constexpr int32 RuntimeLODIndex = UWetClothingAsset::RuntimeSimulationLODIndex;
    if (Receiver.MutableRuntimeData == nullptr)
    {
        return false;
    }

    FSkeletalMeshLODRenderData* LODData = nullptr;
    if (!GetLODRenderData(Receiver.TargetSkeletalMesh, RuntimeLODIndex, LODData))
    {
        return false;
    }

    const int32 VertexCount = LODData->GetNumVertices();
    Receiver.MutableRuntimeData->ResetNeighborGraph();

    const USkeletalMesh* SkeletalMesh = Receiver.TargetSkeletalMesh ? Receiver.TargetSkeletalMesh->GetSkeletalMeshAsset() : nullptr;
    FString ErrorMessage;
    if (!FWetPrecomputedSimulationDataBridge::TryCopyPrecomputedNeighborGraph(
            Receiver.WetClothingAsset,
            SkeletalMesh,
            VertexCount,
            Receiver.MutableRuntimeData->NeighborRanges,
            Receiver.MutableRuntimeData->FlatNeighborIndices,
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

    Receiver.MutableRuntimeData->bHasNeighborGraph = true;
    return true;
}

void FWetRuntimeDataBuilder::EnsureWetnessBufferSize(FWetRuntimeDataBuildArgs& Receiver, const int32 VertexCount)
{
    if (VertexCount <= 0)
    {
        Receiver.SimulationState->AbsorbedWetnessPerVertex.Reset();
        Receiver.SimulationState->UpdatingPendingWetnessAmounts.Reset();
        Receiver.SimulationState->WetnessDryHoldTimePerVertex.Reset();
        Receiver.SimulationState->UpdatingPendingWetnessVertexIndexQueue.Reset();
        Receiver.SimulationState->CurrentPendingWetnessVertexIndexQueue.Reset();
        Receiver.SimulationState->CurrentPendingWetnessAmounts.Reset();
        Receiver.SimulationState->CurrentPendingWetnessReadIndex = 0;
        Receiver.SimulationState->bPendingWetnessQueued.Reset();
        Receiver.SimulationState->DirtyWetVertexIndices.Reset();
        Receiver.SimulationState->bDirtyWetVertexQueued.Reset();
        return;
    }

    if (Receiver.SimulationState->AbsorbedWetnessPerVertex.Num() != VertexCount)
    {
        Receiver.SimulationState->AbsorbedWetnessPerVertex.SetNumZeroed(VertexCount);
    }

    if (Receiver.RuntimeData != nullptr && Receiver.RuntimeData->VertexCount != VertexCount)
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("DynamicWetClothesComponent: Shared runtime vertex count mismatch on %s. Shared=%d Runtime=%d."),
            *GetNameSafe(Receiver.OwnerForLogs),
            Receiver.RuntimeData->VertexCount,
            VertexCount);
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

    if (Receiver.SimulationState->bDirtyWetVertexQueued.Num() != VertexCount)
    {
        Receiver.SimulationState->DirtyWetVertexIndices.Reset();
        Receiver.SimulationState->bDirtyWetVertexQueued.Init(false, VertexCount);
    }
}

void FWetRuntimeDataBuilder::EnsureWetnessBufferSize(FWetInputStageArgs& Receiver, const int32 VertexCount)
{
    if (Receiver.SimulationState == nullptr)
    {
        return;
    }

    if (VertexCount <= 0)
    {
        Receiver.SimulationState->AbsorbedWetnessPerVertex.Reset();
        Receiver.SimulationState->UpdatingPendingWetnessAmounts.Reset();
        Receiver.SimulationState->WetnessDryHoldTimePerVertex.Reset();
        Receiver.SimulationState->UpdatingPendingWetnessVertexIndexQueue.Reset();
        Receiver.SimulationState->CurrentPendingWetnessVertexIndexQueue.Reset();
        Receiver.SimulationState->CurrentPendingWetnessAmounts.Reset();
        Receiver.SimulationState->CurrentPendingWetnessReadIndex = 0;
        Receiver.SimulationState->bPendingWetnessQueued.Reset();
        Receiver.SimulationState->DirtyWetVertexIndices.Reset();
        Receiver.SimulationState->bDirtyWetVertexQueued.Reset();
        return;
    }

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
    if (Receiver.SimulationState->bDirtyWetVertexQueued.Num() != VertexCount)
    {
        Receiver.SimulationState->DirtyWetVertexIndices.Reset();
        Receiver.SimulationState->bDirtyWetVertexQueued.Init(false, VertexCount);
    }

    if (Receiver.RuntimeData != nullptr && Receiver.RuntimeData->VertexCount != VertexCount)
    {
        UE_LOG(
            LogTemp,
            Error,
            TEXT("DynamicWetClothesComponent: Shared runtime vertex count changed after initialization. Shared=%d Runtime=%d."),
            Receiver.RuntimeData->VertexCount,
            VertexCount);
    }
}

bool FWetRuntimeDataBuilder::GetLODRenderData(
    const USkeletalMeshComponent* TargetSkeletalMesh,
    int32                         LODIndex,
    FSkeletalMeshLODRenderData*&  OutLODData)
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
    FWetRuntimeDataBuildArgs& Receiver)
{
    constexpr int32 RuntimeLODIndex = UWetClothingAsset::RuntimeSimulationLODIndex;
    if (!Receiver.MutableRuntimeData)
    {
        return false;
    }

    Receiver.MutableRuntimeData->ResetBoneOptimizationCache();

    auto SetFallbackReason = [&Receiver](const FString& Reason)
    {
        if (Receiver.MutableRuntimeData)
        {
            Receiver.MutableRuntimeData->BoneOptimizationCacheFallbackReason = Reason;
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
            Receiver.MutableRuntimeData->BoneOptimizationCache,
            &PrecomputedCacheErrorMessage))
    {
        SetFallbackReason(
            PrecomputedCacheErrorMessage.IsEmpty()
                ? FString(TEXT("The precomputed bone optimization cache is unavailable or invalid."))
                : PrecomputedCacheErrorMessage);
        return false;
    }

    const FWetBonePrimaryVertexCache& PrimaryCache =
        Receiver.MutableRuntimeData->BoneOptimizationCache.PrimaryVertexCache;
    if (PrimaryCache.SourceMesh != SkeletalMesh ||
        PrimaryCache.LODIndex != RuntimeLODIndex ||
        PrimaryCache.BoneCount <= 0 ||
        PrimaryCache.VertexCount <= 0 ||
        PrimaryCache.BoneStartOffsets.Num() != PrimaryCache.BoneCount + 1)
    {
        Receiver.MutableRuntimeData->ResetBoneOptimizationCache();
        SetFallbackReason(TEXT("The copied precomputed bone cache contains no valid LOD primary-bone data."));
        return false;
    }

    Receiver.MutableRuntimeData->bHasBoneOptimizationCache = true;
    Receiver.MutableRuntimeData->BoneOptimizationCacheFallbackReason.Reset();
    return true;
}

bool FWetRuntimeDataBuilder::ResolveSpecificBonesToLoopThrough(
    const FWetClothingRuntimeData& RuntimeData,
    const USkeletalMeshComponent*  TargetSkeletalMesh,
    const FName                    HitBoneName,
    TArray<int32>&                 OutBoneIndices,
    FString*                       OutFallbackReason,
    const bool                     bRequireFullVertexTraversal)
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
    const bool                     bRequireFullVertexTraversal)
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
