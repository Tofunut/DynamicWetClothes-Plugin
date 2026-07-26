#include "WetClothing/DerivedAssets/Materials/DWCMaterialSetupEditorLibrary.h"

#include "DataAssets/WetClothingAsset.h"
#include "WetClothing/DerivedAssets/Materials/WCAMaterialGenerator.h"

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
         WetClothingAsset->Derived.Inline.GeneratedWetMaterialOverrides)
    {
        UMaterialInterface* SourceMaterial = MaterialOverride.SourceMaterial.Get();
        if (!SourceMaterial)
        {
            Messages.Add(FString::Printf(
                TEXT("Slot %d skipped: no source material."),
                MaterialOverride.MaterialSlotIndex));
            continue;
        }

        const FWCAMaterialGenerator::FOptions Options =
            FWCAMaterialGenerator::MakeOptionsForAsset(
                WetClothingAsset,
                EDWCSimulationMode::VertexCPU,
                MaterialOverride.MaterialSlotIndex);
        const FWetClothingUnifiedMaterialSetupResult Result =
            FWCAMaterialGenerator::CreateOrUpdateUnifiedMaterialSet(SourceMaterial, Options);
        if (!Result.bSucceeded || !Result.GeneratedMaterial ||
            !Result.CPUMaterialInstance || !Result.GPUMaterialInstance)
        {
            Messages.Add(FString::Printf(
                TEXT("Slot %d failed: %s"),
                MaterialOverride.MaterialSlotIndex,
                *Result.Message));
            continue;
        }

        MaterialOverride.GeneratedMaterial = Result.GeneratedMaterial;
        MaterialOverride.CPUMaterialInstance = Result.CPUMaterialInstance;
        MaterialOverride.GPUMaterialInstance = Result.GPUMaterialInstance;
        ++RepairedCount;
        Messages.Add(FString::Printf(
            TEXT("Slot %d repaired: shared=%s CPU=%s GPU=%s (%s)"),
            MaterialOverride.MaterialSlotIndex,
            *GetNameSafe(Result.GeneratedMaterial),
            *GetNameSafe(Result.CPUMaterialInstance),
            *GetNameSafe(Result.GPUMaterialInstance),
            *Result.Message));
    }

    if (RepairedCount > 0)
    {
        WetClothingAsset->MarkPackageDirty();
    }
    OutReport = FString::Join(Messages, TEXT("\n"));
    return RepairedCount > 0;
}
