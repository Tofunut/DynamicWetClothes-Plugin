/*
 *  Wet Clothing Asset 에디터 UI에서 공유하는 Material Slot, Texture, Selection Tool, Profile 항목 타입을 정의합니다.
 */

#pragma once

#include "AssetRegistry/AssetData.h"
#include "CoreMinimal.h"
#include "WetClothing/Widgets/SWetClothingAssetUVView.h"

class UMaterialInterface;
class UTexture;

struct FWetClothingMaterialSlotItem
{
    int32                              SlotIndex = INDEX_NONE;
    FName                              SlotName = NAME_None;
    TWeakObjectPtr<UMaterialInterface> Material;
};

struct FWetClothingTextureItem
{
    TWeakObjectPtr<UTexture> Texture;
    FString                  Label;
};

struct FWetClothingUVSelectionToolItem
{
    EWetClothingAssetUVSelectionTool Tool = EWetClothingAssetUVSelectionTool::Select;
    FText                              Label;
    FText                              Tooltip;
    FName                              IconBrushName;
};

struct FWetnessProfileAssetItem
{
    FAssetData AssetData;
    FString    DisplayName;
    FString    ContentPath;
};
