// Fill out your copyright notice in the Description page of Project Settings.

#include "Components/DynamicWetClothesComponent.h"

#include "Async/DWCLODVertexColorTasks.h"
#include "Async/DWCSkinningTasks.h"
#include "Async/DWCTaskQueue.h"
#include "Runtime/Engine/Classes/Components/SkeletalMeshComponent.h"
#include "RuntimeState/Utils/DWCLODVertexColorTransferCoordinator.h"
#include "RuntimeState/Utils/WetMeshReceiverInitializer.h"
#include "RuntimeState/Utils/WetInputStage.h"
#include "RuntimeState/Utils/DWCLodCoordinator.h"
#include "WetInputSystem/Sampling/WetClothingMeshSampler.h"
#include "WetRendering/WetRenderStage.h"
#include "WetRendering/WetMaterialParameters.h"
#include "RuntimeState/WetClothingRuntimeData.h"
#include "RuntimeState/Utils/WetSimulationStage.h"
#include "WetSimulation/AbsorbedWetness/AbsorbedWetnessSimulationState.h"
#include "GPU/DWCSurfaceWaterSimulationState.h"
#include "Engine/SkeletalMesh.h"
#include "UObject/UnrealType.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Modules/ModuleManager.h"
#include "Profiling/DWCStatsSubsystem.h"
#include "TimerManager.h"
#include "Utility/DWCLog.h"
#include "Utility/DWCProfiling.h"

namespace
{
    void ReleaseSurfaceWaterStates(FDWCWetMeshReceiverRuntime& Receiver)
    {
        for (TPair<int32, TUniquePtr<IDWCSurfaceWaterSimulationState>>& Pair : Receiver.SurfaceWaterStatesByMaterialSlot)
        {
            if (Pair.Value.IsValid()) Pair.Value->Release();
        }
        Receiver.SurfaceWaterStatesByMaterialSlot.Reset();
        Receiver.SurfaceWaterProfilesByMaterialSlot.Reset();
    }

    bool IsMaterialSlotWettableForRuntime(const UWetClothingAsset* WetClothingAsset, const int32 MaterialSlotIndex)
    {
        if (WetClothingAsset == nullptr || MaterialSlotIndex == INDEX_NONE)
        {
            return false;
        }

        const FWetClothingWettableMaterialSlotState* State = WetClothingAsset->Authored.PartData.EditableWetPartData.WettableMaterialSlots.FindByPredicate(
            [MaterialSlotIndex](const FWetClothingWettableMaterialSlotState& Candidate)
            {
                return Candidate.MaterialSlotIndex == MaterialSlotIndex;
            });

        return State != nullptr && State->bIsWettableSlot;
    }

    bool IsGPUWetnessMode(const EDWCSimulationMode Mode)
    {
        return Mode == EDWCSimulationMode::WetnessMapGPU;
    }

    void ShutdownGPUBackend(FDWCWetMeshReceiverRuntime& Receiver)
    {
        if (Receiver.GPUBackend.IsValid())
        {
            Receiver.GPUBackend->Shutdown();
            Receiver.GPUBackend.Reset();
        }
    }

} // namespace

UDynamicWetClothesComponent::UDynamicWetClothesComponent()
{
    // Wetness simulation is timer-driven; tick is enabled only while asynchronous work is pending.
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
    AsyncTaskQueue = MakeUnique<FDWCTaskQueue>();
    LODCoordinator = MakeUnique<FDWCLodCoordinator>();
    LODVertexColorTransferCoordinator = MakeUnique<FDWCLODVertexColorTransferCoordinator>();
    LODCoordinator->NormalizeScreenSizeThresholds(QualityLODScreenSizeThresholds);
}

UDynamicWetClothesComponent::~UDynamicWetClothesComponent() = default;

int32 UDynamicWetClothesComponent::GetCurrentRenderLODLevel() const
{
    return LODCoordinator.IsValid() ? LODCoordinator->GetCurrentRenderLODLevel() : INDEX_NONE;
}

float UDynamicWetClothesComponent::GetMergedReceiverScreenSize() const
{
    return LODCoordinator.IsValid() ? LODCoordinator->GetMergedReceiverScreenSize() : 0.0f;
}

// Called when the game starts

void UDynamicWetClothesComponent::BeginPlay()
{
    Super::BeginPlay();

    ActiveSimulationMode = SimulationMode;
    bSimulationModeLocked = true;

    if (!InitializeWetRuntime())
    {
        return;
    }

    StartWetnessTimers();
    UpdateRenderLOD();
    RequestContinuousCpuSkinningTasks();
}

void UDynamicWetClothesComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (UWorld* World = GetWorld())
    {
        if (UDWCStatsSubsystem* StatsSubsystem = World->GetSubsystem<UDWCStatsSubsystem>())
        {
            StatsSubsystem->UnregisterComponent(this);
        }
    }

    if (AsyncTaskQueue.IsValid())
    {
        AsyncTaskQueue->Shutdown();
    }

    if (GetWorld())
    {
        GetWorld()->GetTimerManager().ClearTimer(WetnessSimulationTimer);
        GetWorld()->GetTimerManager().ClearTimer(SurfaceWaterSimulationTimer);
        GetWorld()->GetTimerManager().ClearTimer(WetnessRenderTimer);
        GetWorld()->GetTimerManager().ClearTimer(RenderLODEvaluationTimer);
    }

    // TStrongObjectPtr roots the transient RT. It must be released before PIE world GC.
    for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers)
    {
        if (Receiver.IsValid())
        {
            ShutdownGPUBackend(*Receiver);
            ReleaseSurfaceWaterStates(*Receiver);
        }
    }
    Receivers.Reset();
    bSimulationModeLocked = false;

    Super::EndPlay(EndPlayReason);
}

