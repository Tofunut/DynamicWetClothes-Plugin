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

    if (const FWetClothingBakedTransparencyMap* Transparency =
            WetClothingAsset->Authored.TransparencyData.FindRuntimeBakedTransparencyMap(MaterialSlotIndex))
    {
        Result.TransparencyMap = Transparency->TransparencyMap;
    }
    return Result;
}
