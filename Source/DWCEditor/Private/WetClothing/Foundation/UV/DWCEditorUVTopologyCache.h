// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Cache/DWCEditorCacheStore.h"
#include "WetClothing/Foundation/MeshAnalysis/WetClothingAssetMeshAnalyzer.h"

class USkeletalMesh;
class UWetClothingAsset;

struct FDWCEditorUVPickTriangle
{
    FVector3f Positions[3] = {
        FVector3f::ZeroVector,
        FVector3f::ZeroVector,
        FVector3f::ZeroVector
    };
    FVector3f Centroid = FVector3f::ZeroVector;
    FBox3f Bounds = FBox3f(ForceInit);
    int32 UVIslandID = INDEX_NONE;
};

struct FDWCEditorUVPickBVHNode
{
    FBox3f Bounds = FBox3f(ForceInit);
    int32 LeftChild = INDEX_NONE;
    int32 RightChild = INDEX_NONE;
    int32 FirstTriangle = 0;
    int32 TriangleCount = 0;

    bool IsLeaf() const { return LeftChild == INDEX_NONE && RightChild == INDEX_NONE; }
};

/**
 * Immutable UV topology shared by UV views, slot presentation and viewport hit testing.
 * The cache lease, rather than individual widgets, owns the payload lifetime.
 */
class FDWCEditorUVTopologyCacheValue final : public IDWCEditorCacheValue
{
  public:
    static FName StaticCacheTypeName();
    virtual FName GetCacheTypeName() const override { return StaticCacheTypeName(); }
    virtual uint64 GetAllocatedSizeBytes() const override;

    TArray<TSharedPtr<FWetClothingAssetUVIsland>> Islands;
    TArray<FDWCEditorUVPickTriangle> PickTriangles;
    TArray<int32> PickTriangleIndices;
    TArray<FDWCEditorUVPickBVHNode> PickBVHNodes;
};

/** Brokered, per-WCA-editor UV topology cache backed by FDWCEditorCacheStore. */
class FDWCEditorUVTopologyCache
{
  public:
    static constexpr int32 AuthoringLODIndex = 0;

    static FName CacheNamespace();

    static bool AcquireForAsset(
        const TSharedPtr<FDWCEditorCacheStore>& CacheStore,
        const UWetClothingAsset* OwnerAsset,
        int32 UVChannelIndex,
        int32 MaterialSlotIndex,
        FDWCEditorCacheKey& OutKey,
        FDWCEditorCacheLease& OutLease,
        FString* OutErrorMessage = nullptr);

    static bool AcquireForMesh(
        const TSharedPtr<FDWCEditorCacheStore>& CacheStore,
        const UWetClothingAsset* OwnerAsset,
        const USkeletalMesh* Mesh,
        int32 LODIndex,
        int32 UVChannelIndex,
        int32 MaterialSlotIndex,
        FDWCEditorCacheKey& OutKey,
        FDWCEditorCacheLease& OutLease,
        FString* OutErrorMessage = nullptr);

    static FDWCEditorCacheKey BuildKey(
        const UObject& Owner,
        const USkeletalMesh& Mesh,
        int32 LODIndex,
        int32 UVChannelIndex,
        int32 MaterialSlotIndex,
        uint64 TopologyRevision);
};
