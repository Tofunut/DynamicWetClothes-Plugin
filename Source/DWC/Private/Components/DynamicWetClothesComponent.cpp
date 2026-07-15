// Fill out your copyright notice in the Description page of Project Settings.

#include "Components/DynamicWetClothesComponent.h"

#include "Async/DWCSkinningTasks.h"
#include "Async/DWCTaskQueue.h"
#include "Runtime/Engine/Classes/Components/SkeletalMeshComponent.h"
#include "WetInputSystem/WetInputStage.h"
#include "WetInputSystem/Sampling/WetClothingMeshSampler.h"
#include "WetRendering/WetRenderStage.h"
#include "WetRendering/WetMaterialParameters.h"
#include "RuntimeState/WetClothingRuntimeData.h"
#include "WetSimulation/WetSimulationStage.h"
#include "WetSimulation/AbsorbedWetness/AbsorbedWetnessSimulationState.h"
#include "WetSimulation/SurfaceWater/SurfaceWaterSimulationState.h"
#include "Runtime/Engine/Public/Rendering/SkeletalMeshLODRenderData.h"
#include "UObject/UnrealType.h"
#include "DataAssets/WetnessProfile.h"
#include "Engine/Texture2D.h"
#include "Utility/DWCLog.h"
#include "Utility/DWCProfiling.h"

namespace
{
    void ReleaseSurfaceWaterStates(FDWCWetMeshReceiverRuntime& Receiver)
    {
        for (TPair<int32, TUniquePtr<FSurfaceWaterSimulationState>>& Pair : Receiver.SurfaceWaterStatesByMaterialSlot)
        {
            if (Pair.Value.IsValid()) Pair.Value->Release();
        }
        Receiver.SurfaceWaterStatesByMaterialSlot.Reset();
        Receiver.SurfaceWaterProfilesByMaterialSlot.Reset();
    }

    bool HasEquivalentSurfacePresentation(
        const FSurfaceWaterProfileParameters& A,
        const FSurfaceWaterProfileParameters& B)
    {
        return FMath::IsNearlyEqual(A.MaterialTimeUpdateInterval, B.MaterialTimeUpdateInterval) &&
               FMath::IsNearlyEqual(A.NormalStrength, B.NormalStrength) &&
               FMath::IsNearlyEqual(A.SurfaceRoughness, B.SurfaceRoughness) &&
               FMath::IsNearlyEqual(A.FlowTiling, B.FlowTiling) &&
               FMath::IsNearlyEqual(A.FlowPanningX, B.FlowPanningX) &&
               FMath::IsNearlyEqual(A.FlowPanningY, B.FlowPanningY) &&
               FMath::IsNearlyEqual(A.FlowNormalStrength, B.FlowNormalStrength) &&
               FMath::IsNearlyEqual(A.FlowRoughness, B.FlowRoughness) &&
               FMath::IsNearlyEqual(A.FlowMaskMin, B.FlowMaskMin) &&
               FMath::IsNearlyEqual(A.FlowMaskMax, B.FlowMaskMax) &&
               A.FlowMaskTexture == B.FlowMaskTexture &&
               A.FlowNormalTexture == B.FlowNormalTexture &&
               FMath::IsNearlyEqual(A.DropletTiling, B.DropletTiling) &&
               FMath::IsNearlyEqual(A.SurfaceAmountThresholdMin, B.SurfaceAmountThresholdMin) &&
               FMath::IsNearlyEqual(A.SurfaceAmountThresholdMax, B.SurfaceAmountThresholdMax) &&
               FMath::IsNearlyEqual(A.DropletMaskMin, B.DropletMaskMin) &&
               FMath::IsNearlyEqual(A.DropletMaskMax, B.DropletMaskMax) &&
               A.DropletMaskTexture == B.DropletMaskTexture &&
               A.DropletNormalTexture == B.DropletNormalTexture;
    }

    bool IsMaterialSlotWettableForRuntime(const UWetClothingAsset* WetClothingAsset, const int32 MaterialSlotIndex)
    {
        if (WetClothingAsset == nullptr || MaterialSlotIndex == INDEX_NONE)
        {
            return false;
        }

        const FWetClothingWettableMaterialSlotState* State = WetClothingAsset->PartData.EditableWetPartData.WettableMaterialSlots.FindByPredicate(
            [MaterialSlotIndex](const FWetClothingWettableMaterialSlotState& Candidate)
            {
                return Candidate.MaterialSlotIndex == MaterialSlotIndex;
            });

        return State != nullptr && State->bIsWettableSlot;
    }

} // namespace

UDynamicWetClothesComponent::UDynamicWetClothesComponent()
{
    // Wetness simulation is timer-driven; tick is enabled only to flush batched contacts.
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;

    WetnessProfileMap0ParameterName = DWCWetMaterialParameters::WetnessProfileMap0();
    UseWetnessProfileMap0ParameterName = DWCWetMaterialParameters::UseWetnessProfileMap0();
    WrinkleNormalMapParameterName = DWCWetMaterialParameters::WrinkleNormalMap();
    UseWrinkleNormalMapParameterName = DWCWetMaterialParameters::UseWrinkleNormalMap();
    WrinkleStrengthParameterName = DWCWetMaterialParameters::WrinkleStrength();
    WrinkleWetnessMinParameterName = DWCWetMaterialParameters::WrinkleWetnessMin();
    WrinkleWetnessMaxParameterName = DWCWetMaterialParameters::WrinkleWetnessMax();
    WrinkleStrength = DWCWetMaterialParameters::DefaultWrinkleStrength();
    WrinkleWetnessMin = DWCWetMaterialParameters::DefaultWrinkleWetnessMin();
    WrinkleWetnessMax = DWCWetMaterialParameters::DefaultWrinkleWetnessMax();

    AsyncTaskQueue = MakeUnique<FDWCTaskQueue>();
}

UDynamicWetClothesComponent::~UDynamicWetClothesComponent() = default;

// Called when the game starts

void UDynamicWetClothesComponent::BeginPlay()
{
    Super::BeginPlay();

    if (!InitializeWetRuntime())
    {
        return;
    }

    StartWetnessTimers();
    RequestContinuousCpuSkinningTasks();
}

void UDynamicWetClothesComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (AsyncTaskQueue.IsValid())
    {
        AsyncTaskQueue->Shutdown();
    }

    if (GetWorld())
    {
        GetWorld()->GetTimerManager().ClearTimer(WetnessSimulationTimer);
        GetWorld()->GetTimerManager().ClearTimer(SurfaceWaterSimulationTimer);
        GetWorld()->GetTimerManager().ClearTimer(WetnessRenderTimer);
    }

    // TStrongObjectPtr roots the transient RT. It must be released before PIE world GC.
    for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers)
    {
        if (Receiver.IsValid()) ReleaseSurfaceWaterStates(*Receiver);
    }
    Receivers.Reset();
    TargetSkeletalMesh = nullptr;

    Super::EndPlay(EndPlayReason);
}

bool UDynamicWetClothesComponent::InitializeWetRuntime()
{
    if (!RebuildWetMeshReceivers())
    {
        return false;
    }

    for (TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers)
    {
        if (Receiver.IsValid() && !InitializeWetMeshReceiverRuntime(*Receiver))
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

    for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers)
    {
        if (!Receiver.IsValid() || !Receiver->RenderStage.IsValid())
        {
            continue;
        }

        FWetRenderStageArgs RenderArgs = MakeWetRenderStageArgs(*Receiver);
        Receiver->RenderStage->InitializeWetMaterialInstance(RenderArgs);
        Receiver->RenderStage->ApplyWetMaterialParameters(RenderArgs);
    }

    UE_LOG(
        LogTemp,
        Log,
        TEXT("DynamicWetClothesComponent: Initialized %d wet mesh receiver(s) on %s."),
        Receivers.Num(),
        *GetNameSafe(GetOwner()));

    return Receivers.Num() > 0;
}

bool UDynamicWetClothesComponent::RebuildWetMeshReceivers()
{
    for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers)
    {
        if (Receiver.IsValid()) ReleaseSurfaceWaterStates(*Receiver);
    }
    Receivers.Reset();
    TargetSkeletalMesh = nullptr;

    USkeletalMeshComponent* Mesh = ResolveTargetSkeletalMesh();
    if (Mesh == nullptr || Mesh->GetSkeletalMeshAsset() == nullptr)
    {
        UE_LOG(LogTemp, Warning, TEXT("DynamicWetClothesComponent: Target skeletal mesh could not be resolved on %s."), *GetNameSafe(GetOwner()));
        return false;
    }

    UWetClothingAsset* ResolvedWetClothingAsset = ResolveWetClothingAssetForMesh(*Mesh);
    if (ResolvedWetClothingAsset == nullptr)
    {
        UE_LOG(LogTemp, Warning, TEXT("DynamicWetClothesComponent: Wet Clothing Asset is missing or does not match the target mesh on %s."), *GetNameSafe(GetOwner()));
        return false;
    }

    TUniquePtr<FDWCWetMeshReceiverRuntime> Receiver = MakeUnique<FDWCWetMeshReceiverRuntime>();
    Receiver->ReceiverId = Mesh->GetFName();
    Receiver->MeshComponent = Mesh;
    Receiver->WetClothingAsset = ResolvedWetClothingAsset;
    Receiver->RuntimeData = MakeUnique<FWetClothingRuntimeData>();
    Receiver->RuntimeDataBuilder = MakeUnique<FWetRuntimeDataBuilder>();
    Receiver->SimulationState = MakeUnique<FAbsorbedWetnessSimulationState>();
    Receiver->SimulationStage = MakeUnique<FWetSimulationStage>();
    Receiver->InputStage = MakeUnique<FWetInputStage>();
    Receiver->MeshSampler = MakeUnique<FWetClothingMeshSampler>();
    Receiver->RenderStage = MakeUnique<FWetRenderStage>();

    TargetSkeletalMesh = Mesh;
    Receivers.Add(MoveTemp(Receiver));
    return true;
}

