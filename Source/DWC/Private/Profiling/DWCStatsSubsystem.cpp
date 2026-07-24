#include "Profiling/DWCStatsSubsystem.h"

#include "Async/DWCLODVertexColorTasks.h"
#include "Async/DWCSkinningTasks.h"
#include "Components/DynamicWetClothesComponent.h"
#include "DataAssets/WetClothingAsset.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "WetRendering/DWCGPUResourceSubsystem.h"

namespace
{
    constexpr float DWCStatsRefreshIntervalSeconds = 0.25f;
    constexpr float DWCWorkloadRateIntervalSeconds = 1.0f;
    constexpr const TCHAR* DWCCPUMemoryStatCommand = TEXT("stat DWCCPUMemory");
    constexpr const TCHAR* DWCGPUMemoryStatCommand = TEXT("stat DWCGPUMemory");

    template <typename PayloadType, typename MemoryFunctionType>
    void AddUniqueSharedPayload(
        const TSharedPtr<const PayloadType, ESPMode::ThreadSafe>& Payload,
        TSet<const PayloadType*>& SeenPayloads,
        uint32& OutCount,
        uint64& OutBytes,
        MemoryFunctionType&& GetMemoryBytes)
    {
        const PayloadType* Pointer = Payload.Get();
        if (Pointer == nullptr || SeenPayloads.Contains(Pointer))
        {
            return;
        }

        SeenPayloads.Add(Pointer);
        ++OutCount;
        OutBytes += GetMemoryBytes(*Pointer);
    }

    void AddUniqueResidentTexture(
        UTexture2D* Texture,
        TSet<const UTexture2D*>& SeenTextures,
        uint32& OutCount,
        uint64& OutBytes)
    {
        if (Texture == nullptr || SeenTextures.Contains(Texture))
        {
            return;
        }

        SeenTextures.Add(Texture);
        ++OutCount;
        OutBytes += Texture->CalcTextureMemorySizeEnum(TMC_ResidentMips);
    }

    uint32 CalculatePerSecondRate(const uint64 Current, const uint64 Previous, const float SampleSeconds)
    {
        if (SampleSeconds <= 0.0f || Current < Previous)
        {
            return 0;
        }

        const double Rate = static_cast<double>(Current - Previous) / static_cast<double>(SampleSeconds);
        return static_cast<uint32>(FMath::Clamp<double>(FMath::RoundToDouble(Rate), 0.0, MAX_uint32));
    }
}

void UDWCStatsSubsystem::Deinitialize()
{
    RegisteredComponents.Reset();
    LatestSnapshot = FDWCStatsSnapshot();
    LatestWorkloadSnapshot = FDWCWorkloadStatsSnapshot();
    PublishStats(LatestSnapshot);
    PublishWorkloadStats(LatestWorkloadSnapshot);
    Super::Deinitialize();
}

void UDWCStatsSubsystem::Tick(const float DeltaTime)
{
    SyncMemoryStatGroups();

    if (!bWorkloadRateStateInitialized)
    {
        LastWorkloadEventTotals = FDWCWorkloadStats::ReadEventTotals();
        FDWCWorkloadStats::ConsumeSurfaceWaterMaxPendingStamps();
        bWorkloadRateStateInitialized = true;
    }
    else
    {
        WorkloadSampleSeconds += DeltaTime;
        if (WorkloadSampleSeconds >= DWCWorkloadRateIntervalSeconds)
        {
            RefreshWorkloadRates(WorkloadSampleSeconds);
            WorkloadSampleSeconds = 0.0f;
        }
    }

    TimeUntilNextRefresh -= DeltaTime;
    if (TimeUntilNextRefresh > 0.0f)
    {
        return;
    }

    TimeUntilNextRefresh = DWCStatsRefreshIntervalSeconds;
    RefreshStats();
}

