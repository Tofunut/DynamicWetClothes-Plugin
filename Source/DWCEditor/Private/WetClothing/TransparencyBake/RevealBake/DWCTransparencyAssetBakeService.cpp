#include "WetClothing/TransparencyBake/RevealBake/DWCTransparencyAssetBakeService.h"

#include "Bake/DWCBakeProjection.h"
#include "Bake/DWCBakeSurface.h"
#include "Components/DWCBakeComponent.h"
#include "DataAssets/WetClothingAsset.h"
#include "FileHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "PreviewScene.h"
#include "UObject/Package.h"
#include "WetClothing/Common/Material/WetClothingMaterialSetup.h"
#include "WetClothing/TransparencyBake/RevealBake/DWCRevealBakeMaterialBuilder.h"
#include "WetClothing/TransparencyBake/RevealBake/DWCRevealBakeSourceResolver.h"
#include "WetClothing/TransparencyBake/RevealBake/DWCRevealBakeSurfaceCache.h"
#include "WetClothing/TransparencyBake/RevealBake/DWCRevealBakeSurfaceResolver.h"
#include "WetClothing/TransparencyBake/RevealBake/DWCRevealBakeTextureWriter.h"
#include "WetClothing/TransparencyBake/RevealBake/DWCRevealBakeUtilities.h"

namespace
{
    constexpr int32 MinRevealBakeResolution = 16;
    constexpr int32 MaxRevealBakeResolution = 8192;

    struct FDWCTransientBakeActor
    {
        TUniquePtr<FPreviewScene> PreviewScene;
        AActor* Actor = nullptr;
        UDWCBakeComponent* BakeComponent = nullptr;
        FDWCBakeSnapshot Snapshot;
    };

    FWetClothingGeneratedWetMaterialOverride* FindPartGeneratedWetMaterialOverride(UWetClothingAsset& WetClothingAsset, const int32 MaterialSlotIndex)
    {
        return WetClothingAsset.PartData.GeneratedWetMaterialOverrides.FindByPredicate(
            [MaterialSlotIndex](const FWetClothingGeneratedWetMaterialOverride& Candidate)
            {
                return Candidate.MaterialSlotIndex == MaterialSlotIndex;
            });
    }

    UMaterialInterface* ResolveOrCreateWetBaseMaterial(
        UWetClothingAsset&          WetClothingAsset,
        const int32                 MaterialSlotIndex,
        UMaterialInterface*         SourceMaterial,
        TArray<FString>&            OutSetupMessages,
        FString&                    OutErrorMessage)
    {
        if (SourceMaterial == nullptr)
        {
            OutErrorMessage = FString::Printf(TEXT("Slot %d has no source material."), MaterialSlotIndex);
            return nullptr;
        }

        if (FWetClothingGeneratedWetMaterialOverride* ExistingOverride = FindPartGeneratedWetMaterialOverride(WetClothingAsset, MaterialSlotIndex))
        {
            if (ExistingOverride->WetMaterial != nullptr && FWetClothingMaterialSetup::IsMaterialConfiguredForDwc(ExistingOverride->WetMaterial))
            {
                return ExistingOverride->WetMaterial;
            }
        }

        FWetClothingMaterialSetupResult WetSetupResult = FWetClothingMaterialSetup::DuplicateAndApplyToMaterialInterface(SourceMaterial);
        if (!WetSetupResult.bSucceeded || WetSetupResult.ConfiguredMaterial == nullptr)
        {
            OutErrorMessage = FString::Printf(
                TEXT("Slot %d wet material setup failed before Transparency build: %s"),
                MaterialSlotIndex,
                *WetSetupResult.Message);
            return nullptr;
        }

        WetClothingAsset.Modify();
        FWetClothingGeneratedWetMaterialOverride* Override = FindPartGeneratedWetMaterialOverride(WetClothingAsset, MaterialSlotIndex);
        if (Override == nullptr)
        {
            Override = &WetClothingAsset.PartData.GeneratedWetMaterialOverrides.AddDefaulted_GetRef();
            Override->MaterialSlotIndex = MaterialSlotIndex;
        }
        Override->SourceMaterial = SourceMaterial;
        Override->WetMaterial = WetSetupResult.ConfiguredMaterial;
        WetClothingAsset.MarkPackageDirty();

        OutSetupMessages.Add(FString::Printf(
            TEXT("Slot %d wet base -> %s"),
            MaterialSlotIndex,
            *GetNameSafe(WetSetupResult.ConfiguredMaterial)));

        return WetSetupResult.ConfiguredMaterial;
    }

