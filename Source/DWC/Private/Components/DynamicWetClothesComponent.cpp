// Fill out your copyright notice in the Description page of Project Settings.

#include "Components/DynamicWetClothesComponent.h"

#include "Async/DWCLODVertexColorTasks.h"
#include "Async/DWCSkinningTasks.h"
#include "Async/DWCTaskQueue.h"
#include "Core/DWCQualityLODController.h"
#include "Core/DWCQualityLODEvaluator.h"
#include "Runtime/Engine/Classes/Components/SkeletalMeshComponent.h"
#include "WetInputSystem/WetInputStage.h"
#include "WetInputSystem/Sampling/WetClothingMeshSampler.h"
#include "WetRendering/WetRenderStage.h"
#include "WetRendering/WetMaterialParameters.h"
#include "WetRendering/WetVertexColorBuffer.h"
#include "RuntimeState/WetClothingRuntimeData.h"
#include "RuntimeState/DWCRuntimeDataSubsystem.h"
#include "RuntimeState/DWCLODVertexColorTransferMapBuilder.h"
#include "WetSimulation/WetSimulationStage.h"
#include "WetSimulation/AbsorbedWetness/AbsorbedWetnessSimulationState.h"
#include "WetSimulation/SurfaceWater/SurfaceWaterSimulationState.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Runtime/Engine/Public/Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "SceneManagement.h"
#include "Slate/SceneViewport.h"
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

    struct FGPUSurfaceWaterAccumulator
    {
        float TotalSurfaceAmount = 0.0f;
        float BestInfluence = -1.0f;
        FVector2f BestUV = FVector2f::ZeroVector;
        FSurfaceWaterProfileParameters Profile;
        bool bHasProfile = false;
    };

    int32 GetDominantTriangleVertexIndex(
        const FDWCGPUBakedTriangle& Triangle,
        const FVector3f& Barycentric)
    {
        if (Barycentric.X >= Barycentric.Y && Barycentric.X >= Barycentric.Z)
        {
            return Triangle.VertexIndices.X;
        }
        if (Barycentric.Y >= Barycentric.Z)
        {
            return Triangle.VertexIndices.Y;
        }
        return Triangle.VertexIndices.Z;
    }

    bool QueueGPUSurfaceWaterStamps(
        FDWCWetMeshReceiverRuntime& Receiver,
        const TArray<FDWCResolvedSurfaceContact>& Contacts)
    {
        const UWetClothingAsset* Asset = Receiver.WetClothingAsset.Get();
        if (Asset == nullptr || !Receiver.SharedRuntimeData.IsValid() || !Receiver.InputStage.IsValid() ||
            !Asset->Authored.SurfaceWaterSettings.bEnabled || Receiver.SurfaceWaterStatesByMaterialSlot.IsEmpty() ||
            Contacts.IsEmpty())
        {
            return false;
        }

        const FDWCGPULODBakeData& GPUData =
            Asset->GetGPUWetMapRuntimeData(UWetClothingAsset::RuntimeSimulationLODIndex);
        TMap<int32, FGPUSurfaceWaterAccumulator> Accumulators;

        for (const FDWCResolvedSurfaceContact& Contact : Contacts)
        {
            if (Contact.Amount <= 0.0f || Contact.MaterialSlotIndex == INDEX_NONE ||
                !GPUData.Triangles.IsValidIndex(Contact.TriangleID) || Contact.ContactUV.ContainsNaN() ||
                !FMath::IsFinite(Contact.ContactUV.X) || !FMath::IsFinite(Contact.ContactUV.Y))
            {
                continue;
            }

            const FDWCGPUBakedTriangle& Triangle = GPUData.Triangles[Contact.TriangleID];
            if (Triangle.MaterialSlotIndex != Contact.MaterialSlotIndex)
            {
                continue;
            }

            const int32 ProfileVertexIndex = GetDominantTriangleVertexIndex(Triangle, Contact.Barycentric);
            const FWetnessProfileParameters* WetnessProfile =
                Receiver.SharedRuntimeData->GetWetnessProfileParameters(ProfileVertexIndex);
            if (WetnessProfile == nullptr ||
                !Receiver.SharedRuntimeData->SupportsSurfaceWater(ProfileVertexIndex))
            {
                continue;
            }

            const FSurfaceWaterProfileParameters& SurfaceProfile = WetnessProfile->SurfaceWater;
            const float SurfaceAmount = Contact.Amount *
                FMath::Clamp(Contact.TriangleInfluence, 0.0f, 1.0f) *
                WetnessProfile->GetRejectedWaterFraction() *
                FMath::Clamp(SurfaceProfile.SurfaceRepresentationFraction, 0.0f, 1.0f);
            if (SurfaceAmount <= 0.0f)
            {
                continue;
            }

            FGPUSurfaceWaterAccumulator& Accumulator = Accumulators.FindOrAdd(Contact.MaterialSlotIndex);
            Accumulator.TotalSurfaceAmount += SurfaceAmount;
            if (Contact.TriangleInfluence > Accumulator.BestInfluence)
            {
                Accumulator.BestInfluence = Contact.TriangleInfluence;
                Accumulator.BestUV = Contact.ContactUV;
                Accumulator.Profile = SurfaceProfile;
                Accumulator.bHasProfile = true;
            }
        }

        FRandomStream& RandomStream = Receiver.InputStage->GetSurfaceWaterRandomStream();
        bool bAnyQueued = false;
        for (const TPair<int32, FGPUSurfaceWaterAccumulator>& Pair : Accumulators)
        {
            const FGPUSurfaceWaterAccumulator& Accumulator = Pair.Value;
            TUniquePtr<FSurfaceWaterSimulationState>* State =
                Receiver.SurfaceWaterStatesByMaterialSlot.Find(Pair.Key);
            if (!Accumulator.bHasProfile || State == nullptr || !State->IsValid())
            {
                continue;
            }

            const FSurfaceWaterProfileParameters& Surface = Accumulator.Profile;
            if (RandomStream.FRand() < FMath::Clamp(Surface.DropletSpawnProbability, 0.0f, 1.0f))
            {
                (*State)->QueueDropletStamp(
                    Accumulator.BestUV,
                    Accumulator.TotalSurfaceAmount * FMath::Max(0.0f, Surface.DropletIntensityMultiplier),
                    Surface.DropletRadiusPixels,
                    Surface.DropletLifetimeSeconds);
                bAnyQueued = true;
            }

            if (Accumulator.TotalSurfaceAmount >= FMath::Max(0.0f, Surface.MinimumFlowSurfaceAmount) &&
                RandomStream.FRand() < FMath::Clamp(Surface.FlowSpawnProbability, 0.0f, 1.0f))
            {
                (*State)->QueueFlowStamp(
                    Accumulator.BestUV,
                    Accumulator.TotalSurfaceAmount * FMath::Max(0.0f, Surface.FlowIntensityMultiplier),
                    Surface.FlowWidthPixels,
                    Surface.FlowLengthPixels,
                    Surface.FlowLifetimeSeconds);
                bAnyQueued = true;
            }
        }

        return bAnyQueued;
    }

    void ShutdownGPUBackend(FDWCWetMeshReceiverRuntime& Receiver)
    {
        if (Receiver.GPUBackend.IsValid())
        {
            Receiver.GPUBackend->Shutdown();
            Receiver.GPUBackend.Reset();
        }
    }

    void LogRuntimeModeData(
        const UDynamicWetClothesComponent* Component,
        const FDWCWetMeshReceiverRuntime& Receiver,
        const EDWCSimulationMode          Mode,
        const int32                       LODIndex)
    {
        const USkeletalMeshComponent* MeshComponent = Receiver.MeshComponent.Get();
        const USkeletalMesh* SkeletalMesh = MeshComponent != nullptr ? MeshComponent->GetSkeletalMeshAsset() : nullptr;
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

} // namespace

