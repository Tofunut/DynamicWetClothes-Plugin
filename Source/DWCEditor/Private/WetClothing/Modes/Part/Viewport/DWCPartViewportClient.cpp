//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "DWCPartViewportClient.h"

#include "AdvancedPreviewScene.h"
#include "Algo/Sort.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"
#include "SceneView.h"
#include "SEditorViewport.h"
#include "SDWCPartViewport.h"
#include "WetClothing/Modes/DWCPreviewViewportToolbarUtils.h"

FDWCPartViewportClient::FDWCPartViewportClient(
    FAdvancedPreviewScene*                       InPreviewScene,
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
    if (MeshComponent == nullptr || PickTriangles.IsEmpty() || PickBVHNodes.IsEmpty())
    {
        return;
    }

    FVector RayOrigin = FVector::ZeroVector;
    FVector RayDirection = FVector::ForwardVector;
    View.DeprojectFVector2D(FVector2D(HitX, HitY), RayOrigin, RayDirection);

    const FVector RayEnd = RayOrigin + RayDirection * 1000000.0f;
    const FTransform ComponentTransform = MeshComponent->GetComponentTransform();
    const FVector LocalRayOrigin = ComponentTransform.InverseTransformPosition(RayOrigin);
    const FVector LocalRayEnd = ComponentTransform.InverseTransformPosition(RayEnd);
    const FVector LocalRayDirection = LocalRayEnd - LocalRayOrigin;

    int32 PickedUVIslandID = INDEX_NONE;
    double ClosestDistanceSq = TNumericLimits<double>::Max();

    TArray<int32, TInlineAllocator<64>> NodeStack;
    NodeStack.Add(0);
    while (!NodeStack.IsEmpty())
    {
        const int32 NodeIndex = NodeStack.Pop(EAllowShrinking::No);
        if (!PickBVHNodes.IsValidIndex(NodeIndex))
        {
            continue;
        }

        const FPickBVHNode& Node = PickBVHNodes[NodeIndex];
        if (!Node.Bounds.IsValid ||
            !FMath::LineBoxIntersection(
                Node.Bounds,
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
            if (!PickTriangleIndices.IsValidIndex(OrderedIndex))
            {
                continue;
            }
            const int32 TriangleIndex = PickTriangleIndices[OrderedIndex];
            if (!PickTriangles.IsValidIndex(TriangleIndex))
            {
                continue;
            }

            const FPickTriangle& Triangle = PickTriangles[TriangleIndex];
            FVector LocalIntersectionPoint = FVector::ZeroVector;
            FVector TriangleNormal = FVector::ZeroVector;
            if (!FMath::SegmentTriangleIntersection(
                    LocalRayOrigin,
                    LocalRayEnd,
                    Triangle.Positions[0],
                    Triangle.Positions[1],
                    Triangle.Positions[2],
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
    Radius = FMath::Max(Radius, MinimumFocusRadius);

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

int32 FDWCPartViewportClient::BuildPickBVHNode(
    const int32 FirstTriangle,
    const int32 TriangleCount)
{
    const int32 NodeIndex = PickBVHNodes.AddDefaulted();
    FPickBVHNode& Node = PickBVHNodes[NodeIndex];
    Node.FirstTriangle = FirstTriangle;
    Node.TriangleCount = TriangleCount;

    FBox Bounds(ForceInit);
    FBox CentroidBounds(ForceInit);
    for (int32 OrderedIndex = FirstTriangle; OrderedIndex < FirstTriangle + TriangleCount; ++OrderedIndex)
    {
        const FPickTriangle& Triangle = PickTriangles[PickTriangleIndices[OrderedIndex]];
        Bounds += Triangle.Bounds;
        CentroidBounds += Triangle.Centroid;
    }
    Node.Bounds = Bounds;

    constexpr int32 MaxTrianglesPerLeaf = 12;
    if (TriangleCount <= MaxTrianglesPerLeaf || !CentroidBounds.IsValid)
    {
        return NodeIndex;
    }

    const FVector Extent = CentroidBounds.GetExtent();
    int32 SplitAxis = 0;
    if (Extent.Y > Extent.X && Extent.Y >= Extent.Z)
    {
        SplitAxis = 1;
    }
    else if (Extent.Z > Extent.X && Extent.Z > Extent.Y)
    {
        SplitAxis = 2;
    }

    TArrayView<int32> OrderedView(
        PickTriangleIndices.GetData() + FirstTriangle,
        TriangleCount);
    Algo::Sort(
        OrderedView,
        [this, SplitAxis](const int32 A, const int32 B)
        {
            return PickTriangles[A].Centroid[SplitAxis] < PickTriangles[B].Centroid[SplitAxis];
        });

    const int32 LeftCount = TriangleCount / 2;
    const int32 RightCount = TriangleCount - LeftCount;
    if (LeftCount <= 0 || RightCount <= 0)
    {
        return NodeIndex;
    }

    const int32 LeftChild = BuildPickBVHNode(FirstTriangle, LeftCount);
    const int32 RightChild = BuildPickBVHNode(FirstTriangle + LeftCount, RightCount);
    PickBVHNodes[NodeIndex].LeftChild = LeftChild;
    PickBVHNodes[NodeIndex].RightChild = RightChild;
    PickBVHNodes[NodeIndex].TriangleCount = 0;
    return NodeIndex;
}

void FDWCPartViewportClient::RebuildPickBVH(
    const TArray<FWetClothingAssetUVIsland>& InIslands)
{
    PickTriangles.Reset();
    PickTriangleIndices.Reset();
    PickBVHNodes.Reset();

    int32 TriangleReserveCount = 0;
    for (const FWetClothingAssetUVIsland& Island : InIslands)
    {
        TriangleReserveCount += Island.UVTriangles.Num();
    }
    PickTriangles.Reserve(TriangleReserveCount);

    for (const FWetClothingAssetUVIsland& Island : InIslands)
    {
        for (const FWetClothingAssetUVTriangle& SourceTriangle : Island.UVTriangles)
        {
            FPickTriangle& Triangle = PickTriangles.AddDefaulted_GetRef();
            Triangle.UVIslandID = Island.UVIslandID;
            Triangle.Bounds = FBox(ForceInit);
            for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
            {
                Triangle.Positions[CornerIndex] = SourceTriangle.LocalPositions[CornerIndex];
                Triangle.Bounds += Triangle.Positions[CornerIndex];
            }
            Triangle.Centroid =
                (Triangle.Positions[0] + Triangle.Positions[1] + Triangle.Positions[2]) / 3.0;
        }
    }

    PickTriangleIndices.Reserve(PickTriangles.Num());
    for (int32 TriangleIndex = 0; TriangleIndex < PickTriangles.Num(); ++TriangleIndex)
    {
        PickTriangleIndices.Add(TriangleIndex);
    }
    if (!PickTriangles.IsEmpty())
    {
        BuildPickBVHNode(0, PickTriangles.Num());
    }
}

void FDWCPartViewportClient::SetPickableIslands(
    const TArray<FWetClothingAssetUVIsland>& InIslands,
    const uint32 TopologyCacheKey)
{
    if (TopologyCacheKey != 0 && ActivePickTopologyCacheKey == TopologyCacheKey)
    {
        return;
    }

    if (TopologyCacheKey != 0)
    {
        if (const FPickBVHCacheEntry* Cached = PickBVHCache.Find(TopologyCacheKey))
        {
            PickTriangles = Cached->Triangles;
            PickTriangleIndices = Cached->TriangleIndices;
            PickBVHNodes = Cached->Nodes;
            ActivePickTopologyCacheKey = TopologyCacheKey;
            return;
        }
    }

    RebuildPickBVH(InIslands);
    ActivePickTopologyCacheKey = TopologyCacheKey;

    if (TopologyCacheKey != 0)
    {
        if (PickBVHCache.Num() >= 8)
        {
            PickBVHCache.Reset();
        }
        FPickBVHCacheEntry& Cached = PickBVHCache.FindOrAdd(TopologyCacheKey);
        Cached.Triangles = PickTriangles;
        Cached.TriangleIndices = PickTriangleIndices;
        Cached.Nodes = PickBVHNodes;
    }
}

void FDWCPartViewportClient::ClearPickableIslandCache()
{
    PickBVHCache.Reset();
    ActivePickTopologyCacheKey = 0;
    PickTriangles.Reset();
    PickTriangleIndices.Reset();
    PickBVHNodes.Reset();
}

void FDWCPartViewportClient::SetPreviewPaused(const bool bInPaused)
{
    bPreviewPaused = bInPaused;
}