bool UDynamicWetClothesComponent::InitializeWetRuntime()
{
    if (LODCoordinator.IsValid())
    {
        LODCoordinator->ResetRenderLODState();
    }
    CurrentRenderLODScreenSize = 0.0f;

    FWetMeshReceiverInitializerContext InitializerContext =
        MakeWetMeshReceiverInitializerContext();
    if (!FWetMeshReceiverInitializer::RebuildReceivers(InitializerContext))
    {
        return false;
    }

    for (TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers)
    {
        if (Receiver.IsValid() &&
            !FWetMeshReceiverInitializer::InitializeReceiver(InitializerContext, *Receiver))
        {
            Receiver.Reset();
        }
    }

    Receivers.RemoveAll([](const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver)
    {
        return !Receiver.IsValid();
    });

    if (Receivers.IsEmpty())
    {
        UE_LOG(LogTemp, Error, TEXT("DynamicWetClothesComponent: No wet mesh receiver could be initialized on %s. Open the Wet Clothing Asset and save it to update precomputed simulation data."), *GetNameSafe(GetOwner()));
        return false;
    }

    ApplyGeneratedWetMaterialOverrides();

    if (IsGPUWetnessMode(GetActiveSimulationMode()))
    {
        for (TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers)
        {
            if (Receiver.IsValid() && !InitializeGPUBackend(*Receiver))
            {
                Receiver.Reset();
            }
        }

        Receivers.RemoveAll([](const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver)
        {
            return !Receiver.IsValid();
        });

        if (Receivers.IsEmpty())
        {
            UE_LOG(
                LogDWC,
                Error,
                TEXT("DynamicWetClothesComponent: No GPU wet mesh receiver could be initialized on %s. Bake GPU Simulation Maps and generate GPU materials for the Wet Clothing Asset."),
                *GetNameSafe(GetOwner()));
            return false;
        }
    }

    if (LODCoordinator.IsValid())
    {
        LODCoordinator->ConfigureQualityLOD(bEnableDWCQualityLOD, QualityLODProfile.Get());
    }
    for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers)
    {
        if (Receiver.IsValid())
        {
            if (LODCoordinator.IsValid())
            {
                LODCoordinator->SetReceiverQualityLOD(*Receiver, CurrentQualityLOD);
            }
        }
    }

    for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers)
    {
        if (!Receiver.IsValid() || !Receiver->RenderStage.IsValid())
        {
            continue;
        }

        FWetRenderStageArgs RenderArgs = MakeWetRenderStageArgs(*Receiver);
        if (!IsGPUWetnessMode(GetActiveSimulationMode()))
        {
            Receiver->RenderStage->InitializeWetMaterialInstance(RenderArgs);
        }
        Receiver->RenderStage->ApplyWetMaterialParameters(RenderArgs);
        if (Receiver->SimulationState.IsValid() && Receiver->SimulationState->DirtyWetVertexIndices.Num() > 0)
        {
            Receiver->RenderStage->ApplyWetnessToMaterial(RenderArgs);
            RequestLODVertexColorTransferTask(*Receiver);
            Receiver->bWetRenderDirty = false;
        }
    }

    UE_LOG(
        LogTemp,
        Log,
        TEXT("DynamicWetClothesComponent: Initialized %d wet mesh receiver(s) on %s."),
        Receivers.Num(),
        *GetNameSafe(GetOwner()));

    const bool bInitialized = Receivers.Num() > 0;
    if (bInitialized && HasBegunPlay())
    {
        if (UWorld* World = GetWorld())
        {
            if (UDWCStatsSubsystem* StatsSubsystem = World->GetSubsystem<UDWCStatsSubsystem>())
            {
                StatsSubsystem->RegisterComponent(this);
            }
        }
    }
    return bInitialized;
}

void UDynamicWetClothesComponent::GetResolvedWetMeshComponents(TArray<USkeletalMeshComponent*>& OutComponents) const
{
    OutComponents.Reset();
    for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers)
    {
        if (Receiver.IsValid())
        {
            if (USkeletalMeshComponent* Mesh = Receiver->MeshComponent.Get())
            {
                OutComponents.AddUnique(Mesh);
            }
        }
    }
}

void UDynamicWetClothesComponent::StartWetnessTimers()
{
    if (!GetWorld())
    {
        return;
    }

    const float SimulationInterval = FMath::Max(KINDA_SMALL_NUMBER, WetnessSettings.WetnessUpdateInterval);
    const float RenderInterval = FMath::Max(KINDA_SMALL_NUMBER, WetnessSettings.WetnessRenderUpdateInterval);
    float SurfaceWaterInterval = 1.0f / 30.0f;
    bool bFoundSurfaceProfile = false;
    for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers)
    {
        if (!Receiver.IsValid()) continue;
        for (const TPair<int32, FSurfaceWaterProfileParameters>& Pair : Receiver->SurfaceWaterProfilesByMaterialSlot)
        {
            const float CandidateInterval = FMath::Max(KINDA_SMALL_NUMBER, Pair.Value.MaterialTimeUpdateInterval);
            SurfaceWaterInterval = bFoundSurfaceProfile
                ? FMath::Min(SurfaceWaterInterval, CandidateInterval)
                : CandidateInterval;
            bFoundSurfaceProfile = true;
        }
    }

    GetWorld()->GetTimerManager().SetTimer(
        WetnessSimulationTimer,
        this,
        &UDynamicWetClothesComponent::UpdateWetness,
        SimulationInterval,
        true);

    GetWorld()->GetTimerManager().SetTimer(
        WetnessRenderTimer,
        this,
        &UDynamicWetClothesComponent::UpdateWetRendering,
        RenderInterval,
        true);
    SurfaceWaterTimerInterval = SurfaceWaterInterval;
    GetWorld()->GetTimerManager().SetTimer(SurfaceWaterSimulationTimer, this, &UDynamicWetClothesComponent::UpdateSurfaceWater, SurfaceWaterInterval, true);

    if (LODCoordinator.IsValid() && LODCoordinator->HasAnyRenderLODSettings(QualityLODScreenSizeThresholds))
    {
        GetWorld()->GetTimerManager().SetTimer(
            RenderLODEvaluationTimer,
            this,
            &UDynamicWetClothesComponent::UpdateRenderLOD,
            FMath::Max(0.01f, RenderLODEvaluationInterval),
            true);
    }
}