    FString MakeTransparencyBuildSignature(
        const UWetClothingAsset&       WetClothingAsset,
        const FDWCBakeSnapshot&        Snapshot,
        const FDWCBakeResolvedLayer&   OuterLayer,
        const int32                    MaterialSlotIndex,
        const UMaterialInterface*      SourceMaterial)
    {
        return FString::Printf(
            TEXT("DWCTransparencyReveal_v1|BP=%s|TargetMesh=%s|Layer=%s|Slot=%d|Resolution=%d|Feather=%g|Snapshot=%s|Material=%s"),
            *WetClothingAsset.TransparencyData.SourceBlueprintClass.ToSoftObjectPath().ToString(),
            *GetPathNameSafe(WetClothingAsset.TargetMesh.Get()),
            *OuterLayer.LayerId.ToString(),
            MaterialSlotIndex,
            FMath::Clamp(WetClothingAsset.TransparencyData.RevealBakeResolution, MinRevealBakeResolution, MaxRevealBakeResolution),
            FMath::Max(0.0f, WetClothingAsset.TransparencyData.RevealMaskFeatherRadiusPixels),
            *Snapshot.BuildSignature,
            *GetPathNameSafe(SourceMaterial));
    }

    bool SpawnBlueprintBakeActor(UWetClothingAsset& WetClothingAsset, FDWCTransientBakeActor& OutBakeActor, FString& OutErrorMessage)
    {
        TSubclassOf<AActor> BlueprintClass = WetClothingAsset.TransparencyData.SourceBlueprintClass.LoadSynchronous();
        if (BlueprintClass == nullptr)
        {
            OutErrorMessage = TEXT("Assign a Source Blueprint before building Transparency.");
            return false;
        }

        if (BlueprintClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists))
        {
            OutErrorMessage = FString::Printf(TEXT("Source Blueprint '%s' cannot be used for Transparency bake."), *GetNameSafe(BlueprintClass.Get()));
            return false;
        }

        OutBakeActor.PreviewScene = MakeUnique<FPreviewScene>(
            FPreviewScene::ConstructionValues()
                .SetCreateDefaultLighting(false)
                .SetCreatePhysicsScene(false)
                .SetTransactional(false));

        UWorld* PreviewWorld = OutBakeActor.PreviewScene->GetWorld();
        if (PreviewWorld == nullptr)
        {
            OutErrorMessage = TEXT("Failed to create Transparency bake preview world.");
            return false;
        }

        FActorSpawnParameters SpawnParameters;
        SpawnParameters.Name = MakeUniqueObjectName(PreviewWorld, BlueprintClass.Get(), TEXT("DWC_TransparencyBakeActor"));
        SpawnParameters.ObjectFlags = RF_Transient;
        SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        SpawnParameters.bTemporaryEditorActor = true;

        OutBakeActor.Actor = PreviewWorld->SpawnActor<AActor>(BlueprintClass, FTransform::Identity, SpawnParameters);
        if (OutBakeActor.Actor == nullptr)
        {
            OutErrorMessage = FString::Printf(TEXT("Failed to spawn Source Blueprint '%s' for Transparency bake."), *GetNameSafe(BlueprintClass.Get()));
            return false;
        }

