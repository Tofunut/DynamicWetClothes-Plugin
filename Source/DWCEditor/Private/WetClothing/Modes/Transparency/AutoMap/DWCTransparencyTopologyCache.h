// Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

class FDWCEditorCacheStore;
class UWetClothingAsset;
struct FDWCBakeResolvedLayer;
struct FDWCRevealBakeSurface;

/**
 * Resolves immutable reference-pose topology through the WCA session cache.
 * Slot-specific surfaces are copied into operation snapshots, so cache
 * eviction can never invalidate an active Stage 2 operation.
 */
class FDWCTransparencyTopologyCache final
{
public:
    static bool BuildSlotSurface(
        const UWetClothingAsset& OwnerAsset,
        const FDWCBakeResolvedLayer& Layer,
        int32 LODIndex,
        int32 UVChannelIndex,
        int32 MaterialSlotIndex,
        const TSharedPtr<FDWCEditorCacheStore>& CacheStore,
        FDWCRevealBakeSurface& OutSurface,
        FString& OutError,
        bool* bOutCacheHit = nullptr);
};
