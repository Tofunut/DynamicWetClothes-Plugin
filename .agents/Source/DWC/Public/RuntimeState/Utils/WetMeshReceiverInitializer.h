#pragma once

#include "CoreMinimal.h"
#include "Core/DWCSimulationMode.h"
#include "DataAssets/WetClothingAsset.h"
#include "RuntimeState/Utils/WetRuntimeDataBuilder.h"
#include "Templates/Function.h"
#include "Templates/UniquePtr.h"

class AActor;
class FDWCLODVertexColorTransferCoordinator;
class UDynamicWetClothesComponent;
class UWorld;
struct FDWCWetMeshReceiverRuntime;

struct FWetMeshReceiverInitializerContext
{
    UDynamicWetClothesComponent* Component = nullptr;
    AActor* Owner = nullptr;
    UWorld* World = nullptr;

    const TArray<TObjectPtr<UWetClothingAsset>>* WetClothingAssets = nullptr;
    TArray<TUniquePtr<FDWCWetMeshReceiverRuntime>>* Receivers = nullptr;
    EDWCSimulationMode SimulationMode = EDWCSimulationMode::VertexCPU;

    FDWCLODVertexColorTransferCoordinator* LODVertexColorTransferCoordinator = nullptr;
    TFunction<FWetRuntimeDataBuildArgs(FDWCWetMeshReceiverRuntime&)> MakeRuntimeDataBuildArgs;
};

/** Builds receiver objects and initializes their per-receiver runtime data. */
class FWetMeshReceiverInitializer
{
  public:
    FWetMeshReceiverInitializer() = delete;

    static bool RebuildReceivers(FWetMeshReceiverInitializerContext& Context);
    static bool InitializeReceiver(
        FWetMeshReceiverInitializerContext& Context,
        FDWCWetMeshReceiverRuntime& Receiver);
};
