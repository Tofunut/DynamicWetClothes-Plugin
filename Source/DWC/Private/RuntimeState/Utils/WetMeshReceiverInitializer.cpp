#include "RuntimeState/Utils/WetMeshReceiverInitializer.h"

#include "Components/DynamicWetClothesComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "RuntimeState/DWCRuntimeDataSubsystem.h"
#include "RuntimeState/Utils/DWCLODVertexColorTransferCoordinator.h"
#include "RuntimeState/Utils/WetRuntimeDataBuilder.h"
#include "WetInputSystem/Sampling/WetClothingMeshSampler.h"
#include "WetRendering/WetRenderStage.h"
#include "WetSimulation/AbsorbedWetness/AbsorbedWetnessSimulationState.h"
#include "GPU/DWCSurfaceWaterSimulationState.h"
#include "GPU/DWCGPUBackend.h"
#include "Modules/ModuleManager.h"
#include "Utility/DWCLog.h"

namespace
{
    const TCHAR* SimulationModeToLogString(const EDWCSimulationMode Mode)
    {
        switch (Mode)
        {
        case EDWCSimulationMode::VertexCPU:
            return TEXT("Vertex (CPU)");
        case EDWCSimulationMode::WetnessMapGPU:
            return TEXT("Wetness Map (GPU)");
        default:
            return TEXT("Unknown");
        }
    }

    bool IsReceiverInitializerGPUWetnessMode(const EDWCSimulationMode Mode)
    {
        return Mode == EDWCSimulationMode::WetnessMapGPU;
    }


    void ReleaseReceiverResources(FDWCWetMeshReceiverRuntime& Receiver)
    {
        if (Receiver.GPUBackend.IsValid())
        {
            Receiver.GPUBackend->Shutdown();
            Receiver.GPUBackend.Reset();
        }

        for (TPair<int32, TUniquePtr<IDWCSurfaceWaterSimulationState>>& Pair :
             Receiver.SurfaceWaterStatesByMaterialSlot)
        {
            if (Pair.Value.IsValid())
            {
                Pair.Value->Release();
            }
        }
        Receiver.SurfaceWaterStatesByMaterialSlot.Reset();
        Receiver.SurfaceWaterProfilesByMaterialSlot.Reset();
    }

