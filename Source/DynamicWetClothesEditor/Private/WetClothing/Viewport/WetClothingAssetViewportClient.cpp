#include "WetClothingAssetViewportClient.h"

#include "AdvancedPreviewScene.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"
#include "SceneView.h"
#include "SEditorViewport.h"
#include "WetClothingAssetViewport.h"

FWetClothingAssetViewportClient::FWetClothingAssetViewportClient(
    FAdvancedPreviewScene*                         InPreviewScene,
    const TSharedRef<SWetClothingAssetViewport>& InViewportWidget)
    : FEditorViewportClient(
          nullptr,
          InPreviewScene,
          StaticCastSharedRef<SEditorViewport>(InViewportWidget)),
      PreviewScene(InPreviewScene), ViewportWidget(InViewportWidget)
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

void FWetClothingAssetViewportClient::Tick(float DeltaSeconds)
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

void FWetClothingAssetViewportClient::ProcessClick(FSceneView& View, HHitProxy* HitProxy, FKey Key, EInputEvent Event, uint32 HitX, uint32 HitY)
{
    FEditorViewportClient::ProcessClick(View, HitProxy, Key, Event, HitX, HitY);

    if (Key != EKeys::LeftMouseButton || Event != IE_Released)
    {
        return;
    }

    const USkeletalMeshComponent* MeshComponent = PreviewMeshComponent.Get();
    if (MeshComponent == nullptr || PickableIslands.Num() == 0)
    {
        return;
    }

    FVector RayOrigin = FVector::ZeroVector;
    FVector RayDirection = FVector::ForwardVector;
    View.DeprojectFVector2D(FVector2D(HitX, HitY), RayOrigin, RayDirection);

    const FVector    RayEnd = RayOrigin + RayDirection * 1000000.0f;
    const FTransform ComponentTransform = MeshComponent->GetComponentTransform();

    int32  PickedIslandID = INDEX_NONE;
    double ClosestDistanceSq = TNumericLimits<double>::Max();

    for (const FWetClothingAssetUVIsland& Island : PickableIslands)
    {
        for (const FWetClothingAssetUVTriangle& Triangle : Island.UVTriangles)
        {
            const FVector WorldA = ComponentTransform.TransformPosition(Triangle.LocalPositions[0]);
            const FVector WorldB = ComponentTransform.TransformPosition(Triangle.LocalPositions[1]);
            const FVector WorldC = ComponentTransform.TransformPosition(Triangle.LocalPositions[2]);

            FVector IntersectionPoint = FVector::ZeroVector;
            FVector TriangleNormal = FVector::ZeroVector;
            if (!FMath::SegmentTriangleIntersection(RayOrigin, RayEnd, WorldA, WorldB, WorldC, IntersectionPoint, TriangleNormal))
            {
                continue;
            }

            const double DistanceSq = FVector::DistSquared(RayOrigin, IntersectionPoint);
            if (DistanceSq < ClosestDistanceSq)
            {
                ClosestDistanceSq = DistanceSq;
                PickedIslandID = Island.IslandID;
            }
        }
    }

    if (PickedIslandID != INDEX_NONE)
    {
        const bool bAppendSelection = Viewport != nullptr && (Viewport->KeyState(EKeys::LeftShift) || Viewport->KeyState(EKeys::RightShift));
        if (const TSharedPtr<SWetClothingAssetViewport> PinnedViewport = ViewportWidget.Pin())
        {
            PinnedViewport->HandleIslandPickedFromClient(PickedIslandID, bAppendSelection);
        }
    }
}

void FWetClothingAssetViewportClient::FocusOnPreviewMesh(const USkeletalMeshComponent* InPreviewMeshComponent, bool bInstant)
{
    if (InPreviewMeshComponent == nullptr || InPreviewMeshComponent->GetSkeletalMeshAsset() == nullptr)
    {
        return;
    }

    const FBoxSphereBounds Bounds = InPreviewMeshComponent->CalcBounds(InPreviewMeshComponent->GetComponentTransform());
    FocusViewportOnBox(Bounds.GetBox(), bInstant);
}

void FWetClothingAssetViewportClient::RequestFocusOnPreviewMeshNextTick(const USkeletalMeshComponent* InPreviewMeshComponent)
{
    PendingFocusMeshComponent = InPreviewMeshComponent;
    bFocusPreviewMeshOnNextTick = InPreviewMeshComponent != nullptr;
    Invalidate();
}

void FWetClothingAssetViewportClient::SetPreviewMeshComponent(const USkeletalMeshComponent* InPreviewMeshComponent)
{
    PreviewMeshComponent = InPreviewMeshComponent;
}

void FWetClothingAssetViewportClient::SetPickableIslands(const TArray<FWetClothingAssetUVIsland>& InIslands)
{
    PickableIslands = InIslands;
}
