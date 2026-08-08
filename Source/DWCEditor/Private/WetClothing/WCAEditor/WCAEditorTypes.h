// Copyright 2026 Team Tofunut. All Rights Reserved.

/*
 * Shared item types used by the Wet Clothing Asset editor UI for material slots, textures, selection tools, and profiles.
 */

#pragma once

#include "UObject/WeakObjectPtr.h"
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
    FText               Label;
    FText               Tooltip;
    FName               IconBrushDisplayName;
};
