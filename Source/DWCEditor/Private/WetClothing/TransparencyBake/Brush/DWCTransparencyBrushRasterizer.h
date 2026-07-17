#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetClothingTransparencyData.h"

struct FDWCTransparencyAutoBakeResult;

class FDWCTransparencyBrushRasterizer
{
  public:
    static void RebuildFromStrokes(
        const FDWCTransparencyAutoBakeResult& AutoResult,
        const FWetClothingTransparencyLayerData& Layer,
        int32 MaterialSlotIndex,
        int32 UVChannelIndex,
        TArray<uint8>& OutManualPremultipliedBuffer,
        TArray<uint8>& OutManualWeightBuffer);

    static float ResolveEditedAlpha(
        const FDWCTransparencyAutoBakeResult& AutoResult,
        const TArray<uint8>& ManualPremultipliedBuffer,
        const TArray<uint8>& ManualWeightBuffer,
        int32 PixelIndex);
};
