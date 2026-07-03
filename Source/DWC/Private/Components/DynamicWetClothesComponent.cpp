// Fill out your copyright notice in the Description page of Project Settings.

#include "Components/DynamicWetClothesComponent.h"

#include "Runtime/Engine/Classes/Components/SkeletalMeshComponent.h"
#include "WetInputSystem/WetInputStage.h"
#include "WetInputSystem/Sampling/WetClothingMeshSampler.h"
#include "WetRendering/WetRenderStage.h"
#include "RuntimeData/WetClothingRuntimeData.h"
#include "WetSimulation/WetSimulationStage.h"
#include "WetSimulation/AbsorbedWetness/AbsorbedWetnessSimulationState.h"
#include "Runtime/Engine/Public/Rendering/SkeletalMeshLODRenderData.h"
#include "UObject/UnrealType.h"
#include "DataAssets/WetnessProfile.h"

UDynamicWetClothesComponent::UDynamicWetClothesComponent()
{
    // Wetness simulation is timer-driven; tick is enabled only to flush batched contacts.
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;

    RuntimeData = MakeUnique<FWetClothingRuntimeData>();
    RuntimeDataBuilder = MakeUnique<FWetRuntimeDataBuilder>();
    SimulationState = MakeUnique<FAbsorbedWetnessSimulationState>();
    SimulationStage = MakeUnique<FWetSimulationStage>();
    InputStage = MakeUnique<FWetInputStage>();
    MeshSampler = MakeUnique<FWetClothingMeshSampler>();
    RenderStage = MakeUnique<FWetRenderStage>();
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

    StartWetnessTimer();
}

bool UDynamicWetClothesComponent::InitializeWetRuntime()
{
    TargetSkeletalMesh = ResolveTargetSkeletalMesh();
    if (!TargetSkeletalMesh)
    {
        UE_LOG(LogTemp, Warning, TEXT("DynamicWetClothesComponent: Target SkeletalMesh not found"));
        return false;
    }

    FWetRuntimeDataBuildArgs RuntimeDataBuildArgs = MakeRuntimeDataBuildArgs();
    RuntimeDataBuilder->InitializeAbsorbedWetnessData(RuntimeDataBuildArgs);
    RuntimeDataBuilder->InitializeWetPartVertexData(RuntimeDataBuildArgs);
    RuntimeDataBuilder->BuildBoneOptimizationCache(RuntimeDataBuildArgs, 0);
    RuntimeDataBuilder->BuildNeighborGraph(RuntimeDataBuildArgs);
    ApplyWetMaterialOverrides();

    FWetRenderStageArgs RenderArgs = MakeWetRenderStageArgs();
    RenderStage->InitializeWetMaterialInstance(RenderArgs);
    RenderStage->ApplyWetMaterialParameters(RenderArgs);

    return true;
}

void UDynamicWetClothesComponent::StartWetnessTimer()
{
    GetWorld()->GetTimerManager().SetTimer(
        WetnessUpdateTimer,
        this,
        &UDynamicWetClothesComponent::UpdateWetness,
        WetnessSettings.WetnessUpdateInterval,
        true);
}

FWetRuntimeDataBuildArgs UDynamicWetClothesComponent::MakeRuntimeDataBuildArgs()
{
    check(RuntimeData.IsValid());
    check(SimulationState.IsValid());
    check(RenderStage.IsValid());

    FWetRuntimeDataBuildArgs Args;
    Args.OwnerForLogs = GetOwner();
    Args.TargetSkeletalMesh = TargetSkeletalMesh;
    Args.WetClothingAsset = WetClothingAsset;
    Args.WetnessProfiles = &WetnessProfiles;
    Args.RuntimeData = RuntimeData.Get();
    Args.SimulationState = SimulationState.Get();
    Args.CachedWetVertexColors = &RenderStage->CachedWetVertexColors;
    Args.UnassignedWetPartDebugColor = UnassignedWetPartDebugColor;
    Args.LODIndex = 0;
    Args.bUseBakedRuntimeData = true;
    Args.bUseBakedBoneOptimizationCache = true;
    Args.bAllowRuntimeFallbackBuild = true;
    return Args;
}

FWetInputStageArgs UDynamicWetClothesComponent::MakeWetInputStageArgs()
{
    check(RuntimeData.IsValid());
    check(RuntimeDataBuilder.IsValid());
    check(SimulationState.IsValid());
    check(SimulationStage.IsValid());
    check(MeshSampler.IsValid());

    FWetInputStageArgs Args;
    Args.OwnerForLogs = GetOwner();
    Args.TargetSkeletalMesh = TargetSkeletalMesh;
    Args.WetnessSettings = &WetnessSettings;
    Args.RuntimeData = RuntimeData.Get();
    Args.SimulationState = SimulationState.Get();
    Args.RuntimeDataBuilder = RuntimeDataBuilder.Get();
    Args.MeshSampler = MeshSampler.Get();
    Args.SimulationStage = SimulationStage.Get();
    Args.LODIndex = 0;
    return Args;
}

