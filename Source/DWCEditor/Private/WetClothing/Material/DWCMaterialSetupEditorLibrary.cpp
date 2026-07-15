#include "WetClothing/Material/DWCMaterialSetupEditorLibrary.h"

#include "DataAssets/WetClothingAsset.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "WetClothing/Common/Material/WetClothingMaterialSetup.h"

bool UDWCMaterialSetupEditorLibrary::RepairGeneratedWetMaterials(
    UWetClothingAsset* WetClothingAsset,
    FString& OutReport)
{
    OutReport.Reset();
    if (!WetClothingAsset)
    {
        OutReport = TEXT("WetClothingAsset is null.");
        return false;
    }

    int32 RepairedCount = 0;
    TArray<FString> Messages;
    WetClothingAsset->Modify();

    for (FWetClothingGeneratedWetMaterialOverride& MaterialOverride :
         WetClothingAsset->PartData.GeneratedWetMaterialOverrides)
    {
        UMaterialInterface* ExistingWetMaterial = MaterialOverride.WetMaterial.Get();
        UMaterialInterface* SourceMaterial = MaterialOverride.SourceMaterial.Get();
        UMaterialInterface* RepairTarget = ExistingWetMaterial &&
                FWetClothingMaterialSetup::IsMaterialConfiguredForDwc(ExistingWetMaterial)
            ? ExistingWetMaterial
            : SourceMaterial;
        if (!RepairTarget)
        {
            Messages.Add(FString::Printf(
                TEXT("Slot %d skipped: no repairable material."), MaterialOverride.MaterialSlotIndex));
            continue;
        }

        // Restore materials that an older DWC Clear Coat path promoted. A source
        // material authored as Clear Coat remains Clear Coat.
        if (ExistingWetMaterial && SourceMaterial)
        {
            UMaterial* ConfiguredBase = ExistingWetMaterial->GetMaterial();
            const UMaterial* SourceBase = SourceMaterial->GetMaterial();
            if (ConfiguredBase && SourceBase &&
                FWetClothingMaterialSetup::IsMaterialConfiguredForDwc(ConfiguredBase) &&
                ConfiguredBase->GetShadingModels().HasOnlyShadingModel(MSM_ClearCoat) &&
                !SourceBase->GetShadingModels().HasOnlyShadingModel(MSM_ClearCoat))
            {
                ConfiguredBase->Modify();
                ConfiguredBase->SetShadingModel(SourceBase->GetShadingModels().GetFirstShadingModel());
                ConfiguredBase->MarkPackageDirty();
            }
        }

        int32 SurfaceWaterUVChannelIndex = WetClothingAsset->SurfaceWaterSettings.UVChannelIndexToConstruct;
        if (const FSurfaceWaterMaterialSlotData* SlotData =
                WetClothingAsset->SurfaceWaterSettings.FindMaterialSlot(MaterialOverride.MaterialSlotIndex))
        {
            SurfaceWaterUVChannelIndex = SlotData->UVChannelIndex;
        }

        const FWetClothingMaterialSetupResult Result =
            FWetClothingMaterialSetup::DuplicateAndApplyToMaterialInterface(
                RepairTarget,
                0,
                SurfaceWaterUVChannelIndex);
        if (!Result.bSucceeded || !Result.ConfiguredMaterial)
        {
            Messages.Add(FString::Printf(
                TEXT("Slot %d failed: %s"), MaterialOverride.MaterialSlotIndex, *Result.Message));
            continue;
        }

        MaterialOverride.WetMaterial = Result.ConfiguredMaterial;
        ++RepairedCount;
        Messages.Add(FString::Printf(
            TEXT("Slot %d repaired: %s (%s)"),
            MaterialOverride.MaterialSlotIndex,
            *GetNameSafe(Result.ConfiguredMaterial),
            *Result.Message));
    }

    if (RepairedCount > 0)
    {
        WetClothingAsset->MarkPackageDirty();
    }
    OutReport = FString::Join(Messages, TEXT("\n"));
    return RepairedCount > 0;
}
