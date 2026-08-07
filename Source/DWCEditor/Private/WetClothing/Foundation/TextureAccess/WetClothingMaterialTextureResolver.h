//Copyright 2026 Team Tofunut. All Rights Reserved.
/*
 * Declares material texture candidate discovery, representative texture selection, and preview suitability evaluation.
 */

#pragma once

#include "CoreMinimal.h"

class UMaterialInterface;
class UTexture;
class UWetClothingAsset;
struct FWCATextureItem;

class FWetClothingMaterialTextureResolver
{
  public:
    static void BuildTextureItems(
        UMaterialInterface*                          Material,
        TArray<TSharedPtr<FWCATextureItem>>& OutItems);

    static UTexture* ResolveBestMaterialTexture(UMaterialInterface* Material);

    static double ScoreTexturePreviewSuitability(UTexture* Texture);

    static UTexture* FindSavedTextureSelection(
        const UWetClothingAsset* WetClothingAsset,
        int32 MaterialSlotIndex);

    static bool HasSavedTextureSelection(
        const UWetClothingAsset* WetClothingAsset,
        int32 MaterialSlotIndex);

    static void SaveTextureSelection(
        UWetClothingAsset* WetClothingAsset,
        int32 MaterialSlotIndex,
        UTexture* Texture);

    static UTexture* ResolveOrSaveTextureSelection(
        UWetClothingAsset* WetClothingAsset,
        int32 MaterialSlotIndex);

    static void BuildTextureItemsForMaterialSlot(
        UWetClothingAsset* WetClothingAsset,
        int32 MaterialSlotIndex,
        TArray<TSharedPtr<FWCATextureItem>>& OutItems,
        TSharedPtr<FWCATextureItem>& OutSelectedItem,
        bool bDefaultToNone = false);
};