bool UDynamicWetClothesComponent::InitializeWetMeshReceiverRuntime(FDWCWetMeshReceiverRuntime& Receiver)
{
    if (Receiver.MeshComponent.Get() == nullptr)
    {
        return false;
    }

    FWetRuntimeDataBuildArgs RuntimeDataBuildArgs = MakeRuntimeDataBuildArgs(Receiver);
    Receiver.RuntimeDataBuilder->InitializeAbsorbedWetnessData(RuntimeDataBuildArgs);

    if (!Receiver.RuntimeDataBuilder->InitializeWetPartVertexData(RuntimeDataBuildArgs))
    {
        return false;
    }

    // Bone-cache failure is non-fatal. WetInputStage will perform a logged
    // full-vertex traversal fallback for contacts that cannot use the cache.
    Receiver.RuntimeDataBuilder->InitializeBoneOptimizationCacheFromPrecomputedData(RuntimeDataBuildArgs, 0);

    if (!Receiver.RuntimeDataBuilder->InitializeNeighborGraphFromPrecomputedData(RuntimeDataBuildArgs))
    {
        return false;
    }

    const FDWCTaskTargetSnapshot TargetSnapshot{Receiver.ReceiverId, 0};
    Receiver.SkinningStaticData = BuildDWCSkinningStaticData(
        Receiver.MeshComponent.Get(),
        RuntimeDataBuildArgs.LODIndex,
        TargetSnapshot);
    if (!Receiver.SkinningStaticData.IsValid())
    {
        UE_LOG(
            LogTemp,
            Warning,
            TEXT("DynamicWetClothesComponent: Failed to build CPU skinning static data on %s."),
            *GetNameSafe(GetOwner()));
    }

    const FSurfaceWaterSimulationSettings& SurfaceSimulationSettings = Receiver.WetClothingAsset->SurfaceWaterSettings;
    if (SurfaceSimulationSettings.bEnabled)
    {
        TSet<int32> SurfaceEnabledMaterialSlots;
        TSet<int32> ConflictingProfileSlots;
        for (int32 VertexIndex = 0; VertexIndex < Receiver.RuntimeData->SurfaceWaterMaterialSlotIndices.Num(); ++VertexIndex)
        {
            if (!Receiver.RuntimeData->SupportsSurfaceWater(VertexIndex)) continue;
            const int32 MaterialSlotIndex = Receiver.RuntimeData->SurfaceWaterMaterialSlotIndices[VertexIndex];
            if (MaterialSlotIndex == INDEX_NONE) continue;
            SurfaceEnabledMaterialSlots.Add(MaterialSlotIndex);

            if (Receiver.RuntimeData->VertexWetnessProfileParameters.IsValidIndex(VertexIndex))
            {
                const FSurfaceWaterProfileParameters& Candidate =
                    Receiver.RuntimeData->VertexWetnessProfileParameters[VertexIndex].SurfaceWater;
                FSurfaceWaterProfileParameters* Existing =
                    Receiver.SurfaceWaterProfilesByMaterialSlot.Find(MaterialSlotIndex);
                if (!Existing)
                {
                    Receiver.SurfaceWaterProfilesByMaterialSlot.Add(MaterialSlotIndex, Candidate);
                }
                else if (!HasEquivalentSurfacePresentation(*Existing, Candidate) &&
                         !ConflictingProfileSlots.Contains(MaterialSlotIndex))
                {
                    ConflictingProfileSlots.Add(MaterialSlotIndex);
                    UE_LOG(
                        LogDWC,
                        Warning,
                        TEXT("DWC Surface Water: material slot %d uses multiple presentation profiles. "
                             "The first deterministic profile is used for slot-wide rendering parameters; "
                             "split the material slot to render different droplet styles."),
                        MaterialSlotIndex);
                }
            }
        }

        for (const int32 MaterialSlotIndex : SurfaceEnabledMaterialSlots)
        {
            const FSurfaceWaterMaterialSlotData* SlotData = SurfaceSimulationSettings.FindMaterialSlot(MaterialSlotIndex);
            if (SlotData && !SlotData->bEnabled) continue;

            TUniquePtr<FSurfaceWaterSimulationState> State = MakeUnique<FSurfaceWaterSimulationState>();
            if (State->Initialize(this, SurfaceSimulationSettings.RenderTargetResolution))
            {
                Receiver.SurfaceWaterStatesByMaterialSlot.Add(MaterialSlotIndex, MoveTemp(State));
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("DynamicWetClothesComponent: Surface Water RTs for material slot %d could not be initialized; absorbed wetness remains active."), MaterialSlotIndex);
            }
        }
        uint64 EstimatedGpuMemoryBytes = 0;
        for (const TPair<int32, TUniquePtr<FSurfaceWaterSimulationState>>& Pair : Receiver.SurfaceWaterStatesByMaterialSlot)
        {
            if (Pair.Value.IsValid()) EstimatedGpuMemoryBytes += Pair.Value->GetEstimatedGpuMemoryBytes();
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
    GetWorld()->GetTimerManager().SetTimer(SurfaceWaterSimulationTimer, this, &UDynamicWetClothesComponent::UpdateSurfaceWater, SurfaceWaterInterval, true);
}

FWetRuntimeDataBuildArgs UDynamicWetClothesComponent::MakeRuntimeDataBuildArgs(FDWCWetMeshReceiverRuntime& Receiver)
{
    check(Receiver.RuntimeData.IsValid());
    check(Receiver.SimulationState.IsValid());
    check(Receiver.RenderStage.IsValid());

    FWetRuntimeDataBuildArgs Args;
    Args.OwnerForLogs = GetOwner();
    Args.TargetSkeletalMesh = Receiver.MeshComponent.Get();
    Args.WetClothingAsset = Receiver.WetClothingAsset.Get();
    Args.WetnessProfiles = &WetnessProfiles;
    Args.RuntimeData = Receiver.RuntimeData.Get();
    Args.SimulationState = Receiver.SimulationState.Get();
    Args.CachedWetVertexColors = &Receiver.RenderStage->CachedWetVertexColors;
    Args.UnassignedWetPartDebugColor = UnassignedWetPartDebugColor;
    Args.LODIndex = 0;

    Args.bUsePrecomputedSimulationData = true;
    Args.bUsePrecomputedBoneOptimizationCache = true;
    return Args;
}

FWetInputStageArgs UDynamicWetClothesComponent::MakeWetInputStageArgs(FDWCWetMeshReceiverRuntime& Receiver)
{
    check(Receiver.RuntimeData.IsValid());
    check(Receiver.RuntimeDataBuilder.IsValid());
    check(Receiver.SimulationState.IsValid());
    check(Receiver.SimulationStage.IsValid());
    check(Receiver.MeshSampler.IsValid());

    FWetInputStageArgs Args;
    Args.OwnerForLogs = GetOwner();
    Args.TargetSkeletalMesh = Receiver.MeshComponent.Get();
    Args.WetnessSettings = &WetnessSettings;
    Args.RuntimeData = Receiver.RuntimeData.Get();
    Args.SimulationState = Receiver.SimulationState.Get();
    Args.SurfaceWaterStatesByMaterialSlot = &Receiver.SurfaceWaterStatesByMaterialSlot;
    Args.SurfaceWaterSettings = Receiver.WetClothingAsset.IsValid() ? &Receiver.WetClothingAsset->SurfaceWaterSettings : nullptr;
    Args.SurfaceWaterRandomStream = &Receiver.InputStage->GetSurfaceWaterRandomStream();
    Args.RuntimeDataBuilder = Receiver.RuntimeDataBuilder.Get();
    Args.MeshSampler = Receiver.MeshSampler.Get();
    Args.SimulationStage = Receiver.SimulationStage.Get();
    Args.LODIndex = 0;
    return Args;
}

FWetSimulationStageArgs UDynamicWetClothesComponent::MakeWetSimulationStageArgs(FDWCWetMeshReceiverRuntime& Receiver)
{
    check(Receiver.RuntimeData.IsValid());
    check(Receiver.RuntimeDataBuilder.IsValid());
    check(Receiver.SimulationState.IsValid());
    check(Receiver.MeshSampler.IsValid());

    FWetSimulationStageArgs Args;
    Args.TargetSkeletalMesh = Receiver.MeshComponent.Get();
    Args.WetnessSettings = &WetnessSettings;
    Args.RuntimeData = Receiver.RuntimeData.Get();
    Args.SimulationState = Receiver.SimulationState.Get();
    Args.RuntimeDataBuilder = Receiver.RuntimeDataBuilder.Get();
    Args.MeshSampler = Receiver.MeshSampler.Get();
    Args.LODIndex = 0;
    return Args;
}

FWetRenderStageArgs UDynamicWetClothesComponent::MakeWetRenderStageArgs(FDWCWetMeshReceiverRuntime& Receiver)
{
    check(Receiver.RuntimeData.IsValid());
    check(Receiver.SimulationState.IsValid());
    check(Receiver.RenderStage.IsValid());

    FWetRenderStageArgs Args;
    Args.TargetSkeletalMesh = Receiver.MeshComponent.Get();
    Args.WetClothingAsset = Receiver.WetClothingAsset.Get();
    Args.WetnessSettings = &WetnessSettings;
    Args.RuntimeData = Receiver.RuntimeData.Get();
    Args.SimulationState = Receiver.SimulationState.Get();
    Args.SurfaceWaterStatesByMaterialSlot = &Receiver.SurfaceWaterStatesByMaterialSlot;
    Args.SurfaceWaterProfilesByMaterialSlot = &Receiver.SurfaceWaterProfilesByMaterialSlot;
    Args.WetMaterialInstances = &Receiver.WetMaterialInstances;
    Args.SurfaceWaterRTParameterName = SurfaceWaterRTParameterName;
    Args.SurfaceDropletRTParameterName = SurfaceDropletRTParameterName;
    Args.SurfaceFlowRTParameterName = SurfaceFlowRTParameterName;
    Args.SurfaceWaterTimeParameterName = SurfaceWaterTimeParameterName;
    Args.SurfaceWaterTexelSizeParameterName = SurfaceWaterTexelSizeParameterName;
    Args.SurfaceWaterTimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
    Args.SurfaceWaterDebugView = SurfaceWaterDebugView;
    Args.UnassignedWetPartDebugColor = UnassignedWetPartDebugColor;
    Args.bEnableWetPartDebugVertexColors = bEnableWetPartDebugVertexColors;
    Args.bWetPartDebugUseWetnessMask = bWetPartDebugUseWetnessMask;
    Args.WetPartDebugStrengthParameterName = WetPartDebugStrengthParameterName;
    Args.WetPartDebugUseWetnessMaskParameterName = WetPartDebugUseWetnessMaskParameterName;
    Args.WetnessProfileMap0ParameterName = WetnessProfileMap0ParameterName;
    Args.UseWetnessProfileMap0ParameterName = UseWetnessProfileMap0ParameterName;
    Args.WrinkleNormalMapParameterName = WrinkleNormalMapParameterName;
    Args.UseWrinkleNormalMapParameterName = UseWrinkleNormalMapParameterName;
    Args.WrinkleStrengthParameterName = WrinkleStrengthParameterName;
    Args.WrinkleWetnessMinParameterName = WrinkleWetnessMinParameterName;
    Args.WrinkleWetnessMaxParameterName = WrinkleWetnessMaxParameterName;
    Args.WrinkleStrength = WrinkleStrength;
    Args.WrinkleWetnessMin = WrinkleWetnessMin;
    Args.WrinkleWetnessMax = WrinkleWetnessMax;
    Args.bLogWrinkleRuntimeBindings = bLogWrinkleRuntimeBindings;
    Args.LODIndex = 0;

    Args.WetPartDebugUseWetnessMaskParameterName = WetPartDebugUseWetnessMaskParameterName;
    Args.WetnessProfileMap0ParameterName = WetnessProfileMap0ParameterName;
    Args.UseWetnessProfileMap0ParameterName = UseWetnessProfileMap0ParameterName;
    Args.UnderColorParameterName = UnderColorParameterName;
    Args.UnderColorBlendStrengthParameterName = UnderColorBlendStrengthParameterName;
    Args.UnderColor = FallbackUnderColor;
    Args.UnderColorBlendStrength = WetUnderColorBlendStrength;
    Args.SurfaceWaterRTParameterName = SurfaceWaterRTParameterName;
    return Args;
}

USkeletalMeshComponent* UDynamicWetClothesComponent::ResolveTargetSkeletalMesh() const
{
    AActor* Owner = GetOwner();
    if (!Owner)
        return nullptr;

    TArray<USkeletalMeshComponent*> Meshes;
    Owner->GetComponents<USkeletalMeshComponent>(Meshes);

    if (!TargetSkeletalMeshName.IsNone())
    {
        for (USkeletalMeshComponent* Mesh : Meshes)
        {
            if (Mesh && Mesh->GetFName() == TargetSkeletalMeshName)
            {
                return Mesh;
            }
        }

        return nullptr;
    }

    return Meshes.Num() > 0 ? Meshes[0] : nullptr;
}

UWetClothingAsset* UDynamicWetClothesComponent::ResolveWetClothingAssetForMesh(const USkeletalMeshComponent& MeshComponent) const
{
    USkeletalMesh* SkeletalMesh = MeshComponent.GetSkeletalMeshAsset();
    if (SkeletalMesh == nullptr || WetClothingAsset == nullptr)
    {
        return nullptr;
    }

    if (WetClothingAsset->TargetMesh != nullptr && WetClothingAsset->TargetMesh != SkeletalMesh)
    {
        return nullptr;
    }

    return WetClothingAsset;
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

        for (const FWetClothingGeneratedWetMaterialOverride& MaterialOverride : ReceiverWetClothingAsset->PartData.GeneratedWetMaterialOverrides)
        {
            if (MaterialOverride.MaterialSlotIndex == INDEX_NONE ||
                MaterialOverride.WetMaterial == nullptr ||
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

            OverrideTargetMesh->SetMaterial(MaterialOverride.MaterialSlotIndex, MaterialOverride.WetMaterial);

            if (bLogWrinkleRuntimeBindings)
            {
                UE_LOG(
                    LogDWC,
                    Log,
                    TEXT("DWC wrinkle runtime: applied generated wet material override '%s' to mesh '%s' slot %d."),
                    *GetNameSafe(MaterialOverride.WetMaterial),
                    *GetNameSafe(OverrideTargetMesh),
                    MaterialOverride.MaterialSlotIndex);
            }
        }

        for (const FWetClothingBakedTransparencyRevealLayer& RevealLayer : ReceiverWetClothingAsset->TransparencyData.BakedRevealLayers)
        {
            if (RevealLayer.MaterialSlotIndex == INDEX_NONE || RevealLayer.RevealMaterial == nullptr)
            {
                continue;
            }

            if (RevealLayer.MaterialSlotIndex >= OverrideTargetMesh->GetNumMaterials())
            {
                UE_LOG(
                    LogTemp,
                    Warning,
                    TEXT("DynamicWetClothesComponent: Transparency reveal material slot %d is out of range on %s."),
                    RevealLayer.MaterialSlotIndex,
                    *GetNameSafe(OverrideTargetMesh));
                continue;
            }

            OverrideTargetMesh->SetMaterial(RevealLayer.MaterialSlotIndex, RevealLayer.RevealMaterial);
        }

        if (bLogWrinkleRuntimeBindings)
        {
            const int32 PreferredUVChannelIndex = 0;
            for (int32 MaterialSlotIndex = 0; MaterialSlotIndex < OverrideTargetMesh->GetNumMaterials(); ++MaterialSlotIndex)
            {
                const FWetWrinkleBakedMapSet* BakedWrinkleMap =
                    ReceiverWetClothingAsset->WrinkleData.FindBakedWrinkleMap(MaterialSlotIndex, PreferredUVChannelIndex, 0);
                if (BakedWrinkleMap == nullptr || BakedWrinkleMap->BakedWrinkleNormalMap == nullptr)
                {
                    continue;
                }

                const FWetClothingGeneratedWetMaterialOverride* MatchingOverride =
                    ReceiverWetClothingAsset->PartData.GeneratedWetMaterialOverrides.FindByPredicate(
                        [MaterialSlotIndex](const FWetClothingGeneratedWetMaterialOverride& Candidate)
                        {
                            return Candidate.MaterialSlotIndex == MaterialSlotIndex &&
                                   Candidate.WetMaterial != nullptr;
                        });

                if (MatchingOverride == nullptr)
                {
                    UE_LOG(
                        LogDWC,
                        Log,
                        TEXT("DWC wrinkle runtime: mesh '%s' slot %d has baked wrinkle map '%s' (UV %d, LOD %d) but no generated wet material override entry. Runtime wrinkle apply expects the current slot material to already contain MF_DWC_ApplyWetness."),
                        *GetNameSafe(OverrideTargetMesh),
                        MaterialSlotIndex,
                        *GetNameSafe(BakedWrinkleMap->BakedWrinkleNormalMap),
                        BakedWrinkleMap->UVChannelIndex,
                        BakedWrinkleMap->LODIndex);
                }
            }
        }
    }
}

bool UDynamicWetClothesComponent::ShouldReceiverConsiderContact(
    const FDWCWetMeshReceiverRuntime& Receiver,
    const FDWCWetContact&             Contact) const
{
    const USkeletalMeshComponent* Mesh = Receiver.MeshComponent.Get();
    if (Mesh == nullptr)
    {
        return false;
    }

    const FBox ReceiverBounds = Mesh->Bounds.GetBox().ExpandBy(FMath::Max(0.0f, Contact.Radius));
    return ReceiverBounds.IsValid && ReceiverBounds.IsInsideOrOn(Contact.Location);
}

bool UDynamicWetClothesComponent::ShouldReceiverConsiderSurface(
    const FDWCWetMeshReceiverRuntime& Receiver,
    const FDWCWaterSurfaceData&       WaterSurfaceData) const
{
    const USkeletalMeshComponent* Mesh = Receiver.MeshComponent.Get();
    if (Mesh == nullptr || !WaterSurfaceData.Bounds.IsValid)
    {
        return false;
    }

    return Mesh->Bounds.GetBox().Intersect(WaterSurfaceData.Bounds);
}

void UDynamicWetClothesComponent::ApplyWetAll(const float Amount)
{
    for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers)
    {
        if (!Receiver.IsValid() || !Receiver->InputStage.IsValid())
        {
            continue;
        }

        FWetInputStageArgs InputArgs = MakeWetInputStageArgs(*Receiver);
        Receiver->InputStage->ApplyWetAll(InputArgs, Amount);
        RequestWetRenderingUpdate(*Receiver);
    }
}