void UDWCStatsSubsystem::SyncMemoryStatGroups()
{
#if STATS
    UWorld* World = GetWorld();
    UGameViewportClient* GameViewport = World != nullptr ? World->GetGameViewport() : nullptr;
    if (GameViewport == nullptr || GEngine == nullptr || GEngine->GameViewport != GameViewport)
    {
        return;
    }

    const bool bDWCStatEnabled = GameViewport->IsStatEnabled(TEXT("DWC"));
    const bool bStateChanged = !bMemoryStatGroupStateInitialized || bDWCStatEnabled != bLastDWCStatEnabled;
    bMemoryStatGroupStateInitialized = true;
    bLastDWCStatEnabled = bDWCStatEnabled;

    if (!bStateChanged)
    {
        return;
    }

    const auto SyncGroup = [World, GameViewport, bDWCStatEnabled](const TCHAR* GroupName, const TCHAR* Command)
    {
        if (GameViewport->IsStatEnabled(GroupName) != bDWCStatEnabled)
        {
            GEngine->Exec(World, Command);
        }
    };

    SyncGroup(TEXT("DWCCPUMemory"), DWCCPUMemoryStatCommand);
    SyncGroup(TEXT("DWCGPUMemory"), DWCGPUMemoryStatCommand);
#endif
}

TStatId UDWCStatsSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UDWCStatsSubsystem, STATGROUP_Tickables);
}

bool UDWCStatsSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game ||
           WorldType == EWorldType::PIE ||
           WorldType == EWorldType::GamePreview;
}

void UDWCStatsSubsystem::RegisterComponent(UDynamicWetClothesComponent* Component)
{
    if (IsValid(Component) && Component->GetWorld() == GetWorld())
    {
        RegisteredComponents.Add(Component);
        TimeUntilNextRefresh = 0.0f;
    }
}

void UDWCStatsSubsystem::UnregisterComponent(UDynamicWetClothesComponent* Component)
{
    RegisteredComponents.Remove(Component);
    TimeUntilNextRefresh = 0.0f;
}

void UDWCStatsSubsystem::RefreshStats()
{
    FDWCStatsSnapshot NewSnapshot;
    CollectStats(NewSnapshot);
    LatestSnapshot = NewSnapshot;
    CollectBacklogStats(LatestWorkloadSnapshot);
    PublishStats(LatestSnapshot);
    PublishWorkloadStats(LatestWorkloadSnapshot);
}

void UDWCStatsSubsystem::RefreshWorkloadRates(const float SampleSeconds)
{
    const FDWCWorkloadEventTotals Current = FDWCWorkloadStats::ReadEventTotals();

    LatestWorkloadSnapshot.SurfaceWaterStampsQueuedPerSecond = CalculatePerSecondRate(
        Current.SurfaceWaterStampsQueued, LastWorkloadEventTotals.SurfaceWaterStampsQueued, SampleSeconds);
    LatestWorkloadSnapshot.SurfaceWaterStampsSubmittedPerSecond = CalculatePerSecondRate(
        Current.SurfaceWaterStampsSubmitted, LastWorkloadEventTotals.SurfaceWaterStampsSubmitted, SampleSeconds);
    LatestWorkloadSnapshot.SurfaceWaterGPUDispatchesPerSecond = CalculatePerSecondRate(
        Current.SurfaceWaterGPUDispatches, LastWorkloadEventTotals.SurfaceWaterGPUDispatches, SampleSeconds);
    LatestWorkloadSnapshot.SurfaceWaterMaxPendingStamps = FDWCWorkloadStats::ConsumeSurfaceWaterMaxPendingStamps();

    LatestWorkloadSnapshot.CPUSkinningCompletedPerSecond = CalculatePerSecondRate(
        Current.CPUSkinningCompleted, LastWorkloadEventTotals.CPUSkinningCompleted, SampleSeconds);
    LatestWorkloadSnapshot.CPUSkinningVerticesProcessedPerSecond = CalculatePerSecondRate(
        Current.CPUSkinningVerticesProcessed, LastWorkloadEventTotals.CPUSkinningVerticesProcessed, SampleSeconds);

    LatestWorkloadSnapshot.LODTransferCompletedPerSecond = CalculatePerSecondRate(
        Current.LODTransferCompleted, LastWorkloadEventTotals.LODTransferCompleted, SampleSeconds);
    LatestWorkloadSnapshot.LODDirtyVerticesTransferredPerSecond = CalculatePerSecondRate(
        Current.LODDirtyVerticesTransferred, LastWorkloadEventTotals.LODDirtyVerticesTransferred, SampleSeconds);

    LatestWorkloadSnapshot.WetContactsReceivedPerSecond = CalculatePerSecondRate(
        Current.WetContactsReceived, LastWorkloadEventTotals.WetContactsReceived, SampleSeconds);
    LatestWorkloadSnapshot.WetContactsAppliedPerSecond = CalculatePerSecondRate(
        Current.WetContactsApplied, LastWorkloadEventTotals.WetContactsApplied, SampleSeconds);
    LatestWorkloadSnapshot.WetContactsRejectedPerSecond = CalculatePerSecondRate(
        Current.WetContactsRejected, LastWorkloadEventTotals.WetContactsRejected, SampleSeconds);

    LatestWorkloadSnapshot.WetnessSimulationUpdatesPerSecond = CalculatePerSecondRate(
        Current.WetnessSimulationUpdates, LastWorkloadEventTotals.WetnessSimulationUpdates, SampleSeconds);
    LatestWorkloadSnapshot.ChangedReceiversPerSecond = CalculatePerSecondRate(
        Current.ChangedReceivers, LastWorkloadEventTotals.ChangedReceivers, SampleSeconds);
    LatestWorkloadSnapshot.DirtyVerticesGeneratedPerSecond = CalculatePerSecondRate(
        Current.DirtyVerticesGenerated, LastWorkloadEventTotals.DirtyVerticesGenerated, SampleSeconds);

    LatestWorkloadSnapshot.RenderUpdatesPerSecond = CalculatePerSecondRate(
        Current.RenderUpdates, LastWorkloadEventTotals.RenderUpdates, SampleSeconds);
    LatestWorkloadSnapshot.MaterialsUpdatedPerSecond = CalculatePerSecondRate(
        Current.MaterialsUpdated, LastWorkloadEventTotals.MaterialsUpdated, SampleSeconds);

    LatestWorkloadSnapshot.GPUBackendUpdatesSubmittedPerSecond = CalculatePerSecondRate(
        Current.GPUBackendUpdatesSubmitted, LastWorkloadEventTotals.GPUBackendUpdatesSubmitted, SampleSeconds);
    LatestWorkloadSnapshot.GPUBackendDispatchesPerSecond = CalculatePerSecondRate(
        Current.GPUBackendDispatches, LastWorkloadEventTotals.GPUBackendDispatches, SampleSeconds);

    LastWorkloadEventTotals = Current;
}

