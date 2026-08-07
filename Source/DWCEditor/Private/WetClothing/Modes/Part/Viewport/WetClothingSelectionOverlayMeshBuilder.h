//Copyright 2026 Team Tofunut. All Rights Reserved.
/*
 *  선택 영역 경계선 Overlay Mesh 데이터 생성 함수를 선언합니다.
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
