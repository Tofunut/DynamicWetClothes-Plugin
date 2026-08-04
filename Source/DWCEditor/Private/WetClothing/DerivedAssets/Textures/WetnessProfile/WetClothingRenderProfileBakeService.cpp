#include "WetClothing/DerivedAssets/Textures/WetnessProfile/WetClothingRenderProfileBakeService.h"

#include "DataAssets/WetClothingAsset.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "FileHelpers.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialFunctionInterface.h"
#include "Materials/MaterialInstanceConstant.h"
#include "UObject/Package.h"
#include "WetClothing/DerivedAssets/Materials/WCAMaterialGenerator.h"
#include "WetClothing/DerivedAssets/Textures/WetnessProfile/WetClothingWetPartDataTextureBaker.h"
#include "WetClothing/DerivedAssets/Textures/WetnessProfile/WetClothingSurfaceTextureNormalizer.h"
#include "WetRendering/DWCSurfaceTextureSharedAsset.h"

namespace
{
    struct FExpectedWetPartRenderProfile
    {
        int32 MaterialSlotIndex = INDEX_NONE;
        const FWetClothingWetPartEntry* Entry = nullptr;
        const FWetPartProfileAssignment* Profile = nullptr;
    };

    bool IsWetPartRenderProfileBakeable(
        const FWetClothingAuthoredMaterialSlot& SlotData,
        const FWetClothingWetPartEntry& Entry)
    {
        return SlotData.bIsWettableSlot &&
               SlotData.MaterialSlotIndex != INDEX_NONE &&
               Entry.WetPartID != 0 &&
               !Entry.AssignedUVIslandIDs.IsEmpty();
    }

    void CollectExpectedWetPartRenderProfiles(
        const UWetClothingAsset& Asset,
        TArray<FExpectedWetPartRenderProfile>& OutProfiles)
    {
        OutProfiles.Reset();

        const FWetClothingEditableWetPartData& EditableData = Asset.Authored.PartData.EditableWetPartData;
        for (const FWetClothingAuthoredMaterialSlot& SlotData : EditableData.MaterialSlots)
        {
            for (const FWetClothingWetPartEntry& Entry : SlotData.WetPartEntries)
            {
                if (!IsWetPartRenderProfileBakeable(SlotData, Entry))
                {
                    continue;
                }

                FExpectedWetPartRenderProfile& ExpectedProfile = OutProfiles.AddDefaulted_GetRef();
                ExpectedProfile.MaterialSlotIndex = SlotData.MaterialSlotIndex;
                ExpectedProfile.Entry = &Entry;
                ExpectedProfile.Profile = EditableData.FindProfile(Entry);
            }
        }
    }

    void CollectWetMaterialSlots(const UWetClothingAsset* Asset, TSet<int32>& OutSlots)
    {
        OutSlots.Reset();
        if (Asset == nullptr)
        {
            return;
        }

        TArray<FExpectedWetPartRenderProfile> ExpectedProfiles;
        CollectExpectedWetPartRenderProfiles(*Asset, ExpectedProfiles);
        for (const FExpectedWetPartRenderProfile& ExpectedProfile : ExpectedProfiles)
        {
            OutSlots.Add(ExpectedProfile.MaterialSlotIndex);
        }
    }