UDynamicWetClothesComponent::UDynamicWetClothesComponent()
{
    // Wetness simulation is timer-driven; tick is enabled only while asynchronous work is pending.
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.bStartWithTickEnabled = false;
    AsyncTaskQueue = MakeUnique<FDWCTaskQueue>();
    QualityLODController = MakeUnique<FDWCQualityLODController>();
    QualityLODEvaluator = MakeUnique<FDWCQualityLODEvaluator>();
    QualityLODEvaluator->NormalizeScreenSizeThresholds(QualityLODScreenSizeThresholds);
}

UDynamicWetClothesComponent::~UDynamicWetClothesComponent() = default;

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
    ResetRenderLODState();

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

    ConfigureQualityLODController();
    for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers)
    {
        if (Receiver.IsValid())
        {
            SetReceiverQualityLOD(*Receiver, CurrentQualityLOD);
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

bool UDynamicWetClothesComponent::RebuildWetMeshReceivers()
{
    for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers)
    {
        if (Receiver.IsValid())
        {
            ShutdownGPUBackend(*Receiver);
            ReleaseSurfaceWaterStates(*Receiver);
        }
    }
    Receivers.Reset();

    AActor* Owner = GetOwner();
    if (Owner == nullptr)
    {
        return false;
    }

    TArray<USkeletalMeshComponent*> MeshComponents;
    Owner->GetComponents<USkeletalMeshComponent>(MeshComponents);
    if (MeshComponents.IsEmpty())
    {
        UE_LOG(LogDWC, Warning, TEXT("DynamicWetClothesComponent: No SkeletalMeshComponent exists on %s."), *GetNameSafe(Owner));
        return false;
    }

    TSet<UWetClothingAsset*> SeenAssets;
    TMap<USkeletalMeshComponent*, UWetClothingAsset*> FirstClaimByMesh;
    TArray<TPair<USkeletalMeshComponent*, UWetClothingAsset*>> OrderedClaims;
    TSet<USkeletalMeshComponent*> ConflictingMeshes;

    for (UWetClothingAsset* Asset : WetClothingAssets)
    {
        if (Asset == nullptr)
        {
            continue;
        }

        if (SeenAssets.Contains(Asset))
        {
            UE_LOG(LogDWC, Warning, TEXT("DynamicWetClothesComponent: Wet Clothing Asset '%s' is registered more than once on %s; duplicate entries are ignored."), *GetNameSafe(Asset), *GetNameSafe(Owner));
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
            UE_LOG(LogDWC, Warning, TEXT("DynamicWetClothesComponent: Wet Clothing Asset '%s' has no DWC Skeletal Mesh on %s."), *GetNameSafe(Asset), *GetNameSafe(Owner));
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
                    UE_LOG(LogDWC, Error, TEXT("DynamicWetClothesComponent: Skeletal mesh component '%s' on %s is targeted by both '%s' and '%s'. Remove one conflicting WCA entry."), *GetNameSafe(MeshComponent), *GetNameSafe(Owner), *GetNameSafe(*ExistingAsset), *GetNameSafe(Asset));
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
                UE_LOG(LogDWC, Warning, TEXT("DynamicWetClothesComponent: WCA '%s' requires '%s', but %s still uses source mesh '%s'. Use the Details-panel Apply action before play."), *GetNameSafe(Asset), *GetNameSafe(RequiredMesh), *GetNameSafe(Owner), *GetNameSafe(SourceMesh));
            }
            else
            {
                UE_LOG(LogDWC, Warning, TEXT("DynamicWetClothesComponent: No SkeletalMeshComponent on %s uses DWC mesh '%s' required by WCA '%s'."), *GetNameSafe(Owner), *GetNameSafe(RequiredMesh), *GetNameSafe(Asset));
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
        Receiver->ReceiverId = FName(*FString::Printf(TEXT("%s__%s"), *Mesh->GetFName().ToString(), *Asset->GetFName().ToString()));
        Receiver->MeshComponent = Mesh;
        Receiver->WetClothingAsset = Asset;
        Receiver->RuntimeDataBuilder = MakeUnique<FWetRuntimeDataBuilder>();
        Receiver->SimulationState = MakeUnique<FAbsorbedWetnessSimulationState>();
        Receiver->SimulationStage = MakeUnique<FWetSimulationStage>();
        Receiver->InputStage = MakeUnique<FWetInputStage>();
        Receiver->SurfaceContactResolver = MakeUnique<FWetSurfaceContactResolver>();
        Receiver->MeshSampler = MakeUnique<FWetClothingMeshSampler>();
        Receiver->RenderStage = MakeUnique<FWetRenderStage>();
        Receivers.Add(MoveTemp(Receiver));
    }

    return !Receivers.IsEmpty();
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

bool UDynamicWetClothesComponent::InitializeWetMeshReceiverRuntime(FDWCWetMeshReceiverRuntime& Receiver)
{
    constexpr int32 RuntimeLODIndex = UWetClothingAsset::RuntimeSimulationLODIndex;
    if (Receiver.MeshComponent.Get() == nullptr)
    {
        return false;
    }

    FWetRuntimeDataBuildArgs RuntimeDataBuildArgs = MakeRuntimeDataBuildArgs(Receiver);
    LogRuntimeModeData(this, Receiver, GetActiveSimulationMode(), RuntimeLODIndex);

    UWorld* World = GetWorld();
    UDWCRuntimeDataSubsystem* RuntimeDataSubsystem =
        World != nullptr ? World->GetSubsystem<UDWCRuntimeDataSubsystem>() : nullptr;
    if (RuntimeDataSubsystem == nullptr || !Receiver.WetClothingAsset.IsValid())
    {
        UE_LOG(LogDWC, Error, TEXT("DynamicWetClothesComponent: Shared runtime data subsystem is unavailable on %s."), *GetNameSafe(GetOwner()));
        return false;
    }

    Receiver.SharedRuntimeData = RuntimeDataSubsystem->AcquireSharedRuntimeData(
        *Receiver.WetClothingAsset.Get(),
        *Receiver.MeshComponent.Get(),
        GetOwner());
    if (!Receiver.SharedRuntimeData.IsValid())
    {
        UE_LOG(
            LogDWC,
            Error,
            TEXT("DynamicWetClothesComponent: Failed to acquire shared runtime data for WCA '%s' on %s."),
            *GetNameSafe(Receiver.WetClothingAsset.Get()),
            *GetNameSafe(GetOwner()));
        return false;
    }

    RuntimeDataBuildArgs.RuntimeData = Receiver.SharedRuntimeData.Get();
    Receiver.RuntimeDataBuilder->InitializeAbsorbedWetnessData(RuntimeDataBuildArgs);

    FSkeletalMeshLODRenderData* LODData = nullptr;
    if (!Receiver.RuntimeDataBuilder->GetLODRenderData(
            Receiver.MeshComponent.Get(),
            RuntimeLODIndex,
            LODData) ||
        LODData == nullptr)
    {
        return false;
    }

    Receiver.RuntimeDataBuilder->EnsureWetnessBufferSize(RuntimeDataBuildArgs, LODData->GetNumVertices());
    Receiver.SimulationState->MarkAllWetVertexColorsDirty();

    if (!IsGPUWetnessMode(GetActiveSimulationMode()))
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

        if (!InitializeLODVertexColorTransfer(Receiver, *RuntimeDataSubsystem, RuntimeLODIndex))
        {
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
                *GetNameSafe(GetOwner()));
        }
    }

    const FSurfaceWaterSimulationSettings& SurfaceSimulationSettings = Receiver.WetClothingAsset->Authored.SurfaceWaterSettings;
    if (SurfaceSimulationSettings.bEnabled)
    {
        TSet<int32> SurfaceEnabledMaterialSlots;
        TSet<int32> ConflictingProfileSlots;
        for (int32 VertexIndex = 0; VertexIndex < Receiver.SharedRuntimeData->SurfaceWaterMaterialSlotIndices.Num(); ++VertexIndex)
        {
            if (!Receiver.SharedRuntimeData->SupportsSurfaceWater(VertexIndex)) continue;
            const int32 MaterialSlotIndex = Receiver.SharedRuntimeData->SurfaceWaterMaterialSlotIndices[VertexIndex];
            if (MaterialSlotIndex == INDEX_NONE) continue;
            SurfaceEnabledMaterialSlots.Add(MaterialSlotIndex);

            if (const FWetnessProfileParameters* Profile =
                    Receiver.SharedRuntimeData->GetWetnessProfileParameters(VertexIndex))
            {
                const FSurfaceWaterProfileParameters& Candidate =
                    Profile->SurfaceWater;
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
    SurfaceWaterTimerInterval = SurfaceWaterInterval;
    GetWorld()->GetTimerManager().SetTimer(SurfaceWaterSimulationTimer, this, &UDynamicWetClothesComponent::UpdateSurfaceWater, SurfaceWaterInterval, true);

    if (HasAnyRenderLODSettings())
    {
        GetWorld()->GetTimerManager().SetTimer(
            RenderLODEvaluationTimer,
            this,
            &UDynamicWetClothesComponent::UpdateRenderLOD,
            FMath::Max(0.01f, RenderLODEvaluationInterval),
            true);
    }
}

bool UDynamicWetClothesComponent::HasAnyRenderLODSettings() const
{
    return !QualityLODScreenSizeThresholds.IsEmpty();
}

bool UDynamicWetClothesComponent::CalculateRenderLODScreenSize(
    float& OutScreenSize,
    FBoxSphereBounds& OutBounds) const
{
    OutScreenSize = 0.0f;
    OutBounds = FBoxSphereBounds();

    UWorld* World = GetWorld();
    UGameViewportClient* GameViewport = World != nullptr ? World->GetGameViewport() : nullptr;
    UGameInstance* GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;
    FSceneViewport* SceneViewport = GameViewport != nullptr ? GameViewport->GetGameViewport() : nullptr;
    ULocalPlayer* LocalPlayer = GameInstance != nullptr ? GameInstance->GetFirstGamePlayer() : nullptr;
    if (SceneViewport == nullptr || LocalPlayer == nullptr)
    {
        return false;
    }

    FBox MergedBox(ForceInit);
    for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers)
    {
        USkeletalMeshComponent* Mesh = Receiver.IsValid() ? Receiver->MeshComponent.Get() : nullptr;
        if (Mesh != nullptr && Mesh->Bounds.SphereRadius > KINDA_SMALL_NUMBER)
        {
            MergedBox += Mesh->Bounds.GetBox();
        }
    }

    if (!MergedBox.IsValid)
    {
        return false;
    }

    FSceneViewProjectionData ProjectionData;
    if (!LocalPlayer->GetProjectionData(SceneViewport, ProjectionData, INDEX_NONE))
    {
        return false;
    }

    OutBounds = FBoxSphereBounds(MergedBox);
    if (OutBounds.SphereRadius <= KINDA_SMALL_NUMBER)
    {
        return false;
    }

    OutScreenSize = FMath::Clamp(
        ComputeBoundsScreenSize(
            FVector4(OutBounds.Origin, 1.0f),
            OutBounds.SphereRadius,
            FVector4(ProjectionData.ViewOrigin, 1.0f),
            ProjectionData.ProjectionMatrix),
        0.0f,
        1.0f);
    return true;
}

bool UDynamicWetClothesComponent::FindRenderLODLevel(const float ScreenSize, int32& OutLODLevel) const
{
    return QualityLODEvaluator.IsValid() &&
           QualityLODEvaluator->ResolveLODFromScreenSize(
        QualityLODScreenSizeThresholds,
        ScreenSize,
        OutLODLevel);
}

void UDynamicWetClothesComponent::ResetRenderLODState()
{
    RenderLODState = FDWCQualityLODScreenSizeRuntimeState();
    CurrentRenderLODScreenSize = 0.0f;
}

void UDynamicWetClothesComponent::UpdateRenderLOD()
{
    float ScreenSize = 0.0f;
    FBoxSphereBounds MergedBounds;
    //Calculate Merged Sphere Bound of Actor
    if (!CalculateRenderLODScreenSize(ScreenSize, MergedBounds))
    {
        CurrentRenderLODScreenSize = 0.0f;
        return;
    }

    CurrentRenderLODScreenSize = ScreenSize;
    RenderLODState.ScreenSize = ScreenSize;
    RenderLODState.MergedBounds = MergedBounds;
    RenderLODState.bHasValidScreenSize = true;

    int32 NewLODLevel = INDEX_NONE;
    if (!FindRenderLODLevel(ScreenSize, NewLODLevel))
    {
        return;
    }

    const int32 PreviousLODLevel = RenderLODState.ActiveLODLevel;
    if (PreviousLODLevel != NewLODLevel)
    {
        UE_LOG(
            LogDWC,
            Warning,
            TEXT("DWC Rendering LOD changed on '%s': LOD %d -> %d (Merged Screen Size: %.4f)."),
            *GetNameSafe(GetOwner()),
            PreviousLODLevel,
            NewLODLevel,
            ScreenSize);
    }

    RenderLODState.ActiveLODLevel = NewLODLevel;
    if (CurrentQualityLOD != NewLODLevel)
    {
        SetDWCQualityLOD(NewLODLevel);
    }
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

FWetInputStageArgs UDynamicWetClothesComponent::MakeWetInputStageArgs(FDWCWetMeshReceiverRuntime& Receiver)
{
    check(Receiver.SharedRuntimeData.IsValid());
    check(Receiver.RuntimeDataBuilder.IsValid());
    check(Receiver.SimulationState.IsValid());
    check(Receiver.SimulationStage.IsValid());
    check(Receiver.MeshSampler.IsValid());

    FWetInputStageArgs Args;
    Args.OwnerForLogs = GetOwner();
    Args.TargetSkeletalMesh = Receiver.MeshComponent.Get();
    Args.WetnessSettings = &WetnessSettings;
    Args.RuntimeData = Receiver.SharedRuntimeData.Get();
    Args.SimulationState = Receiver.SimulationState.Get();
    Args.SurfaceWaterStatesByMaterialSlot = &Receiver.SurfaceWaterStatesByMaterialSlot;
    Args.SurfaceWaterSettings = Receiver.WetClothingAsset.IsValid() ? &Receiver.WetClothingAsset->Authored.SurfaceWaterSettings : nullptr;
    Args.SurfaceWaterRandomStream = &Receiver.InputStage->GetSurfaceWaterRandomStream();
    Args.RuntimeDataBuilder = Receiver.RuntimeDataBuilder.Get();
    Args.MeshSampler = Receiver.MeshSampler.Get();
    Args.SimulationStage = Receiver.SimulationStage.Get();
    return Args;
}

FWetSurfaceContactResolverArgs UDynamicWetClothesComponent::MakeWetSurfaceContactResolverArgs(FDWCWetMeshReceiverRuntime& Receiver)
{
    check(Receiver.SharedRuntimeData.IsValid());
    check(Receiver.RuntimeDataBuilder.IsValid());
    check(Receiver.MeshSampler.IsValid());

    FWetSurfaceContactResolverArgs Args;
    Args.OwnerForLogs = GetOwner();
    Args.TargetSkeletalMesh = Receiver.MeshComponent.Get();
    Args.WetnessSettings = &WetnessSettings;
    Args.WetClothingAsset = Receiver.WetClothingAsset.Get();
    Args.RuntimeData = Receiver.SharedRuntimeData.Get();
    Args.RuntimeDataBuilder = Receiver.RuntimeDataBuilder.Get();
    Args.MeshSampler = Receiver.MeshSampler.Get();
    Args.LODIndex = UWetClothingAsset::RuntimeSimulationLODIndex;
    Args.MaxNearestSeedVertices = GPUContactNearestSeedVertexCount;
    return Args;
}

FWetSimulationStageArgs UDynamicWetClothesComponent::MakeWetSimulationStageArgs(FDWCWetMeshReceiverRuntime& Receiver)
{
    check(Receiver.SharedRuntimeData.IsValid());
    check(Receiver.RuntimeDataBuilder.IsValid());
    check(Receiver.SimulationState.IsValid());
    check(Receiver.MeshSampler.IsValid());

    FWetSimulationStageArgs Args;
    Args.TargetSkeletalMesh = Receiver.MeshComponent.Get();
    Args.WetnessSettings = &WetnessSettings;
    Args.RuntimeData = Receiver.SharedRuntimeData.Get();
    Args.SimulationState = Receiver.SimulationState.Get();
    Args.RuntimeDataBuilder = Receiver.RuntimeDataBuilder.Get();
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
    Args.SurfaceWaterProfilesByMaterialSlot = &Receiver.SurfaceWaterProfilesByMaterialSlot;
    Args.WetMaterialInstances = &Receiver.WetMaterialInstances;
    Args.SurfaceWaterTimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
    Args.WrinkleStrength = WrinkleStrength;
    Args.WrinkleWetnessMin = WrinkleWetnessMin;
    Args.WrinkleWetnessMax = WrinkleWetnessMax;
    Args.bEnableWrinkle = !QualityLODController.IsValid() || QualityLODController->ShouldUpdateWrinkle(Receiver.QualityLODState);
    Args.TransparencyWetnessMin = TransparencyWetnessMin;
    Args.TransparencyWetnessMax = TransparencyWetnessMax;
    Args.bEnableTransparency = !QualityLODController.IsValid() || QualityLODController->ShouldUpdateTransparency(Receiver.QualityLODState);
    Args.UnderColor = FallbackUnderColor;
    Args.UnderColorBlendStrength = WetUnderColorBlendStrength;
    Args.bShowWetPartDebugColors = bShowWetPartDebugColors;
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
    InitArgs.WetMaterialInstances = &Receiver.WetMaterialInstances;
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

        for (const FWetClothingBakedTransparencyRevealLayer& RevealLayer : ReceiverWetClothingAsset->Authored.TransparencyData.BakedRevealLayers)
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
    if (IsGPUWetnessMode(GetActiveSimulationMode()))
    {
        for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers)
        {
            if (Receiver.IsValid() && Receiver->GPUBackend.IsValid())
            {
                Receiver->GPUBackend->ApplyWetAll(Amount);
            }
        }
        return;
    }

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
    FDWCWorkloadStats::RecordWetContactsReceived(1);
    const auto RecordContactResult = [](const bool bApplied)
    {
        FDWCWorkloadStats::RecordWetContactsOutcome(1, bApplied);
        return bApplied;
    };

    if (bBatchWetContactsPerFrame)
    {
        const int32 MaxQueuedContacts = FMath::Max(1, MaxBatchedWetContactsPerFrame);
        if (FMath::IsNearlyZero(Contact.Amount) || PendingWetContacts.Num() >= MaxQueuedContacts)
        {
            return RecordContactResult(false);
        }

        PendingWetContacts.Add(Contact);
        bPendingWetContactsApplyMaterial |= bApplyMaterial;
        SetComponentTickEnabled(true);
        return true;
    }

    if (IsGPUWetnessMode(GetActiveSimulationMode()))
    {
        bool bAnyQueued = false;
        for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers)
        {
            if (!Receiver.IsValid() ||
                !Receiver->SurfaceContactResolver.IsValid() ||
                !Receiver->GPUBackend.IsValid() ||
                !ShouldReceiverConsiderContact(*Receiver, Contact))
            {
                continue;
            }

            TArray<FDWCResolvedSurfaceContact> ResolvedContacts;
            FWetSurfaceContactResolverArgs ResolverArgs = MakeWetSurfaceContactResolverArgs(*Receiver);
            if (Receiver->SurfaceContactResolver->ResolveContact(ResolverArgs, Contact, ResolvedContacts) &&
                Receiver->GPUBackend->EnqueueResolvedContacts(ResolvedContacts))
            {
                QueueGPUSurfaceWaterStamps(*Receiver, ResolvedContacts);
                bAnyQueued = true;
            }
        }
        return RecordContactResult(bAnyQueued);
    }

    bool bAnyChanged = false;
    for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers)
    {
        if (!Receiver.IsValid() ||
            !Receiver->InputStage.IsValid() ||
            !ShouldReceiverConsiderContact(*Receiver, Contact))
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
    return RecordContactResult(bAnyChanged);
}

bool UDynamicWetClothesComponent::ApplyWetContacts(const TArray<FDWCWetContact>& Contacts, const bool bApplyMaterial)
{
    const uint32 ContactCount = static_cast<uint32>(Contacts.Num());
    FDWCWorkloadStats::RecordWetContactsReceived(ContactCount);
    const auto RecordContactResults = [ContactCount](const bool bApplied)
    {
        FDWCWorkloadStats::RecordWetContactsOutcome(ContactCount, bApplied);
        return bApplied;
    };

    FlushPendingWetContacts();

    if (IsGPUWetnessMode(GetActiveSimulationMode()))
    {
        bool bAnyQueued = false;
        for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers)
        {
            if (!Receiver.IsValid() ||
                !Receiver->SurfaceContactResolver.IsValid() ||
                !Receiver->GPUBackend.IsValid())
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
            TArray<FDWCResolvedSurfaceContact> ResolvedContacts;
            FWetSurfaceContactResolverArgs ResolverArgs = MakeWetSurfaceContactResolverArgs(*Receiver);
            if (Receiver->SurfaceContactResolver->ResolveContacts(ResolverArgs, ReceiverContacts, ResolvedContacts) &&
                Receiver->GPUBackend->EnqueueResolvedContacts(ResolvedContacts))
            {
                QueueGPUSurfaceWaterStamps(*Receiver, ResolvedContacts);
                bAnyQueued = true;
            }
        }
        return RecordContactResults(bAnyQueued);
    }

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
    return RecordContactResults(bAnyChanged);
}

bool UDynamicWetClothesComponent::ApplyWetArea(const FDWCWetAreaData& AreaData, const bool bApplyMaterial)
{
    if (IsGPUWetnessMode(GetActiveSimulationMode()))
    {
        bool bAnyQueued = false;
        for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers)
        {
            if (!Receiver.IsValid() ||
                !Receiver->SurfaceContactResolver.IsValid() ||
                !Receiver->GPUBackend.IsValid())
            {
                continue;
            }

            TArray<FDWCResolvedSurfaceContact> ResolvedContacts;
            FWetSurfaceContactResolverArgs ResolverArgs = MakeWetSurfaceContactResolverArgs(*Receiver);
            if (Receiver->SurfaceContactResolver->ResolveWetArea(ResolverArgs, AreaData, ResolvedContacts) &&
                Receiver->GPUBackend->EnqueueResolvedContacts(ResolvedContacts))
            {
                QueueGPUSurfaceWaterStamps(*Receiver, ResolvedContacts);
                bAnyQueued = true;
            }
        }
        return bAnyQueued;
    }

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
    if (IsGPUWetnessMode(GetActiveSimulationMode()))
    {
        bool bAnyQueued = false;
        for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers)
        {
            if (!Receiver.IsValid() ||
                !Receiver->SurfaceContactResolver.IsValid() ||
                !Receiver->GPUBackend.IsValid() ||
                !ShouldReceiverConsiderSurface(*Receiver, WaterSurfaceData))
            {
                continue;
            }

            TArray<FDWCResolvedSurfaceContact> ResolvedContacts;
            FWetSurfaceContactResolverArgs ResolverArgs = MakeWetSurfaceContactResolverArgs(*Receiver);
            if (Receiver->SurfaceContactResolver->ResolveWaterSurface(ResolverArgs, WaterSurfaceData, Amount, ResolvedContacts) &&
                Receiver->GPUBackend->EnqueueResolvedContacts(ResolvedContacts))
            {
                QueueGPUSurfaceWaterStamps(*Receiver, ResolvedContacts);
                bAnyQueued = true;
            }
        }
        return bAnyQueued;
    }

    bool bAnyChanged = false;
    for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers)
    {
        if (!Receiver.IsValid() ||
            !Receiver->InputStage.IsValid() ||
            !ShouldReceiverConsiderSurface(*Receiver, WaterSurfaceData))
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
        Receiver->bCpuSkinningTaskRequestedAgain = false;
        Receiver->bCpuSkinningTaskNeedsNormals = false;

        SetComponentTickEnabled(true);
        return;
    }
}