void UDynamicWetClothesComponent::UpdateRenderLOD()
{
    if (!LODCoordinator.IsValid())
    {
        CurrentRenderLODScreenSize = 0.0f;
        return;
    }

    int32 NewLODLevel = INDEX_NONE;
    if (!LODCoordinator->UpdateRenderLOD(
            GetWorld(),
            GetOwner(),
            Receivers,
            QualityLODScreenSizeThresholds,
            NewLODLevel))
    {
        CurrentRenderLODScreenSize = 0.0f;
        return;
    }

    CurrentRenderLODScreenSize = LODCoordinator->GetMergedReceiverScreenSize();
    if (NewLODLevel != INDEX_NONE && CurrentQualityLOD != NewLODLevel)
    {
        SetDWCQualityLOD(NewLODLevel);
    }
}

FWetMeshReceiverInitializerContext UDynamicWetClothesComponent::MakeWetMeshReceiverInitializerContext()
{
    FWetMeshReceiverInitializerContext Context;
    Context.Component = this;
    Context.Owner = GetOwner();
    Context.World = GetWorld();
    Context.WetClothingAssets = &WetClothingAssets;
    Context.Receivers = &Receivers;
    Context.SimulationMode = GetActiveSimulationMode();
    Context.LODVertexColorTransferCoordinator = LODVertexColorTransferCoordinator.Get();
    Context.MakeRuntimeDataBuildArgs = [this](FDWCWetMeshReceiverRuntime& Receiver)
    {
        return MakeRuntimeDataBuildArgs(Receiver);
    };
    return Context;
}

FWetApplicationStageContext UDynamicWetClothesComponent::MakeWetApplicationStageContext()
{
    FWetApplicationStageContext Context;
    Context.OwnerForLogs = GetOwner();
    Context.WetnessSettings = &WetnessSettings;
    Context.MaxNearestSeedVertices = GPUContactNearestSeedVertexCount;
    Context.SimulationMode = GetActiveSimulationMode();
    Context.Receivers = &Receivers;
    Context.PendingWetContacts = &PendingWetContacts;
    Context.bPendingWetContactsApplyMaterial = &bPendingWetContactsApplyMaterial;
    Context.bBatchWetContactsPerFrame = bBatchWetContactsPerFrame;
    Context.MaxBatchedWetContactsPerFrame = MaxBatchedWetContactsPerFrame;
    Context.EnsureWetRuntimeInitialized = [this]()
    {
        return InitializeWetRuntime();
    };
    Context.RequestContinuousCpuSkinningTasks = [this]()
    {
        RequestContinuousCpuSkinningTasks();
    };
    Context.SetComponentTickEnabled = [this](const bool bEnabled)
    {
        SetComponentTickEnabled(bEnabled);
    };
    Context.RequestWetRenderingUpdate = [this](FDWCWetMeshReceiverRuntime& Receiver)
    {
        RequestWetRenderingUpdate(Receiver);
    };
    return Context;
}

FWetRuntimeDataBuildArgs UDynamicWetClothesComponent::MakeRuntimeDataBuildArgs(FDWCWetMeshReceiverRuntime& Receiver)
{
    check(Receiver.SimulationState.IsValid());
    check(Receiver.RenderStage.IsValid());

    FWetRuntimeDataBuildArgs Args;
    Args.OwnerForLogs = GetOwner();
    Args.TargetSkeletalMesh = Receiver.MeshComponent.Get();
    Args.WetClothingAsset = Receiver.WetClothingAsset.Get();
    // Static runtime data is acquired from UDWCRuntimeDataSubsystem and is not rebuilt per receiver.
    Args.RuntimeData = nullptr;
    Args.SimulationState = Receiver.SimulationState.Get();
    Args.CachedWetVertexColors = &Receiver.RenderStage->CachedWetVertexColors;

    Args.bUsePrecomputedSimulationData = true;
    Args.bUsePrecomputedBoneOptimizationCache = true;
    return Args;
}

FWetSimulationStageArgs UDynamicWetClothesComponent::MakeWetSimulationStageArgs(FDWCWetMeshReceiverRuntime& Receiver)
{
    check(Receiver.SharedRuntimeData.IsValid());
    check(Receiver.SimulationState.IsValid());
    check(Receiver.MeshSampler.IsValid());

    FWetSimulationStageArgs Args;
    Args.TargetSkeletalMesh = Receiver.MeshComponent.Get();
    Args.WetnessSettings = &WetnessSettings;
    Args.RuntimeData = Receiver.SharedRuntimeData.Get();
    Args.SimulationState = Receiver.SimulationState.Get();
    Args.MeshSampler = Receiver.MeshSampler.Get();
    return Args;
}

