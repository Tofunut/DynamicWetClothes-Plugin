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
        // Keep the last transparency color visible for stale diagnostics, but
        // only bind a Reveal Normal that satisfies the runtime RG contract.
        const FWetClothingTransparencyLayerData* Layer =
            WetClothingAsset->Authored.TransparencyData.FindTransparencyLayer(MaterialSlotIndex);
        const bool bRequiresRevealNormal = Layer != nullptr && Layer->RequiresRuntimeRevealNormal();
        const bool bHasRevealNormal = bRequiresRevealNormal && Transparency->HasRuntimeRevealNormalPayload();
        Result.RevealNormalMap = bHasRevealNormal
            ? Transparency->RevealNormalMap.Get()
            : nullptr;
        Result.RevealNormalStrength = bHasRevealNormal
            ? FMath::Clamp(Layer->RevealNormalStrength, 0.0f, 4.0f)
            : 0.0f;
        Result.TransparencyState = Transparency->IsRuntimeUsableForLayer(bRequiresRevealNormal)
            ? EDWCEditorSavedLayerState::Current
            : EDWCEditorSavedLayerState::Stale;
    }
    return Result;
}