bool UDynamicWetClothesComponent::ApplyWetContact(const FDWCWetContact& Contact, const bool bApplyMaterial)
{
    if (bBatchWetContactsPerFrame)
    {
        const int32 MaxQueuedContacts = FMath::Max(1, MaxBatchedWetContactsPerFrame);
        if (FMath::IsNearlyZero(Contact.Amount) || PendingWetContacts.Num() >= MaxQueuedContacts)
        {
            return false;
        }

        PendingWetContacts.Add(Contact);
        bPendingWetContactsApplyMaterial |= bApplyMaterial;
        SetComponentTickEnabled(true);
        return true;
    }

    bool bAnyChanged = false;
    for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers)
    {
        if (!Receiver.IsValid() || !Receiver->InputStage.IsValid() || !ShouldReceiverConsiderContact(*Receiver, Contact))
        {
            continue;
        }

        FWetInputStageArgs InputArgs = MakeWetInputStageArgs(*Receiver);
        const bool bChanged = Receiver->InputStage->ApplyWetContact(InputArgs, Contact, bApplyMaterial);
        if (bChanged)
        {
            bAnyChanged = true;
            if (bApplyMaterial)
            {
                RequestWetRenderingUpdate(*Receiver);
            }
        }
    }
    return bAnyChanged;
}

