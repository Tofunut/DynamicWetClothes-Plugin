#include "WetWrinkleViewportClient.h"

#include "AdvancedPreviewScene.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"
#include "SceneView.h"
#include "SEditorViewport.h"
#include "WetWrinkleViewport.h"

FWetWrinkleViewportClient::FWetWrinkleViewportClient(
    FAdvancedPreviewScene* InPreviewScene,
    const TSharedRef<SWetWrinkleViewport>& InViewportWidget)
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

void FWetWrinkleViewportClient::Tick(float DeltaSeconds)
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

bool FWetWrinkleViewportClient::InputKey(const FInputKeyEventArgs& EventArgs)
{
    if (EventArgs.Key == EKeys::Escape && EventArgs.Event == IE_Pressed && bIsPainting)
    {
        if (const TSharedPtr<SWetWrinkleViewport> PinnedViewport = ViewportWidget.Pin())
        {
            PinnedViewport->CancelPaintStrokeFromClient();
        }
        bIsPainting = false;
        return true;
    }

    const bool bIsLeftMouseButton = EventArgs.Key == EKeys::LeftMouseButton;
    const bool bIsCameraModifierDown =
        Viewport != nullptr &&
        (Viewport->KeyState(EKeys::LeftAlt) || Viewport->KeyState(EKeys::RightAlt));

    if (bIsLeftMouseButton && EventArgs.Event == IE_Released && bIsPainting)
    {
        if (const TSharedPtr<SWetWrinkleViewport> PinnedViewport = ViewportWidget.Pin())
        {
            PinnedViewport->EndPaintStrokeFromClient();
        }

        bIsPainting = false;
        return true;
    }

    if (bIsLeftMouseButton && EventArgs.Event == IE_Pressed && !bIsCameraModifierDown)
    {
        FWetWrinkleSurfaceHit SurfaceHit;
        if (TraceSurfaceUnderCursor(SurfaceHit))
        {
            if (const TSharedPtr<SWetWrinkleViewport> PinnedViewport = ViewportWidget.Pin())
            {
                PinnedViewport->HandleSurfaceHitFromClient(SurfaceHit);
                PinnedViewport->BeginPaintStrokeFromClient(SurfaceHit);
                bHasCurrentSurfaceHit = true;
                bIsPainting = true;
                return true;
            }
        }
    }

    return FEditorViewportClient::InputKey(EventArgs);
}

void FWetWrinkleViewportClient::MouseMove(FViewport* InViewport, int32 X, int32 Y)
{
    FEditorViewportClient::MouseMove(InViewport, X, Y);
    UpdateSurfaceHitUnderCursor();
}

void FWetWrinkleViewportClient::CapturedMouseMove(FViewport* InViewport, int32 X, int32 Y)
{
    if (bIsPainting)
    {
        FWetWrinkleSurfaceHit SurfaceHit;
        if (TraceSurfaceUnderCursor(SurfaceHit))
        {
            if (const TSharedPtr<SWetWrinkleViewport> PinnedViewport = ViewportWidget.Pin())
            {
                PinnedViewport->HandleSurfaceHitFromClient(SurfaceHit);
                PinnedViewport->RequestPaintStampFromClient(SurfaceHit);
                bHasCurrentSurfaceHit = true;
            }
        }
        else
        {
            ClearSurfaceHit();
        }
        return;
    }

    FEditorViewportClient::CapturedMouseMove(InViewport, X, Y);
    UpdateSurfaceHitUnderCursor();
}

