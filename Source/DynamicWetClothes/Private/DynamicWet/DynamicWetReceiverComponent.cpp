// Fill out your copyright notice in the Description page of Project Settings.

#include "DynamicWet/DynamicWetReceiverComponent.h"

#include "Runtime/Engine/Classes/Components/SkeletalMeshComponent.h"
#include "DynamicWet/DynamicWetReceiverInputApplicator.h"
#include "DynamicWet/DynamicWetReceiverMeshSampler.h"
#include "DynamicWet/DynamicWetReceiverRenderApplier.h"
#include "DynamicWet/DynamicWetReceiverRuntimeData.h"
#include "DynamicWet/DynamicWetReceiverSimulationSolver.h"
#include "DynamicWet/DynamicWetReceiverSimulationState.h"
#include "Runtime/Engine/Public/Rendering/SkeletalMeshLODRenderData.h"
#include "UObject/UnrealType.h"
#include "WetnessProfile.h"

UDynamicWetReceiverComponent::UDynamicWetReceiverComponent()
{
    // Wetness simulation is timer-driven; tick is enabled only to flush batched contacts.
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;

    RuntimeData = MakeUnique<FDynamicWetReceiverRuntimeData>();
    RuntimeDataBuilder = MakeUnique<FDynamicWetReceiverRuntimeDataBuilder>();
    SimulationState = MakeUnique<FDynamicWetReceiverSimulationState>();
    SimulationSolver = MakeUnique<FDynamicWetReceiverSimulationSolver>();
    InputApplicator = MakeUnique<FDynamicWetReceiverInputApplicator>();
    MeshSampler = MakeUnique<FDynamicWetReceiverMeshSampler>();
    RenderApplier = MakeUnique<FDynamicWetReceiverRenderApplier>();
}

UDynamicWetReceiverComponent::~UDynamicWetReceiverComponent() = default;

// Called when the game starts

void UDynamicWetReceiverComponent::BeginPlay()
{
    Super::BeginPlay();

    if (!InitializeReceiverRuntime())
    {
        return;
    }

    StartWetnessTimer();
}

bool UDynamicWetReceiverComponent::InitializeReceiverRuntime()
{
    TargetSkeletalMesh = ResolveTargetSkeletalMesh();
    if (!TargetSkeletalMesh)
    {
        UE_LOG(LogTemp, Warning, TEXT("DynamicWetReceiverComponent: Target SkeletalMesh not found"));
        return false;
    }

    FDynamicWetReceiverContext Context = MakeContext();
    RuntimeDataBuilder->InitializeWetnessData(Context);
    RuntimeDataBuilder->InitializeWetPartVertexData(Context);
    RuntimeDataBuilder->BuildBoneOptimizationCache(Context, 0);
    RuntimeDataBuilder->BuildNeighborGraph(Context);
    RenderApplier->InitializeWetMaterialInstance(Context);
    RenderApplier->ApplyWetMaterialParameters(Context);

    return true;
}

void UDynamicWetReceiverComponent::StartWetnessTimer()
{
    GetWorld()->GetTimerManager().SetTimer(
        WetnessUpdateTimer,
        this,
        &UDynamicWetReceiverComponent::UpdateWetness,
        WetnessSettings.WetnessUpdateInterval,
        true);
}

FDynamicWetReceiverContext UDynamicWetReceiverComponent::MakeContext()
{
    check(RuntimeData.IsValid());
    check(RuntimeDataBuilder.IsValid());
    check(SimulationState.IsValid());
    check(SimulationSolver.IsValid());
    check(InputApplicator.IsValid());
    check(MeshSampler.IsValid());
    check(RenderApplier.IsValid());

    return FDynamicWetReceiverContext(
        GetOwner(),
        TargetSkeletalMesh,
        MaterialProfiles,
        WetClothingProfile,
        WetnessSettings,
        FallbackUnderColor,
        WetUnderColorBlendStrength,
        bEnableWetPartDebugVertexColors,
        bWetPartDebugUseWetnessMask,
        UnassignedWetPartDebugColor,
        WetPartDebugStrengthParameterName,
        WetPartDebugUseWetnessMaskParameterName,
        ProfileMap0ParameterName,
        UseProfileMap0ParameterName,
        WetMaterialInstances,
        *RuntimeData,
        *RuntimeDataBuilder,
        *SimulationState,
        *SimulationSolver,
        *MeshSampler,
        *RenderApplier);
}

USkeletalMeshComponent* UDynamicWetReceiverComponent::ResolveTargetSkeletalMesh() const
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

void UDynamicWetReceiverComponent::ApplyWetAll(const float Amount)
{
    FDynamicWetReceiverContext Context = MakeContext();
    InputApplicator->ApplyWetAll(Context, Amount);
}

