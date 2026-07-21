#pragma once

#include "CoreMinimal.h"
#include "Stats/Stats.h"

struct DWC_API FDWCModeInstanceStats
{
    uint32 ActiveComponentCount = 0;
    uint32 RuntimeReceiverCount = 0;
    uint32 ActiveMaterialCount = 0;
};

struct DWC_API FDWCStatsSnapshot
{
    FDWCModeInstanceStats CPU;
    FDWCModeInstanceStats GPU;

    uint32 SharedRuntimeDataCount = 0;
    uint32 SharedSkinningStaticDataCount = 0;
    uint32 SharedLODVertexStaticDataCount = 0;
    uint32 SharedLODVertexColorTransferMapCount = 0;
    uint32 AbsorbedSimulationStateCount = 0;
    uint32 SurfaceWaterStateCount = 0;
    uint32 WrinkleMaterialBindingCount = 0;
    uint32 WrinkleTextureCount = 0;
    uint32 TransparencyMaterialBindingCount = 0;
    uint32 TransparencyTextureCount = 0;

    uint64 SharedRuntimeDataCPUBytes = 0;
    uint64 SharedSkinningStaticDataCPUBytes = 0;
    uint64 SharedLODVertexStaticDataCPUBytes = 0;
    uint64 SharedLODVertexColorTransferMapCPUBytes = 0;

    uint64 AbsorbedSimulationStateCPUBytes = 0;
    uint64 MeshSamplerCPUBytes = 0;
    uint64 RenderStageCPUBytes = 0;
    uint64 LODVertexColorCacheCPUBytes = 0;
    uint64 PendingLODVertexColorDirtyCPUBytes = 0;
    uint64 SurfaceWaterCPUBytes = 0;
    uint64 GPUBackendCPUBytes = 0;
    uint64 ReceiverMetadataCPUBytes = 0;

    uint64 SurfaceWaterGPUBytes = 0;
    uint64 GPUBackendGPUBytes = 0;
    uint64 WrinkleTextureGPUBytes = 0;
    uint64 TransparencyTextureGPUBytes = 0;

    uint64 GetTrackedCPUBytes() const
    {
        return SharedRuntimeDataCPUBytes +
               SharedSkinningStaticDataCPUBytes +
               SharedLODVertexStaticDataCPUBytes +
               SharedLODVertexColorTransferMapCPUBytes +
               AbsorbedSimulationStateCPUBytes +
               MeshSamplerCPUBytes +
               RenderStageCPUBytes +
               LODVertexColorCacheCPUBytes +
               PendingLODVertexColorDirtyCPUBytes +
               SurfaceWaterCPUBytes +
               GPUBackendCPUBytes +
               ReceiverMetadataCPUBytes;
    }

    uint64 GetTrackedGPUBytes() const
    {
        return SurfaceWaterGPUBytes +
               GPUBackendGPUBytes +
               WrinkleTextureGPUBytes +
               TransparencyTextureGPUBytes;
    }
};

struct DWC_API FDWCWorkloadEventTotals
{
    uint64 SurfaceWaterStampsQueued = 0;
    uint64 SurfaceWaterStampsSubmitted = 0;
    uint64 SurfaceWaterGPUDispatches = 0;
    uint64 CPUSkinningCompleted = 0;
    uint64 CPUSkinningVerticesProcessed = 0;
    uint64 LODTransferCompleted = 0;
    uint64 LODDirtyVerticesTransferred = 0;
    uint64 WetContactsReceived = 0;
    uint64 WetContactsApplied = 0;
    uint64 WetContactsRejected = 0;
    uint64 WetnessSimulationUpdates = 0;
    uint64 ChangedReceivers = 0;
    uint64 DirtyVerticesGenerated = 0;
    uint64 RenderUpdates = 0;
    uint64 MaterialsUpdated = 0;
    uint64 GPUBackendUpdatesSubmitted = 0;
    uint64 GPUBackendDispatches = 0;
};

struct DWC_API FDWCWorkloadStatsSnapshot
{
    uint32 SurfaceWaterStampsQueuedPerSecond = 0;
    uint32 SurfaceWaterStampsSubmittedPerSecond = 0;
    uint32 SurfaceWaterGPUDispatchesPerSecond = 0;
    uint32 SurfaceWaterMaxPendingStamps = 0;
    uint32 SurfaceWaterPendingStamps = 0;

    uint32 CPUSkinningCompletedPerSecond = 0;
    uint32 CPUSkinningVerticesProcessedPerSecond = 0;
    uint32 CPUSkinningPendingTasks = 0;

    uint32 LODTransferCompletedPerSecond = 0;
    uint32 LODDirtyVerticesTransferredPerSecond = 0;
    uint32 LODTransferPendingTasks = 0;
    uint32 PendingLODDirtyVertices = 0;

