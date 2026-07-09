/*
 *  Material 텍스처 후보 검색, 대표 텍스처 선택, 미리보기 적합도 평가 인터페이스를 선언합니다.
 */

#pragma once

#include "CoreMinimal.h"

class UMaterialInterface;
class UTexture;
class UWetClothingAsset;
struct FWetClothingTextureItem;

class FWetClothingMaterialTextureResolver
{
  public:
    static void BuildTextureItems(
        UMaterialInterface*                          Material,
        TArray<TSharedPtr<FWetClothingTextureItem>>& OutItems);

    static UTexture* ResolveBestMaterialTexture(UMaterialInterface* Material);

    static double ScoreTexturePreviewSuitability(UTexture* Texture);

    static UTexture* FindSavedTextureSelection(
        const UWetClothingAsset* WetClothingAsset,
        int32 MaterialSlotIndex,
        int32 UVChannelIndex);

    static bool HasSavedTextureSelection(
        const UWetClothingAsset* WetClothingAsset,
        int32 MaterialSlotIndex,
        int32 UVChannelIndex);

    static void SaveTextureSelection(
        UWetClothingAsset* WetClothingAsset,
        int32 MaterialSlotIndex,
        int32 UVChannelIndex,
        UTexture* Texture);

    static UTexture* ResolveOrSaveTextureSelection(
        UWetClothingAsset* WetClothingAsset,
        int32 MaterialSlotIndex,
        int32 UVChannelIndex);

    static void BuildTextureItemsForMaterialSlot(
        UWetClothingAsset* WetClothingAsset,
        int32 MaterialSlotIndex,
        int32 UVChannelIndex,
        TArray<TSharedPtr<FWetClothingTextureItem>>& OutItems,
        TSharedPtr<FWetClothingTextureItem>& OutSelectedItem);
};
