#pragma once

#include "CoreMinimal.h"

class UMaterialInterface;

struct FWetClothingMaterialSetupResult
{
    bool                bSucceeded = false;
    bool                bAlreadyConfigured = false;
    UMaterialInterface* ConfiguredMaterial = nullptr;
    FString             Message;
};

class FWetClothingMaterialSetup
{
  public:
    static FWetClothingMaterialSetupResult DuplicateAndApplyToMaterialInterface(
        UMaterialInterface* MaterialInterface,
        int32 WrinkleUVChannelIndex = INDEX_NONE,
        int32 SurfaceWaterUVChannelIndex = 1);
    static bool                            IsMaterialConfiguredForDwc(UMaterialInterface* MaterialInterface);
};
