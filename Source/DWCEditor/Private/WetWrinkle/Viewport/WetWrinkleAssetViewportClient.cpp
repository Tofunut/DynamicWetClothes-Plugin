#include "WetWrinkleAssetViewportClient.h"

#include "AdvancedPreviewScene.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"
#include "SceneView.h"
#include "SEditorViewport.h"
#include "WetWrinkleAssetViewport.h"

FWetWrinkleAssetViewportClient::FWetWrinkleAssetViewportClient(
    FAdvancedPreviewScene* InPreviewScene,
    const TSharedRef<SWetWrinkleAssetViewport>& InViewportWidget)
    : FEditorViewportClient(
          nullptr,
          InPreviewScene,
          StaticCastSharedRef<SEditorViewport>(InViewportWidget)),
      PreviewScene(InPreviewScene),
      ViewportWidget(InViewportWidget)
{
    SetViewMode(VMI_Lit);
    SetRealtime(true);
    ViewFOV = 65.0f;
    FOVAngle = 65.0f;

    SetViewLocation(FVector(250.0f, 0.0f, 120.0f));
    SetViewRotation(FRotator(-20.0f, 180.0f, 0.0f));

    EngineShowFlags.SetGrid(true);
    EngineShowFlags.SetSelectionOutline(true);
    EngineShowFlags.SetCompositeEditorPrimitives(true);

    bSetListenerPosition = false;
    bUsingOrbitCamera = true;
}

void FWetWrinkleAssetViewportClient::Tick(float DeltaSeconds)
{
    FEditorViewportClient::Tick(DeltaSeconds);

    if (PreviewScene != nullptr && PreviewScene->GetWorld() != nullptr)
    {
        PreviewScene->GetWorld()->Tick(LEVELTICK_All, DeltaSeconds);
    }

    if (bFocusPreviewMeshOnNextTick)
    {
        if (const USkeletalMeshComponent* MeshComponent = PendingFocusMeshComponent.Get())
        {
            if (MeshComponent->GetSkeletalMeshAsset() != nullptr)
            {
                FocusOnPreviewMesh(MeshComponent, true);
            }
        }

        bFocusPreviewMeshOnNextTick = false;
        PendingFocusMeshComponent = nullptr;
    }
}

void FWetWrinkleAssetViewportClient::MouseMove(FViewport* InViewport, int32 X, int32 Y)
{
    FEditorViewportClient::MouseMove(InViewport, X, Y);
    UpdateSurfaceHitUnderCursor();
}

void FWetWrinkleAssetViewportClient::CapturedMouseMove(FViewport* InViewport, int32 X, int32 Y)
{
    FEditorViewportClient::CapturedMouseMove(InViewport, X, Y);
    UpdateSurfaceHitUnderCursor();
}

void FWetWrinkleAssetViewportClient::ProcessClick(FSceneView& View, HHitProxy* HitProxy, FKey Key, EInputEvent Event, uint32 HitX, uint32 HitY)
{
    FEditorViewportClient::ProcessClick(View, HitProxy, Key, Event, HitX, HitY);

    if (Key == EKeys::LeftMouseButton && (Event == IE_Pressed || Event == IE_Released))
    {
        FVector RayOrigin = FVector::ZeroVector;
        FVector RayDirection = FVector::ForwardVector;
        View.DeprojectFVector2D(FVector2D(HitX, HitY), RayOrigin, RayDirection);

        if (const TSharedPtr<SWetWrinkleAssetViewport> PinnedViewport = ViewportWidget.Pin())
        {
            FWetWrinkleSurfaceHit SurfaceHit;
            if (PinnedViewport->TraceSurface(RayOrigin, RayDirection, SurfaceHit))
            {
                PinnedViewport->HandleSurfaceHitFromClient(SurfaceHit);
                bHasCurrentSurfaceHit = true;
                return;
            }
        }
    }

    ClearSurfaceHit();
}

void FWetWrinkleAssetViewportClient::FocusOnPreviewMesh(const USkeletalMeshComponent* InPreviewMeshComponent, bool bInstant)
{
    if (InPreviewMeshComponent == nullptr || InPreviewMeshComponent->GetSkeletalMeshAsset() == nullptr)
    {
        return;
    }

    const FBoxSphereBounds Bounds = InPreviewMeshComponent->CalcBounds(InPreviewMeshComponent->GetComponentTransform());
    float Radius = FMath::Max3(
        static_cast<float>(Bounds.BoxExtent.X),
        static_cast<float>(Bounds.BoxExtent.Y),
        static_cast<float>(Bounds.BoxExtent.Z));
    Radius = FMath::Max(Radius, static_cast<float>(Bounds.SphereRadius));
    Radius = FMath::Max(Radius, 32.0f);

    float AspectToUse = AspectRatio;
    if (Viewport != nullptr)
    {
        const FIntPoint ViewportSize = Viewport->GetSizeXY();
        if (ViewportSize.X > 0 && ViewportSize.Y > 0)
        {
            AspectToUse = Viewport->GetDesiredAspectRatio();
        }
    }

    if (AspectToUse > 1.0f)
    {
        Radius *= AspectToUse;
    }

    const float HalfFOVRadians = FMath::DegreesToRadians(FMath::Max(ViewFOV, 5.0f) * 0.5f);
    const float DistanceToCamera = (Radius / FMath::Tan(HalfFOVRadians)) * 1.15f;
    ToggleOrbitCamera(true);
    SetViewLocationForOrbiting(Bounds.Origin, DistanceToCamera);
    Invalidate();
}

void FWetWrinkleAssetViewportClient::RequestFocusOnPreviewMeshNextTick(const USkeletalMeshComponent* InPreviewMeshComponent)
{
    PendingFocusMeshComponent = InPreviewMeshComponent;
    bFocusPreviewMeshOnNextTick = InPreviewMeshComponent != nullptr;
    Invalidate();
}

void FWetWrinkleAssetViewportClient::SetPreviewMeshComponent(const USkeletalMeshComponent* InPreviewMeshComponent)
{
    PreviewMeshComponent = InPreviewMeshComponent;
}

void FWetWrinkleAssetViewportClient::UpdateSurfaceHitUnderCursor()
{
    if (Viewport == nullptr || PreviewMeshComponent.Get() == nullptr)
    {
        ClearSurfaceHit();
        return;
    }

    const FViewportCursorLocation Cursor = GetCursorWorldLocationFromMousePos();

    if (const TSharedPtr<SWetWrinkleAssetViewport> PinnedViewport = ViewportWidget.Pin())
    {
        FWetWrinkleSurfaceHit SurfaceHit;
        if (PinnedViewport->TraceSurface(Cursor.GetOrigin(), Cursor.GetDirection(), SurfaceHit))
        {
            PinnedViewport->HandleSurfaceHitFromClient(SurfaceHit);
            bHasCurrentSurfaceHit = true;
            return;
        }
    }

    ClearSurfaceHit();
}

void FWetWrinkleAssetViewportClient::ClearSurfaceHit()
{
    if (!bHasCurrentSurfaceHit)
    {
        return;
    }

    bHasCurrentSurfaceHit = false;

    if (const TSharedPtr<SWetWrinkleAssetViewport> PinnedViewport = ViewportWidget.Pin())
    {
        PinnedViewport->HandleSurfaceHitFromClient(FWetWrinkleSurfaceHit());
    }
}