FWetRenderStageArgs UDynamicWetClothesComponent::MakeWetRenderStageArgs(FDWCWetMeshReceiverRuntime& Receiver)
{
    check(Receiver.SharedRuntimeData.IsValid());
    check(Receiver.SimulationState.IsValid());
    check(Receiver.RenderStage.IsValid());

    FWetRenderStageArgs Args;
    Args.TargetSkeletalMesh = Receiver.MeshComponent.Get();
    Args.WetClothingAsset = Receiver.WetClothingAsset.Get();
    Args.WetnessSettings = &WetnessSettings;
    Args.RuntimeData = Receiver.SharedRuntimeData.Get();
    Args.SimulationState = Receiver.SimulationState.Get();
    Args.SurfaceWaterStatesByMaterialSlot = &Receiver.SurfaceWaterStatesByMaterialSlot;
    Args.WetMaterialInstances = &Receiver.RenderStage->WetMaterialInstances;
    Args.SurfaceWaterTimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
    Args.WrinkleStrength = WrinkleStrength;
    Args.WrinkleWetnessMin = WrinkleWetnessMin;
    Args.WrinkleWetnessMax = WrinkleWetnessMax;
    Args.bEnableWrinkle = !LODCoordinator.IsValid() || LODCoordinator->ShouldUpdateWrinkle(Receiver.QualityLODState);
    Args.TransparencyWetnessMin = TransparencyWetnessMin;
    Args.TransparencyWetnessMax = TransparencyWetnessMax;
    Args.bEnableTransparency = !LODCoordinator.IsValid() || LODCoordinator->ShouldUpdateTransparency(Receiver.QualityLODState);
    Args.UnderColor = FallbackUnderColor;
    Args.UnderColorBlendStrength = WetUnderColorBlendStrength;
    Args.bShowWetPartDebugColors =
        bShowWetPartDebugColors &&
        (IsGPUWetnessMode(GetActiveSimulationMode()) || ShouldEnableCPUWetnessRendering(Receiver));
    Args.bShowSurfaceWaterDebugColors =
        bShowSurfaceWaterDebugColors &&
        IsGPUWetnessMode(GetActiveSimulationMode());
    Args.bGPUWetnessMode = IsGPUWetnessMode(GetActiveSimulationMode());
    Args.LODIndex = UWetClothingAsset::RuntimeSimulationLODIndex;
    return Args;
}

bool UDynamicWetClothesComponent::InitializeGPUBackend(FDWCWetMeshReceiverRuntime& Receiver)
{
    ShutdownGPUBackend(Receiver);

    USkeletalMeshComponent* Mesh = Receiver.MeshComponent.Get();
    UWetClothingAsset* Asset = Receiver.WetClothingAsset.Get();
    if (Mesh == nullptr || Asset == nullptr)
    {
        return false;
    }

    IDWCGPUModule* GPUModule = FModuleManager::Get().LoadModulePtr<IDWCGPUModule>(TEXT("DWCGPU"));
    if (GPUModule == nullptr)
    {
        UE_LOG(LogDWC, Error, TEXT("DynamicWetClothesComponent: DWCGPU module is not available for GPU wetness simulation on %s."), *GetNameSafe(GetOwner()));
        return false;
    }

    TUniquePtr<IDWCGPUBackend> Backend = GPUModule->CreateBackend();
    if (!Backend.IsValid())
    {
        UE_LOG(LogDWC, Error, TEXT("DynamicWetClothesComponent: DWCGPU module could not create a GPU backend on %s."), *GetNameSafe(GetOwner()));
        return false;
    }

    FDWCGPUBackendInitArgs InitArgs;
    InitArgs.OwnerComponent = this;
    InitArgs.TargetSkeletalMesh = Mesh;
    InitArgs.WetClothingAsset = Asset;
    InitArgs.WetnessSettings = &WetnessSettings;
    InitArgs.WetMaterialInstances = &Receiver.RenderStage->WetMaterialInstances;
    InitArgs.LODIndex = UWetClothingAsset::RuntimeSimulationLODIndex;
    InitArgs.SpreadRateScale = GPUSpreadRateScale;
    InitArgs.DryRateScale = GPUDryRateScale;
    InitArgs.GravityFlowStrengthScale = GPUGravityFlowStrengthScale;
    InitArgs.bUseEightDirectionDiffusion =
        GPUDiffusionNeighborMode == EDWCGPUDiffusionNeighborMode::EightDirections;

    if (!Backend->Initialize(InitArgs))
    {
        Backend->Shutdown();
        UE_LOG(
            LogDWC,
            Error,
            TEXT("DynamicWetClothesComponent: GPU wetness backend failed to initialize on %s. Check GPU map bake data and generated GPU material bindings."),
            *GetNameSafe(GetOwner()));
        return false;
    }

    Receiver.GPUBackend = MoveTemp(Backend);
    return true;
}