void UDWCStatsSubsystem::CollectBacklogStats(FDWCWorkloadStatsSnapshot& OutSnapshot) const
{
    uint64 SurfaceWaterPendingStamps = 0;
    uint64 CPUSkinningPendingTasks = 0;
    uint64 LODTransferPendingTasks = 0;
    uint64 PendingLODDirtyVertices = 0;

    for (const TWeakObjectPtr<UDynamicWetClothesComponent>& ComponentPointer : RegisteredComponents)
    {
        const UDynamicWetClothesComponent* Component = ComponentPointer.Get();
        if (!IsValid(Component) || Component->GetWorld() != GetWorld())
        {
            continue;
        }

        for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& ReceiverPointer : Component->Receivers)
        {
            const FDWCWetMeshReceiverRuntime* Receiver = ReceiverPointer.Get();
            if (Receiver == nullptr)
            {
                continue;
            }

            CPUSkinningPendingTasks += Receiver->bCpuSkinningTaskPending ? 1u : 0u;
            LODTransferPendingTasks += Receiver->bLODVertexColorTransferPending ? 1u : 0u;
            PendingLODDirtyVertices += Receiver->PendingLODVertexColorDirtySourceVertices.Num();

            for (const TPair<int32, TUniquePtr<IDWCSurfaceWaterSimulationState>>& Pair : Receiver->SurfaceWaterStatesByMaterialSlot)
            {
                if (Pair.Value.IsValid())
                {
                    SurfaceWaterPendingStamps += Pair.Value->GetPendingStampCount();
                }
            }
        }
    }

    OutSnapshot.SurfaceWaterPendingStamps = static_cast<uint32>(FMath::Min<uint64>(SurfaceWaterPendingStamps, MAX_uint32));
    OutSnapshot.CPUSkinningPendingTasks = static_cast<uint32>(FMath::Min<uint64>(CPUSkinningPendingTasks, MAX_uint32));
    OutSnapshot.LODTransferPendingTasks = static_cast<uint32>(FMath::Min<uint64>(LODTransferPendingTasks, MAX_uint32));
    OutSnapshot.PendingLODDirtyVertices = static_cast<uint32>(FMath::Min<uint64>(PendingLODDirtyVertices, MAX_uint32));
}

