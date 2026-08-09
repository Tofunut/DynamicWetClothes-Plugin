//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Foundation/Preview/Layers/DWCEditorPreviewLayerResolver.h"

#include "DataAssets/WetClothingAsset.h"

FDWCEditorPreviewSavedLayers FDWCEditorPreviewLayerResolver::Resolve(
    const UWetClothingAsset* WetClothingAsset,
    const int32 MaterialSlotIndex)
{
    FDWCEditorPreviewSavedLayers Result;
    if (WetClothingAsset == nullptr || MaterialSlotIndex == INDEX_NONE)
    {
        return Result;
    }

    const FWetWrinkleResolvedNormalMap Wrinkle =
        WetClothingAsset->Authored.WrinkleData.ResolveRuntimeWrinkleNormalMap(MaterialSlotIndex);
    Result.WrinkleNormal = Wrinkle.Texture;
    Result.bWrinkleUsesCustomTexture =
        Wrinkle.Source == EDWCWrinkleNormalSource::CustomTexture && Wrinkle.Texture != nullptr;

    // Editor cross-preview keeps the last baked texture visible even after a
    // dependent wrinkle bake marks it stale. Runtime lookup remains strict.
    if (const FWetClothingBakedTransparencyMap* Transparency =
            WetClothingAsset->Authored.TransparencyData.FindBakedTransparencyMap(MaterialSlotIndex))
    {
        Result.TransparencyMap = Transparency->TransparencyMap;
        Result.TransparencyState = Transparency->IsRuntimeUsable()
            ? EDWCEditorSavedLayerState::Current
            : EDWCEditorSavedLayerState::Stale;
    }
    return Result;
}
