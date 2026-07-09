#include "WetClothing/TransparencyBake/RevealBake/DWCRevealBakeSurfaceCache.h"

#include "WetClothing/TransparencyBake/RevealBake/DWCRevealBakeLog.h"
#include "WetClothing/TransparencyBake/RevealBake/DWCRevealBakeUtilities.h"

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
        UE_LOG(
            LogDWCRevealBake,
            Log,
            TEXT("DWC Reveal Bake: Surface cache hit. Layer='%s', LOD=%d, UV=%d."),
            *Layer.LayerId.ToString(),
            LODIndex,
            UVChannelIndex);
        return CachedSurface;
    }

    const double BuildStartTime = FPlatformTime::Seconds();
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

    UE_LOG(
        LogDWCRevealBake,
        Log,
        TEXT("DWC Reveal Bake: Surface build. Layer='%s', LOD=%d, UV=%d, Triangles=%d, Time=%.2f ms."),
        *Layer.LayerId.ToString(),
        LODIndex,
        UVChannelIndex,
        BuiltSurface.Triangles.Num(),
        FDWCRevealBakeUtilities::GetElapsedMilliseconds(BuildStartTime));

    return &Surfaces.Add(Key, MoveTemp(BuiltSurface));
}
