#include "WetClothing/DerivedAssets/Textures/WetnessProfile/WetClothingWetnessProfileMapBakeService.h"

#include "DataAssets/WetClothingAsset.h"

bool FWetClothingWetnessProfileMapBakeService::HasPendingVisualBakeTasks(
    const UWetClothingAsset* /*WetClothingAsset*/,
    FString* OutSummary)
{
    if (OutSummary != nullptr)
    {
        *OutSummary = TEXT("Wetness Profile Maps were removed. Runtime uses resolved wetness profile parameters.");
    }
    return false;
}

bool FWetClothingWetnessProfileMapBakeService::BakeWetnessProfileMapsAndUpdateMaterials(
    UWetClothingAsset* /*WetClothingAsset*/,
    FString& OutSummary,
    bool* OutHadWarnings)
{
    OutSummary = TEXT("Wetness Profile Maps were removed. Runtime uses resolved wetness profile parameters.");
    if (OutHadWarnings != nullptr)
    {
        *OutHadWarnings = false;
    }
    return true;
}

bool FWetClothingWetnessProfileMapBakeService::SaveBakedWetnessAssets(UWetClothingAsset* /*WetClothingAsset*/)
{
    return true;
}