bool UDynamicWetClothesComponent::ApplyWetContacts(const TArray<FDWCWetContact>& Contacts, const bool bApplyMaterial)
{
    FlushPendingWetContacts();

    bool bAnyChanged = false;
    for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers)
    {
        if (!Receiver.IsValid() || !Receiver->InputStage.IsValid())
        {
            continue;
        }

        TArray<FDWCWetContact> ReceiverContacts;
        ReceiverContacts.Reserve(Contacts.Num());
        for (const FDWCWetContact& Contact : Contacts)
        {
            if (ShouldReceiverConsiderContact(*Receiver, Contact))
            {
                ReceiverContacts.Add(Contact);
            }
        }

        if (ReceiverContacts.IsEmpty())
        {
            continue;
        }

        FWetInputStageArgs InputArgs = MakeWetInputStageArgs(*Receiver);
        const bool bChanged = Receiver->InputStage->ApplyWetContacts(InputArgs, ReceiverContacts, bApplyMaterial);
        if (bChanged)
        {
            bAnyChanged = true;
            if (bApplyMaterial)
            {
                RequestWetRenderingUpdate(*Receiver);
            }
        }
    }
    return bAnyChanged;
}

bool UDynamicWetClothesComponent::ApplyWetArea(const FDWCWetAreaData& AreaData, const bool bApplyMaterial)
{
    bool bAnyChanged = false;
    for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers)
    {
        if (!Receiver.IsValid() || !Receiver->InputStage.IsValid())
        {
            continue;
        }

        FWetInputStageArgs InputArgs = MakeWetInputStageArgs(*Receiver);
        const bool bChanged = Receiver->InputStage->ApplyWetArea(InputArgs, AreaData, bApplyMaterial);
        if (bChanged)
        {
            bAnyChanged = true;
            if (bApplyMaterial)
            {
                RequestWetRenderingUpdate(*Receiver);
            }
        }
    }
    return bAnyChanged;
}

