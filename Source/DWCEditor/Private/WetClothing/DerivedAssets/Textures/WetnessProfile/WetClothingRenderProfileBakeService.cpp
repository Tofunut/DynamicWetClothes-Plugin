#include "WetClothing/DerivedAssets/Textures/WetnessProfile/WetClothingRenderProfileBakeService.h"

#include "DataAssets/WetClothingAsset.h"
#include "Engine/SkeletalMesh.h"
#include "FileHelpers.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialFunctionInterface.h"
#include "Materials/MaterialInstanceConstant.h"
#include "UObject/Package.h"
#include "WetClothing/DerivedAssets/Materials/WCAMaterialGenerator.h"
#include "WetClothing/DerivedAssets/Textures/WetnessProfile/WetClothingWetPartDataTextureBaker.h"
#include "WetClothing/DerivedAssets/Textures/WetnessProfile/WetClothingSurfaceTextureNormalizer.h"

namespace
{
    void CollectWetMaterialSlots(const UWetClothingAsset* Asset, TSet<int32>& OutSlots)
    {
        OutSlots.Reset();
        if (Asset == nullptr)
        {
            return;
        }

        for (const FWetClothingAuthoredMaterialSlot& SlotData : Asset->Authored.PartData.EditableWetPartData.MaterialSlots)
        {
            if (!SlotData.bIsWettableSlot || SlotData.MaterialSlotIndex == INDEX_NONE)
            {
                continue;
            }

            const bool bHasAssignedIslands = SlotData.WetPartEntries.ContainsByPredicate(
                [](const FWetClothingWetPartEntry& Entry)
                {
                    return Entry.AssignedUVIslandIDs.Num() > 0;
                });
            if (bHasAssignedIslands)
            {
                OutSlots.Add(SlotData.MaterialSlotIndex);
            }
        }
    }

    void AddRenderProfilePackageForObject(UObject* Object, TArray<UPackage*>& InOutPackages)
    {
        if (Object != nullptr)
        {
            if (UPackage* Package = Object->GetOutermost())
            {
                InOutPackages.AddUnique(Package);
            }
        }
    }

    UMaterialInterface* ResolveVisualBakeSourceMaterial(
        const UWetClothingAsset* Asset,
        UMaterialInterface*      CandidateMaterial)
    {
        if (Asset == nullptr || CandidateMaterial == nullptr)
        {
            return CandidateMaterial;
        }

        UMaterial* CandidateBase = CandidateMaterial->GetMaterial();
        for (const FWetClothingGeneratedWetMaterialOverride& MaterialOverride :
             Asset->Derived.Inline.GeneratedWetMaterialOverrides)
        {
            UMaterialInterface* SourceMaterial = MaterialOverride.SourceMaterial.Get();
            UMaterial* GeneratedMaterial = MaterialOverride.GeneratedMaterial.Get();
            UMaterialInterface* CPUMaterialInstance = MaterialOverride.CPUMaterialInstance.Get();
            UMaterialInterface* GPUMaterialInstance = MaterialOverride.GPUMaterialInstance.Get();
            if (SourceMaterial != nullptr &&
                (CandidateMaterial == SourceMaterial ||
                 CandidateMaterial == GeneratedMaterial ||
                 CandidateMaterial == CPUMaterialInstance ||
                 CandidateMaterial == GPUMaterialInstance ||
                 CandidateBase == GeneratedMaterial))
            {
                return SourceMaterial;
            }
        }

        return CandidateMaterial;
    }
}

