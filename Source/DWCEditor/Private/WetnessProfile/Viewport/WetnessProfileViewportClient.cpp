#include "WetnessProfileViewportClient.h"

#include "AdvancedPreviewScene.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "SEditorViewport.h"
#include "SWetnessProfileViewport.h"

FWetnessProfileViewportClient::FWetnessProfileViewportClient(FAdvancedPreviewScene* InPreviewScene, const TSharedRef<SWetnessProfileViewport>& InViewportWidget)
    : FEditorViewportClient(nullptr, InPreviewScene, StaticCastSharedRef<SEditorViewport>(InViewportWidget)), PreviewScene(InPreviewScene)
{
    SetViewMode(VMI_Lit);
    SetRealtime(true);
    SetViewLocation(FVector(180.0f, 0.0f, 80.0f));
    SetViewRotation(FRotator(-18.0f, 180.0f, 0.0f));

    EngineShowFlags.SetGrid(true);
    EngineShowFlags.SetSelectionOutline(true);
    EngineShowFlags.SetCompositeEditorPrimitives(true);

    bSetListenerPosition = false;
    bUsingOrbitCamera = true;
}

void FWetnessProfileViewportClient::Tick(float DeltaSeconds)
{
    FEditorViewportClient::Tick(DeltaSeconds);

    if (PreviewScene != nullptr && PreviewScene->GetWorld() != nullptr)
    {
        PreviewScene->GetWorld()->Tick(LEVELTICK_All, DeltaSeconds);
    }
}

void FWetnessProfileViewportClient::FocusOnPreviewMesh(const UStaticMeshComponent* InPreviewMeshComponent, bool bInstant)
{
    if (InPreviewMeshComponent == nullptr || InPreviewMeshComponent->GetStaticMesh() == nullptr)
    {
        return;
    }

    const FBoxSphereBounds Bounds = InPreviewMeshComponent->CalcBounds(InPreviewMeshComponent->GetComponentTransform());
    FocusViewportOnBox(Bounds.GetBox(), bInstant);
}

void FWetnessProfileViewportClient::SetPreviewMeshComponent(const UStaticMeshComponent* InPreviewMeshComponent)
{
    PreviewMeshComponent = InPreviewMeshComponent;
}
