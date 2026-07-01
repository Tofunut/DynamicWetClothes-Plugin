#pragma once

#include "CoreMinimal.h"

class UMaterialInterface;
class UMaterial;

struct FWetClothingMaterialSetupResult
{
    bool    bSucceeded = false;
    bool    bAlreadyConfigured = false;
    UMaterial* ConfiguredMaterial = nullptr;
    FString Message;
};

class FWetClothingMaterialSetup
{
  public:
    static FWetClothingMaterialSetupResult DuplicateAndApplyToMaterialInterface(UMaterialInterface* MaterialInterface);
    static bool IsMaterialConfiguredForDwc(UMaterialInterface* MaterialInterface);
};