void UDynamicWetClothesComponent::CommitLODVertexColorTransferResult(FDWCLODVertexColorTransferResult&& Result)
{
    DWC_PROFILE_SCOPE(DWC_Component_CommitLODVertexColorTransferResult);

    for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers)
    {
        if (!Receiver.IsValid() ||
            Receiver->ReceiverId != Result.ReceiverId ||
            Receiver->LODVertexColorTransferGeneration != Result.Generation)
        {
            continue;
        }

        Receiver->bLODVertexColorTransferPending = false;

        USkeletalMeshComponent* Mesh = Receiver->MeshComponent.Get();
        if (Mesh == nullptr)
        {
            return;
        }

        FDWCWorkloadStats::RecordLODTransferCompleted(
            static_cast<uint32>(FMath::Max(0, Result.DirtySourceVertexCount)));

        UWorld* World = GetWorld();
        UDWCRuntimeDataSubsystem* RuntimeDataSubsystem =
            World != nullptr ? World->GetSubsystem<UDWCRuntimeDataSubsystem>() : nullptr;
        const TSharedPtr<const FDWCLODVertexStaticData, ESPMode::ThreadSafe> SourceLODData =
            Receiver->LODVertexStaticDataByLOD.FindRef(UWetClothingAsset::RuntimeSimulationLODIndex);
        const FString MeshSignature = Receiver->SharedRuntimeData.IsValid()
                                          ? Receiver->SharedRuntimeData->MeshSignature
                                          : FString();

        for (FDWCLODVertexColorTransferResult::FLODColors& LODResult : Result.LODResults)
        {
            if (LODResult.LODIndex == UWetClothingAsset::RuntimeSimulationLODIndex ||
                LODResult.Colors.IsEmpty())
            {
                continue;
            }

            if (!LODResult.TargetToSourceVertex.IsEmpty())
            {
                TSharedPtr<const TArray<int32>, ESPMode::ThreadSafe> TransferMap;
                const TSharedPtr<const FDWCLODVertexStaticData, ESPMode::ThreadSafe> TargetLODData =
                    Receiver->LODVertexStaticDataByLOD.FindRef(LODResult.LODIndex);
                if (RuntimeDataSubsystem != nullptr && SourceLODData.IsValid() && TargetLODData.IsValid())
                {
                    TransferMap = RuntimeDataSubsystem->CacheLODVertexColorTransferMap(
                        *Mesh,
                        *SourceLODData,
                        *TargetLODData,
                        MeshSignature,
                        FDWCLODVertexColorTransferSettings(),
                        MoveTemp(LODResult.TargetToSourceVertex));
                }

                if (!TransferMap.IsValid())
                {
                    TransferMap = MakeShared<TArray<int32>, ESPMode::ThreadSafe>(MoveTemp(LODResult.TargetToSourceVertex));
                }

                if (TransferMap.IsValid())
                {
                    Receiver->LODVertexColorTransferMapsByLOD.Add(LODResult.LODIndex, TransferMap);
                }
            }

            TSharedRef<TArray<FColor>, ESPMode::ThreadSafe> ColorCache =
                MakeShared<TArray<FColor>, ESPMode::ThreadSafe>(LODResult.Colors);
            Receiver->LODVertexColorCachesByLOD.Add(LODResult.LODIndex, ColorCache);

            FWetVertexColorBuffer::ApplyVertexColorOverride(*Mesh, LODResult.LODIndex, LODResult.Colors);
        }

        if (Receiver->bLODVertexColorTransferRequestedAgain)
        {
            Receiver->bLODVertexColorTransferRequestedAgain = false;
            RequestLODVertexColorTransferTask(*Receiver);
        }

        SetComponentTickEnabled(HasPendingCpuSkinningTasks() || HasPendingLODVertexColorTransferTasks());
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

void UDynamicWetClothesComponent::SetReceiverQualityLOD(FDWCWetMeshReceiverRuntime& Receiver, const int32 InQualityLOD)
{
    if (QualityLODController.IsValid())
    {
        QualityLODController->SetLOD(Receiver.QualityLODState, InQualityLOD);
    }
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

void UDynamicWetClothesComponent::ConfigureQualityLODController()
{
    if (!QualityLODController.IsValid())
    {
        return;
    }

    QualityLODController->SetEnabled(bEnableDWCQualityLOD);
    QualityLODController->SetProfile(QualityLODProfile.Get());
}

void UDynamicWetClothesComponent::RefreshResolvedQualityLODPolicies()
{
    ConfigureQualityLODController();
    if (!QualityLODController.IsValid())
    {
        return;
    }

    for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers)
    {
        if (Receiver.IsValid())
        {
            QualityLODController->RefreshPolicy(Receiver->QualityLODState);
            ApplyQualityLODMaterialParameters(*Receiver);
        }
    }
}

bool UDynamicWetClothesComponent::ShouldUpdateGPUWetness(FDWCWetMeshReceiverRuntime& Receiver) const
{
    if (!Receiver.GPUBackend.IsValid())
    {
        return false;
    }

    return true;
}

bool UDynamicWetClothesComponent::ShouldUpdateCPUWetness(FDWCWetMeshReceiverRuntime& Receiver) const
{
    if (!Receiver.SimulationStage.IsValid())
    {
        return false;
    }

    return true;
}

bool UDynamicWetClothesComponent::ShouldUpdateSurfaceWater(FDWCWetMeshReceiverRuntime& Receiver) const
{
    if (!Receiver.WetClothingAsset.IsValid())
    {
        return false;
    }

    return !QualityLODController.IsValid() ||
           QualityLODController->ShouldRunSurfaceWater(Receiver.QualityLODState, SurfaceWaterTimerInterval);
}

bool UDynamicWetClothesComponent::ShouldUpdateWetRendering(FDWCWetMeshReceiverRuntime& Receiver) const
{
    if (!Receiver.RenderStage.IsValid() || !Receiver.SimulationState.IsValid())
    {
        return false;
    }

    return !QualityLODController.IsValid() ||
           QualityLODController->ShouldRunRendering(Receiver.QualityLODState, WetnessSettings.WetnessRenderUpdateInterval);
}

void UDynamicWetClothesComponent::SetDWCQualityLOD(const int32 InQualityLOD)
{
    CurrentQualityLOD = FMath::Max(0, InQualityLOD);
    ConfigureQualityLODController();
    for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers)
    {
        if (Receiver.IsValid())
        {
            SetReceiverQualityLOD(*Receiver, CurrentQualityLOD);
            ApplyQualityLODMaterialParameters(*Receiver);
        }
    }
}

