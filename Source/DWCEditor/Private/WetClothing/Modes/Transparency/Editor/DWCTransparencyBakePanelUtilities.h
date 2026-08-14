// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetClothingTransparencyData.h"

class UWetClothingAsset;
struct FWetClothingBakedTransparencyMap;
struct FWetClothingTransparencyLayerData;

namespace UE::DWCEditor::TransparencyPanel
{
inline constexpr float MinBrushSizeCm = 0.5f;
inline constexpr float MaxBrushSizeCm = 40.0f;

int32 ResolveDataUVChannel(const UWetClothingAsset* Asset);
const TCHAR* GetStrokeModeLabel(EDWCTransparencyBrushMode Mode);
const TCHAR* GetRevealColorStrokeModeLabel(EDWCTransparencyRevealColorBrushMode Mode);
FText GetSourceTypeLabel(EDWCTransparencySourceType SourceType);
FText GetBlueprintSourceRoleLabel(EDWCTransparencyBlueprintSourceRole Role);
const FWetClothingBakedTransparencyMap* FindExactBakedMap(
    const UWetClothingAsset* Asset,
    const FWetClothingTransparencyLayerData* Layer);

float RadiusUVToSizeCm(float RadiusUV);
float SizeCmToRadiusUV(float SizeCm);
FText FormatBrushSizeCm(float SizeCm);
}