void UDynamicWetClothesComponent::ApplyGeneratedWetMaterialOverrides()
{
    for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers)
    {
        if (!Receiver.IsValid())
        {
            continue;
        }

        USkeletalMeshComponent* OverrideTargetMesh = Receiver->MeshComponent.Get();
        const UWetClothingAsset* ReceiverWetClothingAsset = Receiver->WetClothingAsset.Get();
        if (OverrideTargetMesh == nullptr || ReceiverWetClothingAsset == nullptr)
        {
            continue;
        }

        for (const FWetClothingGeneratedWetMaterialOverride& MaterialOverride : ReceiverWetClothingAsset->Derived.Inline.GeneratedWetMaterialOverrides)
        {
            UMaterialInterface* WetMaterial = GetActiveSimulationMode() == EDWCSimulationMode::WetnessMapGPU
                ? static_cast<UMaterialInterface*>(MaterialOverride.GPUMaterialInstance.Get())
                : static_cast<UMaterialInterface*>(MaterialOverride.CPUMaterialInstance.Get());

            if (MaterialOverride.MaterialSlotIndex == INDEX_NONE ||
                WetMaterial == nullptr ||
                !IsMaterialSlotWettableForRuntime(ReceiverWetClothingAsset, MaterialOverride.MaterialSlotIndex))
            {
                continue;
            }

            if (MaterialOverride.MaterialSlotIndex >= OverrideTargetMesh->GetNumMaterials())
            {
                UE_LOG(
                    LogTemp,
                    Warning,
                    TEXT("DynamicWetClothesComponent: Wet material override slot %d is out of range on %s."),
                    MaterialOverride.MaterialSlotIndex,
                    *GetNameSafe(OverrideTargetMesh));
                continue;
            }

            OverrideTargetMesh->SetMaterial(MaterialOverride.MaterialSlotIndex, WetMaterial);


        }

    }
}

void UDynamicWetClothesComponent::ApplyWetAll(const float Amount)
{
    FWetApplicationStageContext Context = MakeWetApplicationStageContext();
    FWetApplicationStage::ApplyWetAll(Context, Amount);
}

bool UDynamicWetClothesComponent::ApplyWetContact(const FDWCWetContact& Contact, const bool bApplyMaterial)
{
    FWetApplicationStageContext Context = MakeWetApplicationStageContext();
    return FWetApplicationStage::ApplyWetContact(Context, Contact, bApplyMaterial);
}

bool UDynamicWetClothesComponent::ApplyWetContacts(const TArray<FDWCWetContact>& Contacts, const bool bApplyMaterial)
{
    FWetApplicationStageContext Context = MakeWetApplicationStageContext();
    return FWetApplicationStage::ApplyWetContacts(Context, Contacts, bApplyMaterial);
}

bool UDynamicWetClothesComponent::ApplyWetArea(const FDWCWetAreaData& AreaData, const bool bApplyMaterial)
{
    FWetApplicationStageContext Context = MakeWetApplicationStageContext();
    return FWetApplicationStage::ApplyWetArea(Context, AreaData, bApplyMaterial);
}

bool UDynamicWetClothesComponent::ApplyWetSurface(
    const FDWCWaterSurfaceData& WaterSurfaceData,
    const float                 Amount,
    const bool                  bApplyMaterial)
{
    FWetApplicationStageContext Context = MakeWetApplicationStageContext();
    return FWetApplicationStage::ApplyWetSurface(Context, WaterSurfaceData, Amount, bApplyMaterial);
}

bool UDynamicWetClothesComponent::GetWetnessWorldBounds(FBox& OutBounds) const
{
    OutBounds = FBox(ForceInit);

    for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers)
    {
        if (!Receiver.IsValid())
        {
            continue;
        }

        const USkeletalMeshComponent* Mesh = Receiver->MeshComponent.Get();
        if (Mesh == nullptr)
        {
            continue;
        }

        OutBounds += Mesh->Bounds.GetBox();
    }

    return OutBounds.IsValid && !OutBounds.GetExtent().IsNearlyZero();
}

int32 UDynamicWetClothesComponent::GetWetSurfaceSampleResolution() const
{
    return FMath::Max(2, WetSurfaceSampleResolution);
}

void UDynamicWetClothesComponent::CommitCpuSkinningTaskResult(FDWCSkinningTaskResult&& Result)
{
    DWC_PROFILE_SCOPE(DWC_Component_CommitCpuSkinningTaskResult);

    for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers)
    {
        if (!Receiver.IsValid() ||
            !Receiver->MeshSampler.IsValid() ||
            Receiver->ReceiverId != Result.ReceiverId)
        {
            continue;
        }

        USkeletalMeshComponent* Mesh = Receiver->MeshComponent.Get();
        if (Mesh == nullptr)
        {
            return;
        }

        if (Result.HasPositions() || Result.HasNormals())
        {
            const uint32 ProcessedVertexCount = static_cast<uint32>(FMath::Max(
                Result.SkinnedPositions.Num(),
                Result.SkinnedNormals.Num()));
            FDWCWorkloadStats::RecordCPUSkinningCompleted(ProcessedVertexCount);
            Receiver->MeshSampler->CommitSkinnedCacheFromTask(
                Mesh,
                UWetClothingAsset::RuntimeSimulationLODIndex,
                Result.FrameNumber,
                MoveTemp(Result.SkinnedPositions),
                MoveTemp(Result.SkinnedNormals));
        }

        Receiver->bCpuSkinningTaskPending = false;

        SetComponentTickEnabled(true);
        return;
    }
}

void UDynamicWetClothesComponent::CommitLODVertexColorTransferResult(FDWCLODVertexColorTransferResult&& Result)
{
    if (LODVertexColorTransferCoordinator.IsValid())
    {
        LODVertexColorTransferCoordinator->CommitTaskResult(
            *this,
            Receivers,
            AsyncTaskQueue.Get(),
            GetWorld(),
            MoveTemp(Result),
            HasPendingCpuSkinningTasks());
    }
}

void UDynamicWetClothesComponent::RequestWetRenderingUpdate()
{
    bWetRenderDirty = true;

    for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers)
    {
        if (Receiver.IsValid())
        {
            Receiver->bWetRenderDirty = true;
        }
    }
}

void UDynamicWetClothesComponent::RequestWetRenderingUpdate(FDWCWetMeshReceiverRuntime& Receiver)
{
    Receiver.bWetRenderDirty = true;
    bWetRenderDirty = true;
}