    void LogRuntimeModeData(
        const UDynamicWetClothesComponent* Component,
        const FDWCWetMeshReceiverRuntime& Receiver,
        const EDWCSimulationMode Mode,
        const int32 LODIndex)
    {
        const USkeletalMeshComponent* MeshComponent = Receiver.MeshComponent.Get();
        const USkeletalMesh* SkeletalMesh =
            MeshComponent != nullptr ? MeshComponent->GetSkeletalMeshAsset() : nullptr;
        const UWetClothingAsset* Asset = Receiver.WetClothingAsset.Get();
        if (MeshComponent == nullptr || SkeletalMesh == nullptr || Asset == nullptr)
        {
            UE_LOG(
                LogDWC,
                Error,
                TEXT("DWC runtime mode '%s' cannot initialize on '%s'. MeshComponent='%s', skeletalMesh='%s', asset='%s'."),
                SimulationModeToLogString(Mode),
                *GetNameSafe(Component),
                *GetNameSafe(MeshComponent),
                *GetNameSafe(SkeletalMesh),
                *GetNameSafe(Asset));
            return;
        }

        const FString CPUDiagnostics = Asset->GetPrecomputedSimulationDataValidationSummary(SkeletalMesh);
        const bool bCPUDataValid = Asset->IsPrecomputedSimulationDataValidForMesh(SkeletalMesh);

        if (Mode == EDWCSimulationMode::VertexCPU)
        {
            if (bCPUDataValid)
            {
                UE_LOG(
                    LogDWC,
                    Log,
                    TEXT("DWC runtime mode '%s' active on '%s'. Using CPU runtime data from asset '%s' for mesh '%s' LOD%d (hasCPUPayload=%s). %s"),
                    SimulationModeToLogString(Mode),
                    *GetNameSafe(Component),
                    *GetNameSafe(Asset),
                    *GetNameSafe(SkeletalMesh),
                    LODIndex,
                    Asset->HasCPURuntimeDataPayload() ? TEXT("true") : TEXT("false"),
                    *CPUDiagnostics);
            }
            else
            {
                UE_LOG(
                    LogDWC,
                    Error,
                    TEXT("DWC runtime mode '%s' requested on '%s' but CPU runtime data is not usable. Asset='%s', mesh='%s', LOD=%d, hasCPUPayload=%s. %s"),
                    SimulationModeToLogString(Mode),
                    *GetNameSafe(Component),
                    *GetNameSafe(Asset),
                    *GetNameSafe(SkeletalMesh),
                    LODIndex,
                    Asset->HasCPURuntimeDataPayload() ? TEXT("true") : TEXT("false"),
                    *CPUDiagnostics);
            }
            return;
        }

        const bool bGPURuntimeDataValid = Asset->IsGPURuntimeDataValidForMesh(SkeletalMesh, LODIndex);
        const bool bGPUMapDataValid = Asset->IsGPUWetMapDataValidForMesh(SkeletalMesh, LODIndex);
        if (bGPURuntimeDataValid && bGPUMapDataValid)
        {
            UE_LOG(
                LogDWC,
                Log,
                TEXT("DWC runtime mode '%s' active on '%s'. Using GPU runtime/map data from asset '%s' for mesh '%s' LOD%d (hasGPUPayload=%s, hasGPUMapPayload=%s, CPUDataValid=%s). %s"),
                SimulationModeToLogString(Mode),
                *GetNameSafe(Component),
                *GetNameSafe(Asset),
                *GetNameSafe(SkeletalMesh),
                LODIndex,
                Asset->HasGPURuntimeDataPayload() ? TEXT("true") : TEXT("false"),
                Asset->HasGPUMapDataPayload() ? TEXT("true") : TEXT("false"),
                bCPUDataValid ? TEXT("true") : TEXT("false"),
                *CPUDiagnostics);
        }
        else
        {
            UE_LOG(
                LogDWC,
                Warning,
                TEXT("DWC runtime mode '%s' requested on '%s' but GPU data is not fully usable. Asset='%s', mesh='%s', LOD=%d, GPURuntimeDataValid=%s, GPUMapDataValid=%s, hasGPUPayload=%s, hasGPUMapPayload=%s, CPUDataValid=%s. %s"),
                SimulationModeToLogString(Mode),
                *GetNameSafe(Component),
                *GetNameSafe(Asset),
                *GetNameSafe(SkeletalMesh),
                LODIndex,
                bGPURuntimeDataValid ? TEXT("true") : TEXT("false"),
                bGPUMapDataValid ? TEXT("true") : TEXT("false"),
                Asset->HasGPURuntimeDataPayload() ? TEXT("true") : TEXT("false"),
                Asset->HasGPUMapDataPayload() ? TEXT("true") : TEXT("false"),
                bCPUDataValid ? TEXT("true") : TEXT("false"),
                *CPUDiagnostics);
        }
    }
}