bool UDynamicWetClothesComponent::ApplyWetSurface(
    const FDWCWaterSurfaceData& WaterSurfaceData,
    const float                 Amount,
    const bool                  bApplyMaterial)
{
    bool bAnyChanged = false;
    for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers)
    {
        if (!Receiver.IsValid() || !Receiver->InputStage.IsValid() || !ShouldReceiverConsiderSurface(*Receiver, WaterSurfaceData))
        {
            continue;
        }

        FWetInputStageArgs InputArgs = MakeWetInputStageArgs(*Receiver);
        const bool bChanged = Receiver->InputStage->ApplyWetSurface(InputArgs, WaterSurfaceData, Amount, bApplyMaterial);
        if (bChanged)
        {
            bAnyChanged = true;
            if (bApplyMaterial)
            {
                RequestWetRenderingUpdate(*Receiver);
            }
        }
    }
    return bAnyChanged;
}

void UDynamicWetClothesComponent::SetWetPartDebugVertexColorsEnabled(const bool bEnabled)
{
    if (bEnableWetPartDebugVertexColors == bEnabled)
    {
        return;
    }

    bEnableWetPartDebugVertexColors = bEnabled;

    for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers)
    {
        if (!Receiver.IsValid() || !Receiver->RenderStage.IsValid())
        {
            continue;
        }

        FWetRenderStageArgs RenderArgs = MakeWetRenderStageArgs(*Receiver);
        Receiver->RenderStage->ApplyWetMaterialParameters(RenderArgs);
    }
    RefreshWetVertexColors();
}

void UDynamicWetClothesComponent::RefreshWetVertexColors()
{
    if (Receivers.IsEmpty() && !InitializeWetRuntime())
    {
        return;
    }

    for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers)
    {
        if (!Receiver.IsValid() || !Receiver->RuntimeDataBuilder.IsValid() || !Receiver->SimulationState.IsValid())
        {
            continue;
        }

        USkeletalMeshComponent* Mesh = Receiver->MeshComponent.Get();
        FWetRuntimeDataBuildArgs RuntimeDataBuildArgs = MakeRuntimeDataBuildArgs(*Receiver);

        FSkeletalMeshLODRenderData* LODData = nullptr;
        if (Mesh == nullptr || !Receiver->RuntimeDataBuilder->GetLODRenderData(Mesh, 0, LODData))
        {
            continue;
        }

        Receiver->RuntimeDataBuilder->EnsureWetnessBufferSize(RuntimeDataBuildArgs, LODData->GetNumVertices());
        if (!Receiver->RuntimeDataBuilder->InitializeWetPartVertexData(RuntimeDataBuildArgs))
        {
            continue;
        }
        Receiver->SimulationState->MarkAllWetVertexColorsDirty();

        FWetRenderStageArgs RenderArgs = MakeWetRenderStageArgs(*Receiver);
        Receiver->RenderStage->ApplyWetnessToMaterial(RenderArgs);
        Receiver->bWetRenderDirty = false;
    }

    bWetRenderDirty = false;
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
            Receiver->ReceiverId != Result.VertexTarget.Target.TargetId)
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
            Receiver->MeshSampler->CommitSkinnedCacheFromTask(
                Mesh,
                Result.VertexTarget.LODIndex,
                Result.FrameNumber,
                MoveTemp(Result.SkinnedPositions),
                MoveTemp(Result.SkinnedNormals));
        }

        Receiver->bCpuSkinningTaskPending = false;
        Receiver->bCpuSkinningTaskRequestedAgain = false;
        Receiver->bCpuSkinningTaskNeedsNormals = false;

        SetComponentTickEnabled(true);
        return;
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
        Receiver.bCpuSkinningTaskRequestedAgain = true;
        Receiver.bCpuSkinningTaskNeedsNormals |= bComputeNormals;
        SetComponentTickEnabled(true);
        return true;
    }

    USkeletalMeshComponent* Mesh = Receiver.MeshComponent.Get();
    if (Mesh == nullptr)
    {
        return false;
    }

    FDWCSkinningTaskSnapshot Snapshot;
    const FDWCTaskTargetSnapshot TargetSnapshot{Receiver.ReceiverId, 0};
    if (!BuildDWCSkinningTaskSnapshot(
            Mesh,
            0,
            TargetSnapshot,
            Receiver.SkinningStaticData,
            bComputePositions,
            bComputeNormals,
            Snapshot))
    {
        return false;
    }

    Receiver.bCpuSkinningTaskPending = true;
    Receiver.bCpuSkinningTaskRequestedAgain = false;
    Receiver.bCpuSkinningTaskNeedsNormals = bComputeNormals;

    AsyncTaskQueue->Enqueue(MakeShared<FDWCCpuSkinningTask, ESPMode::ThreadSafe>(this, MoveTemp(Snapshot)));
    SetComponentTickEnabled(true);
    return true;
}

