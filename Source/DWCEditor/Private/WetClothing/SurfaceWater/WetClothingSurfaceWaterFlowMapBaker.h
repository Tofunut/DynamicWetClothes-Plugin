#pragma once
#include "CoreMinimal.h"
class UWetClothingAsset;
class UTexture2D;

class FWetClothingSurfaceWaterFlowMapBaker
{
public:
    static FString MakeBuildSignature(const UWetClothingAsset* Asset, int32 MaterialSlotIndex);
    static bool IsStale(const UWetClothingAsset* Asset);
    static bool Bake(UWetClothingAsset* Asset, FString& OutError);
private:
    static bool CreateOrUpdateTexture(UWetClothingAsset& Asset, int32 MaterialSlotIndex, const TArray<FFloat16Color>& Pixels, int32 Resolution, UTexture2D*& OutTexture, FString& OutError);
};
