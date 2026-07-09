#pragma once

#include "CoreMinimal.h"

class UTexture2D;
class UWetClothingAsset;

struct FWetWrinkleTextureGenerationSettings
{
    UTexture2D* BaseNormalTexture = nullptr;
    int32 LODIndex = 0;
    int32 UVChannelIndex = INDEX_NONE;
    int32 Resolution = 1024;
    float Intensity = 1.0f;
    float PatternScale = 1.0f;
    FVector2D PatternOffset = FVector2D::ZeroVector;
    float DirectionRadians = 0.0f;
    float Noise = 0.0f;
};

struct FWetWrinkleTextureGenerationResult
{
    UTexture2D* GeneratedNormalMap = nullptr;
    UTexture2D* PreviewDisplayMap = nullptr;
    int32 Width = 0;
    int32 Height = 0;
};

class FWetWrinkleTextureGenerator
{
  public:
    static bool GeneratePreviewMaterialSlotTexture(
        UWetClothingAsset* WetClothingAsset,
        int32 MaterialSlotIndex,
        const FWetWrinkleTextureGenerationSettings& Settings,
        FWetWrinkleTextureGenerationResult& OutResult,
        FString& OutErrorMessage);
};
