//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "RuntimeState/Utils/DWCLODVertexColorTransferCoordinator.h"

#include "Async/DWCLODVertexColorTasks.h"
#include "Async/DWCTaskQueue.h"
#include "Components/DynamicWetClothesComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Profiling/DWCStats.h"
#include "RuntimeState/DWCLODVertexColorTransferMapBuilder.h"
#include "RuntimeState/DWCRuntimeDataSubsystem.h"
#include "WetRendering/WetVertexColorBuffer.h"
#include "Utility/DWCProfiling.h"

bool FDWCLODVertexColorTransferCoordinator::InitializeReceiver(
    FDWCWetMeshReceiverRuntime& Receiver,
    UDWCRuntimeDataSubsystem& RuntimeDataSubsystem,
    const int32 RuntimeLODIndex) const
{
    Receiver.LODVertexStaticDataByLOD.Reset();
    Receiver.LODVertexColorTransferMapsByLOD.Reset();
    Receiver.LODVertexColorCachesByLOD.Reset();
    Receiver.PendingLODVertexColorDirtySourceVertices.Reset();

    if (!Receiver.WetClothingAsset.IsValid() || !Receiver.SharedRuntimeData.IsValid())
    {
        return false;
    }

    USkeletalMeshComponent* Mesh = Receiver.MeshComponent.Get();
    const USkeletalMesh* SkeletalMesh = Mesh != nullptr ? Mesh->GetSkeletalMeshAsset() : nullptr;
    FSkeletalMeshRenderData* RenderData = SkeletalMesh != nullptr ? SkeletalMesh->GetResourceForRendering() : nullptr;
    if (Mesh == nullptr || RenderData == nullptr)
    {
        return false;
    }

    const FString& MeshSignature = Receiver.SharedRuntimeData->MeshSignature;
    for (int32 LODIndex = 0; LODIndex < RenderData->LODRenderData.Num(); ++LODIndex)
    {
        TSharedPtr<const FDWCLODVertexStaticData, ESPMode::ThreadSafe> LODVertexData =
            RuntimeDataSubsystem.AcquireLODVertexStaticData(*Mesh, LODIndex, MeshSignature);
        if (LODVertexData.IsValid())
        {
            Receiver.LODVertexStaticDataByLOD.Add(LODIndex, LODVertexData);
        }
    }

    const TSharedPtr<const FDWCLODVertexStaticData, ESPMode::ThreadSafe> SourceLODData =
        Receiver.LODVertexStaticDataByLOD.FindRef(RuntimeLODIndex);
    if (!SourceLODData.IsValid())
    {
        return false;
    }

    for (const FWCALODVertexColorRuntimeData& RuntimeData :
         Receiver.WetClothingAsset->Derived.Bulk.LODVertexColorRuntimeData)
    {
        const TSharedPtr<const FDWCLODVertexStaticData, ESPMode::ThreadSafe> TargetLODData =
            Receiver.LODVertexStaticDataByLOD.FindRef(RuntimeData.TargetLODIndex);
        if (!RuntimeData.IsValid() ||
            RuntimeData.SourceLODIndex != RuntimeLODIndex ||
            RuntimeData.MeshSignature != MeshSignature ||
            !TargetLODData.IsValid() ||
            TargetLODData->Geometry.VertexCount != RuntimeData.TargetVertexCount)
        {
            continue;
        }

        TSharedPtr<const TArray<int32>, ESPMode::ThreadSafe> SharedTransferMap =
            RuntimeDataSubsystem.FindLODVertexColorTransferMap(
                *Mesh,
                *SourceLODData,
                *TargetLODData,
                MeshSignature);
        if (!SharedTransferMap.IsValid())
        {
            TArray<int32> TransferMapCopy(RuntimeData.TargetToSourceVertex);
            SharedTransferMap = RuntimeDataSubsystem.CacheLODVertexColorTransferMap(
                *Mesh,
                *SourceLODData,
                *TargetLODData,
                MeshSignature,
                MoveTemp(TransferMapCopy));
        }

        if (SharedTransferMap.IsValid())
        {
            Receiver.LODVertexColorTransferMapsByLOD.Add(RuntimeData.TargetLODIndex, SharedTransferMap);
        }
    }

    TArray<FDWCLODVertexColorTransferTargetGeometryView> MissingTargetGeometries;
    for (const TPair<int32, TSharedPtr<const FDWCLODVertexStaticData, ESPMode::ThreadSafe>>& Pair :
         Receiver.LODVertexStaticDataByLOD)
    {
        if (Pair.Key == RuntimeLODIndex ||
            !Pair.Value.IsValid() ||
            Receiver.LODVertexColorTransferMapsByLOD.Contains(Pair.Key))
        {
            continue;
        }

        TSharedPtr<const TArray<int32>, ESPMode::ThreadSafe> SharedTransferMap =
            RuntimeDataSubsystem.FindLODVertexColorTransferMap(
                *Mesh,
                *SourceLODData,
                *Pair.Value,
                MeshSignature);
        if (!SharedTransferMap.IsValid())
        {
            MissingTargetGeometries.Add({
                Pair.Key,
                FDWCLODVertexColorTransferGeometryView{
                    Pair.Value->Geometry.LocalPositions,
                    Pair.Value->Geometry.LocalNormals
                }
            });
            continue;
        }

        Receiver.LODVertexColorTransferMapsByLOD.Add(Pair.Key, SharedTransferMap);
    }

    TArray<FDWCLODVertexColorTransferMapBuildResult> BuiltTransferMaps;
    if (BuildDWCLODVertexColorTransferMaps(
            FDWCLODVertexColorTransferGeometryView{
                SourceLODData->Geometry.LocalPositions,
                SourceLODData->Geometry.LocalNormals
            },
            MissingTargetGeometries,
            BuiltTransferMaps))
    {
        for (FDWCLODVertexColorTransferMapBuildResult& BuiltTransferMap : BuiltTransferMaps)
        {
            const TSharedPtr<const FDWCLODVertexStaticData, ESPMode::ThreadSafe> TargetLODData =
                Receiver.LODVertexStaticDataByLOD.FindRef(BuiltTransferMap.LODIndex);
            if (!TargetLODData.IsValid())
            {
                continue;
            }

            TSharedPtr<const TArray<int32>, ESPMode::ThreadSafe> SharedTransferMap =
                RuntimeDataSubsystem.CacheLODVertexColorTransferMap(
                    *Mesh,
                    *SourceLODData,
                    *TargetLODData,
                    MeshSignature,
                    MoveTemp(BuiltTransferMap.TargetToSourceVertex));
            if (SharedTransferMap.IsValid())
            {
                Receiver.LODVertexColorTransferMapsByLOD.Add(BuiltTransferMap.LODIndex, SharedTransferMap);
            }
        }
    }

    return true;
}

