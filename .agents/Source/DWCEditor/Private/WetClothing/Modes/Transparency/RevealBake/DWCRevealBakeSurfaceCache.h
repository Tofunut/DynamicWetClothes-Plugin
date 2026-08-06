#pragma once

#include "WetClothing/Modes/Transparency/RevealBake/DWCRevealBakeSurface.h"
#include "CoreMinimal.h"

struct FDWCRevealBakeSurfaceCacheKey
{
    int32 LayerIndex = INDEX_NONE;
    int32 LODIndex = 0;
    int32 UVChannelIndex = 0;

    bool operator==(const FDWCRevealBakeSurfaceCacheKey& Other) const
    {
        return LayerIndex == Other.LayerIndex &&
               LODIndex == Other.LODIndex &&
               UVChannelIndex == Other.UVChannelIndex;
    }
};

uint32 GetTypeHash(const FDWCRevealBakeSurfaceCacheKey& Key);

class FDWCRevealBakeSurfaceCache
{
  public:
    const FDWCRevealBakeSurface* FindOrBuild(
        const FDWCBakeResolvedLayer& Layer,
        int32                       LayerIndex,
        int32                       LODIndex,
        int32                       UVChannelIndex,
        FString&                    OutErrorMessage);

  private:
    TMap<FDWCRevealBakeSurfaceCacheKey, FDWCRevealBakeSurface> Surfaces;
};
