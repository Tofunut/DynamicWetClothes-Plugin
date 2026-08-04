#include "WetClothing/DerivedAssets/Textures/Transparency/DWCTransparencyAssetBakeService.h"

#include "DataAssets/WetClothingAsset.h"
#include "FileHelpers.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "UObject/Package.h"

namespace
{
    void AddPackageForObject(UObject* Object, TArray<UPackage*>& InOutPackages)
    {
        if (Object != nullptr && Object->GetOutermost() != nullptr)
        {
            InOutPackages.AddUnique(Object->GetOutermost());
        }
    }
}

bool FDWCTransparencyAssetBakeService::SaveTransparencySetupAssets(UWetClothingAsset* WetClothingAsset)
{
    if (WetClothingAsset == nullptr)
    {
        return false;
    }

    TArray<UPackage*> PackagesToSave;
    AddPackageForObject(WetClothingAsset, PackagesToSave);

    for (const FWetClothingGeneratedWetMaterialOverride& MaterialOverride :
         WetClothingAsset->Derived.Inline.GeneratedWetMaterialOverrides)
    {
        AddPackageForObject(MaterialOverride.GeneratedMaterial.Get(), PackagesToSave);
        AddPackageForObject(MaterialOverride.GeneratedMaterialInstance.Get(), PackagesToSave);
    }

    for (const FWetClothingTransparencyLayerData& Layer :
         WetClothingAsset->Authored.TransparencyData.TransparencyLayers)
    {
        for (const FWetClothingBakedTransparencyMap& BakedMap : Layer.BakedMaps)
        {
            AddPackageForObject(BakedMap.TransparencyMap.Get(), PackagesToSave);
        }
    }

    if (PackagesToSave.IsEmpty())
    {
        return true;
    }

    return FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSave, false, false) ==
           FEditorFileUtils::PR_Success;
}
