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
    static FWetClothingMaterialSetupResult DuplicateAndApplyToMaterialInterface(UMaterialInterface* MaterialInterface);
    static bool                            IsMaterialConfiguredForDwc(UMaterialInterface* MaterialInterface);
    static FWetClothingMaterialSetupResult EnsurePreviewSupportOnMaterialInterface(UMaterialInterface* MaterialInterface);
};
