#pragma once

#include "CoreMinimal.h"

class UWetClothingAsset;

class FDWCTransparencyAssetBakeService
{
  public:
    static bool BakeTransparencyRevealAssets(UWetClothingAsset* WetClothingAsset, FString& OutSummary, bool* OutHadWarnings = nullptr);
    static bool HasPendingTransparencySetup(UWetClothingAsset* WetClothingAsset, FString* OutSummary = nullptr);
    static bool SaveTransparencySetupAssets(UWetClothingAsset* WetClothingAsset);
};
