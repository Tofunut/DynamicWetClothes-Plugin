// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "UObject/WeakObjectPtr.h"
#include "CoreMinimal.h"
#include "WetClothing/Foundation/MeshAnalysis/WetClothingAssetMeshAnalyzer.h"
#include "EditorViewportClient.h"

class FAdvancedPreviewScene;
class FSceneView;
class SDWCPartViewport;
class USkeletalMeshComponent;
class HHitProxy;

class FDWCPartViewportClient : public FEditorViewportClient
{
  public:
    FDWCPartViewportClient(
        FAdvancedPreviewScene*              InPreviewScene,
        const TSharedRef<SDWCPartViewport>& InViewportWidget);

    virtual void Tick(float DeltaSeconds) override;
    virtual void ProcessClick(FSceneView& View, HHitProxy* HitProxy, FKey Key, EInputEvent Event, uint32 HitX, uint32 HitY) override;

    void FocusOnPreviewMesh(const USkeletalMeshComponent* InPreviewMeshComponent, bool bInstant = false);
    void RequestFocusOnPreviewMeshNextTick(const USkeletalMeshComponent* InPreviewMeshComponent);
    void SetPreviewMeshComponent(const USkeletalMeshComponent* InPreviewMeshComponent);
    void SetPickableIslands(const TArray<FWetClothingAssetUVIsland>& InIslands, uint32 TopologyCacheKey);
    void ClearPickableIslandCache();
    void SetPreviewPaused(bool bInPaused);

  private:
    struct FPickTriangle
    {
        FVector Positions[3] = { FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector };
        FVector Centroid = FVector::ZeroVector;
        FBox    Bounds = FBox(ForceInit);
        int32   UVIslandID = INDEX_NONE;
    };

    struct FPickBVHNode
    {
        FBox  Bounds = FBox(ForceInit);
        int32 LeftChild = INDEX_NONE;
        int32 RightChild = INDEX_NONE;
        int32 FirstTriangle = 0;
        int32 TriangleCount = 0;

        bool IsLeaf() const { return LeftChild == INDEX_NONE && RightChild == INDEX_NONE; }
    };

    struct FPickBVHCacheEntry
    {
        TArray<FPickTriangle> Triangles;
        TArray<int32>         TriangleIndices;
        TArray<FPickBVHNode>  Nodes;
    };

    int32 BuildPickBVHNode(int32 FirstTriangle, int32 TriangleCount);
    void  RebuildPickBVH(const TArray<FWetClothingAssetUVIsland>& InIslands);

    FAdvancedPreviewScene*                       PreviewScene = nullptr;
    TWeakPtr<SDWCPartViewport>                   ViewportWidget;
    TWeakObjectPtr<const USkeletalMeshComponent> PreviewMeshComponent;
    TWeakObjectPtr<const USkeletalMeshComponent> PendingFocusMeshComponent;
    bool                                         bFocusPreviewMeshOnNextTick = false;
    bool                                         bPreviewPaused = false;
    TArray<FPickTriangle>                        PickTriangles;
    TArray<int32>                                PickTriangleIndices;
    TArray<FPickBVHNode>                         PickBVHNodes;
    TMap<uint32, FPickBVHCacheEntry>             PickBVHCache;
    uint32                                       ActivePickTopologyCacheKey = 0;
};
