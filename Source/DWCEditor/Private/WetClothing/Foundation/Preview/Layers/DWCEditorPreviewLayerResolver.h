//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

class UTexture2D;
class UWetClothingAsset;

struct FDWCEditorPreviewSavedLayers
{
    TObjectPtr<UTexture2D> WrinkleNormal = nullptr;
    TObjectPtr<UTexture2D> TransparencyMap = nullptr;
    bool bWrinkleUsesCustomTexture = false;
};

/** Resolves only persisted runtime layer textures. Live editor RTs are intentionally excluded. */
class FDWCEditorPreviewLayerResolver
{
  public:
    static FDWCEditorPreviewSavedLayers Resolve(
        const UWetClothingAsset* WetClothingAsset,
        int32 MaterialSlotIndex);
};
