#include "RuntimeState/Utils/DWCLodCoordinator.h"

#include "Components/DynamicWetClothesComponent.h"
#include "Core/DWCQualityLODController.h"
#include "Core/DWCQualityLODEvaluator.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Runtime/Engine/Classes/Components/SkeletalMeshComponent.h"
#include "SceneManagement.h"
#include "Slate/SceneViewport.h"
#include "Utility/DWCLog.h"

FDWCLodCoordinator::FDWCLodCoordinator()
    : QualityLODController(MakeUnique<FDWCQualityLODController>()),
      QualityLODEvaluator(MakeUnique<FDWCQualityLODEvaluator>())
{
}

FDWCLodCoordinator::~FDWCLodCoordinator() = default;

void FDWCLodCoordinator::NormalizeScreenSizeThresholds(
    TArray<FDWCQualityLODScreenSizeThreshold>& Thresholds) const
{
    if (QualityLODEvaluator.IsValid())
    {
        QualityLODEvaluator->NormalizeScreenSizeThresholds(Thresholds);
    }
}

void FDWCLodCoordinator::ConfigureQualityLOD(
    const bool bEnabled,
    const UDWCQualityLODProfile* Profile)
{
    if (!QualityLODController.IsValid())
    {
        return;
    }

    QualityLODController->SetEnabled(bEnabled);
    QualityLODController->SetProfile(Profile);
}

void FDWCLodCoordinator::SetReceiverQualityLOD(
    FDWCWetMeshReceiverRuntime& Receiver,
    const int32 InQualityLOD) const
{
    if (QualityLODController.IsValid())
    {
        QualityLODController->SetLOD(Receiver.QualityLODState, InQualityLOD);
    }
}

void FDWCLodCoordinator::RefreshReceiverQualityLODPolicy(
    FDWCWetMeshReceiverRuntime& Receiver) const
{
    if (QualityLODController.IsValid())
    {
        QualityLODController->RefreshPolicy(Receiver.QualityLODState);
    }
}

bool FDWCLodCoordinator::ShouldRunSurfaceWater(
    FDWCQualityLODRuntimeState& State,
    const float BaseInterval)
{
    return !QualityLODController.IsValid() ||
           QualityLODController->ShouldRunSurfaceWater(State, BaseInterval);
}

bool FDWCLodCoordinator::ShouldRunRendering(
    FDWCQualityLODRuntimeState& State,
    const float BaseInterval)
{
    return !QualityLODController.IsValid() ||
           QualityLODController->ShouldRunRendering(State, BaseInterval);
}

bool FDWCLodCoordinator::ShouldUpdateWrinkle(
    const FDWCQualityLODRuntimeState& State) const
{
    return !QualityLODController.IsValid() ||
           QualityLODController->ShouldUpdateWrinkle(State);
}

bool FDWCLodCoordinator::ShouldUpdateTransparency(
    const FDWCQualityLODRuntimeState& State) const
{
    return !QualityLODController.IsValid() ||
           QualityLODController->ShouldUpdateTransparency(State);
}

bool FDWCLodCoordinator::HasAnyRenderLODSettings(
    const TArray<FDWCQualityLODScreenSizeThreshold>& Thresholds) const
{
    return !Thresholds.IsEmpty();
}

bool FDWCLodCoordinator::CalculateRenderLODScreenSize(
    UWorld* World,
    const TArray<TUniquePtr<FDWCWetMeshReceiverRuntime>>& Receivers,
    float& OutScreenSize,
    FBoxSphereBounds& OutBounds) const
{
    OutScreenSize = 0.0f;
    OutBounds = FBoxSphereBounds();

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

bool FDWCLodCoordinator::FindRenderLODLevel(
    const TArray<FDWCQualityLODScreenSizeThreshold>& Thresholds,
    const float ScreenSize,
    int32& OutLODLevel) const
{
    return QualityLODEvaluator.IsValid() &&
           QualityLODEvaluator->ResolveLODFromScreenSize(Thresholds, ScreenSize, OutLODLevel);
}

bool FDWCLodCoordinator::UpdateRenderLOD(
    UWorld* World,
    const UObject* OwnerForLogs,
    const TArray<TUniquePtr<FDWCWetMeshReceiverRuntime>>& Receivers,
    const TArray<FDWCQualityLODScreenSizeThreshold>& Thresholds,
    int32& OutLODLevel)
{
    OutLODLevel = INDEX_NONE;

    float ScreenSize = 0.0f;
    FBoxSphereBounds MergedBounds;
    if (!CalculateRenderLODScreenSize(World, Receivers, ScreenSize, MergedBounds))
    {
        return false;
    }

    RenderLODState.ScreenSize = ScreenSize;
    RenderLODState.MergedBounds = MergedBounds;
    RenderLODState.bHasValidScreenSize = true;

    if (!FindRenderLODLevel(Thresholds, ScreenSize, OutLODLevel))
    {
        return true;
    }

    const int32 PreviousLODLevel = RenderLODState.ActiveLODLevel;
    if (PreviousLODLevel != OutLODLevel)
    {
        UE_LOG(
            LogDWC,
            Warning,
            TEXT("DWC Rendering LOD changed on '%s': LOD %d -> %d (Merged Screen Size: %.4f)."),
            *GetNameSafe(OwnerForLogs),
            PreviousLODLevel,
            OutLODLevel,
            ScreenSize);
    }

    RenderLODState.ActiveLODLevel = OutLODLevel;
    return true;
}

void FDWCLodCoordinator::ResetRenderLODState()
{
    RenderLODState = FDWCQualityLODScreenSizeRuntimeState();
}

int32 FDWCLodCoordinator::GetCurrentRenderLODLevel() const
{
    return RenderLODState.ActiveLODLevel;
}

float FDWCLodCoordinator::GetMergedReceiverScreenSize() const
{
    return RenderLODState.ScreenSize;
}