        TArray<UDWCBakeComponent*> BakeComponents;
        OutBakeActor.Actor->GetComponents<UDWCBakeComponent>(BakeComponents);
        if (BakeComponents.Num() != 1)
        {
            OutErrorMessage = FString::Printf(
                TEXT("Source Blueprint '%s' must contain exactly one DWC Bake Component. Found %d."),
                *GetNameSafe(BlueprintClass.Get()),
                BakeComponents.Num());
            return false;
        }

        OutBakeActor.BakeComponent = BakeComponents[0];
        if (OutBakeActor.BakeComponent == nullptr || !OutBakeActor.BakeComponent->BuildBakeSnapshot(OutBakeActor.Snapshot))
        {
            OutErrorMessage = TEXT("Failed to build DWC bake snapshot from Source Blueprint.");
            return false;
        }

        return true;
    }

    bool FindSingleTargetOuterLayer(
        const UWetClothingAsset&     WetClothingAsset,
        const FDWCBakeSnapshot&      Snapshot,
        int32&                       OutLayerIndex,
        const FDWCBakeResolvedLayer*& OutLayer,
        FString&                     OutErrorMessage)
    {
        OutLayerIndex = INDEX_NONE;
        OutLayer = nullptr;

        if (WetClothingAsset.TargetMesh == nullptr)
        {
            OutErrorMessage = TEXT("Assign a TargetMesh before building Transparency.");
            return false;
        }

        for (int32 LayerIndex = 0; LayerIndex < Snapshot.Layers.Num(); ++LayerIndex)
        {
            const FDWCBakeResolvedLayer& Layer = Snapshot.Layers[LayerIndex];
            if (Layer.bCanBeWetOuterLayer && Layer.SkeletalMesh == WetClothingAsset.TargetMesh)
            {
                if (OutLayer != nullptr)
                {
                    OutErrorMessage = FString::Printf(
                        TEXT("Source Blueprint has multiple wet outer layers using TargetMesh '%s'. Keep exactly one matching layer."),
                        *GetNameSafe(WetClothingAsset.TargetMesh.Get()));
                    return false;
                }

                OutLayerIndex = LayerIndex;
                OutLayer = &Layer;
            }
        }

        if (OutLayer == nullptr)
        {
            OutErrorMessage = FString::Printf(
                TEXT("Source Blueprint has no wet outer layer using TargetMesh '%s'."),
                *GetNameSafe(WetClothingAsset.TargetMesh.Get()));
            return false;
        }

        return true;
    }

    bool BuildRevealTexturesForLayer(
        const UWetClothingAsset&        WetClothingAsset,
        const FDWCBakeSnapshot&         Snapshot,
        const FDWCBakeResolvedLayer&    OuterLayer,
        const int32                     OuterLayerIndex,
        FDWCRevealBakeTextureSet&       OutTextureSet,
        TArray<FName>&                  OutSourceLayerIds,
        FString&                        OutErrorMessage)
    {
        FDWCRevealBakeSurfaceCache SurfaceCache;
        const FDWCBakeSurface* OuterSurface = SurfaceCache.FindOrBuild(
            OuterLayer,
            OuterLayerIndex,
            0,
            OuterLayer.OuterUVChannel,
            OutErrorMessage);
        if (OuterSurface == nullptr)
        {
            return false;
        }

        TArray<FDWCBakeSurface> SourceSurfaces =
            FDWCRevealBakeSurfaceResolver::BuildSourceSurfacesForOuter(Snapshot, OuterLayer, SurfaceCache, OutErrorMessage);
        if (SourceSurfaces.Num() == 0)
        {
            OutErrorMessage = FString::Printf(TEXT("No reveal source surfaces found inside layer '%s'."), *OuterLayer.LayerId.ToString());
            return false;
        }

        FDWCBakeTexelSamplingSettings SamplingSettings;
        SamplingSettings.Resolution = FIntPoint(
            FMath::Clamp(WetClothingAsset.TransparencyData.RevealBakeResolution, MinRevealBakeResolution, MaxRevealBakeResolution),
            FMath::Clamp(WetClothingAsset.TransparencyData.RevealBakeResolution, MinRevealBakeResolution, MaxRevealBakeResolution));
        SamplingSettings.MaterialSlotIndex = INDEX_NONE;

        TArray<FDWCBakeTexelSample> Samples;
        if (!FDWCBakeTexelSampler::BuildOuterTexelSamples(*OuterSurface, SamplingSettings, Samples, &OutErrorMessage))
        {
            return false;
        }

        TArray<FDWCBakeRayHit> Hits;
        FDWCBakeRayProjectionSettings ProjectionSettings;
        if (!FDWCBakeRayProjector::ProjectSamplesToSources(*OuterSurface, SourceSurfaces, Samples, ProjectionSettings, Hits, &OutErrorMessage))
        {
            return false;
        }

        OutSourceLayerIds = FDWCRevealBakeSourceResolver::BuildRevealSourceLayerIds(SourceSurfaces);

        FDWCRevealBakeTextureWriteSettings TextureSettings;
        TextureSettings.Resolution = SamplingSettings.Resolution;
        TextureSettings.PackagePath = FDWCRevealBakeUtilities::GetDefaultRevealBakePackagePath();
        TextureSettings.MaskFeatherRadiusPixels = FMath::Max(0.0f, WetClothingAsset.TransparencyData.RevealMaskFeatherRadiusPixels);
        TextureSettings.AssetNamePrefix = FString::Printf(
            TEXT("T_DWCReveal_%s_%s"),
            *FDWCRevealBakeUtilities::SanitizeAssetToken(WetClothingAsset.GetName()),
            *FDWCRevealBakeUtilities::SanitizeAssetToken(OuterLayer.LayerId.ToString()));
        TextureSettings.SourceLayerIds = OutSourceLayerIds;
        FDWCRevealBakeSourceResolver::PopulateSourceLayerTextures(Snapshot, OutSourceLayerIds, TextureSettings);

        return FDWCRevealBakeTextureWriter::WriteTextures(Hits, TextureSettings, OutTextureSet, &OutErrorMessage);
    }

    void AddPackageForObject(UObject* Object, TArray<UPackage*>& InOutPackages)
    {
        if (Object != nullptr && Object->GetOutermost() != nullptr)
        {
            InOutPackages.AddUnique(Object->GetOutermost());
        }
    }
}

