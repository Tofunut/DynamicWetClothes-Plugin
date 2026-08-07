//Copyright 2026 Team Tofunut. All Rights Reserved.
/*
 *  Wet Clothing Asset 에디터 UI에서 공유하는 Material Slot, Texture, Selection Tool, Profile 항목 타입을 정의합니다.
 */

#pragma once

#include "CoreMinimal.h"
#include "WetClothing/WCAEditor/UI/UVView/SWCAUVView.h"

class UMaterialInterface;
class UTexture;

struct FWCAMaterialSlotItem
{
    int32                              SlotIndex = INDEX_NONE;
    FName                              SlotName = NAME_None;
    TWeakObjectPtr<UMaterialInterface> Material;
    bool                               bIsWettableSlot = false;
};

struct FWCATextureItem
{
    TWeakObjectPtr<UTexture> Texture;
    FString                  Label;
};

struct FWCAUVSelectionToolItem
{
    EWCAUVSelectionTool Tool = EWCAUVSelectionTool::Select;
    FText                            Label;
    FText                            Tooltip;
    FName                            IconBrushDisplayName;
};