bool UDynamicWetClothesComponent::SetReceiverDWCQualityLOD(const FName ReceiverId, const int32 InQualityLOD)
{
    ConfigureQualityLODController();
    for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers)
    {
        if (Receiver.IsValid() && Receiver->ReceiverId == ReceiverId)
        {
            SetReceiverQualityLOD(*Receiver, InQualityLOD);
            ApplyQualityLODMaterialParameters(*Receiver);
            return true;
        }
    }

    return false;
}

bool UDynamicWetClothesComponent::InitializeLODVertexColorTransfer(
    FDWCWetMeshReceiverRuntime& Receiver,
    UDWCRuntimeDataSubsystem& RuntimeDataSubsystem,
    const int32 RuntimeLODIndex)
{
    Receiver.LODVertexStaticDataByLOD.Reset();
    Receiver.LODVertexColorTransferMapsByLOD.Reset();
    Receiver.LODVertexColorCachesByLOD.Reset();
    Receiver.PendingLODVertexColorDirtySourceVertices.Reset();

    if (!Receiver.WetClothingAsset.IsValid() || !Receiver.SharedRuntimeData.IsValid())
    {
        return false;
    }

    USkeletalMeshComponent* Mesh = Receiver.MeshComponent.Get();
    const USkeletalMesh* SkeletalMesh = Mesh != nullptr ? Mesh->GetSkeletalMeshAsset() : nullptr;
    FSkeletalMeshRenderData* RenderData = SkeletalMesh != nullptr ? SkeletalMesh->GetResourceForRendering() : nullptr;
    if (Mesh == nullptr || RenderData == nullptr)
    {
        return false;
    }

    const FString& MeshSignature = Receiver.SharedRuntimeData->MeshSignature;
    for (int32 LODIndex = 0; LODIndex < RenderData->LODRenderData.Num(); ++LODIndex)
    {
        TSharedPtr<const FDWCLODVertexStaticData, ESPMode::ThreadSafe> LODVertexData =
            RuntimeDataSubsystem.AcquireLODVertexStaticData(
                *Mesh,
                LODIndex,
                MeshSignature);
        if (LODVertexData.IsValid())
        {
            Receiver.LODVertexStaticDataByLOD.Add(LODIndex, LODVertexData);
        }
    }

    const TSharedPtr<const FDWCLODVertexStaticData, ESPMode::ThreadSafe> SourceLODData =
        Receiver.LODVertexStaticDataByLOD.FindRef(RuntimeLODIndex);
    if (!SourceLODData.IsValid())
    {
        return false;
    }

    const FDWCLODVertexColorTransferSettings TransferSettings;
    for (const FWCALODVertexColorRuntimeData& RuntimeData :
         Receiver.WetClothingAsset->Derived.Bulk.LODVertexColorRuntimeData)
    {
        const TSharedPtr<const FDWCLODVertexStaticData, ESPMode::ThreadSafe> TargetLODData =
            Receiver.LODVertexStaticDataByLOD.FindRef(RuntimeData.TargetLODIndex);
        if (!RuntimeData.IsValid() ||
            RuntimeData.SourceLODIndex != RuntimeLODIndex ||
            RuntimeData.MeshSignature != MeshSignature ||
            !TargetLODData.IsValid() ||
            TargetLODData->Geometry.VertexCount != RuntimeData.TargetVertexCount)
        {
            continue;
        }

        TSharedPtr<const TArray<int32>, ESPMode::ThreadSafe> SharedTransferMap =
            RuntimeDataSubsystem.FindLODVertexColorTransferMap(
                *Mesh,
                *SourceLODData,
                *TargetLODData,
                MeshSignature,
                TransferSettings);
        if (!SharedTransferMap.IsValid())
        {
            TArray<int32> TransferMapCopy(RuntimeData.TargetToSourceVertex);
            SharedTransferMap = RuntimeDataSubsystem.CacheLODVertexColorTransferMap(
                *Mesh,
                *SourceLODData,
                *TargetLODData,
                MeshSignature,
                TransferSettings,
                MoveTemp(TransferMapCopy));
        }

        if (SharedTransferMap.IsValid())
        {
            Receiver.LODVertexColorTransferMapsByLOD.Add(RuntimeData.TargetLODIndex, SharedTransferMap);
        }
    }

    for (const TPair<int32, TSharedPtr<const FDWCLODVertexStaticData, ESPMode::ThreadSafe>>& Pair :
         Receiver.LODVertexStaticDataByLOD)
    {
        if (Pair.Key == RuntimeLODIndex ||
            !Pair.Value.IsValid() ||
            Receiver.LODVertexColorTransferMapsByLOD.Contains(Pair.Key))
        {
            continue;
        }

        TSharedPtr<const TArray<int32>, ESPMode::ThreadSafe> SharedTransferMap =
            RuntimeDataSubsystem.FindLODVertexColorTransferMap(
                *Mesh,
                *SourceLODData,
                *Pair.Value,
                MeshSignature,
                TransferSettings);
        if (!SharedTransferMap.IsValid())
        {
            TArray<int32> BuiltTransferMap;
            if (BuildDWCLODVertexColorTransferMap(
                    *SourceLODData,
                    *Pair.Value,
                    TransferSettings,
                    BuiltTransferMap))
            {
                SharedTransferMap = RuntimeDataSubsystem.CacheLODVertexColorTransferMap(
                    *Mesh,
                    *SourceLODData,
                    *Pair.Value,
                    MeshSignature,
                    TransferSettings,
                    MoveTemp(BuiltTransferMap));
            }
        }

        if (SharedTransferMap.IsValid())
        {
            Receiver.LODVertexColorTransferMapsByLOD.Add(Pair.Key, SharedTransferMap);
        }
    }

    return true;
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
    Receiver.bCpuSkinningTaskRequestedAgain = false;
    Receiver.bCpuSkinningTaskNeedsNormals = bComputeNormals;

    AsyncTaskQueue->Enqueue(MakeShared<FDWCCpuSkinningTask, ESPMode::ThreadSafe>(this, MoveTemp(Snapshot)));
    SetComponentTickEnabled(true);
    return true;
}