bool FDWCTransparencyAssetBakeService::BuildTransparencySetup(UWetClothingAsset* WetClothingAsset, FString& OutSummary, bool* OutHadWarnings)
{
    if (OutHadWarnings != nullptr)
    {
        *OutHadWarnings = false;
    }

    if (WetClothingAsset == nullptr)
    {
        OutSummary = TEXT("No Wet Clothing Asset was provided.");
        return false;
    }

    FDWCTransientBakeActor BakeActor;
    FString ErrorMessage;
    if (!SpawnBlueprintBakeActor(*WetClothingAsset, BakeActor, ErrorMessage))
    {
        OutSummary = ErrorMessage;
        return false;
    }

    int32 TargetLayerIndex = INDEX_NONE;
    const FDWCBakeResolvedLayer* TargetLayer = nullptr;
    if (!FindSingleTargetOuterLayer(*WetClothingAsset, BakeActor.Snapshot, TargetLayerIndex, TargetLayer, ErrorMessage))
    {
        OutSummary = ErrorMessage;
        return false;
    }

    FDWCRevealBakeTextureSet TextureSet;
    TArray<FName> SourceLayerIds;
    if (!BuildRevealTexturesForLayer(*WetClothingAsset, BakeActor.Snapshot, *TargetLayer, TargetLayerIndex, TextureSet, SourceLayerIds, ErrorMessage))
    {
        OutSummary = ErrorMessage;
        return false;
    }

    TArray<FString> WetBaseMaterials;
    TArray<FString> CreatedRevealMaterials;
    TArray<FWetClothingBakedTransparencyRevealLayer> NewBakedLayers;

    for (int32 MaterialSlotIndex = 0; MaterialSlotIndex < TargetLayer->Materials.Num(); ++MaterialSlotIndex)
    {
        UMaterialInterface* SourceMaterial = TargetLayer->Materials[MaterialSlotIndex];
        if (SourceMaterial == nullptr)
        {
            OutSummary = FString::Printf(TEXT("Slot %d has no material."), MaterialSlotIndex);
            return false;
        }

        UMaterialInterface* WetBaseMaterial = ResolveOrCreateWetBaseMaterial(
            *WetClothingAsset,
            MaterialSlotIndex,
            SourceMaterial,
            WetBaseMaterials,
            ErrorMessage);
        if (WetBaseMaterial == nullptr)
        {
            OutSummary = ErrorMessage;
            return false;
        }

        const FString MaterialPrefix = FString::Printf(
            TEXT("M_DWCRevealWet_%s_%s_%d"),
            *FDWCRevealBakeUtilities::SanitizeAssetToken(WetClothingAsset->GetName()),
            *FDWCRevealBakeUtilities::SanitizeAssetToken(TargetLayer->LayerId.ToString()),
            MaterialSlotIndex);

        UMaterialInterface* RevealMaterial = FDWCRevealBakeMaterialBuilder::CreateConfiguredRevealMaterial(
            WetBaseMaterial,
            MaterialPrefix,
            *BakeActor.BakeComponent,
            *TargetLayer,
            TextureSet);
        if (RevealMaterial == nullptr)
        {
            OutSummary = FString::Printf(TEXT("Slot %d reveal material setup failed."), MaterialSlotIndex);
            return false;
        }

        FWetClothingBakedTransparencyRevealLayer BakedLayer;
        BakedLayer.LayerId = TargetLayer->LayerId;
        BakedLayer.MaterialSlotIndex = MaterialSlotIndex;
        BakedLayer.LookupMap = TextureSet.LookupMap;
        BakedLayer.ColorMap = TextureSet.ColorMap;
        BakedLayer.MaskMap = TextureSet.MaskMap;
        BakedLayer.ConfidenceMap = TextureSet.ConfidenceMap;
        BakedLayer.RevealMaterial = RevealMaterial;
        BakedLayer.BuildSignature = MakeTransparencyBuildSignature(*WetClothingAsset, BakeActor.Snapshot, *TargetLayer, MaterialSlotIndex, WetBaseMaterial);
        BakedLayer.BakeGuid = FGuid::NewGuid();
        NewBakedLayers.Add(BakedLayer);

        CreatedRevealMaterials.Add(FString::Printf(TEXT("Slot %d reveal -> %s"), MaterialSlotIndex, *GetNameSafe(RevealMaterial)));
    }

    WetClothingAsset->Modify();
    WetClothingAsset->TransparencyData.BakedRevealLayers = MoveTemp(NewBakedLayers);
    WetClothingAsset->MarkPackageDirty();

    TArray<FString> Sections;
    Sections.Add(FString::Printf(TEXT("Transparency reveal textures baked for layer '%s'."), *TargetLayer->LayerId.ToString()));
    if (WetBaseMaterials.Num() > 0)
    {
        Sections.Add(FString::Printf(TEXT("Wet base materials:\n- %s"), *FString::Join(WetBaseMaterials, TEXT("\n- "))));
    }
    if (CreatedRevealMaterials.Num() > 0)
    {
        Sections.Add(FString::Printf(TEXT("Reveal materials:\n- %s"), *FString::Join(CreatedRevealMaterials, TEXT("\n- "))));
    }

    OutSummary = FString::Join(Sections, TEXT("\n\n"));
    return true;
}

