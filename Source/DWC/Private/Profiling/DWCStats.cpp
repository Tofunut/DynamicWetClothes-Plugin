#include "Profiling/DWCStats.h"

#include <atomic>

#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"

#if STATS
namespace
{
    std::atomic<uint64> GSurfaceWaterStampsQueued{0};
    std::atomic<uint64> GSurfaceWaterStampsSubmitted{0};
    std::atomic<uint64> GSurfaceWaterGPUDispatches{0};
    std::atomic<uint64> GSurfaceWaterMaxPendingStamps{0};
    std::atomic<uint64> GCPUSkinningCompleted{0};
    std::atomic<uint64> GCPUSkinningVerticesProcessed{0};
    std::atomic<uint64> GLODTransferCompleted{0};
    std::atomic<uint64> GLODDirtyVerticesTransferred{0};
    std::atomic<uint64> GWetContactsReceived{0};
    std::atomic<uint64> GWetContactsApplied{0};
    std::atomic<uint64> GWetContactsRejected{0};
    std::atomic<uint64> GWetnessSimulationUpdates{0};
    std::atomic<uint64> GChangedReceivers{0};
    std::atomic<uint64> GDirtyVerticesGenerated{0};
    std::atomic<uint64> GRenderUpdates{0};
    std::atomic<uint64> GMaterialsUpdated{0};
    std::atomic<uint64> GGPUBackendUpdatesSubmitted{0};
    std::atomic<uint64> GGPUBackendDispatches{0};

    void SetAtomicMax(std::atomic<uint64>& Target, const uint64 Value)
    {
        uint64 Current = Target.load(std::memory_order_relaxed);
        while (Current < Value &&
               !Target.compare_exchange_weak(Current, Value, std::memory_order_relaxed))
        {
        }
    }
}

void FDWCWorkloadStats::RecordSurfaceWaterStampQueued(const uint32 PendingStampCount)
{
    GSurfaceWaterStampsQueued.fetch_add(1, std::memory_order_relaxed);
    SetAtomicMax(GSurfaceWaterMaxPendingStamps, PendingStampCount);
}

void FDWCWorkloadStats::RecordSurfaceWaterStampsSubmitted(const uint32 StampCount)
{
    GSurfaceWaterStampsSubmitted.fetch_add(StampCount, std::memory_order_relaxed);
}

void FDWCWorkloadStats::RecordSurfaceWaterGPUDispatch()
{
    GSurfaceWaterGPUDispatches.fetch_add(1, std::memory_order_relaxed);
}

void FDWCWorkloadStats::RecordCPUSkinningCompleted(const uint32 VertexCount)
{
    GCPUSkinningCompleted.fetch_add(1, std::memory_order_relaxed);
    GCPUSkinningVerticesProcessed.fetch_add(VertexCount, std::memory_order_relaxed);
}

void FDWCWorkloadStats::RecordLODTransferCompleted(const uint32 DirtyVertexCount)
{
    GLODTransferCompleted.fetch_add(1, std::memory_order_relaxed);
    GLODDirtyVerticesTransferred.fetch_add(DirtyVertexCount, std::memory_order_relaxed);
}

void FDWCWorkloadStats::RecordWetContactsReceived(const uint32 ContactCount)
{
    GWetContactsReceived.fetch_add(ContactCount, std::memory_order_relaxed);
}

void FDWCWorkloadStats::RecordWetContactsOutcome(const uint32 ContactCount, const bool bApplied)
{
    (bApplied ? GWetContactsApplied : GWetContactsRejected).fetch_add(ContactCount, std::memory_order_relaxed);
}

void FDWCWorkloadStats::RecordWetnessSimulationUpdate(const bool bChanged)
{
    GWetnessSimulationUpdates.fetch_add(1, std::memory_order_relaxed);
    if (bChanged)
    {
        GChangedReceivers.fetch_add(1, std::memory_order_relaxed);
    }
}

void FDWCWorkloadStats::RecordDirtyVerticesGenerated(const uint32 VertexCount)
{
    GDirtyVerticesGenerated.fetch_add(VertexCount, std::memory_order_relaxed);
}

void FDWCWorkloadStats::RecordRenderUpdate(const uint32 MaterialCount)
{
    GRenderUpdates.fetch_add(1, std::memory_order_relaxed);
    GMaterialsUpdated.fetch_add(MaterialCount, std::memory_order_relaxed);
}

void FDWCWorkloadStats::RecordGPUBackendUpdateSubmitted()
{
    GGPUBackendUpdatesSubmitted.fetch_add(1, std::memory_order_relaxed);
}

