// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Cache/DWCEditorCacheStore.h"
#include "WetClothing/Foundation/MeshAnalysis/WetClothingAssetMeshAnalyzer.h"

class UWetClothingAsset;

struct FDWCPartPickTriangle
{
    FVector3f Positions[3] = { FVector3f::ZeroVector, FVector3f::ZeroVector, FVector3f::ZeroVector };
    FVector3f Centroid = FVector3f::ZeroVector;
    FBox3f    Bounds = FBox3f(ForceInit);
    int32     UVIslandID = INDEX_NONE;
};

struct FDWCPartPickBVHNode
{
    FBox3f Bounds = FBox3f(ForceInit);
    int32  LeftChild = INDEX_NONE;
    int32  RightChild = INDEX_NONE;
    int32  FirstTriangle = 0;
    int32  TriangleCount = 0;

    bool IsLeaf() const { return LeftChild == INDEX_NONE && RightChild == INDEX_NONE; }
};

/** Immutable LOD topology shared by the Wet Part list, UV view and viewport hit test. */
class FDWCPartTopologyCacheValue final : public IDWCEditorCacheValue
{
  public:
    static FName StaticCacheTypeName();
    virtual FName GetCacheTypeName() const override { return StaticCacheTypeName(); }
    virtual uint64 GetAllocatedSizeBytes() const override;

    TArray<TSharedPtr<FWetClothingAssetUVIsland>> Islands;
    TArray<FDWCPartPickTriangle>                   PickTriangles;
    TArray<int32>                                  PickTriangleIndices;
    TArray<FDWCPartPickBVHNode>                    PickBVHNodes;
};

/** Builds and leases one canonical Wet Part topology entry from the WCA editor cache. */
class FDWCPartTopologyCache
{
  public:
    static constexpr int32 AuthoringLODIndex = 0;

    static bool Acquire(
        const TSharedPtr<FDWCEditorCacheStore>& CacheStore,
        const UWetClothingAsset*                WetClothingAsset,
        int32                                   UVChannelIndex,
        int32                                   MaterialSlotIndex,
        FDWCEditorCacheKey&                      OutKey,
        FDWCEditorCacheLease&                    OutLease,
        FString*                                 OutErrorMessage = nullptr);

    static FDWCEditorCacheKey BuildKey(
        const UWetClothingAsset& WetClothingAsset,
        int32                    UVChannelIndex,
        int32                    MaterialSlotIndex);
};
