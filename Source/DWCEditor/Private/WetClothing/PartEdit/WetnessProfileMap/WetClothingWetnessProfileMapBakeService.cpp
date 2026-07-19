#include "WetClothing/PartEdit/WetnessProfileMap/WetClothingWetnessProfileMapBakeService.h"

#include "DataAssets/WetClothingAsset.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture.h"
#include "FileHelpers.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceConstant.h"
#include "UObject/Package.h"
#include "WetClothing/Common/Material/WetClothingMaterialSetup.h"
#include "WetClothing/Common/Texture/WetClothingMaterialTextureResolver.h"
#include "WetClothing/PartEdit/WetnessProfileMap/WetClothingWetnessProfileMapBaker.h"

namespace
{
    bool IsWetPartEntryRelevantForVisualBake(const FWetClothingWetPartEntry& Entry)
    {
        return Entry.MaterialSlotIndex != INDEX_NONE &&
               Entry.UVChannelIndex != INDEX_NONE &&
               Entry.AssignedUVIslandIDs.Num() > 0 &&
               Entry.ProfileAssignment.SourceProfile.IsValid();
    }

    FString MakeTextureUvKey(const UTexture* Texture, const int32 UVChannelIndex)
    {
        return Texture != nullptr && UVChannelIndex != INDEX_NONE
                   ? FString::Printf(TEXT("%s|%d"), *Texture->GetPathName(), UVChannelIndex)
                   : FString();
    }

    const FWetClothingBakedWetnessProfileMap* FindBakedWetnessProfileMap(
        const UWetClothingAsset* WetClothingAsset,
        UTexture* SourceTexture,
        const int32 UVChannelIndex)
    {
        if (WetClothingAsset == nullptr || SourceTexture == nullptr || UVChannelIndex == INDEX_NONE)
        {
            return nullptr;
        }

        return WetClothingAsset->PartData.BakedWetnessProfileMaps.FindByPredicate(
            [SourceTexture, UVChannelIndex](const FWetClothingBakedWetnessProfileMap& BakedWetnessProfileMap)
            {
                return BakedWetnessProfileMap.SourceTexture == SourceTexture &&
                       BakedWetnessProfileMap.UVChannelIndex == UVChannelIndex;
            });
    }

    void CollectWetBakeScopes(
        const UWetClothingAsset* WetClothingAsset,
        TSet<int32>& OutWetMaterialSlots,
        TSet<FIntPoint>& OutWetPartScopePairs)
    {
        OutWetMaterialSlots.Reset();
        OutWetPartScopePairs.Reset();

        if (WetClothingAsset == nullptr)
        {
            return;
        }

        for (const FWetClothingWetPartEntry& Entry : WetClothingAsset->PartData.EditableWetPartData.WetPartEntries)
        {
            if (IsWetPartEntryRelevantForVisualBake(Entry) &&
                WetClothingAsset->IsMaterialSlotWettable(Entry.MaterialSlotIndex))
            {
                OutWetMaterialSlots.Add(Entry.MaterialSlotIndex);
                OutWetPartScopePairs.Add(FIntPoint(Entry.MaterialSlotIndex, Entry.UVChannelIndex));
            }
        }
    }

    void AddWetnessProfileMapPackageForObject(UObject* Object, TArray<UPackage*>& InOutPackages)
    {
        if (Object == nullptr)
        {
            return;
        }

        if (UPackage* Package = Object->GetOutermost())
        {
            InOutPackages.AddUnique(Package);
        }
    }
}