void FDWCWorkloadStats::RecordGPUBackendDispatch()
{
    GGPUBackendDispatches.fetch_add(1, std::memory_order_relaxed);
}

FDWCWorkloadEventTotals FDWCWorkloadStats::ReadEventTotals()
{
    FDWCWorkloadEventTotals Totals;
    Totals.SurfaceWaterStampsQueued = GSurfaceWaterStampsQueued.load(std::memory_order_relaxed);
    Totals.SurfaceWaterStampsSubmitted = GSurfaceWaterStampsSubmitted.load(std::memory_order_relaxed);
    Totals.SurfaceWaterGPUDispatches = GSurfaceWaterGPUDispatches.load(std::memory_order_relaxed);
    Totals.CPUSkinningCompleted = GCPUSkinningCompleted.load(std::memory_order_relaxed);
    Totals.CPUSkinningVerticesProcessed = GCPUSkinningVerticesProcessed.load(std::memory_order_relaxed);
    Totals.LODTransferCompleted = GLODTransferCompleted.load(std::memory_order_relaxed);
    Totals.LODDirtyVerticesTransferred = GLODDirtyVerticesTransferred.load(std::memory_order_relaxed);
    Totals.WetContactsReceived = GWetContactsReceived.load(std::memory_order_relaxed);
    Totals.WetContactsApplied = GWetContactsApplied.load(std::memory_order_relaxed);
    Totals.WetContactsRejected = GWetContactsRejected.load(std::memory_order_relaxed);
    Totals.WetnessSimulationUpdates = GWetnessSimulationUpdates.load(std::memory_order_relaxed);
    Totals.ChangedReceivers = GChangedReceivers.load(std::memory_order_relaxed);
    Totals.DirtyVerticesGenerated = GDirtyVerticesGenerated.load(std::memory_order_relaxed);
    Totals.RenderUpdates = GRenderUpdates.load(std::memory_order_relaxed);
    Totals.MaterialsUpdated = GMaterialsUpdated.load(std::memory_order_relaxed);
    Totals.GPUBackendUpdatesSubmitted = GGPUBackendUpdatesSubmitted.load(std::memory_order_relaxed);
    Totals.GPUBackendDispatches = GGPUBackendDispatches.load(std::memory_order_relaxed);
    return Totals;
}

uint32 FDWCWorkloadStats::ConsumeSurfaceWaterMaxPendingStamps()
{
    return static_cast<uint32>(FMath::Min<uint64>(
        GSurfaceWaterMaxPendingStamps.exchange(0, std::memory_order_relaxed),
        MAX_uint32));
}
#endif

DEFINE_STAT(STAT_DWC_MemoryTrackedReceivers);
DEFINE_STAT(STAT_DWC_CPUComponents);
DEFINE_STAT(STAT_DWC_CPUReceivers);
DEFINE_STAT(STAT_DWC_CPUMaterials);
DEFINE_STAT(STAT_DWC_GPUComponents);
DEFINE_STAT(STAT_DWC_GPUReceivers);
DEFINE_STAT(STAT_DWC_GPUMaterials);
DEFINE_STAT(STAT_DWC_SharedRuntimeDataCount);
DEFINE_STAT(STAT_DWC_SharedSkinningDataCount);
DEFINE_STAT(STAT_DWC_SharedLODStaticDataCount);
DEFINE_STAT(STAT_DWC_SharedLODTransferMapCount);
DEFINE_STAT(STAT_DWC_AbsorbedStateCount);
DEFINE_STAT(STAT_DWC_SurfaceWaterStateCount);
DEFINE_STAT(STAT_DWC_WrinkleMaterialBindingCount);
DEFINE_STAT(STAT_DWC_WrinkleTextureCount);
DEFINE_STAT(STAT_DWC_TransparencyMaterialBindingCount);
DEFINE_STAT(STAT_DWC_TransparencyTextureCount);
DEFINE_STAT(STAT_DWC_LODTotalReceivers);
DEFINE_STAT(STAT_DWC_LODSurfaceWaterEnabledReceivers);
DEFINE_STAT(STAT_DWC_LODCPUWetnessRenderingEnabledReceivers);
DEFINE_STAT(STAT_DWC_LODWrinkleEnabledReceivers);
DEFINE_STAT(STAT_DWC_LODTransparencyEnabledReceivers);