bool FWetClothingRenderProfileBakeService::HasPendingVisualBakeTasks(
    const UWetClothingAsset* WetClothingAsset,
    FString* OutSummary)
{
    TArray<FString> PendingLines;
    if (WetClothingAsset == nullptr || WetClothingAsset->GetRuntimeSkeletalMesh() == nullptr)
    {
        PendingLines.Add(TEXT("Prepared DWC Skeletal Mesh is unavailable."));
    }
    else if (!WetClothingAsset->HasValidDataUVForLOD(WetClothingAsset->GetSimulationLODIndex()))
    {
        PendingLines.Add(TEXT("DWC Data UV must be rebuilt before the Wet Part Data Texture can be baked."));
    }
    else
    {
        TSet<int32> WetMaterialSlots;
        CollectWetMaterialSlots(WetClothingAsset, WetMaterialSlots);
        if (!WetMaterialSlots.IsEmpty())
        {
            const TArray<FSkeletalMaterial>& Materials = WetClothingAsset->GetRuntimeSkeletalMesh()->GetMaterials();

            for (const int32 MaterialSlotIndex : WetMaterialSlots)
            {
                if (!Materials.IsValidIndex(MaterialSlotIndex))
                {
                    PendingLines.Add(FString::Printf(TEXT("Material slot %d is out of range."), MaterialSlotIndex));
                    continue;
                }

                const FWetClothingGeneratedWetMaterialOverride* Override =
                    WetClothingAsset->Derived.Inline.GeneratedWetMaterialOverrides.FindByPredicate(
                        [MaterialSlotIndex](const FWetClothingGeneratedWetMaterialOverride& Candidate)
                        {
                            return Candidate.MaterialSlotIndex == MaterialSlotIndex;
                        });

                UMaterialInterface* SourceMaterial = ResolveVisualBakeSourceMaterial(
                    WetClothingAsset,
                    Materials[MaterialSlotIndex].MaterialInterface);
                if (Override == nullptr ||
                    Override->GeneratedMaterial == nullptr ||
                    Override->CPUMaterialInstance == nullptr ||
                    Override->GPUMaterialInstance == nullptr ||
                    Override->SourceMaterial != SourceMaterial)
                {
                    PendingLines.Add(FString::Printf(TEXT("Unified wet material setup is required for slot %d."), MaterialSlotIndex));
                }
            }

            const FWetClothingBakedWetPartData& Baked = WetClothingAsset->Derived.Inline.BakedWetPartData;
            const FString ExpectedSignature = FWetClothingWetPartDataTextureBaker::MakeBuildSignature(WetClothingAsset);
            if (!Baked.IsValid())
            {
                PendingLines.Add(TEXT("Wet Part Data Texture bake is required."));
            }
            else if (Baked.DataUVChannelIndex != WetClothingAsset->GetDWCDataUVChannelIndex())
            {
                PendingLines.Add(TEXT("Wet Part Data Texture was built for an old DWC Data UV channel."));
            }
            else if (Baked.Resolution != DWCWetPartDataTextureBake::Resolution ||
                     Baked.PaddingPixels != DWCWetPartDataTextureBake::PaddingPixels ||
                     Baked.SurfaceTextureResolution != DWCSurfaceTextureNormalization::Resolution)
            {
                PendingLines.Add(TEXT("Wet Part Data Texture fixed bake settings are outdated."));
            }
            else if (Baked.BuildSignature != ExpectedSignature)
            {
                PendingLines.Add(TEXT("Wet Part Data Texture data is out of date."));
            }
        }
    }

    if (OutSummary != nullptr)
    {
        *OutSummary = PendingLines.IsEmpty()
            ? TEXT("Render profile data is up to date.")
            : FString::Printf(TEXT("Pending Render Profile Bake:\n- %s"), *FString::Join(PendingLines, TEXT("\n- ")));
    }
    return !PendingLines.IsEmpty();
}