bool FWetClothingWetnessProfileMapBakeService::HasPendingVisualBakeTasks(
    const UWetClothingAsset* WetClothingAsset,
    FString* OutSummary)
{
    if (WetClothingAsset == nullptr || WetClothingAsset->GetRuntimeSkeletalMesh() == nullptr)
    {
        if (OutSummary != nullptr)
        {
            *OutSummary = TEXT("Visual maps have no pending work.");
        }
        return false;
    }

    TSet<int32> WetMaterialSlots;
    TSet<FIntPoint> WetPartScopePairs;
    CollectWetBakeScopes(WetClothingAsset, WetMaterialSlots, WetPartScopePairs);

    TSet<FString> ExpectedTextureUvKeys;
    TArray<FString> PendingLines;

    for (const FWetClothingGeneratedWetMaterialOverride& MaterialOverride : WetClothingAsset->PartData.GeneratedWetMaterialOverrides)
    {
        if (MaterialOverride.MaterialSlotIndex != INDEX_NONE &&
            !WetClothingAsset->IsMaterialSlotWettable(MaterialOverride.MaterialSlotIndex))
        {
            PendingLines.Add(FString::Printf(TEXT("Stale wet material override will be removed for non-wettable slot %d."), MaterialOverride.MaterialSlotIndex));
        }
    }

    const TArray<FSkeletalMaterial>& Materials = WetClothingAsset->GetRuntimeSkeletalMesh()->GetMaterials();
    for (const int32 MaterialSlotIndex : WetMaterialSlots)
    {
        const FWetClothingGeneratedWetMaterialOverride* MaterialOverride = WetClothingAsset->PartData.GeneratedWetMaterialOverrides.FindByPredicate(
            [MaterialSlotIndex](const FWetClothingGeneratedWetMaterialOverride& ExistingOverride)
            {
                return ExistingOverride.MaterialSlotIndex == MaterialSlotIndex;
            });

        if (!Materials.IsValidIndex(MaterialSlotIndex))
        {
            PendingLines.Add(FString::Printf(TEXT("Material slot %d is out of range."), MaterialSlotIndex));
            continue;
        }

        UMaterialInterface* ExistingCPUMaterialInstance =
            MaterialOverride != nullptr ? MaterialOverride->CPUMaterialInstance.Get() : nullptr;
        UMaterialInterface* ExistingGPUMaterialInstance =
            MaterialOverride != nullptr ? MaterialOverride->GPUMaterialInstance.Get() : nullptr;

        if (MaterialOverride == nullptr || MaterialOverride->GeneratedMaterial == nullptr ||
            ExistingCPUMaterialInstance == nullptr || ExistingGPUMaterialInstance == nullptr)
        {
            PendingLines.Add(FString::Printf(TEXT("Material setup needed for slot %d."), MaterialSlotIndex));
        }
        else if (MaterialOverride->SourceMaterial != Materials[MaterialSlotIndex].MaterialInterface)
        {
            PendingLines.Add(FString::Printf(TEXT("Material setup source changed for slot %d."), MaterialSlotIndex));
        }
        else if (!FWetClothingMaterialSetup::IsMaterialConfiguredForDwc(
                     ExistingCPUMaterialInstance,
                     FWetClothingMaterialSetup::MakeOptionsForAsset(WetClothingAsset, EDWCSimulationMode::VertexCPU)) ||
                 !FWetClothingMaterialSetup::IsMaterialConfiguredForDwc(
                     ExistingGPUMaterialInstance,
                     FWetClothingMaterialSetup::MakeOptionsForAsset(WetClothingAsset, EDWCSimulationMode::WetnessMapGPU)))
        {
            PendingLines.Add(FString::Printf(
                TEXT("Wet material override on slot %d is missing the unified DWC graph or a valid CPU/GPU static permutation."),
                MaterialSlotIndex));
        }
    }

    for (const FIntPoint& WetPartScopePair : WetPartScopePairs)
    {
        UTexture* SourceTexture = FWetClothingMaterialTextureResolver::FindSavedTextureSelection(
            WetClothingAsset,
            WetPartScopePair.X,
            WetPartScopePair.Y);
        if (SourceTexture == nullptr)
        {
            PendingLines.Add(FString::Printf(TEXT("Wetness Profile Map source texture is not selected for slot %d UV Channel %d."), WetPartScopePair.X, WetPartScopePair.Y));
            continue;
        }
        ExpectedTextureUvKeys.Add(MakeTextureUvKey(SourceTexture, WetPartScopePair.Y));

        TArray<int32> MaterialSlotIndices;
        for (const FWetClothingSourceTextureSelection& Selection : WetClothingAsset->PartData.EditableWetPartData.SourceTextureSelections)
        {
            if (Selection.Texture == SourceTexture &&
                Selection.UVChannelIndex == WetPartScopePair.Y &&
                WetMaterialSlots.Contains(Selection.MaterialSlotIndex))
            {
                MaterialSlotIndices.AddUnique(Selection.MaterialSlotIndex);
            }
        }
        MaterialSlotIndices.Sort();

        const FWetClothingBakedWetnessProfileMap* ExistingWetnessProfileMap =
            FindBakedWetnessProfileMap(WetClothingAsset, SourceTexture, WetPartScopePair.Y);
        if (ExistingWetnessProfileMap == nullptr || ExistingWetnessProfileMap->WetnessProfileMap0 == nullptr)
        {
            PendingLines.Add(FString::Printf(TEXT("Wetness Profile Map bake needed for '%s' UV Channel %d."), *GetNameSafe(SourceTexture), WetPartScopePair.Y));
        }
        else if (ExistingWetnessProfileMap->MaterialSlotIndices != MaterialSlotIndices)
        {
            PendingLines.Add(FString::Printf(TEXT("Wetness Profile Map slot list is outdated for '%s' UV Channel %d."), *GetNameSafe(SourceTexture), WetPartScopePair.Y));
        }
        else if (ExistingWetnessProfileMap->Resolution != DWCWetnessProfileMapBake::Resolution ||
                 ExistingWetnessProfileMap->PaddingPixels != DWCWetnessProfileMapBake::PaddingPixels)
        {
            PendingLines.Add(FString::Printf(TEXT("Wetness Profile Map fixed bake settings are outdated for '%s' UV Channel %d."), *GetNameSafe(SourceTexture), WetPartScopePair.Y));
        }
        else
        {
            const FString CurrentBuildSignature = FWetClothingWetnessProfileMapBaker::MakeBuildSignature(
                WetClothingAsset,
                SourceTexture,
                WetPartScopePair.Y,
                MaterialSlotIndices);
            if (ExistingWetnessProfileMap->BuildSignature != CurrentBuildSignature)
            {
                PendingLines.Add(FString::Printf(TEXT("Wetness Profile Map data is outdated for '%s' UV Channel %d."), *GetNameSafe(SourceTexture), WetPartScopePair.Y));
            }
        }
    }

    for (const FWetClothingBakedWetnessProfileMap& BakedWetnessProfileMap : WetClothingAsset->PartData.BakedWetnessProfileMaps)
    {
        const FString TextureUvKey = MakeTextureUvKey(BakedWetnessProfileMap.SourceTexture.Get(), BakedWetnessProfileMap.UVChannelIndex);
        if (!TextureUvKey.IsEmpty() && !ExpectedTextureUvKeys.Contains(TextureUvKey))
        {
            PendingLines.Add(FString::Printf(
                TEXT("Stale Wetness Profile Map reference will be removed for '%s' UV Channel %d."),
                *GetNameSafe(BakedWetnessProfileMap.SourceTexture.Get()),
                BakedWetnessProfileMap.UVChannelIndex));
        }
    }

    if (OutSummary != nullptr)
    {
        *OutSummary = PendingLines.Num() == 0
                          ? TEXT("Visual maps are up to date.")
                          : FString::Printf(TEXT("Pending Visual Bake:\n- %s"), *FString::Join(PendingLines, TEXT("\n- ")));
    }

    return PendingLines.Num() > 0;
}