void UDynamicWetClothesComponent::ApplyQualityLODMaterialParameters(FDWCWetMeshReceiverRuntime& Receiver)
{
    if (!Receiver.RenderStage.IsValid() ||
        !Receiver.SharedRuntimeData.IsValid() ||
        !Receiver.SimulationState.IsValid())
    {
        return;
    }

    FWetRenderStageArgs RenderArgs = MakeWetRenderStageArgs(Receiver);
    Receiver.RenderStage->ApplyWetMaterialParameters(RenderArgs);
}

void UDynamicWetClothesComponent::MarkCPUWetnessRenderingDirty(FDWCWetMeshReceiverRuntime& Receiver)
{
    if (Receiver.SimulationState.IsValid())
    {
        Receiver.SimulationState->MarkAllWetVertexColorsDirty();
    }
    Receiver.bWetRenderDirty = true;
    bWetRenderDirty = true;
}

void UDynamicWetClothesComponent::RefreshResolvedQualityLODPolicies()
{
    if (!LODCoordinator.IsValid())
    {
        return;
    }

    LODCoordinator->ConfigureQualityLOD(bEnableDWCQualityLOD, QualityLODProfile.Get());

    for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers)
    {
        if (Receiver.IsValid())
        {
            LODCoordinator->RefreshReceiverQualityLODPolicy(*Receiver);
            MarkCPUWetnessRenderingDirty(*Receiver);
            ApplyQualityLODMaterialParameters(*Receiver);
        }
    }
}

bool UDynamicWetClothesComponent::ShouldUpdateSurfaceWater(FDWCWetMeshReceiverRuntime& Receiver) const
{
    if (!Receiver.WetClothingAsset.IsValid())
    {
        return false;
    }

    return !LODCoordinator.IsValid() ||
           LODCoordinator->ShouldRunSurfaceWater(Receiver.QualityLODState, SurfaceWaterTimerInterval);
}

bool UDynamicWetClothesComponent::ShouldUpdateCPUWetnessRendering(FDWCWetMeshReceiverRuntime& Receiver) const
{
    if (!Receiver.RenderStage.IsValid() || !Receiver.SimulationState.IsValid())
    {
        return false;
    }

    return !LODCoordinator.IsValid() ||
           LODCoordinator->ShouldRunCPUWetnessRendering(Receiver.QualityLODState, WetnessSettings.WetnessRenderUpdateInterval);
}

bool UDynamicWetClothesComponent::ShouldEnableCPUWetnessRendering(const FDWCWetMeshReceiverRuntime& Receiver) const
{
    return !LODCoordinator.IsValid() ||
           LODCoordinator->ShouldEnableCPUWetnessRendering(Receiver.QualityLODState);
}

void UDynamicWetClothesComponent::SetDWCQualityLOD(const int32 InQualityLOD)
{
    CurrentQualityLOD = FMath::Max(0, InQualityLOD);
    if (LODCoordinator.IsValid())
    {
        LODCoordinator->ConfigureQualityLOD(bEnableDWCQualityLOD, QualityLODProfile.Get());
    }
    for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers)
    {
        if (Receiver.IsValid())
        {
            if (LODCoordinator.IsValid())
            {
                LODCoordinator->SetReceiverQualityLOD(*Receiver, CurrentQualityLOD);
            }
            MarkCPUWetnessRenderingDirty(*Receiver);
            ApplyQualityLODMaterialParameters(*Receiver);
        }
    }
}

bool UDynamicWetClothesComponent::SetReceiverDWCQualityLOD(const FName ReceiverId, const int32 InQualityLOD)
{
    if (LODCoordinator.IsValid())
    {
        LODCoordinator->ConfigureQualityLOD(bEnableDWCQualityLOD, QualityLODProfile.Get());
    }
    for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers)
    {
        if (Receiver.IsValid() && Receiver->ReceiverId == ReceiverId)
        {
            if (LODCoordinator.IsValid())
            {
                LODCoordinator->SetReceiverQualityLOD(*Receiver, InQualityLOD);
            }
            MarkCPUWetnessRenderingDirty(*Receiver);
            ApplyQualityLODMaterialParameters(*Receiver);
            return true;
        }
    }

    return false;
}

bool UDynamicWetClothesComponent::RequestCpuSkinningTask(
    FDWCWetMeshReceiverRuntime& Receiver,
    const bool                  bComputePositions,
    const bool                  bComputeNormals)
{
    DWC_PROFILE_SCOPE(DWC_Component_RequestCpuSkinningTask);

    if (!AsyncTaskQueue.IsValid() ||
        !Receiver.MeshSampler.IsValid() ||
        (!bComputePositions && !bComputeNormals))
    {
        return false;
    }

    if (Receiver.bCpuSkinningTaskPending)
    {
        SetComponentTickEnabled(true);
        return true;
    }

    USkeletalMeshComponent* Mesh = Receiver.MeshComponent.Get();
    if (Mesh == nullptr)
    {
        return false;
    }

    FDWCSkinningTaskSnapshot Snapshot;
    if (!BuildDWCSkinningTaskSnapshot(
            Mesh,
            Receiver.ReceiverId,
            Receiver.SkinningStaticData,
            bComputePositions,
            bComputeNormals,
            Snapshot))
    {
        return false;
    }

    Receiver.bCpuSkinningTaskPending = true;

    AsyncTaskQueue->Enqueue(MakeShared<FDWCCpuSkinningTask, ESPMode::ThreadSafe>(this, MoveTemp(Snapshot)));
    SetComponentTickEnabled(true);
    return true;
}

