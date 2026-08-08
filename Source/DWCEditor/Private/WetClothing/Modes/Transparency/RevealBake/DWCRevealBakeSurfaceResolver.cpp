// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "WetClothing/Modes/Transparency/RevealBake/DWCRevealBakeSurfaceResolver.h"

#include "WetClothing/Modes/Transparency/RevealBake/DWCRevealBakeSurfaceCache.h"

TArray<FDWCRevealBakeSurface> FDWCRevealBakeSurfaceResolver::BuildSourceSurfacesForOuter(
    const FDWCBakeSnapshot&      Snapshot,
    const FDWCBakeResolvedLayer& OuterLayer,
    FDWCRevealBakeSurfaceCache&  SurfaceCache,
    FString&                     OutErrorMessage)
{
    TArray<FDWCRevealBakeSurface> SourceSurfaces;

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

        const FDWCRevealBakeSurface* SourceSurface = SurfaceCache.FindOrBuild(
            CandidateLayer,
            LayerIndex,
            0,
            CandidateLayer.SourceUVChannel,
            OutErrorMessage);
        if (SourceSurface == nullptr)
        {
            return TArray<FDWCRevealBakeSurface>();
        }

        SourceSurfaces.Add(*SourceSurface);
    }

    return SourceSurfaces;
}

int32 FDWCRevealBakeSurfaceResolver::CountTriangles(const TArray<FDWCRevealBakeSurface>& Surfaces)
{
    int32 TriangleCount = 0;
    for (const FDWCRevealBakeSurface& Surface : Surfaces)
    {
        TriangleCount += Surface.Triangles.Num();
    }
    return TriangleCount;
}
