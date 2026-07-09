#pragma once

#include "WetClothing/TransparencyBake/RevealBake/DWCRevealBakeSurface.h"
#include "CoreMinimal.h"
#include "DataAssets/DWCBakeLayer.h"

class FDWCRevealBakeSurfaceCache;

class FDWCRevealBakeSurfaceResolver
{
  public:
    static TArray<FDWCRevealBakeSurface> BuildSourceSurfacesForOuter(
        const FDWCBakeSnapshot&      Snapshot,
        const FDWCBakeResolvedLayer& OuterLayer,
        FDWCRevealBakeSurfaceCache&  SurfaceCache,
        FString&                     OutErrorMessage);

    static int32 CountTriangles(const TArray<FDWCRevealBakeSurface>& Surfaces);
};