bool UDynamicWetClothesComponent::RequestLODVertexColorTransferTask(FDWCWetMeshReceiverRuntime& Receiver)
{
    return LODVertexColorTransferCoordinator.IsValid() &&
           LODVertexColorTransferCoordinator->RequestTask(
               *this,
               AsyncTaskQueue.Get(),
               GetWorld(),
               Receiver);
}

void UDynamicWetClothesComponent::RequestContinuousCpuSkinningTasks()
{
    if (IsGPUWetnessMode(GetActiveSimulationMode()))
    {
        return;
    }

    for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers)
    {
        if (!Receiver.IsValid() ||
            Receiver->bCpuSkinningTaskPending)
        {
            continue;
        }

        RequestCpuSkinningTask(*Receiver, true, true);
    }
}

bool UDynamicWetClothesComponent::HasPendingCpuSkinningTasks() const
{
    for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers)
    {
        if (Receiver.IsValid() && Receiver->bCpuSkinningTaskPending)
        {
            return true;
        }
    }

    return false;
}

bool UDynamicWetClothesComponent::HasPendingLODVertexColorTransferTasks() const
{
    return LODVertexColorTransferCoordinator.IsValid() &&
           LODVertexColorTransferCoordinator->HasPendingTasks(Receivers);
}

void UDynamicWetClothesComponent::FlushAsyncTaskQueueGameThread()
{
    if (AsyncTaskQueue.IsValid())
    {
        AsyncTaskQueue->FlushGameThread();
    }
}

bool UDynamicWetClothesComponent::FlushPendingWetContacts()
{
    FWetApplicationStageContext Context = MakeWetApplicationStageContext();
    return FWetApplicationStage::FlushPendingWetContacts(Context);
}

void UDynamicWetClothesComponent::UpdateWetness()
{
    FlushAsyncTaskQueueGameThread();

    if (IsGPUWetnessMode(GetActiveSimulationMode()))
    {
        FlushPendingWetContacts();

        const float DeltaSeconds = FMath::Max(KINDA_SMALL_NUMBER, WetnessSettings.WetnessUpdateInterval);
        for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers)
        {
            if (!Receiver.IsValid() || !Receiver->GPUBackend.IsValid())
            {
                continue;
            }

            Receiver->GPUBackend->Update(DeltaSeconds);
        }
        return;
    }

    RequestContinuousCpuSkinningTasks();

    FlushPendingWetContacts();

    for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers)
    {
        if (!Receiver.IsValid())
        {
            continue;
        }

        FWetSimulationStageArgs SimulationArgs = MakeWetSimulationStageArgs(*Receiver);
        const int32 DirtyVertexCountBeforeUpdate = Receiver->SimulationState.IsValid()
            ? Receiver->SimulationState->DirtyWetVertexIndices.Num()
            : 0;
        const bool bChanged = FWetSimulationStage::UpdateWetness(SimulationArgs);
        FDWCWorkloadStats::RecordWetnessSimulationUpdate(bChanged);
        if (Receiver->SimulationState.IsValid())
        {
            const int32 GeneratedDirtyVertexCount = FMath::Max(
                0,
                Receiver->SimulationState->DirtyWetVertexIndices.Num() - DirtyVertexCountBeforeUpdate);
            FDWCWorkloadStats::RecordDirtyVerticesGenerated(
                static_cast<uint32>(GeneratedDirtyVertexCount));
        }
        if (bChanged)
        {
            RequestWetRenderingUpdate(*Receiver);
        }
    }
}

void UDynamicWetClothesComponent::UpdateSurfaceWater()
{
    const float CurrentSurfaceTimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
    for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers)
    {
        if (!Receiver.IsValid() || !ShouldUpdateSurfaceWater(*Receiver))
        {
            continue;
        }

        const FSurfaceWaterSimulationSettings& Settings = Receiver->WetClothingAsset->Authored.SurfaceWaterSettings;
        if (!Settings.bEnabled) continue;
        bool bAnyChanged = false;
        for (TPair<int32, TUniquePtr<IDWCSurfaceWaterSimulationState>>& Pair : Receiver->SurfaceWaterStatesByMaterialSlot)
        {
            if (!Pair.Value.IsValid()) continue;
            const FSurfaceWaterMaterialSlotData* SlotData = Settings.FindMaterialSlot(Pair.Key);
            if (SlotData != nullptr && !SlotData->bEnabled) continue;
            bAnyChanged |= Pair.Value->FlushStamps(CurrentSurfaceTimeSeconds);
        }
        if (bAnyChanged || !Receiver->SurfaceWaterStatesByMaterialSlot.IsEmpty())
        {
            FWetRenderStageArgs RenderArgs = MakeWetRenderStageArgs(*Receiver);
            Receiver->RenderStage->ApplyWetMaterialParameters(RenderArgs);
        }
    }
}