void FWetWrinkleViewportClient::ProcessClick(FSceneView& View, HHitProxy* HitProxy, FKey Key, EInputEvent Event, uint32 HitX, uint32 HitY)
{
    FEditorViewportClient::ProcessClick(View, HitProxy, Key, Event, HitX, HitY);

    if (Key == EKeys::LeftMouseButton && Event == IE_Released && bIsPainting)
    {
        if (const TSharedPtr<SWetWrinkleViewport> PinnedViewport = ViewportWidget.Pin())
        {
            PinnedViewport->EndPaintStrokeFromClient();
        }

        bIsPainting = false;
        return;
    }

    if (Key == EKeys::LeftMouseButton && (Event == IE_Pressed || Event == IE_Released))
    {
        FVector RayOrigin = FVector::ZeroVector;
        FVector RayDirection = FVector::ForwardVector;
        View.DeprojectFVector2D(FVector2D(HitX, HitY), RayOrigin, RayDirection);

        if (const TSharedPtr<SWetWrinkleViewport> PinnedViewport = ViewportWidget.Pin())
        {
            FWetWrinkleSurfaceHit SurfaceHit;
            if (PinnedViewport->TraceSurface(RayOrigin, RayDirection, SurfaceHit))
            {
                PinnedViewport->HandleSurfaceHitFromClient(SurfaceHit);
                if (Event == IE_Pressed && !bIsPainting)
                {
                    PinnedViewport->BeginPaintStrokeFromClient(SurfaceHit);
                    bIsPainting = true;
                }
                bHasCurrentSurfaceHit = true;
                return;
            }
        }
    }

    ClearSurfaceHit();
}

void FWetWrinkleViewportClient::Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
    FEditorViewportClient::Draw(View, PDI);

    if (const TSharedPtr<SWetWrinkleViewport> PinnedViewport = ViewportWidget.Pin())
    {
        PinnedViewport->DrawProceduralStrokeGuides(PDI);
    }
}

void FWetWrinkleViewportClient::FocusOnPreviewMesh(const USkeletalMeshComponent* InPreviewMeshComponent, bool bInstant)
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

void FWetWrinkleViewportClient::RequestFocusOnPreviewMeshNextTick(const USkeletalMeshComponent* InPreviewMeshComponent)
{
    PendingFocusMeshComponent = InPreviewMeshComponent;
    bFocusPreviewMeshOnNextTick = InPreviewMeshComponent != nullptr;
    Invalidate();
}

void FWetWrinkleViewportClient::SetPreviewMeshComponent(const USkeletalMeshComponent* InPreviewMeshComponent)
{
    PreviewMeshComponent = InPreviewMeshComponent;
}

void FWetWrinkleViewportClient::UpdateSurfaceHitUnderCursor()
{
    FWetWrinkleSurfaceHit SurfaceHit;
    if (TraceSurfaceUnderCursor(SurfaceHit))
    {
        if (const TSharedPtr<SWetWrinkleViewport> PinnedViewport = ViewportWidget.Pin())
        {
            PinnedViewport->HandleSurfaceHitFromClient(SurfaceHit);
            bHasCurrentSurfaceHit = true;
            return;
        }
    }

    ClearSurfaceHit();
}

bool FWetWrinkleViewportClient::TraceSurfaceUnderCursor(FWetWrinkleSurfaceHit& OutSurfaceHit)
{
    if (Viewport == nullptr || PreviewMeshComponent.Get() == nullptr)
    {
        return false;
    }

    const FViewportCursorLocation Cursor = GetCursorWorldLocationFromMousePos();

    if (const TSharedPtr<SWetWrinkleViewport> PinnedViewport = ViewportWidget.Pin())
    {
        return PinnedViewport->TraceSurface(Cursor.GetOrigin(), Cursor.GetDirection(), OutSurfaceHit);
    }

    return false;
}

void FWetWrinkleViewportClient::ClearSurfaceHit()
{
    if (!bHasCurrentSurfaceHit)
    {
        return;
    }

    bHasCurrentSurfaceHit = false;

    if (const TSharedPtr<SWetWrinkleViewport> PinnedViewport = ViewportWidget.Pin())
    {
        PinnedViewport->HandleSurfaceHitFromClient(FWetWrinkleSurfaceHit());
    }
}
