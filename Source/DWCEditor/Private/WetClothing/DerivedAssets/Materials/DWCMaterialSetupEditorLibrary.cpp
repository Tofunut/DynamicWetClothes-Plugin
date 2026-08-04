#include "WetClothing/DerivedAssets/Materials/DWCMaterialSetupEditorLibrary.h"

#include "DataAssets/WetClothingAsset.h"
#include "WetClothing/DerivedAssets/Materials/WCAMaterialGenerator.h"
#include "WetClothing/DerivedAssets/Textures/WetnessProfile/WetClothingRenderProfileBakeService.h"

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
            !Result.GeneratedMaterialInstance)
        {
            Messages.Add(FString::Printf(
                TEXT("Slot %d failed: %s"),
                MaterialOverride.MaterialSlotIndex,
                *Result.Message));
            continue;
        }

        MaterialOverride.GeneratedMaterial = Result.GeneratedMaterial;
        MaterialOverride.GeneratedMaterialInstance = Result.GeneratedMaterialInstance;
        ++RepairedCount;
        Messages.Add(FString::Printf(
            TEXT("Slot %d repaired: shared=%s CPU=%s GPU=%s (%s)"),
            MaterialOverride.MaterialSlotIndex,
            *GetNameSafe(Result.GeneratedMaterial),
            *GetNameSafe(Result.GeneratedMaterialInstance),
            *GetNameSafe(Result.GeneratedMaterialInstance),
            *Result.Message));
    }

    if (RepairedCount > 0)
    {
        WetClothingAsset->MarkPackageDirty();
    }
    OutReport = FString::Join(Messages, TEXT("\n"));
    return RepairedCount > 0;
}

bool UDWCMaterialSetupEditorLibrary::BakeRenderProfileDataAndUpdateMaterials(
    UWetClothingAsset* WetClothingAsset,
    FString& OutReport,
    bool& bOutHadWarnings)
{
    bOutHadWarnings = false;
    OutReport.Reset();
    if (!WetClothingAsset)
    {
        OutReport = TEXT("WetClothingAsset is null.");
        return false;
    }

    if (!FWetClothingRenderProfileBakeService::BakeRenderProfileDataAndUpdateMaterials(
            WetClothingAsset,
            OutReport,
            &bOutHadWarnings))
    {
        return false;
    }

    const bool bSaved = FWetClothingRenderProfileBakeService::SaveBakedRenderProfileAssets(WetClothingAsset);
    if (!bSaved)
    {
        bOutHadWarnings = true;
        OutReport += TEXT("\n\nWarning: Render profile data was generated, but one or more generated assets could not be saved.");
    }
    return true;
}
