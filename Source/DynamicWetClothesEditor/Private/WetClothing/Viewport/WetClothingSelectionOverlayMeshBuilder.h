/*
 *  선택 영역 경계선 Overlay Mesh 데이터 생성 함수를 선언합니다.
 */

#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Viewport/WetClothingOverlayMeshData.h"

struct FWetClothingAssetUVIsland;

class FWetClothingSelectionOverlayMeshBuilder
{
  public:
    static void BuildMeshData(
        const TArray<FWetClothingAssetUVIsland>& Islands,
        const TSet<int32>&                         HighlightedIslandIDs,
        float                                      HalfThickness,
        const FLinearColor&                        Color,
        FWetClothingOverlayMeshData&               OutMeshData);
};
