// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "Profiling/DWCStats.h"

#include <atomic>

#include "Engine/Console.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "ConsoleSettings.h"
#include "HAL/IConsoleManager.h"
#include "Stats/StatsCommand.h"
#include "Utility/DWCLog.h"
#include "ViewportClient.h"

#if STATS
namespace
{
    std::atomic<uint64> GSurfaceWaterStampsQueued{ 0 };
    std::atomic<uint64> GSurfaceWaterStampsSubmitted{ 0 };
    std::atomic<uint64> GSurfaceWaterGPUDispatches{ 0 };
    std::atomic<uint64> GSurfaceWaterMaxPendingStamps{ 0 };
    std::atomic<uint64> GCPUSkinningCompleted{ 0 };
    std::atomic<uint64> GCPUSkinningVerticesProcessed{ 0 };
    std::atomic<uint64> GLODTransferCompleted{ 0 };
    std::atomic<uint64> GLODDirtyVerticesTransferred{ 0 };
    std::atomic<uint64> GWetContactsReceived{ 0 };
    std::atomic<uint64> GWetContactsApplied{ 0 };
    std::atomic<uint64> GWetContactsRejected{ 0 };
    std::atomic<uint64> GWetnessSimulationUpdates{ 0 };
    std::atomic<uint64> GChangedReceivers{ 0 };
    std::atomic<uint64> GDirtyVerticesGenerated{ 0 };
    std::atomic<uint64> GRenderUpdates{ 0 };
    std::atomic<uint64> GMaterialsUpdated{ 0 };
    std::atomic<uint64> GGPUBackendUpdatesSubmitted{ 0 };
    std::atomic<uint64> GGPUBackendDispatches{ 0 };

