#include "WetClothing/TransparencyMode/RevealBake/DWCRevealBakeSurfaceResolver.h"

#include "WetClothing/TransparencyMode/RevealBake/DWCRevealBakeSurfaceCache.h"

TArray<FDWCBakeSurface> FDWCRevealBakeSurfaceResolver::BuildSourceSurfacesForOuter(
    const FDWCBakeSnapshot&      Snapshot,
    const FDWCBakeResolvedLayer& OuterLayer,
    FDWCRevealBakeSurfaceCache&  SurfaceCache,
    FString&                     OutErrorMessage)
{
    TArray<FDWCBakeSurface> SourceSurfaces;

    for (int32 LayerIndex = 0; LayerIndex < Snapshot.Layers.Num(); ++LayerIndex)
    {
        const FDWCBakeResolvedLayer& CandidateLayer = Snapshot.Layers[LayerIndex];
        if (CandidateLayer.LayerOrder >= OuterLayer.LayerOrder)
        {
            continue;
        }

        if (!CandidateLayer.bCanBeRevealSource && !CandidateLayer.bBlocksReveal)
        {
            continue;
        }

        const FDWCBakeSurface* SourceSurface = SurfaceCache.FindOrBuild(
            CandidateLayer,
            LayerIndex,
            0,
            CandidateLayer.SourceUVChannel,
            OutErrorMessage);
        if (SourceSurface == nullptr)
        {
            return TArray<FDWCBakeSurface>();
        }

        SourceSurfaces.Add(*SourceSurface);
    }

    return SourceSurfaces;
}

int32 FDWCRevealBakeSurfaceResolver::CountTriangles(const TArray<FDWCBakeSurface>& Surfaces)
{
    int32 TriangleCount = 0;
    for (const FDWCBakeSurface& Surface : Surfaces)
    {
        TriangleCount += Surface.Triangles.Num();
    }
    return TriangleCount;
}