bool FWetClothingWetnessProfileMapBakeService::BakeWetnessProfileMapsAndUpdateMaterials(
    UWetClothingAsset* WetClothingAsset,
    FString& OutSummary,
    bool* OutHadWarnings)
{
    if (OutHadWarnings != nullptr)
    {
        *OutHadWarnings = false;
    }

    if (WetClothingAsset == nullptr || WetClothingAsset->GetRuntimeSkeletalMesh() == nullptr)
    {
        OutSummary = TEXT("Assign a Source Skeletal Mesh and generate DWC Data UV before baking visual maps.");
        return false;
    }

    TSet<int32> WetMaterialSlots;
    TSet<FIntPoint> WetPartScopePairs;
    CollectWetBakeScopes(WetClothingAsset, WetMaterialSlots, WetPartScopePairs);

    if (WetMaterialSlots.Num() == 0)
    {
        OutSummary = TEXT("No WetPart material slots were found.");
        return false;
    }

    TArray<FString> CreatedOrUpdatedMaterials;
    TArray<FString> BakedWetnessProfileMaps;
    TArray<FString> RemovedStaleWetnessProfileMaps;
    TArray<FString> Skipped;
    TArray<FString> Warnings;

    WetClothingAsset->Modify();
    const int32 RemovedOverrideCount = WetClothingAsset->PartData.GeneratedWetMaterialOverrides.RemoveAll(
        [WetClothingAsset](const FWetClothingGeneratedWetMaterialOverride& MaterialOverride)
        {
            return MaterialOverride.MaterialSlotIndex != INDEX_NONE &&
                   !WetClothingAsset->IsMaterialSlotWettable(MaterialOverride.MaterialSlotIndex);
        });
    if (RemovedOverrideCount > 0)
    {
        WetClothingAsset->MarkPackageDirty();
    }

    const TArray<FSkeletalMaterial>& Materials = WetClothingAsset->GetRuntimeSkeletalMesh()->GetMaterials();
    for (const int32 MaterialSlotIndex : WetMaterialSlots)
    {
        if (!Materials.IsValidIndex(MaterialSlotIndex))
        {
            Warnings.Add(FString::Printf(TEXT("Material slot %d is out of range."), MaterialSlotIndex));
            continue;
        }

        UMaterialInterface* SourceMaterial = Materials[MaterialSlotIndex].MaterialInterface;
        if (SourceMaterial == nullptr)
        {
            Warnings.Add(FString::Printf(TEXT("Material slot %d has no source material."), MaterialSlotIndex));
            continue;
        }

        FWetClothingGeneratedWetMaterialOverride* ExistingOverride = WetClothingAsset->PartData.GeneratedWetMaterialOverrides.FindByPredicate(
            [MaterialSlotIndex](const FWetClothingGeneratedWetMaterialOverride& MaterialOverride)
            {
                return MaterialOverride.MaterialSlotIndex == MaterialSlotIndex;
            });

        const FWetClothingMaterialSetup::FOptions MaterialSetupOptions =
            FWetClothingMaterialSetup::MakeOptionsForAsset(WetClothingAsset, EDWCSimulationMode::VertexCPU);
        UMaterialInterface* ExistingCPUMaterialInstance =
            ExistingOverride != nullptr ? ExistingOverride->CPUMaterialInstance.Get() : nullptr;
        UMaterialInterface* ExistingGPUMaterialInstance =
            ExistingOverride != nullptr ? ExistingOverride->GPUMaterialInstance.Get() : nullptr;

        const bool bExistingSetValid =
            ExistingOverride != nullptr &&
            ExistingOverride->SourceMaterial == SourceMaterial &&
            ExistingOverride->GeneratedMaterial != nullptr &&
            ExistingCPUMaterialInstance != nullptr &&
            ExistingGPUMaterialInstance != nullptr &&
            FWetClothingMaterialSetup::IsMaterialConfiguredForDwc(
                ExistingCPUMaterialInstance,
                FWetClothingMaterialSetup::MakeOptionsForAsset(WetClothingAsset, EDWCSimulationMode::VertexCPU)) &&
            FWetClothingMaterialSetup::IsMaterialConfiguredForDwc(
                ExistingGPUMaterialInstance,
                FWetClothingMaterialSetup::MakeOptionsForAsset(WetClothingAsset, EDWCSimulationMode::WetnessMapGPU));

        const FWetClothingUnifiedMaterialSetupResult MaterialSet =
            FWetClothingMaterialSetup::CreateOrUpdateUnifiedMaterialSet(SourceMaterial, MaterialSetupOptions);
        if (!MaterialSet.bSucceeded || MaterialSet.GeneratedMaterial == nullptr ||
            MaterialSet.CPUMaterialInstance == nullptr || MaterialSet.GPUMaterialInstance == nullptr)
        {
            Warnings.Add(FString::Printf(
                TEXT("Slot %d material setup failed: %s"),
                MaterialSlotIndex,
                *MaterialSet.Message));
            continue;
        }

        WetClothingAsset->Modify();
        if (ExistingOverride == nullptr)
        {
            ExistingOverride = &WetClothingAsset->PartData.GeneratedWetMaterialOverrides.AddDefaulted_GetRef();
            ExistingOverride->MaterialSlotIndex = MaterialSlotIndex;
        }
        ExistingOverride->SourceMaterial = SourceMaterial;
        ExistingOverride->GeneratedMaterial = MaterialSet.GeneratedMaterial;
        ExistingOverride->CPUMaterialInstance = MaterialSet.CPUMaterialInstance;
        ExistingOverride->GPUMaterialInstance = MaterialSet.GPUMaterialInstance;
        WetClothingAsset->MarkPackageDirty();

        if (bExistingSetValid && MaterialSet.bAlreadyConfigured)
        {
            Skipped.Add(FString::Printf(
                TEXT("Slot %d unified material set refreshed: %s / %s / %s."),
                MaterialSlotIndex,
                *GetNameSafe(MaterialSet.GeneratedMaterial),
                *GetNameSafe(MaterialSet.CPUMaterialInstance),
                *GetNameSafe(MaterialSet.GPUMaterialInstance)));
        }
        else
        {
            CreatedOrUpdatedMaterials.Add(FString::Printf(
                TEXT("Slot %d -> shared %s, CPU %s, GPU %s"),
                MaterialSlotIndex,
                *GetNameSafe(MaterialSet.GeneratedMaterial),
                *GetNameSafe(MaterialSet.CPUMaterialInstance),
                *GetNameSafe(MaterialSet.GPUMaterialInstance)));
        }
    }

    TSet<FString> BakedTextureUvKeys;
    bool bCanPruneStaleWetnessProfileMaps = true;
    for (const FIntPoint& WetPartScopePair : WetPartScopePairs)
    {
        UTexture* SourceTexture = FWetClothingMaterialTextureResolver::ResolveOrSaveTextureSelection(
            WetClothingAsset,
            WetPartScopePair.X,
            WetPartScopePair.Y);
        if (SourceTexture == nullptr)
        {
            bCanPruneStaleWetnessProfileMaps = false;
            Warnings.Add(FString::Printf(TEXT("Slot %d UV Channel %d has no selected source texture."), WetPartScopePair.X, WetPartScopePair.Y));
            continue;
        }

        const FString TextureUvKey = MakeTextureUvKey(SourceTexture, WetPartScopePair.Y);
        if (BakedTextureUvKeys.Contains(TextureUvKey))
        {
            continue;
        }
        BakedTextureUvKeys.Add(TextureUvKey);

        TArray<int32> MaterialSlotIndices;
        for (const FWetClothingSourceTextureSelection& Selection : WetClothingAsset->PartData.EditableWetPartData.SourceTextureSelections)
        {
            if (Selection.Texture == SourceTexture &&
                Selection.UVChannelIndex == WetPartScopePair.Y &&
                WetMaterialSlots.Contains(Selection.MaterialSlotIndex))
            {
                MaterialSlotIndices.AddUnique(Selection.MaterialSlotIndex);
            }
        }
        MaterialSlotIndices.Sort();

        if (MaterialSlotIndices.Num() == 0)
        {
            Warnings.Add(FString::Printf(TEXT("No wet material slots use selected texture '%s' on UV Channel %d."), *GetNameSafe(SourceTexture), WetPartScopePair.Y));
            continue;
        }

        const FWetClothingBakedWetnessProfileMap* ExistingWetnessProfileMap =
            FindBakedWetnessProfileMap(WetClothingAsset, SourceTexture, WetPartScopePair.Y);
        const FString CurrentBuildSignature = FWetClothingWetnessProfileMapBaker::MakeBuildSignature(
            WetClothingAsset,
            SourceTexture,
            WetPartScopePair.Y,
            MaterialSlotIndices);
        const bool bNeedsBake = ExistingWetnessProfileMap == nullptr ||
                                ExistingWetnessProfileMap->WetnessProfileMap0 == nullptr ||
                                ExistingWetnessProfileMap->MaterialSlotIndices != MaterialSlotIndices ||
                                ExistingWetnessProfileMap->Resolution != DWCWetnessProfileMapBake::Resolution ||
                                ExistingWetnessProfileMap->PaddingPixels != DWCWetnessProfileMapBake::PaddingPixels ||
                                ExistingWetnessProfileMap->BuildSignature != CurrentBuildSignature;
        if (!bNeedsBake)
        {
            Skipped.Add(FString::Printf(TEXT("Wetness Profile Map for %s UV Channel %d is up to date."), *GetNameSafe(SourceTexture), WetPartScopePair.Y));
            continue;
        }

        const FWetClothingWetnessProfileMapBakeSettings Settings;

        FWetClothingWetnessProfileMapBakeResult Result;
        FString ErrorMessage;
        if (!FWetClothingWetnessProfileMapBaker::BakeWetnessProfileMap0(WetClothingAsset, SourceTexture, WetPartScopePair.Y, MaterialSlotIndices, Settings, Result, ErrorMessage))
        {
            Warnings.Add(FString::Printf(TEXT("Wetness Profile Map bake failed for %s UV Channel %d: %s"), *GetNameSafe(SourceTexture), WetPartScopePair.Y, *ErrorMessage));
            continue;
        }

        BakedWetnessProfileMaps.Add(FString::Printf(TEXT("%s -> %s"), *GetNameSafe(SourceTexture), *GetNameSafe(Result.WetnessProfileMap0.Get())));
    }

    for (int32 MapIndex = bCanPruneStaleWetnessProfileMaps ? WetClothingAsset->PartData.BakedWetnessProfileMaps.Num() - 1 : INDEX_NONE; MapIndex >= 0; --MapIndex)
    {
        const FWetClothingBakedWetnessProfileMap& BakedWetnessProfileMap = WetClothingAsset->PartData.BakedWetnessProfileMaps[MapIndex];
        const FString TextureUvKey = MakeTextureUvKey(BakedWetnessProfileMap.SourceTexture.Get(), BakedWetnessProfileMap.UVChannelIndex);
        if (TextureUvKey.IsEmpty() || BakedTextureUvKeys.Contains(TextureUvKey))
        {
            continue;
        }

        RemovedStaleWetnessProfileMaps.Add(FString::Printf(
            TEXT("%s UV Channel %d"),
            *GetNameSafe(BakedWetnessProfileMap.SourceTexture.Get()),
            BakedWetnessProfileMap.UVChannelIndex));

        WetClothingAsset->Modify();
        WetClothingAsset->PartData.BakedWetnessProfileMaps.RemoveAt(MapIndex);
        WetClothingAsset->MarkPackageDirty();
    }

    TArray<FString> Sections;
    if (CreatedOrUpdatedMaterials.Num() > 0)
    {
        Sections.Add(FString::Printf(TEXT("Wet materials:\n- %s"), *FString::Join(CreatedOrUpdatedMaterials, TEXT("\n- "))));
    }
    if (BakedWetnessProfileMaps.Num() > 0)
    {
        Sections.Add(FString::Printf(TEXT("Wetness Profile Maps:\n- %s"), *FString::Join(BakedWetnessProfileMaps, TEXT("\n- "))));
    }
    if (RemovedStaleWetnessProfileMaps.Num() > 0)
    {
        Sections.Add(FString::Printf(TEXT("Removed stale Wetness Profile Map references:\n- %s"), *FString::Join(RemovedStaleWetnessProfileMaps, TEXT("\n- "))));
    }
    if (Skipped.Num() > 0)
    {
        Sections.Add(FString::Printf(TEXT("Skipped:\n- %s"), *FString::Join(Skipped, TEXT("\n- "))));
    }
    if (Warnings.Num() > 0)
    {
        Sections.Add(FString::Printf(TEXT("Warnings:\n- %s"), *FString::Join(Warnings, TEXT("\n- "))));
    }

    if (Warnings.Num() > 0)
    {
        Sections.Insert(TEXT("Bake Maps completed with warnings. Successful outputs were kept."), 0);
    }

    OutSummary = Sections.Num() > 0 ? FString::Join(Sections, TEXT("\n\n")) : TEXT("Visual maps are already up to date.");

    if (OutHadWarnings != nullptr)
    {
        *OutHadWarnings = Warnings.Num() > 0;
    }

    return true;
}

