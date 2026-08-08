// Copyright 2026 Team Tofunut. All Rights Reserved.

/*
 * Declares mesh-data generation for selection-boundary overlays.
 */

#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Modes/Part/Viewport/DWCOverlayMeshData.h"

struct FWetClothingAssetUVIsland;

class FWetClothingSelectionOverlayMeshBuilder
{
  public:
    static void BuildMeshData(
        const TArray<FWetClothingAssetUVIsland>& Islands,
        const TSet<int32>&                       HighlightedUVIslandIDs,
        float                                    HalfThickness,
        const FLinearColor&                      Color,
        FDWCOverlayMeshData&                     OutMeshData);

    static void BuildMeshData(
        const TArray<FWetClothingAssetUVIsland>& Islands,
        const TSet<int32>&                       HighlightedUVIslandIDs,
        float                                    HalfThickness,
        FDWCOverlayMeshData&                     OutMeshData)
    {
        BuildMeshData(
            Islands,
            HighlightedUVIslandIDs,
            HalfThickness,
            FLinearColor(1.0f, 0.58f, 0.02f, 1.0f),
            OutMeshData);
    }
};