void UDynamicWetClothesComponent::RequestContinuousCpuSkinningTasks()
{
    for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers)
    {
        if (!Receiver.IsValid() || Receiver->bCpuSkinningTaskPending)
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

void UDynamicWetClothesComponent::FlushAsyncTaskQueueGameThread()
{
    if (AsyncTaskQueue.IsValid())
    {
        AsyncTaskQueue->FlushGameThread();
    }
}

bool UDynamicWetClothesComponent::FlushPendingWetContacts()
{
    if (PendingWetContacts.IsEmpty())
    {
        bPendingWetContactsApplyMaterial = false;
        return false;
    }

    TArray<FDWCWetContact> ContactsToApply;
    ContactsToApply.Reserve(PendingWetContacts.Num());
    Swap(ContactsToApply, PendingWetContacts);

    const bool bApplyMaterial = bPendingWetContactsApplyMaterial;
    bPendingWetContactsApplyMaterial = false;

    if (Receivers.IsEmpty() && !InitializeWetRuntime())
    {
        return false;
    }
    RequestContinuousCpuSkinningTasks();

    bool bAnyChanged = false;
    bool bWaitingForSkinningCache = false;
    for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers)
    {
        if (!Receiver.IsValid() || !Receiver->InputStage.IsValid())
        {
            continue;
        }

        TArray<FDWCWetContact> ReceiverContacts;
        ReceiverContacts.Reserve(ContactsToApply.Num());
        for (const FDWCWetContact& Contact : ContactsToApply)
        {
            if (ShouldReceiverConsiderContact(*Receiver, Contact))
            {
                ReceiverContacts.Add(Contact);
            }
        }

        if (ReceiverContacts.IsEmpty())
        {
            continue;
        }

        if (!Receiver->MeshSampler.IsValid() ||
            !Receiver->SimulationState.IsValid() ||
            Receiver->MeshSampler->CachedSkinnedPositions.Num() != Receiver->SimulationState->AbsorbedWetnessPerVertex.Num())
        {
            bWaitingForSkinningCache = true;
            continue;
        }

        FWetInputStageArgs InputArgs = MakeWetInputStageArgs(*Receiver);
        const bool bChanged = Receiver->InputStage->ApplyWetContacts(InputArgs, ReceiverContacts, bApplyMaterial);
        if (bChanged)
        {
            bAnyChanged = true;
            if (bApplyMaterial)
            {
                RequestWetRenderingUpdate(*Receiver);
            }
        }
    }

    if (!bAnyChanged && bWaitingForSkinningCache)
    {
        PendingWetContacts = MoveTemp(ContactsToApply);
        bPendingWetContactsApplyMaterial |= bApplyMaterial;
        SetComponentTickEnabled(true);
    }

    return bAnyChanged;
}

void UDynamicWetClothesComponent::UpdateWetness()
{
    FlushAsyncTaskQueueGameThread();
    RequestContinuousCpuSkinningTasks();

    FlushPendingWetContacts();

    for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers)
    {
        if (!Receiver.IsValid() || !Receiver->SimulationStage.IsValid())
        {
            continue;
        }

        FWetSimulationStageArgs SimulationArgs = MakeWetSimulationStageArgs(*Receiver);
        const bool bChanged = Receiver->SimulationStage->UpdateWetness(SimulationArgs);
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
        if (!Receiver.IsValid() || !Receiver->WetClothingAsset.IsValid()) continue;
        const FSurfaceWaterSimulationSettings& Settings = Receiver->WetClothingAsset->SurfaceWaterSettings;
        if (!Settings.bEnabled) continue;
        bool bAnyChanged = false;
        for (TPair<int32, TUniquePtr<FSurfaceWaterSimulationState>>& Pair : Receiver->SurfaceWaterStatesByMaterialSlot)
        {
            if (!Pair.Value.IsValid()) continue;
            const FSurfaceWaterMaterialSlotData* SlotData = Settings.FindMaterialSlot(Pair.Key);
            const FSurfaceWaterBakedFlowMapData* FlowData = SlotData ? &SlotData->BakedFlowMap : nullptr;
            UTexture2D* FlowMap = FlowData && FlowData->bIsValid && FlowData->bEnabled && FlowData->Resolution == Pair.Value->GetResolution() ? FlowData->FlowMap.Get() : nullptr;
            bAnyChanged |= Pair.Value->FlushStamps(FlowMap, CurrentSurfaceTimeSeconds);
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
        if (!Receiver.IsValid() || !Receiver->RenderStage.IsValid() || !Receiver->SimulationState.IsValid())
        {
            continue;
        }

        if (!Receiver->bWetRenderDirty && Receiver->SimulationState->DirtyWetVertexIndices.Num() == 0)
        {
            continue;
        }

        FWetRenderStageArgs RenderArgs = MakeWetRenderStageArgs(*Receiver);
        Receiver->RenderStage->ApplyWetnessToMaterial(RenderArgs);
        Receiver->bWetRenderDirty = false;
    }

    bWetRenderDirty = false;
}