    void SetAtomicMax(std::atomic<uint64>& Target, const uint64 Value)
    {
        uint64 Current = Target.load(std::memory_order_relaxed);
        while (Current < Value &&
               !Target.compare_exchange_weak(Current, Value, std::memory_order_relaxed))
        {
        }
    }
} // namespace

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
DEFINE_STAT(STAT_DWC_SharedGPUStaticResourceCount);
DEFINE_STAT(STAT_DWC_RuntimeRenderProfileCount);
DEFINE_STAT(STAT_DWC_AbsorbedStateCount);
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
DEFINE_STAT(STAT_DWC_GPUBackendCPU);
DEFINE_STAT(STAT_DWC_GPUResourceSubsystemCPU);
DEFINE_STAT(STAT_DWC_ReceiverMetadataCPU);
DEFINE_STAT(STAT_DWC_TotalTrackedCPU);
DEFINE_STAT(STAT_DWC_GPUBackendGPU);
DEFINE_STAT(STAT_DWC_SharedGPUStaticBufferGPU);
DEFINE_STAT(STAT_DWC_SharedGPURenderProfileLUTGPU);
DEFINE_STAT(STAT_DWC_SharedGPUWetPartDataRemapGPU);
DEFINE_STAT(STAT_DWC_SharedGPUSurfaceNormalArrayGPU);
DEFINE_STAT(STAT_DWC_SharedGPUResourceGPU);
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
    bool            bDWCStatCommandsRegistered = false;
    FDelegateHandle DWCStatAutocompleteHandle;

    struct FDWCStatAlias
    {
        const TCHAR* Command;
        const TCHAR* Description;
    };

    const FDWCStatAlias GDWCStatAliases[] = {
        { TEXT("stat dwc"), TEXT("Toggle DWC CPU and GPU memory stats.") },
        { TEXT("stat dwc mem"), TEXT("Toggle DWC CPU and GPU memory stats.") },
        { TEXT("stat dwc memory"), TEXT("Toggle DWC CPU and GPU memory stats.") },
        { TEXT("stat dwc workload"), TEXT("Toggle DWC workload rate stats.") },
        { TEXT("stat dwc instances"), TEXT("Toggle DWC instance/resource count stats.") },
        { TEXT("stat dwc cpu"), TEXT("Toggle DWC CPU memory stats.") },
        { TEXT("stat dwc gpu"), TEXT("Toggle DWC GPU memory stats.") },
        { TEXT("stat dwc help"), TEXT("Print DWC stat command help.") }
    };

    FCommonViewportClient* ResolveDWCViewportClient(UWorld* World)
    {
        return World != nullptr ? World->GetGameViewport() : nullptr;
    }

    void ExecuteDWCStatCommand(UWorld* World, FCommonViewportClient* ViewportClient, const TCHAR* GroupName)
    {
        if (GEngine != nullptr && World != nullptr && ViewportClient != nullptr)
        {
            GEngine->ExecEngineStat(World, ViewportClient, GroupName);
            return;
        }

        UE::Stats::DirectStatsCommand(*FString::Printf(TEXT("stat %s"), GroupName), true);
    }

    void ExecuteDWCStatCommand(UWorld* World, const TCHAR* GroupName)
    {
        ExecuteDWCStatCommand(World, ResolveDWCViewportClient(World), GroupName);
    }

    bool IsDWCStatGroupEnabled(const FCommonViewportClient* ViewportClient, const TCHAR* GroupName, bool& bOutEnabled)
    {
        if (ViewportClient == nullptr)
        {
            return false;
        }

        bOutEnabled = ViewportClient->IsStatEnabled(FString(GroupName));
        return true;
    }

    void SetDWCStatGroupEnabled(UWorld* World, FCommonViewportClient* ViewportClient, const TCHAR* GroupName, const bool bEnabled)
    {
        bool bCurrentEnabled = false;
        if (!IsDWCStatGroupEnabled(ViewportClient, GroupName, bCurrentEnabled) || bCurrentEnabled != bEnabled)
        {
            ExecuteDWCStatCommand(World, ViewportClient, GroupName);
        }
    }

    void ToggleDWCStatGroup(UWorld* World, FCommonViewportClient* ViewportClient, const TCHAR* GroupName)
    {
        bool bEnabled = false;
        if (IsDWCStatGroupEnabled(ViewportClient, GroupName, bEnabled))
        {
            SetDWCStatGroupEnabled(World, ViewportClient, GroupName, !bEnabled);
            return;
        }

        ExecuteDWCStatCommand(World, ViewportClient, GroupName);
    }

    void ToggleDWCMemoryStats(UWorld* World, FCommonViewportClient* ViewportClient)
    {
        bool bDWCEnabled = false;
        if (IsDWCStatGroupEnabled(ViewportClient, TEXT("DWC"), bDWCEnabled))
        {
            const bool bEnable = !bDWCEnabled;
            SetDWCStatGroupEnabled(World, ViewportClient, TEXT("DWC"), bEnable);
            SetDWCStatGroupEnabled(World, ViewportClient, TEXT("DWCCPUMemory"), bEnable);
            SetDWCStatGroupEnabled(World, ViewportClient, TEXT("DWCGPUMemory"), bEnable);
            return;
        }

        ExecuteDWCStatCommand(World, ViewportClient, TEXT("DWC"));
        ExecuteDWCStatCommand(World, ViewportClient, TEXT("DWCCPUMemory"));
        ExecuteDWCStatCommand(World, ViewportClient, TEXT("DWCGPUMemory"));
    }

    void ToggleDWCMemoryStats(UWorld* World)
    {
        ToggleDWCMemoryStats(World, ResolveDWCViewportClient(World));
    }

    void LogDWCStatHelp()
    {
        UE_LOG(LogDWC, Display, TEXT("DWC stat commands:"));
        UE_LOG(LogDWC, Display, TEXT("  stat dwc"));
        UE_LOG(LogDWC, Display, TEXT("  stat dwc mem"));
        UE_LOG(LogDWC, Display, TEXT("  stat dwc memory"));
        UE_LOG(LogDWC, Display, TEXT("  stat dwc workload"));
        UE_LOG(LogDWC, Display, TEXT("  stat dwc instances"));
        UE_LOG(LogDWC, Display, TEXT("  stat dwc cpu"));
        UE_LOG(LogDWC, Display, TEXT("  stat dwc gpu"));
        UE_LOG(LogDWC, Display, TEXT("  stat dwc help / stat dwc ?"));
    }

    void AppendDWCStatAutocomplete(TArray<FAutoCompleteCommand>& AutoCompleteList)
    {
        const UConsoleSettings* ConsoleSettings = GetDefault<UConsoleSettings>();
        for (const FDWCStatAlias& Alias : GDWCStatAliases)
        {
            FAutoCompleteCommand& AutoCompleteCommand = AutoCompleteList.AddDefaulted_GetRef();
            AutoCompleteCommand.Command = Alias.Command;
            AutoCompleteCommand.Desc = Alias.Description;
            if (ConsoleSettings != nullptr)
            {
                AutoCompleteCommand.Color = ConsoleSettings->AutoCompleteCommandColor;
            }
        }
    }

    bool ToggleDWCStatCommand(UWorld* World, FCommonViewportClient* ViewportClient, const TCHAR* Stream)
    {
        const TCHAR* Cmd = Stream != nullptr ? Stream : TEXT("");

        if (FParse::Command(&Cmd, TEXT("MEM")) || FParse::Command(&Cmd, TEXT("MEMORY")))
        {
            ToggleDWCMemoryStats(World, ViewportClient);
            return true;
        }

        if (FParse::Command(&Cmd, TEXT("WORKLOAD")))
        {
            ExecuteDWCStatCommand(World, ViewportClient, TEXT("DWCWorkload"));
            return true;
        }

        if (FParse::Command(&Cmd, TEXT("INSTANCES")) || FParse::Command(&Cmd, TEXT("INSTANCE")))
        {
            ExecuteDWCStatCommand(World, ViewportClient, TEXT("DWCInstances"));
            return true;
        }

        if (FParse::Command(&Cmd, TEXT("CPU")))
        {
            ToggleDWCStatGroup(World, ViewportClient, TEXT("DWCCPUMemory"));
            return true;
        }

        if (FParse::Command(&Cmd, TEXT("GPU")))
        {
            ToggleDWCStatGroup(World, ViewportClient, TEXT("DWCGPUMemory"));
            return true;
        }

        if (FParse::Command(&Cmd, TEXT("HELP")) || FParse::Command(&Cmd, TEXT("?")))
        {
            LogDWCStatHelp();
            return true;
        }

        if (FString(Cmd).TrimStartAndEnd().IsEmpty())
        {
            ToggleDWCMemoryStats(World, ViewportClient);
            return true;
        }

        LogDWCStatHelp();
        return true;
    }

    FAutoConsoleCommandWithWorld GDWCMemoryStatAlias(
        TEXT("stat dwc mem"),
        TEXT("Toggle DWC memory statistics."),
        FConsoleCommandWithWorldDelegate::CreateStatic(&ToggleDWCMemoryStats),
        ECVF_Default);

    FAutoConsoleCommandWithWorld GDWCMemoryLongStatAlias(
        TEXT("stat dwc memory"),
        TEXT("Toggle DWC memory statistics."),
        FConsoleCommandWithWorldDelegate::CreateStatic(&ToggleDWCMemoryStats),
        ECVF_Default);

    FAutoConsoleCommandWithWorld GDWCWorkloadStatAlias(
        TEXT("stat dwc workload"),
        TEXT("Toggle DWC workload statistics."),
        FConsoleCommandWithWorldDelegate::CreateLambda(
            [](UWorld* World)
            {
                ExecuteDWCStatCommand(World, TEXT("DWCWorkload"));
            }),
        ECVF_Default);

    FAutoConsoleCommandWithWorld GDWCInstancesStatAlias(
        TEXT("stat dwc instances"),
        TEXT("Toggle DWC instance/resource count statistics."),
        FConsoleCommandWithWorldDelegate::CreateLambda(
            [](UWorld* World)
            {
                ExecuteDWCStatCommand(World, TEXT("DWCInstances"));
            }),
        ECVF_Default);
} // namespace