void UDWCStatsSubsystem::CollectStats(FDWCStatsSnapshot& OutSnapshot)
{
    if (UWorld* World = GetWorld())
    {
        if (const UDWCGPUResourceSubsystem* GPUResourceSubsystem = World->GetSubsystem<UDWCGPUResourceSubsystem>())
        {
            const FDWCGPUResourceSubsystemStats ResourceStats = GPUResourceSubsystem->GetStats();
            OutSnapshot.SharedGPUStaticResourceCount = ResourceStats.StaticSlotResourceCount;
            OutSnapshot.RuntimeRenderProfileCount = ResourceStats.RuntimeProfileCount;
            OutSnapshot.GPUResourceSubsystemCPUBytes = ResourceStats.CPUBytes;
            OutSnapshot.SharedGPUStaticBufferGPUBytes = ResourceStats.StaticBufferGPUBytes;
            OutSnapshot.SharedGPURenderProfileLUTGPUBytes = ResourceStats.RenderProfileLUTGPUBytes;
            OutSnapshot.SharedGPUProfileIDRemapGPUBytes = ResourceStats.ProfileIDRemapGPUBytes;
            OutSnapshot.SharedGPUSurfaceNormalArrayGPUBytes = ResourceStats.SurfaceNormalArrayGPUBytes;
            OutSnapshot.SharedGPUResourceGPUBytes = ResourceStats.GetGPUBytes();
        }
    }

    TSet<const FWetClothingRuntimeData*> SeenRuntimeData;
    TSet<const FDWCSkinningStaticData*> SeenSkinningStaticData;
    TSet<const FDWCLODVertexStaticData*> SeenLODVertexStaticData;
    TSet<const TArray<int32>*> SeenLODVertexColorTransferMaps;
    TSet<const UTexture2D*> SeenWrinkleTextures;
    TSet<const UTexture2D*> SeenTransparencyTextures;

    for (auto It = RegisteredComponents.CreateIterator(); It; ++It)
    {
        UDynamicWetClothesComponent* Component = It->Get();
        if (!IsValid(Component) || Component->GetWorld() != GetWorld())
        {
            It.RemoveCurrent();
            continue;
        }

        FDWCModeInstanceStats& ModeStats =
            Component->GetActiveSimulationMode() == EDWCSimulationMode::WetnessMapGPU
                ? OutSnapshot.GPU
                : OutSnapshot.CPU;
        bool bHasActiveReceiver = false;

        for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& ReceiverPointer : Component->Receivers)
        {
            const FDWCWetMeshReceiverRuntime* Receiver = ReceiverPointer.Get();
            if (Receiver == nullptr)
            {
                continue;
            }

            bHasActiveReceiver = true;
            ++ModeStats.RuntimeReceiverCount;
            ++OutSnapshot.LODTotalReceiverCount;

            const FDWCQualityLODPolicy& LODPolicy = Receiver->QualityLODState.ResolvedPolicy;
            if (LODPolicy.bUpdateSurfaceWater)
            {
                ++OutSnapshot.LODSurfaceWaterEnabledReceiverCount;
            }
            if (LODPolicy.bUpdateWetRendering)
            {
                ++OutSnapshot.LODCPUWetnessRenderingEnabledReceiverCount;
            }
            if (LODPolicy.bUpdateWrinkle)
            {
                ++OutSnapshot.LODWrinkleEnabledReceiverCount;
            }
            if (LODPolicy.bUpdateTransparency)
            {
                ++OutSnapshot.LODTransparencyEnabledReceiverCount;
            }

            OutSnapshot.ReceiverMetadataCPUBytes += sizeof(*Receiver);
            OutSnapshot.ReceiverMetadataCPUBytes += Receiver->SurfaceWaterProfilesByMaterialSlot.GetAllocatedSize();
            OutSnapshot.ReceiverMetadataCPUBytes += Receiver->LODVertexStaticDataByLOD.GetAllocatedSize();
            OutSnapshot.ReceiverMetadataCPUBytes += Receiver->LODVertexColorTransferMapsByLOD.GetAllocatedSize();

            AddUniqueSharedPayload(
                Receiver->SharedRuntimeData,
                SeenRuntimeData,
                OutSnapshot.SharedRuntimeDataCount,
                OutSnapshot.SharedRuntimeDataCPUBytes,
                [](const FWetClothingRuntimeData& Data) { return Data.GetAllocatedMemoryBytes(); });
            AddUniqueSharedPayload(
                Receiver->SkinningStaticData,
                SeenSkinningStaticData,
                OutSnapshot.SharedSkinningStaticDataCount,
                OutSnapshot.SharedSkinningStaticDataCPUBytes,
                [](const FDWCSkinningStaticData& Data) { return Data.GetAllocatedMemoryBytes(); });

            for (const TPair<int32, TSharedPtr<const FDWCLODVertexStaticData, ESPMode::ThreadSafe>>& Pair : Receiver->LODVertexStaticDataByLOD)
            {
                AddUniqueSharedPayload(
                    Pair.Value,
                    SeenLODVertexStaticData,
                    OutSnapshot.SharedLODVertexStaticDataCount,
                    OutSnapshot.SharedLODVertexStaticDataCPUBytes,
                    [](const FDWCLODVertexStaticData& Data) { return Data.GetAllocatedMemoryBytes(); });
            }

            for (const TPair<int32, TSharedPtr<const TArray<int32>, ESPMode::ThreadSafe>>& Pair : Receiver->LODVertexColorTransferMapsByLOD)
            {
                AddUniqueSharedPayload(
                    Pair.Value,
                    SeenLODVertexColorTransferMaps,
                    OutSnapshot.SharedLODVertexColorTransferMapCount,
                    OutSnapshot.SharedLODVertexColorTransferMapCPUBytes,
                    [](const TArray<int32>& Data) { return sizeof(Data) + Data.GetAllocatedSize(); });
            }

            if (Receiver->SimulationState.IsValid())
            {
                ++OutSnapshot.AbsorbedSimulationStateCount;
                OutSnapshot.AbsorbedSimulationStateCPUBytes += Receiver->SimulationState->GetAllocatedMemoryBytes();
            }
            if (Receiver->MeshSampler.IsValid())
            {
                OutSnapshot.MeshSamplerCPUBytes += Receiver->MeshSampler->GetAllocatedMemoryBytes();
            }
            if (Receiver->RenderStage.IsValid())
            {
                OutSnapshot.RenderStageCPUBytes += Receiver->RenderStage->GetAllocatedMemoryBytes();
            }

            OutSnapshot.LODVertexColorCacheCPUBytes += Receiver->LODVertexColorCachesByLOD.GetAllocatedSize();
            for (const TPair<int32, TSharedPtr<const TArray<FColor>, ESPMode::ThreadSafe>>& Pair : Receiver->LODVertexColorCachesByLOD)
            {
                if (Pair.Value.IsValid())
                {
                    OutSnapshot.LODVertexColorCacheCPUBytes += sizeof(TArray<FColor>) + Pair.Value->GetAllocatedSize();
                }
            }
            OutSnapshot.PendingLODVertexColorDirtyCPUBytes +=
                Receiver->PendingLODVertexColorDirtySourceVertices.GetAllocatedSize();

            OutSnapshot.SurfaceWaterCPUBytes += Receiver->SurfaceWaterStatesByMaterialSlot.GetAllocatedSize();
            for (const TPair<int32, TUniquePtr<IDWCSurfaceWaterSimulationState>>& Pair : Receiver->SurfaceWaterStatesByMaterialSlot)
            {
                if (Pair.Value.IsValid())
                {
                    ++OutSnapshot.SurfaceWaterStateCount;
                    OutSnapshot.SurfaceWaterCPUBytes += Pair.Value->GetAllocatedMemoryBytes();
                }
            }

            if (Receiver->GPUBackend.IsValid())
            {
                const FDWCGPUBackendStats BackendStats = Receiver->GPUBackend->GetStats();
                ModeStats.ActiveMaterialCount += BackendStats.ActiveMaterialCount;
                OutSnapshot.GPUBackendCPUBytes += BackendStats.CPUBytes;
                OutSnapshot.GPUBackendGPUBytes += BackendStats.GPUBytes;
            }
            else if (Receiver->RenderStage.IsValid())
            {
                for (const TObjectPtr<UMaterialInstanceDynamic>& Material : Receiver->RenderStage->WetMaterialInstances)
                {
                    ModeStats.ActiveMaterialCount += Material != nullptr ? 1u : 0u;
                }
            }

            const UWetClothingAsset* WetClothingAsset = Receiver->WetClothingAsset.Get();
            if (WetClothingAsset != nullptr && Receiver->RenderStage.IsValid())
            {
                const int32 PreferredWrinkleUVChannel =
                    WetClothingAsset->Authored.WrinkleData.WrinkleUVChannelIndex != INDEX_NONE
                        ? WetClothingAsset->Authored.WrinkleData.WrinkleUVChannelIndex
                        : 0;

                for (int32 MaterialSlotIndex = 0; MaterialSlotIndex < Receiver->RenderStage->WetMaterialInstances.Num(); ++MaterialSlotIndex)
                {
                    if (Receiver->RenderStage->WetMaterialInstances[MaterialSlotIndex] == nullptr ||
                        !WetClothingAsset->IsMaterialSlotWettable(MaterialSlotIndex))
                    {
                        continue;
                    }

                    const FWetWrinkleResolvedNormalMap WrinkleMap =
                        WetClothingAsset->Authored.WrinkleData.ResolveRuntimeWrinkleNormalMap(
                            MaterialSlotIndex,
                            PreferredWrinkleUVChannel,
                            UWetClothingAsset::RuntimeSimulationLODIndex);
                    if (WrinkleMap.IsValid() &&
                        WrinkleMap.Texture != nullptr &&
                        WrinkleMap.UVChannelIndex == PreferredWrinkleUVChannel &&
                        WrinkleMap.LODIndex == UWetClothingAsset::RuntimeSimulationLODIndex)
                    {
                        ++OutSnapshot.WrinkleMaterialBindingCount;
                        AddUniqueResidentTexture(
                            WrinkleMap.Texture,
                            SeenWrinkleTextures,
                            OutSnapshot.WrinkleTextureCount,
                            OutSnapshot.WrinkleTextureGPUBytes);
                    }

                    const FWetClothingTransparencyLayerData* TransparencyLayer =
                        WetClothingAsset->Authored.TransparencyData.TransparencyLayers.FindByPredicate(
                            [MaterialSlotIndex](const FWetClothingTransparencyLayerData& Candidate)
                            {
                                return Candidate.TargetSurface.OuterMaterialSlotIndex == MaterialSlotIndex;
                            });
                    if (TransparencyLayer == nullptr)
                    {
                        continue;
                    }

                    const FWetClothingBakedTransparencyMap* TransparencyMap =
                        WetClothingAsset->Authored.TransparencyData.FindRuntimeBakedTransparencyMap(
                            MaterialSlotIndex,
                            TransparencyLayer->TargetSurface.OuterUVChannel,
                            UWetClothingAsset::RuntimeSimulationLODIndex);
                    if (TransparencyMap == nullptr ||
                        TransparencyMap->TransparencyMap == nullptr ||
                        TransparencyMap->UVChannelIndex < 0 ||
                        TransparencyMap->UVChannelIndex > 3)
                    {
                        continue;
                    }

                    ++OutSnapshot.TransparencyMaterialBindingCount;
                    AddUniqueResidentTexture(
                        TransparencyMap->TransparencyMap,
                        SeenTransparencyTextures,
                        OutSnapshot.TransparencyTextureCount,
                        OutSnapshot.TransparencyTextureGPUBytes);
                }
            }
        }

        ModeStats.ActiveComponentCount += bHasActiveReceiver ? 1u : 0u;
    }
}

