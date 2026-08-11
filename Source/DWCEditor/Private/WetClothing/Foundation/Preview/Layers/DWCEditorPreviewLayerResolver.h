//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

class UTexture2D;
class UWetClothingAsset;

enum class EDWCEditorSavedLayerState : uint8
{
    Missing,
    Current,
    Stale
};

struct FDWCEditorPreviewSavedLayers
{
    TObjectPtr<UTexture2D> WrinkleNormal = nullptr;
    TObjectPtr<UTexture2D> TransparencyMap = nullptr;
    TObjectPtr<UTexture2D> RevealNormalMap = nullptr;
    float RevealNormalStrength = 0.0f;
    bool bWrinkleUsesCustomTexture = false;
    EDWCEditorSavedLayerState TransparencyState = EDWCEditorSavedLayerState::Missing;
};

/** Resolves persisted layer textures for editor preview. Live editor RTs are intentionally excluded. */
class FDWCEditorPreviewLayerResolver
{
  public:
    static FDWCEditorPreviewSavedLayers Resolve(
        const UWetClothingAsset* WetClothingAsset,
        int32 MaterialSlotIndex);
};
