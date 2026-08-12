// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "WetRendering/DWCTransparencyRuntimeContract.h"

#include "DataAssets/WetClothingTransparencyData.h"
#include "Engine/Texture2D.h"

namespace
{
    bool MatchesResolvedOutputResolution(const UTexture2D* Texture, const int32 Resolution)
    {
        return Texture != nullptr && Resolution > 0 &&
            Texture->GetSizeX() == Resolution && Texture->GetSizeY() == Resolution;
    }
}

FDWCTransparencyRuntimeBinding FDWCTransparencyRuntimeContract::Resolve(
    const FWetClothingBakedTransparencyMap* BakedMap,
    const FWetClothingTransparencyLayerData* Layer)
{
    FDWCTransparencyRuntimeBinding Binding;
    if (BakedMap == nullptr || !BakedMap->IsRuntimeUsable() ||
        !MatchesResolvedOutputResolution(BakedMap->TransparencyMap.Get(), BakedMap->Resolution))
    {
        return Binding;
    }

    Binding.TransparencyMap = BakedMap->TransparencyMap.Get();
    Binding.ResolvedOutputResolution = BakedMap->Resolution;
    const bool bEnableRevealNormal = Layer == nullptr || Layer->bEnableRevealNormal;
    Binding.RevealNormalMap = bEnableRevealNormal && BakedMap->HasRuntimeRevealNormalPayload() &&
            MatchesResolvedOutputResolution(BakedMap->RevealNormalMap.Get(), BakedMap->Resolution)
        ? BakedMap->RevealNormalMap.Get()
        : nullptr;
    Binding.RevealNormalStrength = Binding.RevealNormalMap != nullptr
        ? FMath::Clamp(Layer != nullptr ? Layer->RevealNormalStrength : 1.0f, 0.0f, 4.0f)
        : 0.0f;
    return Binding;
}
void FDWCTransparencyRuntimeContract::AccumulateResidentTextureUsage(
    const FDWCTransparencyRuntimeBinding& Binding,
    TSet<const UTexture2D*>& SeenTextures,
    uint32& OutTextureCount,
    uint64& OutTextureBytes)
{
    const UTexture2D* RuntimeTextures[] = {
        Binding.TransparencyMap,
        Binding.RevealNormalMap
    };
    for (const UTexture2D* Texture : RuntimeTextures)
    {
        if (Texture == nullptr || SeenTextures.Contains(Texture))
        {
            continue;
        }

        SeenTextures.Add(Texture);
        ++OutTextureCount;
        const uint64 TextureBytes = Texture->CalcTextureMemorySizeEnum(TMC_ResidentMips);
        OutTextureBytes = TextureBytes > MAX_uint64 - OutTextureBytes
            ? MAX_uint64
            : OutTextureBytes + TextureBytes;
    }
}