void UDWCStatsSubsystem::PublishStats(const FDWCStatsSnapshot& Snapshot) const
{
    SET_DWORD_STAT(STAT_DWC_MemoryTrackedReceivers, Snapshot.CPU.RuntimeReceiverCount + Snapshot.GPU.RuntimeReceiverCount);
    SET_DWORD_STAT(STAT_DWC_CPUComponents, Snapshot.CPU.ActiveComponentCount);
    SET_DWORD_STAT(STAT_DWC_CPUReceivers, Snapshot.CPU.RuntimeReceiverCount);
    SET_DWORD_STAT(STAT_DWC_CPUMaterials, Snapshot.CPU.ActiveMaterialCount);
    SET_DWORD_STAT(STAT_DWC_GPUComponents, Snapshot.GPU.ActiveComponentCount);
    SET_DWORD_STAT(STAT_DWC_GPUReceivers, Snapshot.GPU.RuntimeReceiverCount);
    SET_DWORD_STAT(STAT_DWC_GPUMaterials, Snapshot.GPU.ActiveMaterialCount);
    SET_DWORD_STAT(STAT_DWC_SharedRuntimeDataCount, Snapshot.SharedRuntimeDataCount);
    SET_DWORD_STAT(STAT_DWC_SharedSkinningDataCount, Snapshot.SharedSkinningStaticDataCount);
    SET_DWORD_STAT(STAT_DWC_SharedLODStaticDataCount, Snapshot.SharedLODVertexStaticDataCount);
    SET_DWORD_STAT(STAT_DWC_SharedLODTransferMapCount, Snapshot.SharedLODVertexColorTransferMapCount);
    SET_DWORD_STAT(STAT_DWC_SharedGPUStaticResourceCount, Snapshot.SharedGPUStaticResourceCount);
    SET_DWORD_STAT(STAT_DWC_RuntimeRenderProfileCount, Snapshot.RuntimeRenderProfileCount);
    SET_DWORD_STAT(STAT_DWC_AbsorbedStateCount, Snapshot.AbsorbedSimulationStateCount);
    SET_DWORD_STAT(STAT_DWC_SurfaceWaterStateCount, Snapshot.SurfaceWaterStateCount);
    SET_DWORD_STAT(STAT_DWC_WrinkleMaterialBindingCount, Snapshot.WrinkleMaterialBindingCount);
    SET_DWORD_STAT(STAT_DWC_WrinkleTextureCount, Snapshot.WrinkleTextureCount);
    SET_DWORD_STAT(STAT_DWC_TransparencyMaterialBindingCount, Snapshot.TransparencyMaterialBindingCount);
    SET_DWORD_STAT(STAT_DWC_TransparencyTextureCount, Snapshot.TransparencyTextureCount);
    SET_DWORD_STAT(STAT_DWC_LODTotalReceivers, Snapshot.LODTotalReceiverCount);
    SET_DWORD_STAT(STAT_DWC_LODSurfaceWaterEnabledReceivers, Snapshot.LODSurfaceWaterEnabledReceiverCount);
    SET_DWORD_STAT(STAT_DWC_LODCPUWetnessRenderingEnabledReceivers, Snapshot.LODCPUWetnessRenderingEnabledReceiverCount);
    SET_DWORD_STAT(STAT_DWC_LODWrinkleEnabledReceivers, Snapshot.LODWrinkleEnabledReceiverCount);
    SET_DWORD_STAT(STAT_DWC_LODTransparencyEnabledReceivers, Snapshot.LODTransparencyEnabledReceiverCount);

    SET_MEMORY_STAT(STAT_DWC_SharedRuntimeDataCPU, Snapshot.SharedRuntimeDataCPUBytes);
    SET_MEMORY_STAT(STAT_DWC_SharedSkinningDataCPU, Snapshot.SharedSkinningStaticDataCPUBytes);
    SET_MEMORY_STAT(STAT_DWC_SharedLODStaticDataCPU, Snapshot.SharedLODVertexStaticDataCPUBytes);
    SET_MEMORY_STAT(STAT_DWC_SharedLODTransferMapCPU, Snapshot.SharedLODVertexColorTransferMapCPUBytes);
    SET_MEMORY_STAT(STAT_DWC_AbsorbedStateCPU, Snapshot.AbsorbedSimulationStateCPUBytes);
    SET_MEMORY_STAT(STAT_DWC_MeshSamplerCPU, Snapshot.MeshSamplerCPUBytes);
    SET_MEMORY_STAT(STAT_DWC_RenderStageCPU, Snapshot.RenderStageCPUBytes);
    SET_MEMORY_STAT(STAT_DWC_LODVertexColorCacheCPU, Snapshot.LODVertexColorCacheCPUBytes);
    SET_MEMORY_STAT(STAT_DWC_PendingLODDirtyCPU, Snapshot.PendingLODVertexColorDirtyCPUBytes);
    SET_MEMORY_STAT(STAT_DWC_SurfaceWaterCPU, Snapshot.SurfaceWaterCPUBytes);
    SET_MEMORY_STAT(STAT_DWC_GPUBackendCPU, Snapshot.GPUBackendCPUBytes);
    SET_MEMORY_STAT(STAT_DWC_GPUResourceSubsystemCPU, Snapshot.GPUResourceSubsystemCPUBytes);
    SET_MEMORY_STAT(STAT_DWC_ReceiverMetadataCPU, Snapshot.ReceiverMetadataCPUBytes);
    SET_MEMORY_STAT(STAT_DWC_TotalTrackedCPU, Snapshot.GetTrackedCPUBytes());
    SET_MEMORY_STAT(STAT_DWC_GPUBackendGPU, Snapshot.GPUBackendGPUBytes);
    SET_MEMORY_STAT(STAT_DWC_SharedGPUStaticBufferGPU, Snapshot.SharedGPUStaticBufferGPUBytes);
    SET_MEMORY_STAT(STAT_DWC_SharedGPURenderProfileLUTGPU, Snapshot.SharedGPURenderProfileLUTGPUBytes);
    SET_MEMORY_STAT(STAT_DWC_SharedGPUProfileIDRemapGPU, Snapshot.SharedGPUProfileIDRemapGPUBytes);
    SET_MEMORY_STAT(STAT_DWC_SharedGPUSurfaceNormalArrayGPU, Snapshot.SharedGPUSurfaceNormalArrayGPUBytes);
    SET_MEMORY_STAT(STAT_DWC_SharedGPUResourceGPU, Snapshot.SharedGPUResourceGPUBytes);
    SET_MEMORY_STAT(STAT_DWC_WrinkleTextureGPU, Snapshot.WrinkleTextureGPUBytes);
    SET_MEMORY_STAT(STAT_DWC_TransparencyTextureGPU, Snapshot.TransparencyTextureGPUBytes);
    SET_MEMORY_STAT(STAT_DWC_TotalTrackedGPU, Snapshot.GetTrackedGPUBytes());
}

