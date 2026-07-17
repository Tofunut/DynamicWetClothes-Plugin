/*
 *  UV Island 분석 결과 캐시의 조회, 미리보기 삼각형 생성, 캐시 초기화 인터페이스를 선언합니다.
 */

#pragma once

#include "CoreMinimal.h"

class USkeletalMesh;
class UWetClothingAsset;
struct FWetClothingAssetUVIsland;
struct FWetClothingAssetUVTriangle;

class FWetClothingAssetUVIslandCache
{
  public:
    static bool GetMaterialSlotUVIslands(
        const USkeletalMesh*                           SkeletalMesh,
        int32                                          LODIndex,
        int32                                          UVChannelIndex,
        int32                                          MaterialSlotIndex,
        TArray<TSharedPtr<FWetClothingAssetUVIsland>>& OutIslands,
        FString*                                       OutErrorMessage = nullptr);

    /** Uses persistent WCA island membership when available, then falls back to full mesh analysis. */
    static bool GetMaterialSlotUVIslands(
        const UWetClothingAsset*                       WetClothingAsset,
        int32                                          UVChannelIndex,
        int32                                          MaterialSlotIndex,
        TArray<TSharedPtr<FWetClothingAssetUVIsland>>& OutIslands,
        FString*                                       OutErrorMessage = nullptr);

    static bool BuildMaterialSlotPreviewTriangles(
        const USkeletalMesh*                 SkeletalMesh,
        int32                                MaterialSlotIndex,
        TArray<FWetClothingAssetUVTriangle>& OutTriangles);

    /** Uses the asset's DWC Data UV and Original UV channel. */
    static bool BuildMaterialSlotPreviewTriangles(
        const UWetClothingAsset*             WetClothingAsset,
        int32                                MaterialSlotIndex,
        TArray<FWetClothingAssetUVTriangle>& OutTriangles);

    static void Clear();
};