bool FWetMeshReceiverInitializer::RebuildReceivers(FWetMeshReceiverInitializerContext& Context)
{
    if (Context.WetClothingAssets == nullptr ||
        Context.Receivers == nullptr)
    {
        return false;
    }

    for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : *Context.Receivers)
    {
        if (Receiver.IsValid())
        {
            ReleaseReceiverResources(*Receiver);
        }
    }
    Context.Receivers->Reset();

    if (Context.Owner == nullptr)
    {
        return false;
    }

    TArray<USkeletalMeshComponent*> MeshComponents;
    Context.Owner->GetComponents<USkeletalMeshComponent>(MeshComponents);
    if (MeshComponents.IsEmpty())
    {
        UE_LOG(
            LogDWC,
            Warning,
            TEXT("DynamicWetClothesComponent: No SkeletalMeshComponent exists on %s."),
            *GetNameSafe(Context.Owner));
        return false;
    }

    TSet<UWetClothingAsset*> SeenAssets;
    TMap<USkeletalMeshComponent*, UWetClothingAsset*> FirstClaimByMesh;
    TArray<TPair<USkeletalMeshComponent*, UWetClothingAsset*>> OrderedClaims;
    TSet<USkeletalMeshComponent*> ConflictingMeshes;

    for (UWetClothingAsset* Asset : *Context.WetClothingAssets)
    {
        if (Asset == nullptr)
        {
            continue;
        }

        if (SeenAssets.Contains(Asset))
        {
            UE_LOG(
                LogDWC,
                Warning,
                TEXT("DynamicWetClothesComponent: Wet Clothing Asset '%s' is registered more than once on %s; duplicate entries are ignored."),
                *GetNameSafe(Asset),
                *GetNameSafe(Context.Owner));
            continue;
        }
        SeenAssets.Add(Asset);

        if (!Asset->IsCurrentAssetDataVersion())
        {
            UE_LOG(
                LogDWC,
                Error,
                TEXT("DynamicWetClothesComponent: Wet Clothing Asset '%s' uses unsupported schema version %d (current: %d). Recreate or regenerate the WCA before play."),
                *GetNameSafe(Asset),
                Asset->Metadata.AssetDataVersion,
                UWetClothingAsset::CurrentAssetDataVersion);
            continue;
        }

        USkeletalMesh* RequiredMesh = Asset->GetDWCSkeletalMesh();
        USkeletalMesh* SourceMesh = Asset->GetSourceSkeletalMesh();
        if (RequiredMesh == nullptr)
        {
            UE_LOG(
                LogDWC,
                Warning,
                TEXT("DynamicWetClothesComponent: Wet Clothing Asset '%s' has no DWC Skeletal Mesh on %s."),
                *GetNameSafe(Asset),
                *GetNameSafe(Context.Owner));
            continue;
        }

        bool bMatchedRequiredMesh = false;
        bool bFoundSourceMesh = false;
        for (USkeletalMeshComponent* MeshComponent : MeshComponents)
        {
            if (MeshComponent == nullptr)
            {
                continue;
            }

            USkeletalMesh* CurrentMesh = MeshComponent->GetSkeletalMeshAsset();
            bFoundSourceMesh |= SourceMesh != nullptr && CurrentMesh == SourceMesh;
            if (CurrentMesh != RequiredMesh)
            {
                continue;
            }

            bMatchedRequiredMesh = true;
            if (UWetClothingAsset** ExistingAsset = FirstClaimByMesh.Find(MeshComponent))
            {
                if (*ExistingAsset != Asset)
                {
                    ConflictingMeshes.Add(MeshComponent);
                    UE_LOG(
                        LogDWC,
                        Error,
                        TEXT("DynamicWetClothesComponent: Skeletal mesh component '%s' on %s is targeted by both '%s' and '%s'. Remove one conflicting WCA entry."),
                        *GetNameSafe(MeshComponent),
                        *GetNameSafe(Context.Owner),
                        *GetNameSafe(*ExistingAsset),
                        *GetNameSafe(Asset));
                }
            }
            else
            {
                FirstClaimByMesh.Add(MeshComponent, Asset);
                OrderedClaims.Emplace(MeshComponent, Asset);
            }
        }

        if (!bMatchedRequiredMesh)
        {
            if (bFoundSourceMesh && SourceMesh != RequiredMesh)
            {
                UE_LOG(
                    LogDWC,
                    Warning,
                    TEXT("DynamicWetClothesComponent: WCA '%s' requires '%s', but %s still uses source mesh '%s'. Use the Details-panel Apply action before play."),
                    *GetNameSafe(Asset),
                    *GetNameSafe(RequiredMesh),
                    *GetNameSafe(Context.Owner),
                    *GetNameSafe(SourceMesh));
            }
            else
            {
                UE_LOG(
                    LogDWC,
                    Warning,
                    TEXT("DynamicWetClothesComponent: No SkeletalMeshComponent on %s uses DWC mesh '%s' required by WCA '%s'."),
                    *GetNameSafe(Context.Owner),
                    *GetNameSafe(RequiredMesh),
                    *GetNameSafe(Asset));
            }
        }
    }

    for (const TPair<USkeletalMeshComponent*, UWetClothingAsset*>& Claim : OrderedClaims)
    {
        if (ConflictingMeshes.Contains(Claim.Key))
        {
            continue;
        }

        USkeletalMeshComponent* Mesh = Claim.Key;
        UWetClothingAsset* Asset = Claim.Value;
        if (Mesh == nullptr || Asset == nullptr)
        {
            continue;
        }

        TUniquePtr<FDWCWetMeshReceiverRuntime> Receiver = MakeUnique<FDWCWetMeshReceiverRuntime>();
        Receiver->ReceiverId = FName(*FString::Printf(
            TEXT("%s__%s"),
            *Mesh->GetFName().ToString(),
            *Asset->GetFName().ToString()));
        Receiver->MeshComponent = Mesh;
        Receiver->WetClothingAsset = Asset;
        Receiver->SimulationState = MakeUnique<FAbsorbedWetnessSimulationState>();
        Receiver->MeshSampler = MakeUnique<FWetClothingMeshSampler>();
        Receiver->RenderStage = MakeUnique<FWetRenderStage>();
        Context.Receivers->Add(MoveTemp(Receiver));
    }

    return !Context.Receivers->IsEmpty();
}