bool FWetClothingRenderProfileBakeService::BakeRenderProfileDataAndUpdateMaterials(
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
        OutSummary = TEXT("Assign a Source Skeletal Mesh and generate DWC Data UV before baking render profile data.");
        return false;
    }
    if (!WetClothingAsset->HasValidDataUVForLOD(WetClothingAsset->GetSimulationLODIndex()))
    {
        OutSummary = TEXT("Rebuild DWC Data UV before baking the Wet Part Data Texture.");
        return false;
    }

    TSet<int32> WetMaterialSlots;
    CollectWetMaterialSlots(WetClothingAsset, WetMaterialSlots);
    if (WetMaterialSlots.IsEmpty())
    {
        OutSummary = TEXT("No wettable WetPart material slots were found.");
        return false;
    }

    TArray<FString> UpdatedMaterials;
    TArray<FString> Warnings;

    WetClothingAsset->Modify();
    WetClothingAsset->Derived.Inline.GeneratedWetMaterialOverrides.RemoveAll(
        [WetClothingAsset](const FWetClothingGeneratedWetMaterialOverride& Override)
        {
            return Override.MaterialSlotIndex != INDEX_NONE &&
                   !WetClothingAsset->IsMaterialSlotWettable(Override.MaterialSlotIndex);
        });

    const TArray<FSkeletalMaterial>& Materials = WetClothingAsset->GetRuntimeSkeletalMesh()->GetMaterials();
    for (const int32 MaterialSlotIndex : WetMaterialSlots)
    {
        if (!Materials.IsValidIndex(MaterialSlotIndex) || Materials[MaterialSlotIndex].MaterialInterface == nullptr)
        {
            Warnings.Add(FString::Printf(TEXT("Material slot %d has no valid source material."), MaterialSlotIndex));
            continue;
        }

        UMaterialInterface* SourceMaterial = ResolveVisualBakeSourceMaterial(
            WetClothingAsset,
            Materials[MaterialSlotIndex].MaterialInterface);
        if (SourceMaterial == nullptr)
        {
            Warnings.Add(FString::Printf(TEXT("Material slot %d has no resolvable source material."), MaterialSlotIndex));
            continue;
        }

        const FWCAMaterialGenerator::FOptions Options =
            FWCAMaterialGenerator::MakeOptionsForAsset(
                WetClothingAsset,
                EDWCSimulationMode::VertexCPU,
                MaterialSlotIndex);
        const FWetClothingUnifiedMaterialSetupResult MaterialSet =
            FWCAMaterialGenerator::CreateOrUpdateUnifiedMaterialSet(SourceMaterial, Options);
        if (!MaterialSet.bSucceeded ||
            MaterialSet.GeneratedMaterial == nullptr ||
            MaterialSet.CPUMaterialInstance == nullptr ||
            MaterialSet.GPUMaterialInstance == nullptr)
        {
            Warnings.Add(FString::Printf(
                TEXT("Slot %d material setup failed: %s"),
                MaterialSlotIndex,
                *MaterialSet.Message));
            continue;
        }

        FWetClothingGeneratedWetMaterialOverride* Override =
            WetClothingAsset->Derived.Inline.GeneratedWetMaterialOverrides.FindByPredicate(
                [MaterialSlotIndex](const FWetClothingGeneratedWetMaterialOverride& Candidate)
                {
                    return Candidate.MaterialSlotIndex == MaterialSlotIndex;
                });
        if (Override == nullptr)
        {
            Override = &WetClothingAsset->Derived.Inline.GeneratedWetMaterialOverrides.AddDefaulted_GetRef();
            Override->MaterialSlotIndex = MaterialSlotIndex;
        }
        Override->SourceMaterial = SourceMaterial;
        Override->GeneratedMaterial = MaterialSet.GeneratedMaterial;
        Override->CPUMaterialInstance = MaterialSet.CPUMaterialInstance;
        Override->GPUMaterialInstance = MaterialSet.GPUMaterialInstance;

        UpdatedMaterials.Add(FString::Printf(
            TEXT("Slot %d -> %s / %s / %s"),
            MaterialSlotIndex,
            *GetNameSafe(MaterialSet.GeneratedMaterial),
            *GetNameSafe(MaterialSet.CPUMaterialInstance),
            *GetNameSafe(MaterialSet.GPUMaterialInstance)));
    }

    FWetClothingWetPartDataTextureBakeResult WetPartDataResult;
    FString WetPartDataError;
    if (!FWetClothingWetPartDataTextureBaker::Bake(WetClothingAsset, WetPartDataResult, WetPartDataError))
    {
        OutSummary = FString::Printf(TEXT("Wet Part Data Texture bake failed: %s"), *WetPartDataError);
        return false;
    }
    if (!WetClothingAsset->Derived.Inline.BakedWetPartData.IsValid())
    {
        OutSummary = TEXT("Wet Part Data Texture bake completed but did not produce runtime-usable profile data.");
        return false;
    }

    WetClothingAsset->MarkPackageDirty();

    TArray<FString> Sections;
    TArray<FString> WetPartDataTextureLines;
    for (const FWetClothingWetPartDataSlotBakeResult& SlotResult : WetPartDataResult.SlotResults)
    {
        WetPartDataTextureLines.Add(FString::Printf(
            TEXT("Slot %d -> %s (%d painted pixels)"),
            SlotResult.MaterialSlotIndex,
            *GetNameSafe(SlotResult.WetPartDataTexture.Get()),
            SlotResult.PaintedPixelCount));
    }
    Sections.Add(FString::Printf(
        TEXT("Wet Part Data Textures:\n- %s\n- DWC Data UV channel %d\n- %d local profiles\n- %d total painted pixels"),
        *FString::Join(WetPartDataTextureLines, TEXT("\n- ")),
        WetClothingAsset->GetDWCDataUVChannelIndex(),
        WetPartDataResult.LocalProfileCount,
        WetPartDataResult.PaintedPixelCount));
    if (!UpdatedMaterials.IsEmpty())
    {
        Sections.Add(FString::Printf(TEXT("Wet materials:\n- %s"), *FString::Join(UpdatedMaterials, TEXT("\n- "))));
    }
    if (!Warnings.IsEmpty())
    {
        Sections.Add(FString::Printf(TEXT("Warnings:\n- %s"), *FString::Join(Warnings, TEXT("\n- "))));
    }

    OutSummary = FString::Join(Sections, TEXT("\n\n"));
    if (OutHadWarnings != nullptr)
    {
        *OutHadWarnings = !Warnings.IsEmpty();
    }
    return true;
}

