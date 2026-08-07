//Copyright 2026 Team Tofunut. All Rights Reserved.
/*
 *  Wet Part 색상 Overlay Mesh 데이터 생성 함수를 선언합니다.
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
        const TMap<int32, int32>& UVIslandToWetPartID,
        const TMap<int32, FLinearColor>& IslandColors,
        float NormalOffset,
        float ColorIntensity,
        FDWCOverlayMeshData& OutMeshData);
};
