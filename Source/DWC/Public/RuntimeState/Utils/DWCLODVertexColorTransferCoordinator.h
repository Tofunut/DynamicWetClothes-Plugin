#pragma once

#include "CoreMinimal.h"
#include "Templates/UniquePtr.h"

class FDWCTaskQueue;
class UDynamicWetClothesComponent;
class UDWCRuntimeDataSubsystem;
class UWorld;
struct FDWCLODVertexColorTransferResult;
struct FDWCWetMeshReceiverRuntime;

/** Component-owned coordinator for LOD vertex-color transfer setup and asynchronous work. */
class FDWCLODVertexColorTransferCoordinator
{
  public:
    bool InitializeReceiver(
        FDWCWetMeshReceiverRuntime& Receiver,
        UDWCRuntimeDataSubsystem& RuntimeDataSubsystem,
        int32 RuntimeLODIndex) const;

    bool RequestTask(
        UDynamicWetClothesComponent& Owner,
        FDWCTaskQueue* AsyncTaskQueue,
        UWorld* World,
        FDWCWetMeshReceiverRuntime& Receiver) const;

    void CommitTaskResult(
        UDynamicWetClothesComponent& Owner,
        TArray<TUniquePtr<FDWCWetMeshReceiverRuntime>>& Receivers,
        FDWCTaskQueue* AsyncTaskQueue,
        UWorld* World,
        FDWCLODVertexColorTransferResult&& Result,
        bool bHasPendingCpuSkinningTasks) const;

    bool HasPendingTasks(
        const TArray<TUniquePtr<FDWCWetMeshReceiverRuntime>>& Receivers) const;
};
