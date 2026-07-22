#pragma once

#include "CoreMinimal.h"
#include "Core/DWCSimulationMode.h"
#include "RuntimeState/Utils/WetInputStage.h"
#include "RuntimeState/Utils/WetSurfaceContactResolver.h"
#include "WetInputSystem/WetContactTypes.h"
#include "Templates/Function.h"

struct FDWCWetMeshReceiverRuntime;

/**
 * Explicit runtime dependencies needed by the wetness application stage.
 *
 * This is a per-call view over component-owned state. It does not own the
 * receivers or pending contacts and must not outlive the calling operation.
 */
struct DWC_API FWetApplicationStageContext
{
    UObject* OwnerForLogs = nullptr;
    const FWetClothingSettings* WetnessSettings = nullptr;
    int32 MaxNearestSeedVertices = 12;

    EDWCSimulationMode SimulationMode = EDWCSimulationMode::VertexCPU;

    TArray<TUniquePtr<FDWCWetMeshReceiverRuntime>>* Receivers = nullptr;
    TArray<FDWCWetContact>* PendingWetContacts = nullptr;
    bool* bPendingWetContactsApplyMaterial = nullptr;

    bool bBatchWetContactsPerFrame = true;
    int32 MaxBatchedWetContactsPerFrame = 64;

    TFunction<bool()> EnsureWetRuntimeInitialized;
    TFunction<void()> RequestContinuousCpuSkinningTasks;
    TFunction<void(bool)> SetComponentTickEnabled;
    TFunction<void(FDWCWetMeshReceiverRuntime&)> RequestWetRenderingUpdate;
};

/** Applies wetness inputs through either the CPU or GPU execution path. */
class DWC_API FWetApplicationStage
{
  public:
    FWetApplicationStage() = delete;

    static void ApplyWetAll(FWetApplicationStageContext& Context, float Amount);
    static bool ApplyWetContact(
        FWetApplicationStageContext& Context,
        const FDWCWetContact&        Contact,
        bool                         bApplyMaterial);
    static bool ApplyWetContacts(
        FWetApplicationStageContext& Context,
        const TArray<FDWCWetContact>& Contacts,
        bool                          bApplyMaterial);
    static bool ApplyWetArea(
        FWetApplicationStageContext& Context,
        const FDWCWetAreaData&        AreaData,
        bool                          bApplyMaterial);
    static bool ApplyWetSurface(
        FWetApplicationStageContext& Context,
        const FDWCWaterSurfaceData&   WaterSurfaceData,
        float                         Amount,
        bool                          bApplyMaterial);
    static bool FlushPendingWetContacts(FWetApplicationStageContext& Context);

  private:
    static FWetInputStageArgs MakeWetInputStageArgs(
        const FWetApplicationStageContext& Context,
        FDWCWetMeshReceiverRuntime&        Receiver);
    static FWetSurfaceContactResolverArgs MakeWetSurfaceContactResolverArgs(
        const FWetApplicationStageContext& Context,
        FDWCWetMeshReceiverRuntime&        Receiver);

    static bool ShouldReceiverConsiderContact(
        const FDWCWetMeshReceiverRuntime& Receiver,
        const FDWCWetContact&             Contact);
    static bool ShouldReceiverConsiderSurface(
        const FDWCWetMeshReceiverRuntime& Receiver,
        const FDWCWaterSurfaceData&       WaterSurfaceData);
};