    void AppendMissingRenderProfileBakeData(
        const UWetClothingAsset& Asset,
        const FWetClothingBakedWetPartData& Baked,
        TArray<FString>& PendingLines)
    {
        TArray<FExpectedWetPartRenderProfile> ExpectedProfiles;
        CollectExpectedWetPartRenderProfiles(Asset, ExpectedProfiles);
        if (ExpectedProfiles.IsEmpty())
        {
            return;
        }

        TSet<int32> CheckedMaterialSlots;
        TSet<FString> CheckedProfileKeys;
        TMap<FString, const FWetClothingLocalRenderProfile*> BakedProfilesByKey;
        for (const FWetClothingLocalRenderProfile& LocalProfile : Baked.LocalProfiles)
        {
            if (!LocalProfile.StableKey.IsEmpty())
            {
                BakedProfilesByKey.Add(LocalProfile.StableKey, &LocalProfile);
            }
        }

        for (const FExpectedWetPartRenderProfile& ExpectedProfile : ExpectedProfiles)
        {
            if (!CheckedMaterialSlots.Contains(ExpectedProfile.MaterialSlotIndex))
            {
                CheckedMaterialSlots.Add(ExpectedProfile.MaterialSlotIndex);
                if (Baked.FindSlot(ExpectedProfile.MaterialSlotIndex) == nullptr)
                {
                    PendingLines.Add(FString::Printf(
                        TEXT("Wet Part Data Texture is missing for slot %d."),
                        ExpectedProfile.MaterialSlotIndex));
                }
            }

            FWetnessProfileParameters Parameters;
            FWetClothingWetPartDataTextureBaker::ResolveProfileParameters(ExpectedProfile.Profile, Parameters);
            const FString StableKey =
                FWetClothingWetPartDataTextureBaker::MakeProfileStableKey(ExpectedProfile.Profile, Parameters);
            if (StableKey.IsEmpty() || CheckedProfileKeys.Contains(StableKey))
            {
                continue;
            }

            CheckedProfileKeys.Add(StableKey);
            const FWetClothingLocalRenderProfile* const* BakedProfilePtr =
                BakedProfilesByKey.Find(StableKey);
            const FWetClothingLocalRenderProfile* BakedProfile =
                BakedProfilePtr != nullptr ? *BakedProfilePtr : nullptr;
            if (BakedProfile == nullptr)
            {
                const int32 WetPartID = ExpectedProfile.Entry != nullptr
                    ? ExpectedProfile.Entry->WetPartID
                    : INDEX_NONE;
                PendingLines.Add(FString::Printf(
                    TEXT("Wet Part %d in slot %d uses a profile that is missing from baked Render Profile Lookup Texture."),
                    WetPartID,
                    ExpectedProfile.MaterialSlotIndex));
                continue;
            }

            const FSurfaceWaterProfileParameters& Surface = Parameters.SurfaceWater;
            if (!Surface.bEnabled)
            {
                continue;
            }

            FString SurfaceTextureError;
            if (!FWetClothingSurfaceTextureNormalizer::ValidateProfileTextures(
                    Parameters,
                    SurfaceTextureError))
            {
                PendingLines.Add(FString::Printf(
                    TEXT("Profile '%s' has invalid authored Surface Water texture settings: %s"),
                    *StableKey,
                    *SurfaceTextureError));
                continue;
            }

            const auto SourcePathMatches = [](
                const UTexture2D* AuthoredTexture,
                const FSoftObjectPath& BakedSourcePath)
            {
                return AuthoredTexture != nullptr
                    ? BakedSourcePath == FSoftObjectPath(AuthoredTexture)
                    : !BakedSourcePath.IsValid();
            };

            if (!SourcePathMatches(
                    Surface.DropletNormalTexture,
                    BakedProfile->SourceDropletNormal) ||
                !FWetClothingSurfaceTextureNormalizer::IsPreparedTextureReferenceCurrent(
                    BakedProfile->NormalizedDropletNormal,
                    Surface.DropletNormalTexture,
                    TEXT("DropletNormal"),
                    true))
            {
                PendingLines.Add(FString::Printf(
                    TEXT("Profile '%s' requires a Render Profile Lookup Texture rebake so its 512 Droplet normal reference is regenerated."),
                    *StableKey));
            }
            if (!SourcePathMatches(
                    Surface.DropletMaskTexture,
                    BakedProfile->SourceDropletMask) ||
                !FWetClothingSurfaceTextureNormalizer::IsPreparedTextureReferenceCurrent(
                    BakedProfile->NormalizedDropletMask,
                    Surface.DropletMaskTexture,
                    TEXT("DropletMask"),
                    false))
            {
                PendingLines.Add(FString::Printf(
                    TEXT("Profile '%s' requires a Render Profile Lookup Texture rebake so its 512 Droplet mask reference is regenerated."),
                    *StableKey));
            }
            if (!SourcePathMatches(
                    Surface.DropletFlowNormalTexture,
                    BakedProfile->SourceDropletFlowNormal) ||
                !FWetClothingSurfaceTextureNormalizer::IsPreparedTextureReferenceCurrent(
                    BakedProfile->NormalizedDropletFlowNormal,
                    Surface.DropletFlowNormalTexture,
                    TEXT("DropletFlowNormal"),
                    true))
            {
                PendingLines.Add(FString::Printf(
                    TEXT("Profile '%s' requires a Render Profile Data rebake so its Droplet2 normal reference is regenerated."),
                    *StableKey));
            }
            if (!SourcePathMatches(
                    Surface.DropletFlowMaskTexture,
                    BakedProfile->SourceDropletFlowMask) ||
                !FWetClothingSurfaceTextureNormalizer::IsPreparedTextureReferenceCurrent(
                    BakedProfile->NormalizedDropletFlowMask,
                    Surface.DropletFlowMaskTexture,
                    TEXT("DropletFlowMask"),
                    false))
            {
                PendingLines.Add(FString::Printf(
                    TEXT("Profile '%s' requires a Render Profile Data rebake so its Droplet2 mask reference is regenerated."),
                    *StableKey));
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
        PendingLines.Add(TEXT("The sealed DWC UV Channel is invalid; create a new WCA before building the Render Profile Lookup Texture."));
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

                UMaterialInterface* SourceMaterial = FWCAMaterialGenerator::ResolveGeneratedMaterialSource(
                    WetClothingAsset,
                    MaterialSlotIndex,
                    Materials[MaterialSlotIndex].MaterialInterface);
                if (Override == nullptr ||
                    Override->GeneratedMaterial == nullptr ||
                    Override->GeneratedMaterialInstance == nullptr ||
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
                PendingLines.Add(TEXT("Wet Part Data Texture was built for an old DWC UV Channel."));
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

            if (Baked.IsValid())
            {
                AppendMissingRenderProfileBakeData(*WetClothingAsset, Baked, PendingLines);
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
        OutSummary = TEXT("Assign a Source Skeletal Mesh and generate DWC UV Channel before baking render profile data.");
        return false;
    }
    if (!WetClothingAsset->HasValidDataUVForLOD(WetClothingAsset->GetSimulationLODIndex()))
    {
        OutSummary = TEXT("The sealed DWC UV Channel is invalid. Create a new WCA before building the Render Profile Lookup Texture.");
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
        if (!Materials.IsValidIndex(MaterialSlotIndex))
        {
            Warnings.Add(FString::Printf(TEXT("Material slot %d is out of range."), MaterialSlotIndex));
            continue;
        }

        UMaterialInterface* SourceMaterial = FWCAMaterialGenerator::ResolveGeneratedMaterialSource(
            WetClothingAsset,
            MaterialSlotIndex,
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
            MaterialSet.GeneratedMaterialInstance == nullptr)
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
        Override->GeneratedMaterialInstance = MaterialSet.GeneratedMaterialInstance;

        UpdatedMaterials.Add(FString::Printf(
            TEXT("Slot %d -> %s / %s / %s"),
            MaterialSlotIndex,
            *GetNameSafe(MaterialSet.GeneratedMaterial),
            *GetNameSafe(MaterialSet.GeneratedMaterialInstance),
            *GetNameSafe(MaterialSet.GeneratedMaterialInstance)));
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
        TEXT("Wet Part Data Textures:\n- %s\n- DWC UV Channel %d\n- %d local profiles\n- %d total painted pixels"),
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
    // Save only DWC-generated 512 Surface Water textures. Authored 512 inputs are
    // referenced directly and their source packages must not be force-saved here.
    const FString SharedSurfaceFolder(DWCSurfaceTextureSharedAsset::GetSharedFolder());
    for (const FWetClothingLocalRenderProfile& LocalProfile :
         WetClothingAsset->Derived.Inline.BakedWetPartData.LocalProfiles)
    {
        const auto AddGeneratedSurfaceTexture = [
            &PackagesToSave,
            &SharedSurfaceFolder](UTexture2D* Texture)
        {
            if (Texture != nullptr && Texture->GetPathName().StartsWith(SharedSurfaceFolder))
            {
                AddRenderProfilePackageForObject(Texture, PackagesToSave);
            }
        };
        AddGeneratedSurfaceTexture(LocalProfile.NormalizedDropletNormal.Get());
        AddGeneratedSurfaceTexture(LocalProfile.NormalizedDropletMask.Get());
        AddGeneratedSurfaceTexture(LocalProfile.NormalizedDropletFlowNormal.Get());
        AddGeneratedSurfaceTexture(LocalProfile.NormalizedDropletFlowMask.Get());
    }

    AddRenderProfilePackageForObject(
        WetClothingAsset->Derived.Inline.BakedWetPartData.NormalizedNeutralSurfaceNormal.Get(),
        PackagesToSave);

    for (const FWetClothingGeneratedWetMaterialOverride& Override : WetClothingAsset->Derived.Inline.GeneratedWetMaterialOverrides)
    {
        AddRenderProfilePackageForObject(Override.GeneratedMaterial.Get(), PackagesToSave);
        AddRenderProfilePackageForObject(Override.GeneratedMaterialInstance.Get(), PackagesToSave);
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