void UDynamicWetClothesComponent::UpdateWetRendering()
{
    FlushAsyncTaskQueueGameThread();

    for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers)
    {
        if (!Receiver.IsValid() ||
            !Receiver->RenderStage.IsValid() ||
            !Receiver->SimulationState.IsValid())
        {
            continue;
        }

        const bool bHadDirtyWetVertexColors = Receiver->SimulationState->DirtyWetVertexIndices.Num() > 0;
        if (!Receiver->bWetRenderDirty && Receiver->SimulationState->DirtyWetVertexIndices.Num() == 0)
        {
            continue;
        }

        FWetRenderStageArgs RenderArgs = MakeWetRenderStageArgs(*Receiver);
        if (Receiver->bWetRenderDirty)
        {
            Receiver->RenderStage->ApplyWetMaterialParameters(RenderArgs);
        }

        if (!IsGPUWetnessMode(GetActiveSimulationMode()) && !ShouldUpdateCPUWetnessRendering(*Receiver))
        {
            Receiver->bWetRenderDirty = false;
            continue;
        }

        if (bHadDirtyWetVertexColors)
        {
            Receiver->PendingLODVertexColorDirtySourceVertices = Receiver->SimulationState->DirtyWetVertexIndices;
        }

        if (IsGPUWetnessMode(GetActiveSimulationMode()))
        {
            // GPU wetness remains in the existing texture path. Vertex colors only carry
            // the static Wet Part debug color and are rebuilt when the debug view is toggled.
            if (RenderArgs.bShowWetPartDebugColors && Receiver->SimulationState->DirtyWetVertexIndices.Num() > 0)
            {
                Receiver->RenderStage->ApplyWetnessToMaterial(RenderArgs);
                if (bHadDirtyWetVertexColors)
                {
                    RequestLODVertexColorTransferTask(*Receiver);
                }
            }
        }
        else
        {
            Receiver->RenderStage->ApplyWetnessToMaterial(RenderArgs);
            if (bHadDirtyWetVertexColors)
            {
                RequestLODVertexColorTransferTask(*Receiver);
            }
        }
        Receiver->bWetRenderDirty = false;
    }

    bWetRenderDirty = false;
}


void UDynamicWetClothesComponent::SetWetPartDebugColorsEnabled(const bool bEnabled)
{
    if (bShowWetPartDebugColors == bEnabled)
    {
        return;
    }

    bShowWetPartDebugColors = bEnabled;
    for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers)
    {
        if (!Receiver.IsValid() || !Receiver->SimulationState.IsValid() || !Receiver->RenderStage.IsValid())
        {
            continue;
        }

        MarkCPUWetnessRenderingDirty(*Receiver);

        FWetRenderStageArgs RenderArgs = MakeWetRenderStageArgs(*Receiver);
        Receiver->RenderStage->ApplyWetMaterialParameters(RenderArgs);
    }
    bWetRenderDirty = true;
}

void UDynamicWetClothesComponent::SetSurfaceWaterDebugColorsEnabled(const bool bEnabled)
{
    if (bShowSurfaceWaterDebugColors == bEnabled)
    {
        return;
    }

    bShowSurfaceWaterDebugColors = bEnabled;
    for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers)
    {
        if (!Receiver.IsValid() || !Receiver->RenderStage.IsValid())
        {
            continue;
        }

        FWetRenderStageArgs RenderArgs = MakeWetRenderStageArgs(*Receiver);
        Receiver->RenderStage->ApplyWetMaterialParameters(RenderArgs);
    }
    bWetRenderDirty = true;
}

void UDynamicWetClothesComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    FlushAsyncTaskQueueGameThread();
    FlushPendingWetContacts();
    SetComponentTickEnabled(HasPendingCpuSkinningTasks() || HasPendingLODVertexColorTransferTasks());
}

#if WITH_EDITOR
void UDynamicWetClothesComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);
    if (LODCoordinator.IsValid())
    {
        LODCoordinator->NormalizeScreenSizeThresholds(QualityLODScreenSizeThresholds);
    }

    const FName PropertyName = PropertyChangedEvent.GetPropertyName();
    const bool bRequiresRuntimeRebuild =
        PropertyName == GET_MEMBER_NAME_CHECKED(UDynamicWetClothesComponent, WetClothingAssets) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(UDynamicWetClothesComponent, SimulationMode);
    const bool bRequiresMaterialRefresh =
        bRequiresRuntimeRebuild ||
        PropertyName == GET_MEMBER_NAME_CHECKED(UDynamicWetClothesComponent, bEnableDWCQualityLOD) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(UDynamicWetClothesComponent, QualityLODProfile) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(UDynamicWetClothesComponent, WrinkleStrength) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(UDynamicWetClothesComponent, WrinkleWetnessMin) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(UDynamicWetClothesComponent, WrinkleWetnessMax) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(UDynamicWetClothesComponent, TransparencyWetnessMin) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(UDynamicWetClothesComponent, TransparencyWetnessMax) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(UDynamicWetClothesComponent, FallbackUnderColor) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(UDynamicWetClothesComponent, WetUnderColorBlendStrength) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(UDynamicWetClothesComponent, bShowWetPartDebugColors) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(UDynamicWetClothesComponent, bShowSurfaceWaterDebugColors);

    if (!bRequiresMaterialRefresh)
    {
        return;
    }

    if (bRequiresRuntimeRebuild || Receivers.IsEmpty())
    {
        InitializeWetRuntime();
    }
    else if (PropertyName == GET_MEMBER_NAME_CHECKED(UDynamicWetClothesComponent, bEnableDWCQualityLOD) ||
             PropertyName == GET_MEMBER_NAME_CHECKED(UDynamicWetClothesComponent, QualityLODProfile))
    {
        RefreshResolvedQualityLODPolicies();
    }

    if (PropertyName == GET_MEMBER_NAME_CHECKED(UDynamicWetClothesComponent, bShowWetPartDebugColors))
    {
        for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers)
        {
            if (Receiver.IsValid() && Receiver->SimulationState.IsValid())
            {
                MarkCPUWetnessRenderingDirty(*Receiver);
            }
        }
    }

    for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers)
    {
        if (Receiver.IsValid() && Receiver->RenderStage.IsValid())
        {
            FWetRenderStageArgs RenderArgs = MakeWetRenderStageArgs(*Receiver);
            Receiver->RenderStage->ApplyWetMaterialParameters(RenderArgs);
        }
    }
}
#endif