FWetSimulationStageArgs UDynamicWetClothesComponent::MakeWetSimulationStageArgs()
{
    check(RuntimeData.IsValid());
    check(RuntimeDataBuilder.IsValid());
    check(SimulationState.IsValid());
    check(MeshSampler.IsValid());

    FWetSimulationStageArgs Args;
    Args.TargetSkeletalMesh = TargetSkeletalMesh;
    Args.WetnessSettings = &WetnessSettings;
    Args.RuntimeData = RuntimeData.Get();
    Args.SimulationState = SimulationState.Get();
    Args.RuntimeDataBuilder = RuntimeDataBuilder.Get();
    Args.MeshSampler = MeshSampler.Get();
    Args.LODIndex = 0;
    return Args;
}

FWetRenderStageArgs UDynamicWetClothesComponent::MakeWetRenderStageArgs()
{
    check(RuntimeData.IsValid());
    check(SimulationState.IsValid());
    check(RenderStage.IsValid());

    FWetRenderStageArgs Args;
    Args.TargetSkeletalMesh = TargetSkeletalMesh;
    Args.WetClothingAsset = WetClothingAsset;
    Args.WetnessSettings = &WetnessSettings;
    Args.RuntimeData = RuntimeData.Get();
    Args.SimulationState = SimulationState.Get();
    Args.WetMaterialInstances = &WetMaterialInstances;
    Args.CachedWetVertexColors = &RenderStage->CachedWetVertexColors;
    Args.UnassignedWetPartDebugColor = UnassignedWetPartDebugColor;
    Args.bEnableWetPartDebugVertexColors = bEnableWetPartDebugVertexColors;
    Args.bWetPartDebugUseWetnessMask = bWetPartDebugUseWetnessMask;
    Args.WetPartDebugStrengthParameterName = WetPartDebugStrengthParameterName;
    Args.WetPartDebugUseWetnessMaskParameterName = WetPartDebugUseWetnessMaskParameterName;
    Args.WetnessProfileMap0ParameterName = WetnessProfileMap0ParameterName;
    Args.UseWetnessProfileMap0ParameterName = UseWetnessProfileMap0ParameterName;
    Args.LODIndex = 0;
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

void UDynamicWetClothesComponent::ApplyWetMaterialOverrides()
{
    if (TargetSkeletalMesh == nullptr || WetClothingAsset == nullptr)
    {
        return;
    }

    for (const FWetClothingAssetWetMaterialOverride& MaterialOverride : WetClothingAsset->WetMaterialOverrides)
    {
        if (MaterialOverride.MaterialSlotIndex == INDEX_NONE || MaterialOverride.WetMaterial == nullptr)
        {
            continue;
        }

        if (MaterialOverride.MaterialSlotIndex >= TargetSkeletalMesh->GetNumMaterials())
        {
            UE_LOG(
                LogTemp,
                Warning,
                TEXT("DynamicWetClothesComponent: Wet material override slot %d is out of range on %s."),
                MaterialOverride.MaterialSlotIndex,
                *GetNameSafe(TargetSkeletalMesh));
            continue;
        }

        TargetSkeletalMesh->SetMaterial(MaterialOverride.MaterialSlotIndex, MaterialOverride.WetMaterial);
    }
}

void UDynamicWetClothesComponent::ApplyWetAll(const float Amount)
{
    FWetInputStageArgs InputArgs = MakeWetInputStageArgs();
    InputStage->ApplyWetAll(InputArgs, Amount);

    FWetRenderStageArgs RenderArgs = MakeWetRenderStageArgs();
    RenderStage->ApplyWetnessToMaterial(RenderArgs);
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

    FWetInputStageArgs InputArgs = MakeWetInputStageArgs();
    const bool         bChanged = InputStage->ApplyWetContact(InputArgs, Contact, bApplyMaterial);
    if (bChanged && bApplyMaterial)
    {
        FWetRenderStageArgs RenderArgs = MakeWetRenderStageArgs();
        RenderStage->ApplyWetnessToMaterial(RenderArgs);
    }
    return bChanged;
}

bool UDynamicWetClothesComponent::ApplyWetContacts(const TArray<FDWCWetContact>& Contacts, const bool bApplyMaterial)
{
    FlushPendingWetContacts();

    FWetInputStageArgs InputArgs = MakeWetInputStageArgs();
    const bool         bChanged = InputStage->ApplyWetContacts(InputArgs, Contacts, bApplyMaterial);
    if (bChanged && bApplyMaterial)
    {
        FWetRenderStageArgs RenderArgs = MakeWetRenderStageArgs();
        RenderStage->ApplyWetnessToMaterial(RenderArgs);
    }
    return bChanged;
}

bool UDynamicWetClothesComponent::ApplyWetArea(const FDWCWetAreaData& AreaData, const bool bApplyMaterial)
{
    FWetInputStageArgs InputArgs = MakeWetInputStageArgs();
    const bool         bChanged = InputStage->ApplyWetArea(InputArgs, AreaData, bApplyMaterial);
    if (bChanged && bApplyMaterial)
    {
        FWetRenderStageArgs RenderArgs = MakeWetRenderStageArgs();
        RenderStage->ApplyWetnessToMaterial(RenderArgs);
    }
    return bChanged;
}

bool UDynamicWetClothesComponent::ApplyWetSurface(
    const FDWCWaterSurfaceData& WaterSurfaceData,
    const float                 Amount,
    const bool                  bApplyMaterial)
{
    FWetInputStageArgs InputArgs = MakeWetInputStageArgs();
    const bool         bChanged = InputStage->ApplyWetSurface(InputArgs, WaterSurfaceData, Amount, bApplyMaterial);
    if (bChanged && bApplyMaterial)
    {
        FWetRenderStageArgs RenderArgs = MakeWetRenderStageArgs();
        RenderStage->ApplyWetnessToMaterial(RenderArgs);
    }
    return bChanged;
}

void UDynamicWetClothesComponent::SetWetPartDebugVertexColorsEnabled(const bool bEnabled)
{
    if (bEnableWetPartDebugVertexColors == bEnabled)
    {
        return;
    }

    bEnableWetPartDebugVertexColors = bEnabled;

    FWetRenderStageArgs RenderArgs = MakeWetRenderStageArgs();
    RenderStage->ApplyWetMaterialParameters(RenderArgs);
    RefreshWetVertexColors();
}

void UDynamicWetClothesComponent::RefreshWetVertexColors()
{
    if (!TargetSkeletalMesh)
    {
        TargetSkeletalMesh = ResolveTargetSkeletalMesh();
    }

    FWetRuntimeDataBuildArgs RuntimeDataBuildArgs = MakeRuntimeDataBuildArgs();

    FSkeletalMeshLODRenderData* LODData = nullptr;
    if (!TargetSkeletalMesh || !RuntimeDataBuilder->GetLODRenderData(TargetSkeletalMesh, 0, LODData))
    {
        return;
    }

    RuntimeDataBuilder->EnsureWetnessBufferSize(RuntimeDataBuildArgs, LODData->GetNumVertices());
    RuntimeDataBuilder->InitializeWetPartVertexData(RuntimeDataBuildArgs);
    SimulationState->MarkAllWetVertexColorsDirty();

    FWetRenderStageArgs RenderArgs = MakeWetRenderStageArgs();
    RenderStage->ApplyWetnessToMaterial(RenderArgs);
}

bool UDynamicWetClothesComponent::GetWetnessWorldBounds(FBox& OutBounds) const
{
    OutBounds = FBox(ForceInit);

    if (!TargetSkeletalMesh)
    {
        return false;
    }

    OutBounds = TargetSkeletalMesh->Bounds.GetBox();
    return OutBounds.IsValid && !OutBounds.GetExtent().IsNearlyZero();
}

int32 UDynamicWetClothesComponent::GetWetSurfaceSampleResolution() const
{
    return FMath::Max(2, WetSurfaceSampleResolution);
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

    if (!TargetSkeletalMesh && !InitializeWetRuntime())
    {
        return false;
    }

    FWetInputStageArgs InputArgs = MakeWetInputStageArgs();
    const bool         bChanged = InputStage->ApplyWetContacts(InputArgs, ContactsToApply, bApplyMaterial);
    if (bChanged && bApplyMaterial)
    {
        FWetRenderStageArgs RenderArgs = MakeWetRenderStageArgs();
        RenderStage->ApplyWetnessToMaterial(RenderArgs);
    }
    return bChanged;
}

void UDynamicWetClothesComponent::UpdateWetness()
{
    FlushPendingWetContacts();

    FWetSimulationStageArgs SimulationArgs = MakeWetSimulationStageArgs();
    const bool              bChanged = SimulationStage->UpdateWetness(SimulationArgs);
    if (bChanged)
    {
        FWetRenderStageArgs RenderArgs = MakeWetRenderStageArgs();
        RenderStage->ApplyWetnessToMaterial(RenderArgs);
    }
}

void UDynamicWetClothesComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

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
        PropertyName == GET_MEMBER_NAME_CHECKED(UDynamicWetClothesComponent, UnassignedWetPartDebugColor) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(UDynamicWetClothesComponent, WetClothingAsset))
    {
        FWetRenderStageArgs RenderArgs = MakeWetRenderStageArgs();
        RenderStage->ApplyWetMaterialParameters(RenderArgs);
        RefreshWetVertexColors();
    }
}
#endif
