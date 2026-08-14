// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/UV/DWCEditorUVTopologyCache.h"

class UWetClothingAsset;

// Compatibility aliases keep Part viewport code source-compatible while the
// payload itself is owned by the shared editor UV topology cache.
using FDWCPartPickTriangle = FDWCEditorUVPickTriangle;
using FDWCPartPickBVHNode = FDWCEditorUVPickBVHNode;
using FDWCPartTopologyCacheValue = FDWCEditorUVTopologyCacheValue;

/** Wet Part facade over the canonical brokered UV topology cache. */
class FDWCPartTopologyCache
{
  public:
    static constexpr int32 AuthoringLODIndex = FDWCEditorUVTopologyCache::AuthoringLODIndex;

    static bool Acquire(
        const TSharedPtr<FDWCEditorCacheStore>& CacheStore,
        const UWetClothingAsset* WetClothingAsset,
        int32 UVChannelIndex,
        int32 MaterialSlotIndex,
        FDWCEditorCacheKey& OutKey,
        FDWCEditorCacheLease& OutLease,
        FString* OutErrorMessage = nullptr);

    static FDWCEditorCacheKey BuildKey(
        const UWetClothingAsset& WetClothingAsset,
        int32 UVChannelIndex,
        int32 MaterialSlotIndex);
};
