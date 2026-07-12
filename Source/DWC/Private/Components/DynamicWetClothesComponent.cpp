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
#include "Runtime/Engine/Public/Rendering/SkeletalMeshLODRenderData.h"
#include "UObject/UnrealType.h"
#include "DataAssets/WetnessProfile.h"
#include "Utility/DWCLog.h"
#include "Utility/DWCProfiling.h"

namespace
{
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
        GetWorld()->GetTimerManager().ClearTimer(WetnessRenderTimer);
    }

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
    Args.RuntimeDataBuilder = Receiver.RuntimeDataBuilder.Get();
    Args.MeshSampler = Receiver.MeshSampler.Get();
    Args.SimulationStage = Receiver.SimulationStage.Get();
    Args.LODIndex = 0;
    Args.RequestAsyncSkinning = [this, &Receiver](const bool bComputePositions, const bool bComputeNormals)
    {
        return RequestCpuSkinningTask(Receiver, bComputePositions, bComputeNormals);
    };
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
    Args.RequestAsyncSkinning = [this, &Receiver](const bool bComputePositions, const bool bComputeNormals)
    {
        return RequestCpuSkinningTask(Receiver, bComputePositions, bComputeNormals);
    };
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
    Args.WetMaterialInstances = &Receiver.WetMaterialInstances;
    Args.CachedWetVertexColors = &Receiver.RenderStage->CachedWetVertexColors;
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
        if (Receiver->bCpuSkinningTaskRequestedAgain)
        {
            const bool bNeedsNormals = Receiver->bCpuSkinningTaskNeedsNormals;
            Receiver->bCpuSkinningTaskRequestedAgain = false;
            Receiver->bCpuSkinningTaskNeedsNormals = false;
            RequestCpuSkinningTask(*Receiver, true, bNeedsNormals);
        }

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

    bool bAnyChanged = false;
    bool bRequestedAsyncSkinning = false;
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

        FWetInputStageArgs InputArgs = MakeWetInputStageArgs(*Receiver);
        const bool bChanged = Receiver->InputStage->ApplyWetContacts(InputArgs, ReceiverContacts, bApplyMaterial);
        bRequestedAsyncSkinning |= InputArgs.bAsyncSkinningRequested;
        if (bChanged)
        {
            bAnyChanged = true;
            if (bApplyMaterial)
            {
                RequestWetRenderingUpdate(*Receiver);
            }
        }
    }

    if (!bAnyChanged && bRequestedAsyncSkinning)
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

void UDynamicWetClothesComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    FlushAsyncTaskQueueGameThread();
    FlushPendingWetContacts();
    SetComponentTickEnabled(false);
}

#if WITH_EDITOR
void UDynamicWetClothesComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);

    const FName PropertyName = PropertyChangedEvent.GetPropertyName();
    if (PropertyName == GET_MEMBER_NAME_CHECKED(UDynamicWetClothesComponent, bEnableWetPartDebugVertexColors) ||
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
