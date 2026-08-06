#pragma once

#include "CoreMinimal.h"

class UWetClothingAsset;

/** Explicit, user-invoked relocation of generated WCA outputs. */
class FDWCGeneratedAssetRelocator
{
public:
    static void RegisterContentBrowserMenu(void* Owner);
    static bool RelocateGeneratedAssets(UWetClothingAsset& Asset, FString& OutMessage);
};