bool FDWCLODVertexColorTransferCoordinator::RequestTask(
    UDynamicWetClothesComponent& Owner,
    FDWCTaskQueue* AsyncTaskQueue,
    UWorld* World,
    FDWCWetMeshReceiverRuntime& Receiver) const
{
    DWC_PROFILE_SCOPE(DWC_Component_RequestLODVertexColorTransferTask);

    if (AsyncTaskQueue == nullptr ||
        !Receiver.RenderStage.IsValid() ||
        Receiver.LODVertexStaticDataByLOD.Num() <= 1)
    {
        return false;
    }

    USkeletalMeshComponent* Mesh = Receiver.MeshComponent.Get();
    if (Mesh == nullptr)
    {
        return false;
    }

    if (Receiver.bLODVertexColorTransferPending)
    {
        Receiver.bLODVertexColorTransferRequestedAgain = true;
        Owner.SetComponentTickEnabled(true);
        return true;
    }

    const int32 SourceLODIndex = UWetClothingAsset::RuntimeSimulationLODIndex;
    TSharedPtr<const FDWCLODVertexStaticData, ESPMode::ThreadSafe> SourceLODData =
        Receiver.LODVertexStaticDataByLOD.FindRef(SourceLODIndex);
    if (!SourceLODData.IsValid() ||
        Receiver.RenderStage->CachedWetVertexColors.Num() != SourceLODData->Geometry.VertexCount)
    {
        return false;
    }

    FDWCLODVertexColorTransferSnapshot Snapshot;
    Snapshot.ReceiverId = Receiver.ReceiverId;
    Snapshot.Generation = ++Receiver.LODVertexColorTransferGeneration;
    Snapshot.SourceLODData = SourceLODData;
    Snapshot.SourceColors = Receiver.RenderStage->CachedWetVertexColors;
    Snapshot.DirtySourceVertices = Receiver.PendingLODVertexColorDirtySourceVertices;
    Receiver.PendingLODVertexColorDirtySourceVertices.Reset();

    UDWCRuntimeDataSubsystem* RuntimeDataSubsystem =
        World != nullptr ? World->GetSubsystem<UDWCRuntimeDataSubsystem>() : nullptr;
    const FString MeshSignature = Receiver.SharedRuntimeData.IsValid()
                                      ? Receiver.SharedRuntimeData->MeshSignature
                                      : FString();

    for (const TPair<int32, TSharedPtr<const FDWCLODVertexStaticData, ESPMode::ThreadSafe>>& Pair :
         Receiver.LODVertexStaticDataByLOD)
    {
        if (Pair.Key == SourceLODIndex || !Pair.Value.IsValid())
        {
            continue;
        }

        Snapshot.TargetLODData.Add(Pair.Value);

        TSharedPtr<const TArray<int32>, ESPMode::ThreadSafe> CachedTransferMap =
            Receiver.LODVertexColorTransferMapsByLOD.FindRef(Pair.Key);
        if (!CachedTransferMap.IsValid() && RuntimeDataSubsystem != nullptr)
        {
            CachedTransferMap = RuntimeDataSubsystem->FindLODVertexColorTransferMap(
                *Mesh,
                *SourceLODData,
                *Pair.Value,
                MeshSignature);
            if (CachedTransferMap.IsValid())
            {
                Receiver.LODVertexColorTransferMapsByLOD.Add(Pair.Key, CachedTransferMap);
            }
        }

        if (CachedTransferMap.IsValid())
        {
            Snapshot.CachedTargetToSourceVertexByLOD.Add(Pair.Key, CachedTransferMap);
        }
        if (const TSharedPtr<const TArray<FColor>, ESPMode::ThreadSafe>* CachedColors =
                Receiver.LODVertexColorCachesByLOD.Find(Pair.Key))
        {
            Snapshot.CachedTargetColorsByLOD.Add(Pair.Key, *CachedColors);
        }
    }

    if (Snapshot.TargetLODData.IsEmpty())
    {
        return false;
    }

    Receiver.bLODVertexColorTransferPending = true;
    Receiver.bLODVertexColorTransferRequestedAgain = false;

    AsyncTaskQueue->Enqueue(
        MakeShared<FDWCLODVertexColorTransferTask, ESPMode::ThreadSafe>(&Owner, MoveTemp(Snapshot)));
    Owner.SetComponentTickEnabled(true);
    return true;
}

