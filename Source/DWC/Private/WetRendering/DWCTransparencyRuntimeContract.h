// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UTexture2D;
struct FWetClothingBakedTransparencyMap;
struct FWetClothingTransparencyLayerData;

/** Canonical texture binding consumed by runtime transparency rendering and diagnostics. */
struct FDWCTransparencyRuntimeBinding
{
    UTexture2D* TransparencyMap = nullptr;
    UTexture2D* RevealNormalMap = nullptr;
    float RevealNormalStrength = 0.0f;

    bool UsesTransparencyMap() const { return TransparencyMap != nullptr; }
    bool UsesRevealNormalMap() const { return RevealNormalMap != nullptr; }
};
/** Keeps runtime binding and resident-memory accounting on the same payload contract. */
class FDWCTransparencyRuntimeContract
{
  public:
    static FDWCTransparencyRuntimeBinding Resolve(
        const FWetClothingBakedTransparencyMap* BakedMap,
        const FWetClothingTransparencyLayerData* Layer = nullptr);

    static void AccumulateResidentTextureUsage(
        const FDWCTransparencyRuntimeBinding& Binding,
        TSet<const UTexture2D*>& SeenTextures,
        uint32& OutTextureCount,
        uint64& OutTextureBytes);
};
