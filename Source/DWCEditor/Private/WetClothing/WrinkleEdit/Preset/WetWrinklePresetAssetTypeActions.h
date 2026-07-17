#pragma once

#include "AssetTypeActions_Base.h"
#include "DataAssets/WetWrinklePreset.h"

class FWetWrinklePresetAssetTypeActions final : public FAssetTypeActions_Base
{
  public:
    virtual FText GetName() const override { return NSLOCTEXT("DWCEditor", "WetWrinklePresetAssetName", "Wet Wrinkle Preset"); }
    virtual FColor GetTypeColor() const override { return FColor(76, 168, 255); }
    virtual UClass* GetSupportedClass() const override { return UWetWrinklePreset::StaticClass(); }
    virtual uint32 GetCategories() override { return EAssetTypeCategories::Materials | EAssetTypeCategories::Textures; }
};
