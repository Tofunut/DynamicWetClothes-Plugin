#pragma once

#include "Bake/DWCBakeSurface.h"
#include "CoreMinimal.h"
#include "DataAssets/DWCBakeLayer.h"

class FDWCRevealBakeSurfaceCache;

class FDWCRevealBakeSurfaceResolver
{
  public:
    static TArray<FDWCBakeSurface> BuildSourceSurfacesForOuter(
        const FDWCBakeSnapshot&      Snapshot,
        const FDWCBakeResolvedLayer& OuterLayer,
        FDWCRevealBakeSurfaceCache&  SurfaceCache,
        FString&                     OutErrorMessage);

    static int32 CountTriangles(const TArray<FDWCBakeSurface>& Surfaces);
};
