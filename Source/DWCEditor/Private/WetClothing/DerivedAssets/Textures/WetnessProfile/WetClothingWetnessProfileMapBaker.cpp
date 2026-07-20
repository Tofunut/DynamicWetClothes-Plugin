#include "WetClothing/DerivedAssets/Textures/WetnessProfile/WetClothingWetnessProfileMapBaker.h"

FString FWetClothingWetnessProfileMapBaker::MakeBuildSignature(
    const UWetClothingAsset* /*WetClothingAsset*/,
    const UTexture* /*SourceTexture*/,
    int32 /*UVChannelIndex*/,
    const TArray<int32>& /*MaterialSlotIndices*/)
{
    return TEXT("WetnessProfileMapRemoved");
}

bool FWetClothingWetnessProfileMapBaker::BakeWetnessProfileMap0(
    UWetClothingAsset* /*WetClothingAsset*/,
    UTexture* /*SourceTexture*/,
    int32 /*UVChannelIndex*/,
    const TArray<int32>& /*MaterialSlotIndices*/,
    const FWetClothingWetnessProfileMapBakeSettings& /*Settings*/,
    FWetClothingWetnessProfileMapBakeResult& OutResult,
    FString& OutErrorMessage)
{
    OutResult = FWetClothingWetnessProfileMapBakeResult();
    OutErrorMessage = TEXT("Wetness Profile Maps were removed. Runtime uses resolved wetness profile parameters.");
    return false;
}