bool UDynamicWetClothesComponent::DebugApplySurfaceWaterAtVertex(
    int32 VertexIndex,
    float Amount,
    float RadiusPixels,
    bool bFlowStamp)
{
    bool bQueued = false;
    for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers)
    {
        if (!Receiver.IsValid() || !Receiver->RuntimeData.IsValid() || !Receiver->WetClothingAsset.IsValid()) continue;
        FVector2f UV;
        int32 MaterialSlotIndex = INDEX_NONE;
        if (!Receiver->RuntimeData->TryGetSurfaceWaterBinding(VertexIndex, MaterialSlotIndex, UV)) continue;
        TUniquePtr<FSurfaceWaterSimulationState>* State = Receiver->SurfaceWaterStatesByMaterialSlot.Find(MaterialSlotIndex);
        if (!State || !State->IsValid()) continue;
        const FSurfaceWaterProfileParameters* SurfaceProfile =
            Receiver->SurfaceWaterProfilesByMaterialSlot.Find(MaterialSlotIndex);
        const FSurfaceWaterProfileParameters DefaultSurfaceProfile;
        if (!SurfaceProfile) SurfaceProfile = &DefaultSurfaceProfile;
        if (bFlowStamp)
        {
            (*State)->QueueFlowStamp(
                UV,
                FMath::Max(0.0f, Amount),
                SurfaceProfile->FlowWidthPixels,
                SurfaceProfile->FlowLengthPixels,
                SurfaceProfile->FlowLifetimeSeconds);
        }
        else
        {
            (*State)->QueueDropletStamp(
                UV,
                FMath::Max(0.0f, Amount),
                RadiusPixels,
                SurfaceProfile->DropletLifetimeSeconds);
        }
        bQueued = true;
    }
    if (bQueued) UpdateSurfaceWater();
    return bQueued;
}

void UDynamicWetClothesComponent::SetSurfaceWaterDebugView(const ESurfaceWaterDebugView DebugView)
{
    SurfaceWaterDebugView = DebugView;
    for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers)
    {
        if (!Receiver.IsValid() || !Receiver->RenderStage.IsValid()) continue;
        FWetRenderStageArgs RenderArgs = MakeWetRenderStageArgs(*Receiver);
        Receiver->RenderStage->ApplyWetMaterialParameters(RenderArgs);
    }
}

int64 UDynamicWetClothesComponent::GetSurfaceWaterEstimatedGpuMemoryBytes() const
{
    uint64 TotalBytes = 0;
    for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers)
    {
        if (!Receiver.IsValid()) continue;
        for (const TPair<int32, TUniquePtr<FSurfaceWaterSimulationState>>& Pair : Receiver->SurfaceWaterStatesByMaterialSlot)
        {
            if (Pair.Value.IsValid()) TotalBytes += Pair.Value->GetEstimatedGpuMemoryBytes();
        }
    }
    return TotalBytes > static_cast<uint64>(MAX_int64) ? MAX_int64 : static_cast<int64>(TotalBytes);
}

void UDynamicWetClothesComponent::SetSurfaceWaterSimulationPaused(bool bPaused)
{
    for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers) if (Receiver.IsValid()) for (TPair<int32, TUniquePtr<FSurfaceWaterSimulationState>>& Pair : Receiver->SurfaceWaterStatesByMaterialSlot) if (Pair.Value.IsValid()) Pair.Value->SetSimulationPaused(bPaused);
}
void UDynamicWetClothesComponent::StepSurfaceWaterSimulation()
{
    SetSurfaceWaterSimulationPaused(false); UpdateSurfaceWater(); SetSurfaceWaterSimulationPaused(true);
}
void UDynamicWetClothesComponent::ClearSurfaceWater()
{
    for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers) if (Receiver.IsValid()) for (TPair<int32, TUniquePtr<FSurfaceWaterSimulationState>>& Pair : Receiver->SurfaceWaterStatesByMaterialSlot) if (Pair.Value.IsValid()) Pair.Value->Reset();
}

void UDynamicWetClothesComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    FlushAsyncTaskQueueGameThread();
    FlushPendingWetContacts();
    SetComponentTickEnabled(HasPendingCpuSkinningTasks());
}

#if WITH_EDITOR
void UDynamicWetClothesComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);

    const FName PropertyName = PropertyChangedEvent.GetPropertyName();
    if (PropertyName == GET_MEMBER_NAME_CHECKED(UDynamicWetClothesComponent, bEnableWetPartDebugVertexColors) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(UDynamicWetClothesComponent, SurfaceWaterDebugView) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(UDynamicWetClothesComponent, bWetPartDebugUseWetnessMask) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(UDynamicWetClothesComponent, WetPartDebugStrengthParameterName) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(UDynamicWetClothesComponent, WetPartDebugUseWetnessMaskParameterName) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(UDynamicWetClothesComponent, WetnessProfileMap0ParameterName) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(UDynamicWetClothesComponent, UseWetnessProfileMap0ParameterName) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(UDynamicWetClothesComponent, WrinkleStrength) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(UDynamicWetClothesComponent, WrinkleWetnessMin) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(UDynamicWetClothesComponent, WrinkleWetnessMax) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(UDynamicWetClothesComponent, WrinkleNormalMapParameterName) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(UDynamicWetClothesComponent, UseWrinkleNormalMapParameterName) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(UDynamicWetClothesComponent, WrinkleStrengthParameterName) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(UDynamicWetClothesComponent, WrinkleWetnessMinParameterName) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(UDynamicWetClothesComponent, WrinkleWetnessMaxParameterName) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(UDynamicWetClothesComponent, bLogWrinkleRuntimeBindings) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(UDynamicWetClothesComponent, FallbackUnderColor) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(UDynamicWetClothesComponent, WetUnderColorBlendStrength) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(UDynamicWetClothesComponent, UnderColorParameterName) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(UDynamicWetClothesComponent, UnderColorBlendStrengthParameterName) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(UDynamicWetClothesComponent, UnassignedWetPartDebugColor) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(UDynamicWetClothesComponent, TargetSkeletalMeshName) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(UDynamicWetClothesComponent, WetClothingAsset))
    {
        const bool bRequiresRuntimeRebuild =
            PropertyName == GET_MEMBER_NAME_CHECKED(UDynamicWetClothesComponent, TargetSkeletalMeshName) ||
            PropertyName == GET_MEMBER_NAME_CHECKED(UDynamicWetClothesComponent, WetClothingAsset);

        if (bRequiresRuntimeRebuild || Receivers.IsEmpty())
        {
            InitializeWetRuntime();
        }

        for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers)
        {
            if (!Receiver.IsValid() || !Receiver->RenderStage.IsValid())
            {
                continue;
            }

            FWetRenderStageArgs RenderArgs = MakeWetRenderStageArgs(*Receiver);
            Receiver->RenderStage->ApplyWetMaterialParameters(RenderArgs);
        }
        RefreshWetVertexColors();
    }
}
#endif