bool UDynamicWetReceiverComponent::ApplyWetContact(const FDWCWetContact& Contact, const bool bApplyMaterial)
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

    FDynamicWetReceiverContext Context = MakeContext();
    return InputApplicator->ApplyWetContact(Context, Contact, bApplyMaterial);
}

bool UDynamicWetReceiverComponent::ApplyWetContacts(const TArray<FDWCWetContact>& Contacts, const bool bApplyMaterial)
{
    FlushPendingWetContacts();

    FDynamicWetReceiverContext Context = MakeContext();
    return InputApplicator->ApplyWetContacts(Context, Contacts, bApplyMaterial);
}

bool UDynamicWetReceiverComponent::ApplyWetArea(const FDWCWetAreaData& AreaData, const bool bApplyMaterial)
{
    FDynamicWetReceiverContext Context = MakeContext();
    return InputApplicator->ApplyWetArea(Context, AreaData, bApplyMaterial);
}

bool UDynamicWetReceiverComponent::ApplyWetSurface(
    const FDWCWetSurfaceData& SurfaceData,
    const float Amount,
    const bool bApplyMaterial)
{
    FDynamicWetReceiverContext Context = MakeContext();
    return InputApplicator->ApplyWetSurface(Context, SurfaceData, Amount, bApplyMaterial);
}

void UDynamicWetReceiverComponent::SetWetPartDebugVertexColorsEnabled(const bool bEnabled)
{
    if (bEnableWetPartDebugVertexColors == bEnabled)
    {
        return;
    }

    bEnableWetPartDebugVertexColors = bEnabled;

    FDynamicWetReceiverContext Context = MakeContext();
    RenderApplier->ApplyWetMaterialParameters(Context);
    RefreshWetVertexColors();
}

void UDynamicWetReceiverComponent::RefreshWetVertexColors()
{
    if (!TargetSkeletalMesh)
    {
        TargetSkeletalMesh = ResolveTargetSkeletalMesh();
    }

    FDynamicWetReceiverContext Context = MakeContext();

    FSkeletalMeshLODRenderData* LODData = nullptr;
    if (!TargetSkeletalMesh || !RuntimeDataBuilder->GetLODRenderData(Context, 0, LODData))
    {
        return;
    }

    RuntimeDataBuilder->EnsureWetnessBufferSize(Context, LODData->GetNumVertices());
    RuntimeDataBuilder->InitializeWetPartVertexData(Context);
    SimulationState->MarkAllWetVertexColorsDirty();
    RenderApplier->ApplyWetnessToMaterial(Context);
}

bool UDynamicWetReceiverComponent::GetWetnessWorldBounds(FBox& OutBounds) const
{
    OutBounds = FBox(ForceInit);

    if (!TargetSkeletalMesh)
    {
        return false;
    }

    OutBounds = TargetSkeletalMesh->Bounds.GetBox();
    return OutBounds.IsValid && !OutBounds.GetExtent().IsNearlyZero();
}

bool UDynamicWetReceiverComponent::FlushPendingWetContacts()
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

    if (!TargetSkeletalMesh && !InitializeReceiverRuntime())
    {
        return false;
    }

    FDynamicWetReceiverContext Context = MakeContext();
    return InputApplicator->ApplyWetContacts(Context, ContactsToApply, bApplyMaterial);
}

void UDynamicWetReceiverComponent::UpdateWetness()
{
    FlushPendingWetContacts();

    FDynamicWetReceiverContext Context = MakeContext();
    SimulationSolver->UpdateWetness(Context);
}

void UDynamicWetReceiverComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    FlushPendingWetContacts();
    SetComponentTickEnabled(false);
}

#if WITH_EDITOR
void UDynamicWetReceiverComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);

    const FName PropertyName = PropertyChangedEvent.GetPropertyName();
    if (PropertyName == GET_MEMBER_NAME_CHECKED(UDynamicWetReceiverComponent, bEnableWetPartDebugVertexColors) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(UDynamicWetReceiverComponent, bWetPartDebugUseWetnessMask) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(UDynamicWetReceiverComponent, WetPartDebugStrengthParameterName) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(UDynamicWetReceiverComponent, WetPartDebugUseWetnessMaskParameterName) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(UDynamicWetReceiverComponent, ProfileMap0ParameterName) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(UDynamicWetReceiverComponent, UseProfileMap0ParameterName) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(UDynamicWetReceiverComponent, UnassignedWetPartDebugColor) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(UDynamicWetReceiverComponent, WetClothingProfile))
    {
        FDynamicWetReceiverContext Context = MakeContext();
        RenderApplier->ApplyWetMaterialParameters(Context);
        RefreshWetVertexColors();
    }
}
#endif
