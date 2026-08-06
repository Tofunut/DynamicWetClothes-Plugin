#pragma once

#include "CoreMinimal.h"

class UWetClothingAsset;

class FDWCTransparencyAssetBakeService
{
  public:
    static bool SaveTransparencySetupAssets(UWetClothingAsset* WetClothingAsset);
};