bool UDynamicWetClothesComponent::RequestLODVertexColorTransferTask(FDWCWetMeshReceiverRuntime& Receiver)
{
    DWC_PROFILE_SCOPE(DWC_Component_RequestLODVertexColorTransferTask);

    if (!AsyncTaskQueue.IsValid() ||
        !Receiver.RenderStage.IsValid() ||
        Receiver.LODVertexStaticDataByLOD.Num() <= 1)
    {
        return false;
    }

    USkeletalMeshComponent* Mesh = Receiver.MeshComponent.Get();
    if (Mesh == nullptr)
    {
        return false;
    }

    if (Receiver.bLODVertexColorTransferPending)
    {
        Receiver.bLODVertexColorTransferRequestedAgain = true;
        SetComponentTickEnabled(true);
        return true;
    }

    const int32 SourceLODIndex = UWetClothingAsset::RuntimeSimulationLODIndex;
    TSharedPtr<const FDWCLODVertexStaticData, ESPMode::ThreadSafe> SourceLODData =
        Receiver.LODVertexStaticDataByLOD.FindRef(SourceLODIndex);
    if (!SourceLODData.IsValid() ||
        Receiver.RenderStage->CachedWetVertexColors.Num() != SourceLODData->Geometry.VertexCount)
    {
        return false;
    }

    FDWCLODVertexColorTransferSnapshot Snapshot;
    Snapshot.ReceiverId = Receiver.ReceiverId;
    Snapshot.Generation = ++Receiver.LODVertexColorTransferGeneration;
    Snapshot.SourceLODData = SourceLODData;
    Snapshot.SourceColors = Receiver.RenderStage->CachedWetVertexColors;
    Snapshot.DirtySourceVertices = Receiver.PendingLODVertexColorDirtySourceVertices;
    Receiver.PendingLODVertexColorDirtySourceVertices.Reset();

    UWorld* World = GetWorld();
    UDWCRuntimeDataSubsystem* RuntimeDataSubsystem =
        World != nullptr ? World->GetSubsystem<UDWCRuntimeDataSubsystem>() : nullptr;
    const FString MeshSignature = Receiver.SharedRuntimeData.IsValid()
                                      ? Receiver.SharedRuntimeData->MeshSignature
                                      : FString();

    for (const TPair<int32, TSharedPtr<const FDWCLODVertexStaticData, ESPMode::ThreadSafe>>& Pair : Receiver.LODVertexStaticDataByLOD)
    {
        if (Pair.Key == SourceLODIndex || !Pair.Value.IsValid())
        {
            continue;
        }

        Snapshot.TargetLODData.Add(Pair.Value);

        TSharedPtr<const TArray<int32>, ESPMode::ThreadSafe> CachedTransferMap =
            Receiver.LODVertexColorTransferMapsByLOD.FindRef(Pair.Key);
        if (!CachedTransferMap.IsValid() && RuntimeDataSubsystem != nullptr)
        {
            CachedTransferMap = RuntimeDataSubsystem->FindLODVertexColorTransferMap(
                *Mesh,
                *SourceLODData,
                *Pair.Value,
                MeshSignature,
                Snapshot.Settings);
            if (CachedTransferMap.IsValid())
            {
                Receiver.LODVertexColorTransferMapsByLOD.Add(Pair.Key, CachedTransferMap);
            }
        }

        if (CachedTransferMap.IsValid())
        {
            Snapshot.CachedTargetToSourceVertexByLOD.Add(Pair.Key, CachedTransferMap);
        }
        if (const TSharedPtr<const TArray<FColor>, ESPMode::ThreadSafe>* CachedColors = Receiver.LODVertexColorCachesByLOD.Find(Pair.Key))
        {
            Snapshot.CachedTargetColorsByLOD.Add(Pair.Key, *CachedColors);
        }
    }

    if (Snapshot.TargetLODData.IsEmpty())
    {
        return false;
    }

    Receiver.bLODVertexColorTransferPending = true;
    Receiver.bLODVertexColorTransferRequestedAgain = false;

    AsyncTaskQueue->Enqueue(MakeShared<FDWCLODVertexColorTransferTask, ESPMode::ThreadSafe>(this, MoveTemp(Snapshot)));
    SetComponentTickEnabled(true);
    return true;
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
    for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers)
    {
        if (Receiver.IsValid() && Receiver->bLODVertexColorTransferPending)
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
    const uint32 ContactCount = static_cast<uint32>(ContactsToApply.Num());

    const bool bApplyMaterial = bPendingWetContactsApplyMaterial;
    bPendingWetContactsApplyMaterial = false;

    if (Receivers.IsEmpty() && !InitializeWetRuntime())
    {
        FDWCWorkloadStats::RecordWetContactsOutcome(ContactCount, false);
        return false;
    }

    if (IsGPUWetnessMode(GetActiveSimulationMode()))
    {
        bool bAnyQueued = false;
        for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers)
        {
            if (!Receiver.IsValid() ||
                !Receiver->SurfaceContactResolver.IsValid() ||
                !Receiver->GPUBackend.IsValid())
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

            TArray<FDWCResolvedSurfaceContact> ResolvedContacts;
            FWetSurfaceContactResolverArgs ResolverArgs = MakeWetSurfaceContactResolverArgs(*Receiver);
            if (Receiver->SurfaceContactResolver->ResolveContacts(ResolverArgs, ReceiverContacts, ResolvedContacts) &&
                Receiver->GPUBackend->EnqueueResolvedContacts(ResolvedContacts))
            {
                QueueGPUSurfaceWaterStamps(*Receiver, ResolvedContacts);
                bAnyQueued = true;
            }
        }
        FDWCWorkloadStats::RecordWetContactsOutcome(ContactCount, bAnyQueued);
        return bAnyQueued;
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
        return false;
    }

    FDWCWorkloadStats::RecordWetContactsOutcome(ContactCount, bAnyChanged);
    return bAnyChanged;
}

