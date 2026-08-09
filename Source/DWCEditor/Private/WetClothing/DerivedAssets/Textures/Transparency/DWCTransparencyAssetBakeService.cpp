//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/DerivedAssets/Textures/Transparency/DWCTransparencyAssetBakeService.h"

#include "DataAssets/WetClothingAsset.h"
#include "FileHelpers.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "WetClothing/Modes/Transparency/Temp/DWCTransparencyIntermediateAssetPolicy.h"
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

    TArray<FString> IntermediatePolicyWarnings;
    FDWCTransparencyIntermediateAssetPolicy::RepairLoadedReferences(
        *WetClothingAsset, PackagesToSave, IntermediatePolicyWarnings);

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
#if WITH_EDITORONLY_DATA
        for (const FDWCTransparencyTempArtifactReference& Artifact :
             Layer.EditorStageCache.Artifacts)
        {
            // Do not force-load inactive layer caches merely to save the WCA.
            AddPackageForObject(Artifact.Texture.Get(), PackagesToSave);
        }
#endif
    }

#if WITH_EDITORONLY_DATA
    for (const FDWCTransparencyMaterialColorCacheReference& Reference :
         WetClothingAsset->Authored.TransparencyData.MaterialColorCache)
    {
        // Material-color intermediates follow the same lazy save policy as
        // layer artifacts: save loaded packages without forcing stale caches in.
        AddPackageForObject(Reference.Texture.Get(), PackagesToSave);
    }
#endif

    if (PackagesToSave.IsEmpty())
    {
        return true;
    }

    return FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSave, false, false) ==
           FEditorFileUtils::PR_Success;
}