    uint32 WetContactsReceivedPerSecond = 0;
    uint32 WetContactsAppliedPerSecond = 0;
    uint32 WetContactsRejectedPerSecond = 0;

    uint32 WetnessSimulationUpdatesPerSecond = 0;
    uint32 ChangedReceiversPerSecond = 0;
    uint32 DirtyVerticesGeneratedPerSecond = 0;

    uint32 RenderUpdatesPerSecond = 0;
    uint32 MaterialsUpdatedPerSecond = 0;

    uint32 GPUBackendUpdatesSubmittedPerSecond = 0;
    uint32 GPUBackendDispatchesPerSecond = 0;
};

class DWC_API FDWCWorkloadStats
{
public:
#if STATS
    static void RecordSurfaceWaterStampQueued(uint32 PendingStampCount);
    static void RecordSurfaceWaterStampsSubmitted(uint32 StampCount);
    static void RecordSurfaceWaterGPUDispatch();
    static void RecordCPUSkinningCompleted(uint32 VertexCount);
    static void RecordLODTransferCompleted(uint32 DirtyVertexCount);
    static void RecordWetContactsReceived(uint32 ContactCount);
    static void RecordWetContactsOutcome(uint32 ContactCount, bool bApplied);
    static void RecordWetnessSimulationUpdate(bool bChanged);
    static void RecordDirtyVerticesGenerated(uint32 VertexCount);
    static void RecordRenderUpdate(uint32 MaterialCount);
    static void RecordGPUBackendUpdateSubmitted();
    static void RecordGPUBackendDispatch();

    static FDWCWorkloadEventTotals ReadEventTotals();
    static uint32 ConsumeSurfaceWaterMaxPendingStamps();
#else
    static void RecordSurfaceWaterStampQueued(uint32) {}
    static void RecordSurfaceWaterStampsSubmitted(uint32) {}
    static void RecordSurfaceWaterGPUDispatch() {}
    static void RecordCPUSkinningCompleted(uint32) {}
    static void RecordLODTransferCompleted(uint32) {}
    static void RecordWetContactsReceived(uint32) {}
    static void RecordWetContactsOutcome(uint32, bool) {}
    static void RecordWetnessSimulationUpdate(bool) {}
    static void RecordDirtyVerticesGenerated(uint32) {}
    static void RecordRenderUpdate(uint32) {}
    static void RecordGPUBackendUpdateSubmitted() {}
    static void RecordGPUBackendDispatch() {}
    static FDWCWorkloadEventTotals ReadEventTotals() { return {}; }
    static uint32 ConsumeSurfaceWaterMaxPendingStamps() { return 0; }
#endif
};

// `stat dwc mem` toggles this controller group. The second token is ignored by
// Unreal's built-in stat parser; the subsystem mirrors its state to both memory groups.
DECLARE_STATS_GROUP_SORTBYNAME(TEXT("DWC Memory"), STATGROUP_DWC, STATCAT_Advanced);
DECLARE_STATS_GROUP_SORTBYNAME(TEXT("DWC Instances"), STATGROUP_DWCInstances, STATCAT_Advanced);
DECLARE_STATS_GROUP_SORTBYNAME(TEXT("Memory Counters (CPU)"), STATGROUP_DWCCPUMemory, STATCAT_Advanced);
DECLARE_STATS_GROUP_SORTBYNAME(TEXT("Memory Counters (GPU)"), STATGROUP_DWCGPUMemory, STATCAT_Advanced);
DECLARE_STATS_GROUP_SORTBYNAME(TEXT("DWC Workload (Recent 1s)"), STATGROUP_DWCWorkload, STATCAT_Advanced);

DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Tracked Receivers"), STAT_DWC_MemoryTrackedReceivers, STATGROUP_DWC, DWC_API);

DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("CPU Components"), STAT_DWC_CPUComponents, STATGROUP_DWCInstances, DWC_API);
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("CPU Receivers"), STAT_DWC_CPUReceivers, STATGROUP_DWCInstances, DWC_API);
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("CPU Materials"), STAT_DWC_CPUMaterials, STATGROUP_DWCInstances, DWC_API);
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("GPU Components"), STAT_DWC_GPUComponents, STATGROUP_DWCInstances, DWC_API);
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("GPU Receivers"), STAT_DWC_GPUReceivers, STATGROUP_DWCInstances, DWC_API);
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("GPU Materials"), STAT_DWC_GPUMaterials, STATGROUP_DWCInstances, DWC_API);
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Shared Runtime Data"), STAT_DWC_SharedRuntimeDataCount, STATGROUP_DWCInstances, DWC_API);
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Shared Skinning Data"), STAT_DWC_SharedSkinningDataCount, STATGROUP_DWCInstances, DWC_API);
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Shared LOD Static Data"), STAT_DWC_SharedLODStaticDataCount, STATGROUP_DWCInstances, DWC_API);
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Shared LOD Transfer Maps"), STAT_DWC_SharedLODTransferMapCount, STATGROUP_DWCInstances, DWC_API);
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Absorbed States"), STAT_DWC_AbsorbedStateCount, STATGROUP_DWCInstances, DWC_API);
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Surface Water States"), STAT_DWC_SurfaceWaterStateCount, STATGROUP_DWCInstances, DWC_API);
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Wrinkle Material Bindings"), STAT_DWC_WrinkleMaterialBindingCount, STATGROUP_DWCInstances, DWC_API);
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Unique Wrinkle Textures"), STAT_DWC_WrinkleTextureCount, STATGROUP_DWCInstances, DWC_API);
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Transparency Material Bindings"), STAT_DWC_TransparencyMaterialBindingCount, STATGROUP_DWCInstances, DWC_API);
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Unique Transparency Textures"), STAT_DWC_TransparencyTextureCount, STATGROUP_DWCInstances, DWC_API);

DECLARE_MEMORY_STAT_POOL_EXTERN(TEXT("Shared Runtime Data"), STAT_DWC_SharedRuntimeDataCPU, STATGROUP_DWCCPUMemory, FPlatformMemory::MCR_Physical, DWC_API);
DECLARE_MEMORY_STAT_POOL_EXTERN(TEXT("Shared Skinning Data"), STAT_DWC_SharedSkinningDataCPU, STATGROUP_DWCCPUMemory, FPlatformMemory::MCR_Physical, DWC_API);
DECLARE_MEMORY_STAT_POOL_EXTERN(TEXT("Shared LOD Static Data"), STAT_DWC_SharedLODStaticDataCPU, STATGROUP_DWCCPUMemory, FPlatformMemory::MCR_Physical, DWC_API);
DECLARE_MEMORY_STAT_POOL_EXTERN(TEXT("Shared LOD Transfer Maps"), STAT_DWC_SharedLODTransferMapCPU, STATGROUP_DWCCPUMemory, FPlatformMemory::MCR_Physical, DWC_API);
DECLARE_MEMORY_STAT_POOL_EXTERN(TEXT("Absorbed Simulation States"), STAT_DWC_AbsorbedStateCPU, STATGROUP_DWCCPUMemory, FPlatformMemory::MCR_Physical, DWC_API);
DECLARE_MEMORY_STAT_POOL_EXTERN(TEXT("Mesh Samplers"), STAT_DWC_MeshSamplerCPU, STATGROUP_DWCCPUMemory, FPlatformMemory::MCR_Physical, DWC_API);
DECLARE_MEMORY_STAT_POOL_EXTERN(TEXT("Render Stages"), STAT_DWC_RenderStageCPU, STATGROUP_DWCCPUMemory, FPlatformMemory::MCR_Physical, DWC_API);
DECLARE_MEMORY_STAT_POOL_EXTERN(TEXT("LOD Vertex Color Caches"), STAT_DWC_LODVertexColorCacheCPU, STATGROUP_DWCCPUMemory, FPlatformMemory::MCR_Physical, DWC_API);
DECLARE_MEMORY_STAT_POOL_EXTERN(TEXT("Pending LOD Dirty Vertices"), STAT_DWC_PendingLODDirtyCPU, STATGROUP_DWCCPUMemory, FPlatformMemory::MCR_Physical, DWC_API);
DECLARE_MEMORY_STAT_POOL_EXTERN(TEXT("Surface Water"), STAT_DWC_SurfaceWaterCPU, STATGROUP_DWCCPUMemory, FPlatformMemory::MCR_Physical, DWC_API);
DECLARE_MEMORY_STAT_POOL_EXTERN(TEXT("GPU Backends"), STAT_DWC_GPUBackendCPU, STATGROUP_DWCCPUMemory, FPlatformMemory::MCR_Physical, DWC_API);
DECLARE_MEMORY_STAT_POOL_EXTERN(TEXT("Receiver Metadata"), STAT_DWC_ReceiverMetadataCPU, STATGROUP_DWCCPUMemory, FPlatformMemory::MCR_Physical, DWC_API);
DECLARE_MEMORY_STAT_POOL_EXTERN(TEXT("Total Tracked"), STAT_DWC_TotalTrackedCPU, STATGROUP_DWCCPUMemory, FPlatformMemory::MCR_Physical, DWC_API);