void UDynamicWetClothesComponent::UpdateWetness()
{
    FlushAsyncTaskQueueGameThread();

    if (IsGPUWetnessMode(GetActiveSimulationMode()))
    {
        FlushPendingWetContacts();

        const float DeltaSeconds = GetWorld()
            ? FMath::Max(KINDA_SMALL_NUMBER, GetWorld()->GetDeltaSeconds())
            : FMath::Max(KINDA_SMALL_NUMBER, WetnessSettings.WetnessUpdateInterval);
        for (const TUniquePtr<FDWCWetMeshReceiverRuntime>& Receiver : Receivers)
        {
            if (!Receiver.IsValid() || !ShouldUpdateGPUWetness(*Receiver))
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
        if (!Receiver.IsValid() || !ShouldUpdateCPUWetness(*Receiver))
        {
            continue;
        }

        FWetSimulationStageArgs SimulationArgs = MakeWetSimulationStageArgs(*Receiver);
        const int32 DirtyVertexCountBeforeUpdate = Receiver->SimulationState.IsValid()
            ? Receiver->SimulationState->DirtyWetVertexIndices.Num()
            : 0;
        const bool bChanged = Receiver->SimulationStage->UpdateWetness(SimulationArgs);
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
        if (!Receiver.IsValid() || !ShouldUpdateWetRendering(*Receiver))
        {
            continue;
        }

        if (!Receiver->bWetRenderDirty && Receiver->SimulationState->DirtyWetVertexIndices.Num() == 0)
        {
            continue;
        }

        const bool bHadDirtyWetVertexColors = Receiver->SimulationState->DirtyWetVertexIndices.Num() > 0;
        if (bHadDirtyWetVertexColors)
        {
            Receiver->PendingLODVertexColorDirtySourceVertices = Receiver->SimulationState->DirtyWetVertexIndices;
        }

        FWetRenderStageArgs RenderArgs = MakeWetRenderStageArgs(*Receiver);
        if (IsGPUWetnessMode(GetActiveSimulationMode()))
        {
            // GPU wetness remains in the existing texture path. Vertex colors only carry
            // the static Wet Part debug color and are rebuilt when the debug view is toggled.
            Receiver->RenderStage->ApplyWetMaterialParameters(RenderArgs);
            if (bShowWetPartDebugColors && Receiver->SimulationState->DirtyWetVertexIndices.Num() > 0)
            {
                Receiver->RenderStage->ApplyWetnessToMaterial(RenderArgs);
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

        Receiver->SimulationState->MarkAllWetVertexColorsDirty();
        Receiver->bWetRenderDirty = true;

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
    if (QualityLODEvaluator.IsValid())
    {
        QualityLODEvaluator->NormalizeScreenSizeThresholds(QualityLODScreenSizeThresholds);
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
        PropertyName == GET_MEMBER_NAME_CHECKED(UDynamicWetClothesComponent, bShowWetPartDebugColors);

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
                Receiver->SimulationState->MarkAllWetVertexColorsDirty();
                Receiver->bWetRenderDirty = true;
            }
        }
        bWetRenderDirty = true;
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