bool FWetClothingRenderProfileBakeService::SaveBakedRenderProfileAssets(UWetClothingAsset* WetClothingAsset)
{
    if (WetClothingAsset == nullptr)
    {
        return false;
    }

    TArray<UPackage*> PackagesToSave;
    AddRenderProfilePackageForObject(WetClothingAsset, PackagesToSave);
    for (const FWetClothingBakedWetPartDataSlotTexture& SlotTexture :
         WetClothingAsset->Derived.Inline.BakedWetPartData.SlotTextures)
    {
        AddRenderProfilePackageForObject(SlotTexture.WetPartDataTexture.Get(), PackagesToSave);
    }
    for (const FWetClothingLocalRenderProfile& LocalProfile :
         WetClothingAsset->Derived.Inline.BakedWetPartData.LocalProfiles)
    {
        AddRenderProfilePackageForObject(LocalProfile.NormalizedDropletNormal.Get(), PackagesToSave);
        AddRenderProfilePackageForObject(LocalProfile.NormalizedRivuletNormal.Get(), PackagesToSave);
    }

    for (const FWetClothingGeneratedWetMaterialOverride& Override : WetClothingAsset->Derived.Inline.GeneratedWetMaterialOverrides)
    {
        AddRenderProfilePackageForObject(Override.GeneratedMaterial.Get(), PackagesToSave);
        AddRenderProfilePackageForObject(Override.CPUMaterialInstance.Get(), PackagesToSave);
        AddRenderProfilePackageForObject(Override.GPUMaterialInstance.Get(), PackagesToSave);
    }

    // Save the shared surface-appearance function recorded by this WCA.
#if WITH_EDITORONLY_DATA
    AddRenderProfilePackageForObject(
        WetClothingAsset->Derived.Inline.GeneratedEvaluateSurfaceAppearanceFunction.Get(),
        PackagesToSave);
#endif

    return PackagesToSave.IsEmpty() ||
           FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSave, false, false) == FEditorFileUtils::PR_Success;
}