bool FDWCTransparencyAssetBakeService::HasPendingTransparencySetup(UWetClothingAsset* WetClothingAsset, FString* OutSummary)
{
    if (WetClothingAsset == nullptr || WetClothingAsset->TransparencyData.SourceBlueprintClass.IsNull())
    {
        if (OutSummary != nullptr)
        {
            *OutSummary = TEXT("Transparency setup has no Source Blueprint.");
        }
        return false;
    }

    if (WetClothingAsset->TransparencyData.BakedRevealLayers.Num() == 0)
    {
        if (OutSummary != nullptr)
        {
            *OutSummary = TEXT("Transparency reveal textures have not been built.");
        }
        return true;
    }

    auto IsOutputPackageDirty = [](const UObject* Object)
    {
        const UPackage* Package = Object != nullptr ? Object->GetOutermost() : nullptr;
        return Package != nullptr && Package->IsDirty();
    };

    TArray<FString> DirtyOutputs;
    for (const FWetClothingBakedTransparencyRevealLayer& BakedLayer : WetClothingAsset->TransparencyData.BakedRevealLayers)
    {
        if (IsOutputPackageDirty(BakedLayer.LookupMap.Get()))
        {
            DirtyOutputs.AddUnique(GetNameSafe(BakedLayer.LookupMap.Get()));
        }
        if (IsOutputPackageDirty(BakedLayer.ColorMap.Get()))
        {
            DirtyOutputs.AddUnique(GetNameSafe(BakedLayer.ColorMap.Get()));
        }
        if (IsOutputPackageDirty(BakedLayer.MaskMap.Get()))
        {
            DirtyOutputs.AddUnique(GetNameSafe(BakedLayer.MaskMap.Get()));
        }
        if (IsOutputPackageDirty(BakedLayer.ConfidenceMap.Get()))
        {
            DirtyOutputs.AddUnique(GetNameSafe(BakedLayer.ConfidenceMap.Get()));
        }
        if (IsOutputPackageDirty(BakedLayer.RevealMaterial.Get()))
        {
            DirtyOutputs.AddUnique(GetNameSafe(BakedLayer.RevealMaterial.Get()));
        }
    }

    if (DirtyOutputs.Num() > 0)
    {
        if (OutSummary != nullptr)
        {
            *OutSummary = FString::Printf(
                TEXT("Transparency reveal outputs have not been saved:\n- %s"),
                *FString::Join(DirtyOutputs, TEXT("\n- ")));
        }
        return true;
    }

    return false;
}