bool FWetMeshReceiverInitializer::InitializeReceiver(
    FWetMeshReceiverInitializerContext& Context,
    FDWCWetMeshReceiverRuntime& Receiver)
{
    constexpr int32 RuntimeLODIndex = UWetClothingAsset::RuntimeSimulationLODIndex;
    if (Context.Component == nullptr ||
        !Context.MakeRuntimeDataBuildArgs ||
        Receiver.MeshComponent.Get() == nullptr)
    {
        return false;
    }

    FWetRuntimeDataBuildArgs RuntimeDataBuildArgs = Context.MakeRuntimeDataBuildArgs(Receiver);
    LogRuntimeModeData(Context.Component, Receiver, Context.SimulationMode, RuntimeLODIndex);

    UDWCRuntimeDataSubsystem* RuntimeDataSubsystem = Context.World != nullptr
        ? Context.World->GetSubsystem<UDWCRuntimeDataSubsystem>()
        : nullptr;
    if (RuntimeDataSubsystem == nullptr || !Receiver.WetClothingAsset.IsValid())
    {
        UE_LOG(
            LogDWC,
            Error,
            TEXT("DynamicWetClothesComponent: Shared runtime data subsystem is unavailable on %s."),
            *GetNameSafe(Context.Owner));
        return false;
    }

    Receiver.SharedRuntimeData = RuntimeDataSubsystem->AcquireSharedRuntimeData(
        *Receiver.WetClothingAsset.Get(),
        *Receiver.MeshComponent.Get(),
        Context.Owner);
    if (!Receiver.SharedRuntimeData.IsValid())
    {
        UE_LOG(
            LogDWC,
            Error,
            TEXT("DynamicWetClothesComponent: Failed to acquire shared runtime data for WCA '%s' on %s."),
            *GetNameSafe(Receiver.WetClothingAsset.Get()),
            *GetNameSafe(Context.Owner));
        return false;
    }

    RuntimeDataBuildArgs.RuntimeData = Receiver.SharedRuntimeData.Get();
    FWetRuntimeDataBuilder::InitializeAbsorbedWetnessData(RuntimeDataBuildArgs);

    FSkeletalMeshLODRenderData* LODData = nullptr;
    if (!FWetRuntimeDataBuilder::GetLODRenderData(
            Receiver.MeshComponent.Get(),
            RuntimeLODIndex,
            LODData) ||
        LODData == nullptr)
    {
        return false;
    }

    FWetRuntimeDataBuilder::EnsureWetnessBufferSize(
        RuntimeDataBuildArgs,
        LODData->GetNumVertices());
    Receiver.SimulationState->MarkAllWetVertexColorsDirty();

    const bool bIsGPUWetnessMode = IsReceiverInitializerGPUWetnessMode(Context.SimulationMode);
    const bool bLODVertexColorTransferReady =
        Context.LODVertexColorTransferCoordinator != nullptr &&
        Context.LODVertexColorTransferCoordinator->InitializeReceiver(
            Receiver,
            *RuntimeDataSubsystem,
            RuntimeLODIndex);
    if (!bLODVertexColorTransferReady && !bIsGPUWetnessMode)
    {
        return false;
    }
    if (!bLODVertexColorTransferReady)
    {
        UE_LOG(
            LogDWC,
            Warning,
            TEXT("DynamicWetClothesComponent: GPU Wet Part debug colors cannot be transferred to render LODs for WCA '%s'. GPU wetness simulation will continue."),
            *GetNameSafe(Receiver.WetClothingAsset.Get()));
    }

    if (!bIsGPUWetnessMode)
    {
        if (!Receiver.SharedRuntimeData->bHasNeighborGraph)
        {
            UE_LOG(
                LogDWC,
                Error,
                TEXT("DynamicWetClothesComponent: CPU simulation requires a valid shared neighbor graph for WCA '%s'. Save the WCA to rebuild precomputed data."),
                *GetNameSafe(Receiver.WetClothingAsset.Get()));
            return false;
        }

        const FWetClothingPrecomputedSimulationData& PrecomputedData =
            Receiver.WetClothingAsset->GetPrecomputedSimulationData();
        Receiver.SkinningStaticData = RuntimeDataSubsystem->AcquireSkinningStaticData(
            *Receiver.MeshComponent.Get(),
            PrecomputedData.MeshSignature);
        if (!Receiver.SkinningStaticData.IsValid())
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("DynamicWetClothesComponent: Failed to build CPU skinning static data on %s."),
                *GetNameSafe(Context.Owner));
        }
    }

    const FSurfaceWaterSimulationSettings& SurfaceSimulationSettings =
        Receiver.WetClothingAsset->Authored.SurfaceWaterSettings;
    if (SurfaceSimulationSettings.bEnabled &&
        !IsReceiverInitializerGPUWetnessMode(Context.SimulationMode))
    {
        TSet<int32> SurfaceEnabledMaterialSlots;
        for (int32 VertexIndex = 0;
             VertexIndex < Receiver.SharedRuntimeData->SurfaceWaterMaterialSlotIndices.Num();
             ++VertexIndex)
        {
            if (!Receiver.SharedRuntimeData->SupportsSurfaceWater(VertexIndex))
            {
                continue;
            }
            const int32 MaterialSlotIndex =
                Receiver.SharedRuntimeData->SurfaceWaterMaterialSlotIndices[VertexIndex];
            if (MaterialSlotIndex == INDEX_NONE)
            {
                continue;
            }
            SurfaceEnabledMaterialSlots.Add(MaterialSlotIndex);

            if (const FWetnessProfileParameters* Profile =
                    Receiver.SharedRuntimeData->GetWetnessProfileParameters(VertexIndex))
            {
                const FSurfaceWaterProfileParameters& Candidate = Profile->SurfaceWater;
                FSurfaceWaterProfileParameters* Existing =
                    Receiver.SurfaceWaterProfilesByMaterialSlot.Find(MaterialSlotIndex);
                if (Existing == nullptr ||
                    Candidate.MaterialTimeUpdateInterval < Existing->MaterialTimeUpdateInterval)
                {
                    // The per-pixel presentation now comes from the render-profile LUT. This map is
                    // retained only to resolve the fastest Surface Water material update interval.
                    Receiver.SurfaceWaterProfilesByMaterialSlot.Add(MaterialSlotIndex, Candidate);
                }
            }
        }

        IDWCGPUModule* GPUModule =
            FModuleManager::Get().LoadModulePtr<IDWCGPUModule>(TEXT("DWCGPU"));
        if (GPUModule == nullptr)
        {
            UE_LOG(
                LogDWC,
                Warning,
                TEXT("DynamicWetClothesComponent: DWCGPU is unavailable, so CPU surface-water render targets cannot be created for '%s'."),
                *GetNameSafe(Context.Owner));
        }

        for (const int32 MaterialSlotIndex : SurfaceEnabledMaterialSlots)
        {
            const FSurfaceWaterMaterialSlotData* SlotData =
                SurfaceSimulationSettings.FindMaterialSlot(MaterialSlotIndex);
            if (SlotData != nullptr && !SlotData->bEnabled)
            {
                continue;
            }

            TUniquePtr<IDWCSurfaceWaterSimulationState> State;
            if (GPUModule != nullptr)
            {
                State = GPUModule->CreateSurfaceWaterSimulationState();
            }
            if (State.IsValid() && State->Initialize(
                    Context.Component,
                    SurfaceSimulationSettings.RenderTargetResolution))
            {
                Receiver.SurfaceWaterStatesByMaterialSlot.Add(
                    MaterialSlotIndex,
                    MoveTemp(State));
            }
            else
            {
                UE_LOG(
                    LogTemp,
                    Warning,
                    TEXT("DynamicWetClothesComponent: Surface Water RTs for material slot %d could not be initialized; absorbed wetness remains active."),
                    MaterialSlotIndex);
            }
        }

        uint64 EstimatedGpuMemoryBytes = 0;
        for (const TPair<int32, TUniquePtr<IDWCSurfaceWaterSimulationState>>& Pair :
             Receiver.SurfaceWaterStatesByMaterialSlot)
        {
            if (Pair.Value.IsValid())
            {
                EstimatedGpuMemoryBytes += Pair.Value->GetEstimatedGpuMemoryBytes();
            }
        }
        UE_LOG(
            LogDWC,
            Log,
            TEXT("DWC Surface Water: receiver '%s' initialized %d material-slot state(s), %d render targets, estimated RT memory %.2f MiB at %dx%d."),
            *Receiver.ReceiverId.ToString(),
            Receiver.SurfaceWaterStatesByMaterialSlot.Num(),
            Receiver.SurfaceWaterStatesByMaterialSlot.Num() * 2,
            static_cast<double>(EstimatedGpuMemoryBytes) / (1024.0 * 1024.0),
            SurfaceSimulationSettings.RenderTargetResolution,
            SurfaceSimulationSettings.RenderTargetResolution);
    }

    return true;
}
