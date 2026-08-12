// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "DWCPartViewportClient.h"

#include "AdvancedPreviewScene.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"
#include "SceneView.h"
#include "SEditorViewport.h"
#include "SDWCPartViewport.h"
#include "WetClothing/Foundation/Diagnostics/DWCEditorMemoryDiagnostics.h"
#include "WetClothing/Modes/DWCPreviewViewportToolbarUtils.h"
#include "WetClothing/Modes/Part/Topology/DWCPartTopologyCache.h"

FDWCPartViewportClient::FDWCPartViewportClient(
    FAdvancedPreviewScene*              InPreviewScene,
    const TSharedRef<SDWCPartViewport>& InViewportWidget)
    : FEditorViewportClient(
          nullptr,
          InPreviewScene,
          StaticCastSharedRef<SEditorViewport>(InViewportWidget)),
      PreviewScene(InPreviewScene), ViewportWidget(InViewportWidget)
{
    SetViewMode(VMI_Lit);
    SetRealtime(false);
    ViewFOV = 65.0f;
    FOVAngle = 65.0f;

    SetViewLocation(FVector(250.0f, 0.0f, 120.0f));
    SetViewRotation(FRotator(-20.0f, 180.0f, 0.0f));
    UE::DWCEditor::ApplyDWCPreviewCameraSpeedSettings(*this);

    EngineShowFlags.SetGrid(true);
    EngineShowFlags.SetSelectionOutline(true);
    EngineShowFlags.SetCompositeEditorPrimitives(true);

    bSetListenerPosition = false;
    bUsingOrbitCamera = true;
}

