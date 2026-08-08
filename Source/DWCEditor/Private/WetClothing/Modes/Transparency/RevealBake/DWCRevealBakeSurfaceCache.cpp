// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "WetClothing/Modes/Transparency/RevealBake/DWCRevealBakeSurfaceCache.h"

uint32 GetTypeHash(const FDWCRevealBakeSurfaceCacheKey& Key)
{
    uint32 Hash = ::GetTypeHash(Key.LayerIndex);
    Hash = HashCombine(Hash, ::GetTypeHash(Key.LODIndex));
    Hash = HashCombine(Hash, ::GetTypeHash(Key.UVChannelIndex));
    return Hash;
}

const FDWCRevealBakeSurface* FDWCRevealBakeSurfaceCache::FindOrBuild(
    const FDWCBakeResolvedLayer& Layer,
    const int32                  LayerIndex,
    const int32                  LODIndex,
    const int32                  UVChannelIndex,
    FString&                     OutErrorMessage)
{
    const FDWCRevealBakeSurfaceCacheKey Key{ LayerIndex, LODIndex, UVChannelIndex };
    if (const FDWCRevealBakeSurface* CachedSurface = Surfaces.Find(Key))
    {
        return CachedSurface;
    }

    FDWCRevealBakeSurface BuiltSurface;
    if (!FDWCRevealBakeSurfaceBuilder::BuildReferencePoseSurface(
            Layer,
            LODIndex,
            UVChannelIndex,
            BuiltSurface,
            &OutErrorMessage))
    {
        return nullptr;
    }

    return &Surfaces.Add(Key, MoveTemp(BuiltSurface));
}