void FDWCLODVertexColorTransferCoordinator::CommitTaskResult(
    UDynamicWetClothesComponent& Owner,
    TArray<TUniquePtr<FDWCWetMeshReceiverRuntime>>& Receivers,
    FDWCTaskQueue* AsyncTaskQueue,
    UWorld* World,
    FDWCLODVertexColorTransferResult&& Result,
    const bool bHasPendingCpuSkinningTasks) const
{
    DWC_PROFILE_SCOPE(DWC_Component_CommitLODVertexColorTransferResult);

    for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers)
    {
        if (!Receiver.IsValid() ||
            Receiver->ReceiverId != Result.ReceiverId ||
            Receiver->LODVertexColorTransferGeneration != Result.Generation)
        {
            continue;
        }

        Receiver->bLODVertexColorTransferPending = false;

        USkeletalMeshComponent* Mesh = Receiver->MeshComponent.Get();
        if (Mesh == nullptr)
        {
            return;
        }

        FDWCWorkloadStats::RecordLODTransferCompleted(
            static_cast<uint32>(FMath::Max(0, Result.DirtySourceVertexCount)));

        UDWCRuntimeDataSubsystem* RuntimeDataSubsystem =
            World != nullptr ? World->GetSubsystem<UDWCRuntimeDataSubsystem>() : nullptr;
        const TSharedPtr<const FDWCLODVertexStaticData, ESPMode::ThreadSafe> SourceLODData =
            Receiver->LODVertexStaticDataByLOD.FindRef(UWetClothingAsset::RuntimeSimulationLODIndex);
        const FString MeshSignature = Receiver->SharedRuntimeData.IsValid()
                                          ? Receiver->SharedRuntimeData->MeshSignature
                                          : FString();

        for (FDWCLODVertexColorTransferResult::FLODColors& LODResult : Result.LODResults)
        {
            if (LODResult.LODIndex == UWetClothingAsset::RuntimeSimulationLODIndex ||
                LODResult.Colors.IsEmpty())
            {
                continue;
            }

            if (!LODResult.TargetToSourceVertex.IsEmpty())
            {
                TSharedPtr<const TArray<int32>, ESPMode::ThreadSafe> TransferMap;
                const TSharedPtr<const FDWCLODVertexStaticData, ESPMode::ThreadSafe> TargetLODData =
                    Receiver->LODVertexStaticDataByLOD.FindRef(LODResult.LODIndex);
                if (RuntimeDataSubsystem != nullptr && SourceLODData.IsValid() && TargetLODData.IsValid())
                {
                    TransferMap = RuntimeDataSubsystem->CacheLODVertexColorTransferMap(
                        *Mesh,
                        *SourceLODData,
                        *TargetLODData,
                        MeshSignature,
                        MoveTemp(LODResult.TargetToSourceVertex));
                }

                if (!TransferMap.IsValid())
                {
                    TransferMap = MakeShared<TArray<int32>, ESPMode::ThreadSafe>(
                        MoveTemp(LODResult.TargetToSourceVertex));
                }

                if (TransferMap.IsValid())
                {
                    Receiver->LODVertexColorTransferMapsByLOD.Add(LODResult.LODIndex, TransferMap);
                }
            }

            TSharedRef<TArray<FColor>, ESPMode::ThreadSafe> ColorCache =
                MakeShared<TArray<FColor>, ESPMode::ThreadSafe>(LODResult.Colors);
            Receiver->LODVertexColorCachesByLOD.Add(LODResult.LODIndex, ColorCache);

            FWetVertexColorBuffer::ApplyVertexColorOverride(*Mesh, LODResult.LODIndex, LODResult.Colors);
        }

        if (Receiver->bLODVertexColorTransferRequestedAgain)
        {
            Receiver->bLODVertexColorTransferRequestedAgain = false;
            RequestTask(Owner, AsyncTaskQueue, World, *Receiver);
        }

        Owner.SetComponentTickEnabled(
            bHasPendingCpuSkinningTasks || HasPendingTasks(Receivers));
        return;
    }
}

bool FDWCLODVertexColorTransferCoordinator::HasPendingTasks(
    const TArray<TUniquePtr<FDWCWetMeshReceiverRuntime>>& Receivers) const
{
    for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers)
    {
        if (Receiver.IsValid() && Receiver->bLODVertexColorTransferPending)
        {
            return true;
        }
    }

    return false;
}
