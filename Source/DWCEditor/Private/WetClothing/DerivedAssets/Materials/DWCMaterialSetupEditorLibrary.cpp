// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "WetClothing/DerivedAssets/Materials/DWCMaterialSetupEditorLibrary.h"

#include "DataAssets/WetClothingAsset.h"
#include "WetClothing/DerivedAssets/Materials/WCAMaterialGenerator.h"
#include "WetClothing/DerivedAssets/Textures/WetnessProfile/WetClothingRenderProfileBakeService.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"

bool UDWCMaterialSetupEditorLibrary::RepairGeneratedWetMaterials(
    UWetClothingAsset* WetClothingAsset,
    FString&           OutReport)
{
    OutReport.Reset();
    if (!WetClothingAsset)
    {
        OutReport = TEXT("WetClothingAsset is null.");
        return false;
    }

    int32           RepairedCount = 0;
    TArray<FString> Messages;
    WetClothingAsset->Modify();

    TArray<int32> MaterialSlotIndices;
    for (const FWetClothingGeneratedWetMaterialOverride& MaterialOverride :
         WetClothingAsset->Derived.Inline.GeneratedWetMaterialOverrides)
    {
        if (MaterialOverride.MaterialSlotIndex != INDEX_NONE)
        {
            MaterialSlotIndices.AddUnique(MaterialOverride.MaterialSlotIndex);
        }
    }
    MaterialSlotIndices.Sort();

    for (const int32 MaterialSlotIndex : MaterialSlotIndices)
    {
        const FWetClothingGeneratedWetMaterialOverride* ExistingOverride =
            WetClothingAsset->Derived.Inline.GeneratedWetMaterialOverrides.FindByPredicate(
                [MaterialSlotIndex](const FWetClothingGeneratedWetMaterialOverride& Candidate)
                {
                    return Candidate.MaterialSlotIndex == MaterialSlotIndex;
                });
        UMaterialInterface* SourceMaterial = ExistingOverride != nullptr
            ? ExistingOverride->SourceMaterial.Get()
            : nullptr;
        if (!SourceMaterial)
        {
            Messages.Add(FString::Printf(
                TEXT("Slot %d skipped: no source material."),
                MaterialSlotIndex));
            continue;
        }

        const FWCAMaterialGenerator::FOptions Options =
            FWCAMaterialGenerator::MakeOptionsForAsset(
                WetClothingAsset,
                EDWCSimulationMode::VertexCPU,
                MaterialSlotIndex);
        const FWetClothingUnifiedMaterialSetupResult Result =
            FWCAMaterialGenerator::CreateOrUpdateUnifiedMaterialSet(SourceMaterial, Options);
        if (!Result.bSucceeded || !Result.GeneratedMaterial ||
            !Result.GeneratedMaterialInstance)
        {
            Messages.Add(FString::Printf(
                TEXT("Slot %d failed: %s"),
                MaterialSlotIndex,
                *Result.Message));
            continue;
        }

        FString MetadataError;
        if (!FWCAMaterialGenerator::CommitGeneratedMaterialOverride(
                WetClothingAsset,
                MaterialSlotIndex,
                SourceMaterial,
                Result,
                &MetadataError))
        {
            Messages.Add(FString::Printf(
                TEXT("Slot %d metadata commit failed: %s"),
                MaterialSlotIndex,
                *MetadataError));
            continue;
        }

        ++RepairedCount;
        Messages.Add(FString::Printf(
            TEXT("Slot %d repaired: shared=%s runtime=%s (%s)"),
            MaterialSlotIndex,
            *GetNameSafe(Result.GeneratedMaterial),
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
    FString&           OutReport,
    bool&              bOutHadWarnings)
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
