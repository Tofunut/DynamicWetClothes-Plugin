#pragma once

#include "CoreMinimal.h"

class UWetClothingAsset;

class FWetClothingWetnessProfileMapBakeService
{
public:
    static bool HasPendingVisualBakeTasks(const UWetClothingAsset* WetClothingAsset, FString* OutSummary = nullptr);
    static bool BakeWetnessProfileMapsAndUpdateMaterials(UWetClothingAsset* WetClothingAsset, FString& OutSummary, bool* OutHadWarnings = nullptr);
    static bool SaveBakedWetnessAssets(UWetClothingAsset* WetClothingAsset);
};