DEFINE_STAT(STAT_DWC_SharedRuntimeDataCPU);
DEFINE_STAT(STAT_DWC_SharedSkinningDataCPU);
DEFINE_STAT(STAT_DWC_SharedLODStaticDataCPU);
DEFINE_STAT(STAT_DWC_SharedLODTransferMapCPU);
DEFINE_STAT(STAT_DWC_AbsorbedStateCPU);
DEFINE_STAT(STAT_DWC_MeshSamplerCPU);
DEFINE_STAT(STAT_DWC_RenderStageCPU);
DEFINE_STAT(STAT_DWC_LODVertexColorCacheCPU);
DEFINE_STAT(STAT_DWC_PendingLODDirtyCPU);
DEFINE_STAT(STAT_DWC_SurfaceWaterCPU);
DEFINE_STAT(STAT_DWC_GPUBackendCPU);
DEFINE_STAT(STAT_DWC_ReceiverMetadataCPU);
DEFINE_STAT(STAT_DWC_TotalTrackedCPU);
DEFINE_STAT(STAT_DWC_SurfaceWaterGPU);
DEFINE_STAT(STAT_DWC_GPUBackendGPU);
DEFINE_STAT(STAT_DWC_WrinkleTextureGPU);
DEFINE_STAT(STAT_DWC_TransparencyTextureGPU);
DEFINE_STAT(STAT_DWC_TotalTrackedGPU);

DEFINE_STAT(STAT_DWC_SurfaceWaterStampsQueuedRate);
DEFINE_STAT(STAT_DWC_SurfaceWaterStampsSubmittedRate);
DEFINE_STAT(STAT_DWC_SurfaceWaterGPUDispatchesRate);
DEFINE_STAT(STAT_DWC_SurfaceWaterMaxPendingStamps);
DEFINE_STAT(STAT_DWC_SurfaceWaterPendingStamps);
DEFINE_STAT(STAT_DWC_CPUSkinningCompletedRate);
DEFINE_STAT(STAT_DWC_CPUSkinningVerticesRate);
DEFINE_STAT(STAT_DWC_CPUSkinningPendingTasks);
DEFINE_STAT(STAT_DWC_LODTransferCompletedRate);
DEFINE_STAT(STAT_DWC_LODDirtyVerticesRate);
DEFINE_STAT(STAT_DWC_LODTransferPendingTasks);
DEFINE_STAT(STAT_DWC_PendingLODDirtyVertices);
DEFINE_STAT(STAT_DWC_WetContactsReceivedRate);
DEFINE_STAT(STAT_DWC_WetContactsAppliedRate);
DEFINE_STAT(STAT_DWC_WetContactsRejectedRate);
DEFINE_STAT(STAT_DWC_WetnessSimulationUpdatesRate);
DEFINE_STAT(STAT_DWC_ChangedReceiversRate);
DEFINE_STAT(STAT_DWC_DirtyVerticesGeneratedRate);
DEFINE_STAT(STAT_DWC_RenderUpdatesRate);
DEFINE_STAT(STAT_DWC_MaterialsUpdatedRate);
DEFINE_STAT(STAT_DWC_GPUBackendUpdatesSubmittedRate);
DEFINE_STAT(STAT_DWC_GPUBackendDispatchesRate);

namespace
{
    void ExecuteDWCStatAlias(UWorld* World, const TCHAR* GroupName)
    {
        if (World == nullptr || GEngine == nullptr)
        {
            return;
        }

        GEngine->Exec(World, *FString::Printf(TEXT("stat %s"), GroupName));
    }

    FAutoConsoleCommandWithWorld GDWCMemoryStatAlias(
        TEXT("stat dwc mem"),
        TEXT("Toggle DWC memory statistics."),
        FConsoleCommandWithWorldDelegate::CreateLambda(
            [](UWorld* World)
            {
                ExecuteDWCStatAlias(World, TEXT("DWC"));
            }),
        ECVF_Default);

    FAutoConsoleCommandWithWorld GDWCWorkloadStatAlias(
        TEXT("stat dwc workload"),
        TEXT("Toggle DWC workload statistics."),
        FConsoleCommandWithWorldDelegate::CreateLambda(
            [](UWorld* World)
            {
                ExecuteDWCStatAlias(World, TEXT("DWCWorkload"));
            }),
        ECVF_Default);

    FAutoConsoleCommandWithWorld GDWCLodStatAlias(
        TEXT("stat dwc lod"),
        TEXT("Toggle DWC LOD feature statistics."),
        FConsoleCommandWithWorldDelegate::CreateLambda(
            [](UWorld* World)
            {
                ExecuteDWCStatAlias(World, TEXT("DWCLOD"));
            }),
        ECVF_Default);
}
