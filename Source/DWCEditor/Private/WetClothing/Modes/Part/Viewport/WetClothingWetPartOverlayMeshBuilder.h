// Copyright 2026 Team Tofunut. All Rights Reserved.

/*
 * Declares mesh-data generation for Wet Part color overlays.
 */

#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Modes/Part/Viewport/DWCOverlayMeshData.h"

struct FWetClothingAssetUVIsland;

class FWetClothingWetPartOverlayMeshBuilder
{
  public:
    static void BuildMeshData(
        const TArray<FWetClothingAssetUVIsland>& Islands,
        const TMap<int32, int32>&                UVIslandToWetPartID,
        const TMap<int32, FLinearColor>&         IslandColors,
        float                                    NormalOffset,
        float                                    ColorIntensity,
        FDWCOverlayMeshData&                     OutMeshData);
};