void DWCStats::RegisterStatCommands()
{
    if (GEngine == nullptr || bDWCStatCommandsRegistered)
    {
        return;
    }

    GEngine->AddEngineStat(
        TEXT("STAT_DWC"),
        TEXT("STATCAT_Advanced"),
        FText::FromString(TEXT("Toggle DWC stats. Supports: mem, workload, instances, cpu, gpu.")),
        UEngine::FEngineStatRender(),
        UEngine::FEngineStatToggle::CreateStatic(&ToggleDWCStatCommand));

    if (!DWCStatAutocompleteHandle.IsValid())
    {
        DWCStatAutocompleteHandle = UConsole::RegisterConsoleAutoCompleteEntries.AddStatic(&AppendDWCStatAutocomplete);
    }

    bDWCStatCommandsRegistered = true;
}

void DWCStats::UnregisterStatCommands()
{
    if (DWCStatAutocompleteHandle.IsValid())
    {
        UConsole::RegisterConsoleAutoCompleteEntries.Remove(DWCStatAutocompleteHandle);
        DWCStatAutocompleteHandle.Reset();
    }

    if (GEngine != nullptr && bDWCStatCommandsRegistered)
    {
        GEngine->RemoveEngineStat(TEXT("STAT_DWC"));
    }

    bDWCStatCommandsRegistered = false;
}