void UDWCStatsSubsystem::PublishWorkloadStats(const FDWCWorkloadStatsSnapshot& Snapshot) const
{
    SET_DWORD_STAT(STAT_DWC_SurfaceWaterStampsQueuedRate, Snapshot.SurfaceWaterStampsQueuedPerSecond);
    SET_DWORD_STAT(STAT_DWC_SurfaceWaterStampsSubmittedRate, Snapshot.SurfaceWaterStampsSubmittedPerSecond);
    SET_DWORD_STAT(STAT_DWC_SurfaceWaterGPUDispatchesRate, Snapshot.SurfaceWaterGPUDispatchesPerSecond);
    SET_DWORD_STAT(STAT_DWC_SurfaceWaterMaxPendingStamps, Snapshot.SurfaceWaterMaxPendingStamps);
    SET_DWORD_STAT(STAT_DWC_SurfaceWaterPendingStamps, Snapshot.SurfaceWaterPendingStamps);
    SET_DWORD_STAT(STAT_DWC_CPUSkinningCompletedRate, Snapshot.CPUSkinningCompletedPerSecond);
    SET_DWORD_STAT(STAT_DWC_CPUSkinningVerticesRate, Snapshot.CPUSkinningVerticesProcessedPerSecond);
    SET_DWORD_STAT(STAT_DWC_CPUSkinningPendingTasks, Snapshot.CPUSkinningPendingTasks);
    SET_DWORD_STAT(STAT_DWC_LODTransferCompletedRate, Snapshot.LODTransferCompletedPerSecond);
    SET_DWORD_STAT(STAT_DWC_LODDirtyVerticesRate, Snapshot.LODDirtyVerticesTransferredPerSecond);
    SET_DWORD_STAT(STAT_DWC_LODTransferPendingTasks, Snapshot.LODTransferPendingTasks);
    SET_DWORD_STAT(STAT_DWC_PendingLODDirtyVertices, Snapshot.PendingLODDirtyVertices);
    SET_DWORD_STAT(STAT_DWC_WetContactsReceivedRate, Snapshot.WetContactsReceivedPerSecond);
    SET_DWORD_STAT(STAT_DWC_WetContactsAppliedRate, Snapshot.WetContactsAppliedPerSecond);
    SET_DWORD_STAT(STAT_DWC_WetContactsRejectedRate, Snapshot.WetContactsRejectedPerSecond);
    SET_DWORD_STAT(STAT_DWC_WetnessSimulationUpdatesRate, Snapshot.WetnessSimulationUpdatesPerSecond);
    SET_DWORD_STAT(STAT_DWC_ChangedReceiversRate, Snapshot.ChangedReceiversPerSecond);
    SET_DWORD_STAT(STAT_DWC_DirtyVerticesGeneratedRate, Snapshot.DirtyVerticesGeneratedPerSecond);
    SET_DWORD_STAT(STAT_DWC_RenderUpdatesRate, Snapshot.RenderUpdatesPerSecond);
    SET_DWORD_STAT(STAT_DWC_MaterialsUpdatedRate, Snapshot.MaterialsUpdatedPerSecond);
    SET_DWORD_STAT(STAT_DWC_GPUBackendUpdatesSubmittedRate, Snapshot.GPUBackendUpdatesSubmittedPerSecond);
    SET_DWORD_STAT(STAT_DWC_GPUBackendDispatchesRate, Snapshot.GPUBackendDispatchesPerSecond);
}