bool FDWCTransparencyAssetBakeService::SaveTransparencySetupAssets(UWetClothingAsset* WetClothingAsset)
{
    if (WetClothingAsset == nullptr)
    {
        return false;
    }

    TArray<UPackage*> PackagesToSave;
    AddPackageForObject(WetClothingAsset, PackagesToSave);
    for (const FWetClothingGeneratedWetMaterialOverride& MaterialOverride : WetClothingAsset->PartData.GeneratedWetMaterialOverrides)
    {
        AddPackageForObject(MaterialOverride.WetMaterial.Get(), PackagesToSave);
    }
    for (const FWetClothingBakedTransparencyRevealLayer& BakedLayer : WetClothingAsset->TransparencyData.BakedRevealLayers)
    {
        AddPackageForObject(BakedLayer.LookupMap.Get(), PackagesToSave);
        AddPackageForObject(BakedLayer.ColorMap.Get(), PackagesToSave);
        AddPackageForObject(BakedLayer.MaskMap.Get(), PackagesToSave);
        AddPackageForObject(BakedLayer.ConfidenceMap.Get(), PackagesToSave);
        AddPackageForObject(BakedLayer.RevealMaterial.Get(), PackagesToSave);
    }

    if (PackagesToSave.Num() == 0)
    {
        return true;
    }

    return FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSave, false, false) == FEditorFileUtils::PR_Success;
}