bool FWetClothingWetnessProfileMapBakeService::SaveBakedWetnessAssets(UWetClothingAsset* WetClothingAsset)
{
    if (WetClothingAsset == nullptr)
    {
        return false;
    }

    TArray<UPackage*> PackagesToSave;
    AddWetnessProfileMapPackageForObject(WetClothingAsset, PackagesToSave);

    for (const FWetClothingGeneratedWetMaterialOverride& MaterialOverride : WetClothingAsset->PartData.GeneratedWetMaterialOverrides)
    {
        AddWetnessProfileMapPackageForObject(MaterialOverride.GeneratedMaterial.Get(), PackagesToSave);
        AddWetnessProfileMapPackageForObject(MaterialOverride.CPUMaterialInstance.Get(), PackagesToSave);
        AddWetnessProfileMapPackageForObject(MaterialOverride.GPUMaterialInstance.Get(), PackagesToSave);
    }

    for (const FWetClothingBakedWetnessProfileMap& BakedWetnessProfileMap : WetClothingAsset->PartData.BakedWetnessProfileMaps)
    {
        AddWetnessProfileMapPackageForObject(BakedWetnessProfileMap.WetnessProfileMap0.Get(), PackagesToSave);
    }

    if (PackagesToSave.Num() == 0)
    {
        return true;
    }

    return FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSave, false, false) == FEditorFileUtils::PR_Success;
}