void FDWCPartViewportClient::Tick(float DeltaSeconds)
{
    if (bPreviewPaused)
    {
        return;
    }

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

void FDWCPartViewportClient::ProcessClick(FSceneView& View, HHitProxy* HitProxy, FKey Key, EInputEvent Event, uint32 HitX, uint32 HitY)
{
    if (bPreviewPaused)
    {
        return;
    }

    FEditorViewportClient::ProcessClick(View, HitProxy, Key, Event, HitX, HitY);

    if (Key != EKeys::LeftMouseButton || Event != IE_Released)
    {
        return;
    }

    const USkeletalMeshComponent* MeshComponent = PreviewMeshComponent.Get();
    const FDWCPartTopologyCacheValue* Topology =
        ActivePickTopologyLease.GetAs<FDWCPartTopologyCacheValue>();
    if (MeshComponent == nullptr || Topology == nullptr ||
        Topology->PickTriangles.IsEmpty() || Topology->PickBVHNodes.IsEmpty())
    {
        return;
    }

    FVector RayOrigin = FVector::ZeroVector;
    FVector RayDirection = FVector::ForwardVector;
    View.DeprojectFVector2D(FVector2D(HitX, HitY), RayOrigin, RayDirection);

    const FVector    RayEnd = RayOrigin + RayDirection * 1000000.0f;
    const FTransform ComponentTransform = MeshComponent->GetComponentTransform();
    const FVector    LocalRayOrigin = ComponentTransform.InverseTransformPosition(RayOrigin);
    const FVector    LocalRayEnd = ComponentTransform.InverseTransformPosition(RayEnd);
    const FVector    LocalRayDirection = LocalRayEnd - LocalRayOrigin;

    int32  PickedUVIslandID = INDEX_NONE;
    double ClosestDistanceSq = TNumericLimits<double>::Max();

    TArray<int32, TInlineAllocator<64>> NodeStack;
    NodeStack.Add(0);
    while (!NodeStack.IsEmpty())
    {
        const int32 NodeIndex = NodeStack.Pop(EAllowShrinking::No);
        if (!Topology->PickBVHNodes.IsValidIndex(NodeIndex))
        {
            continue;
        }

        const FDWCPartPickBVHNode& Node = Topology->PickBVHNodes[NodeIndex];
        const FBox NodeBounds(FVector(Node.Bounds.Min), FVector(Node.Bounds.Max));
        if (!Node.Bounds.IsValid ||
            !FMath::LineBoxIntersection(
                NodeBounds,
                LocalRayOrigin,
                LocalRayEnd,
                LocalRayDirection))
        {
            continue;
        }

        if (!Node.IsLeaf())
        {
            if (Node.LeftChild != INDEX_NONE)
            {
                NodeStack.Add(Node.LeftChild);
            }
            if (Node.RightChild != INDEX_NONE)
            {
                NodeStack.Add(Node.RightChild);
            }
            continue;
        }

        const int32 EndTriangle = Node.FirstTriangle + Node.TriangleCount;
        for (int32 OrderedIndex = Node.FirstTriangle; OrderedIndex < EndTriangle; ++OrderedIndex)
        {
            if (!Topology->PickTriangleIndices.IsValidIndex(OrderedIndex))
            {
                continue;
            }
            const int32 TriangleIndex = Topology->PickTriangleIndices[OrderedIndex];
            if (!Topology->PickTriangles.IsValidIndex(TriangleIndex))
            {
                continue;
            }

            const FDWCPartPickTriangle& Triangle = Topology->PickTriangles[TriangleIndex];
            FVector              LocalIntersectionPoint = FVector::ZeroVector;
            FVector              TriangleNormal = FVector::ZeroVector;
            if (!FMath::SegmentTriangleIntersection(
                    LocalRayOrigin,
                    LocalRayEnd,
                    FVector(Triangle.Positions[0]),
                    FVector(Triangle.Positions[1]),
                    FVector(Triangle.Positions[2]),
                    LocalIntersectionPoint,
                    TriangleNormal))
            {
                continue;
            }

            const double DistanceSq = FVector::DistSquared(LocalRayOrigin, LocalIntersectionPoint);
            if (DistanceSq < ClosestDistanceSq)
            {
                ClosestDistanceSq = DistanceSq;
                PickedUVIslandID = Triangle.UVIslandID;
            }
        }
    }

    if (PickedUVIslandID != INDEX_NONE)
    {
        const bool bAppendSelection = Viewport != nullptr && (Viewport->KeyState(EKeys::LeftShift) || Viewport->KeyState(EKeys::RightShift));
        if (const TSharedPtr<SDWCPartViewport> PinnedViewport = ViewportWidget.Pin())
        {
            PinnedViewport->HandleIslandPickedFromClient(PickedUVIslandID, bAppendSelection);
        }
    }
}

void FDWCPartViewportClient::FocusOnPreviewMesh(const USkeletalMeshComponent* InPreviewMeshComponent, bool bInstant)
{
    if (InPreviewMeshComponent == nullptr || InPreviewMeshComponent->GetSkeletalMeshAsset() == nullptr)
    {
        return;
    }

    const FBoxSphereBounds Bounds = InPreviewMeshComponent->CalcBounds(InPreviewMeshComponent->GetComponentTransform());
    float                  Radius = FMath::Max3(
        static_cast<float>(Bounds.BoxExtent.X),
        static_cast<float>(Bounds.BoxExtent.Y),
        static_cast<float>(Bounds.BoxExtent.Z));
    Radius = FMath::Max(Radius, static_cast<float>(Bounds.SphereRadius));
    Radius = FMath::Max(Radius, static_cast<float>(MinimumFocusRadius));

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

void FDWCPartViewportClient::RequestFocusOnPreviewMeshNextTick(const USkeletalMeshComponent* InPreviewMeshComponent)
{
    PendingFocusMeshComponent = InPreviewMeshComponent;
    bFocusPreviewMeshOnNextTick = InPreviewMeshComponent != nullptr;
    Invalidate();
}

void FDWCPartViewportClient::SetPreviewMeshComponent(const USkeletalMeshComponent* InPreviewMeshComponent)
{
    PreviewMeshComponent = InPreviewMeshComponent;
}

void FDWCPartViewportClient::SetPickableTopology(FDWCEditorCacheLease&& InTopologyLease)
{
    ActivePickTopologyLease = MoveTemp(InTopologyLease);
}

void FDWCPartViewportClient::ClearPickableIslandCache()
{
    ActivePickTopologyLease.Reset();
}

void FDWCPartViewportClient::SetPreviewPaused(const bool bInPaused)
{
    bPreviewPaused = bInPaused;
}

void FDWCPartViewportClient::CollectMemoryDiagnostics(
    TArray<FDWCEditorMemoryOwnerRecord>& OutOwners) const
{
    const FDWCPartTopologyCacheValue* Topology =
        ActivePickTopologyLease.GetAs<FDWCPartTopologyCacheValue>();
    const uint64 Bytes = ActivePickTopologyLease.GetAllocatedSizeBytes();
    const int32 TriangleCount = Topology != nullptr ? Topology->PickTriangles.Num() : 0;
    const int32 NodeCount = Topology != nullptr ? Topology->PickBVHNodes.Num() : 0;

    FDWCEditorMemoryOwnerRecord& Owner = OutOwners.AddDefaulted_GetRef();
    Owner.Identifier = FString::Printf(TEXT("WetPartViewportClient.%p.PickBVH"), this);
    Owner.Subsystem = TEXT("WetPart");
    Owner.Resource = TEXT("PickBVH");
    Owner.Category = EDWCEditorMemoryCategory::SharedCacheCPU;
    Owner.Accounting = EDWCEditorMemoryAccounting::Resident;
    // The shared cache store owns and reports the retained bytes. The viewport only
    // reports that it currently holds a lease so global diagnostics do not double-count.
    Owner.CurrentBytes = 0;
    Owner.EntryCount = ActivePickTopologyLease.IsValid() ? 1 : 0;
    Owner.Context = FString::Printf(
        TEXT("leasedTriangles=%d; leasedNodes=%d; leasedBytes=%.2f MiB; activeLeases=%u"),
        TriangleCount,
        NodeCount,
        static_cast<double>(Bytes) / (1024.0 * 1024.0),
        ActivePickTopologyLease.GetActiveLeaseCount());
}