DECLARE_MEMORY_STAT_POOL_EXTERN(TEXT("Surface Water"), STAT_DWC_SurfaceWaterGPU, STATGROUP_DWCGPUMemory, FPlatformMemory::MCR_GPU, DWC_API);
DECLARE_MEMORY_STAT_POOL_EXTERN(TEXT("Wetness Backends"), STAT_DWC_GPUBackendGPU, STATGROUP_DWCGPUMemory, FPlatformMemory::MCR_GPU, DWC_API);
DECLARE_MEMORY_STAT_POOL_EXTERN(TEXT("Wrinkle Textures"), STAT_DWC_WrinkleTextureGPU, STATGROUP_DWCGPUMemory, FPlatformMemory::MCR_GPU, DWC_API);
DECLARE_MEMORY_STAT_POOL_EXTERN(TEXT("Transparency Textures"), STAT_DWC_TransparencyTextureGPU, STATGROUP_DWCGPUMemory, FPlatformMemory::MCR_GPU, DWC_API);
DECLARE_MEMORY_STAT_POOL_EXTERN(TEXT("Total Tracked"), STAT_DWC_TotalTrackedGPU, STATGROUP_DWCGPUMemory, FPlatformMemory::MCR_GPU, DWC_API);

DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Surface Water - Stamps Queued/s"), STAT_DWC_SurfaceWaterStampsQueuedRate, STATGROUP_DWCWorkload, DWC_API);
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Surface Water - Stamps Submitted/s"), STAT_DWC_SurfaceWaterStampsSubmittedRate, STATGROUP_DWCWorkload, DWC_API);
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Surface Water - GPU Dispatches/s"), STAT_DWC_SurfaceWaterGPUDispatchesRate, STATGROUP_DWCWorkload, DWC_API);
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Surface Water - Max Pending Stamps (1s)"), STAT_DWC_SurfaceWaterMaxPendingStamps, STATGROUP_DWCWorkload, DWC_API);
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Surface Water - Pending Stamps"), STAT_DWC_SurfaceWaterPendingStamps, STATGROUP_DWCWorkload, DWC_API);

DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("CPU Skinning - Completed/s"), STAT_DWC_CPUSkinningCompletedRate, STATGROUP_DWCWorkload, DWC_API);
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("CPU Skinning - Vertices Processed/s"), STAT_DWC_CPUSkinningVerticesRate, STATGROUP_DWCWorkload, DWC_API);
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("CPU Skinning - Pending Tasks"), STAT_DWC_CPUSkinningPendingTasks, STATGROUP_DWCWorkload, DWC_API);

DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("LOD Transfer - Completed/s"), STAT_DWC_LODTransferCompletedRate, STATGROUP_DWCWorkload, DWC_API);
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("LOD Transfer - Dirty Vertices/s"), STAT_DWC_LODDirtyVerticesRate, STATGROUP_DWCWorkload, DWC_API);
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("LOD Transfer - Pending Tasks"), STAT_DWC_LODTransferPendingTasks, STATGROUP_DWCWorkload, DWC_API);
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("LOD Transfer - Pending Dirty Vertices"), STAT_DWC_PendingLODDirtyVertices, STATGROUP_DWCWorkload, DWC_API);

DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Wet Contacts - Received/s"), STAT_DWC_WetContactsReceivedRate, STATGROUP_DWCWorkload, DWC_API);
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Wet Contacts - Applied/s"), STAT_DWC_WetContactsAppliedRate, STATGROUP_DWCWorkload, DWC_API);
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Wet Contacts - Rejected/s"), STAT_DWC_WetContactsRejectedRate, STATGROUP_DWCWorkload, DWC_API);

DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Wetness Simulation - Updates/s"), STAT_DWC_WetnessSimulationUpdatesRate, STATGROUP_DWCWorkload, DWC_API);
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Wetness Simulation - Changed Receivers/s"), STAT_DWC_ChangedReceiversRate, STATGROUP_DWCWorkload, DWC_API);
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Wetness Simulation - Dirty Vertices Generated/s"), STAT_DWC_DirtyVerticesGeneratedRate, STATGROUP_DWCWorkload, DWC_API);

DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Render - Updates/s"), STAT_DWC_RenderUpdatesRate, STATGROUP_DWCWorkload, DWC_API);
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("Render - Materials Updated/s"), STAT_DWC_MaterialsUpdatedRate, STATGROUP_DWCWorkload, DWC_API);

DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("GPU Backend - Updates Submitted/s"), STAT_DWC_GPUBackendUpdatesSubmittedRate, STATGROUP_DWCWorkload, DWC_API);
DECLARE_DWORD_ACCUMULATOR_STAT_EXTERN(TEXT("GPU Backend - Dispatches/s"), STAT_DWC_GPUBackendDispatchesRate, STATGROUP_DWCWorkload, DWC_API);
