//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Modes/Transparency/Temp/DWCTransparencyIntermediateAssetPolicy.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "DataAssets/WetClothingAsset.h"
#include "DataAssets/WetClothingTransparencyData.h"
#include "Modules/ModuleManager.h"
#include "UObject/Package.h"

DEFINE_LOG_CATEGORY_STATIC(LogDWCTransparencyCookPolicy, Log, All);

namespace
{
    constexpr TCHAR TransparencyTempPathMarker[] = TEXT("/Textures/Transparency/Temp/");

    UObject* GetLoadedObject(const TSoftObjectPtr<UTexture2D>& Reference)
    {
        return Reference.Get();
    }

    void RepairLoadedObject(
        UObject* Object,
        TArray<UPackage*>& OutChangedPackages,
        TArray<FString>& OutWarnings)
    {
        if (Object == nullptr)
        {
            return;
        }

        bool bChanged = false;
        FString Error;
        if (!FDWCTransparencyIntermediateAssetPolicy::EnsureEditorOnlyPackage(
                *Object, &bChanged, &Error))
        {
            OutWarnings.AddUnique(MoveTemp(Error));
            return;
        }

        if (bChanged)
        {
            OutChangedPackages.AddUnique(Object->GetOutermost());
        }
    }
}

bool FDWCTransparencyIntermediateAssetPolicy::IsIntermediateArtifactKind(
    const EDWCTransparencyTempArtifactKind Kind)
{
    switch (Kind)
    {
    case EDWCTransparencyTempArtifactKind::SourceMaterialColor:
    case EDWCTransparencyTempArtifactKind::BaseRevealColor:
    case EDWCTransparencyTempArtifactKind::ValidHit:
    case EDWCTransparencyTempArtifactKind::HitSource:
    case EDWCTransparencyTempArtifactKind::HitDistance:
    case EDWCTransparencyTempArtifactKind::CorrectedRevealColor:
    case EDWCTransparencyTempArtifactKind::OuterCoverage:
    case EDWCTransparencyTempArtifactKind::OuterIslandID:
    case EDWCTransparencyTempArtifactKind::BaseRevealSurface:
        return true;
    default:
        return false;
    }
}

bool FDWCTransparencyIntermediateAssetPolicy::IsIntermediatePackagePath(
    const FString& PackageName)
{
    return PackageName.Contains(TransparencyTempPathMarker, ESearchCase::CaseSensitive);
}

bool FDWCTransparencyIntermediateAssetPolicy::EnsureEditorOnlyPackage(
    UObject& Artifact,
    bool* OutChanged,
    FString* OutError)
{
    if (OutChanged != nullptr)
    {
        *OutChanged = false;
    }
    if (OutError != nullptr)
    {
        OutError->Reset();
    }

    UPackage* Package = Artifact.GetOutermost();
    if (Package == nullptr)
    {
        if (OutError != nullptr)
        {
            *OutError = TEXT("Transparency intermediate artifact has no package.");
        }
        return false;
    }
    if (!IsIntermediatePackagePath(Package->GetName()))
    {
        if (OutError != nullptr)
        {
            *OutError = FString::Printf(
                TEXT("Transparency intermediate artifact '%s' is outside Textures/Transparency/Temp."),
                *Artifact.GetPathName());
        }
        return false;
    }

    if (!Package->HasAnyPackageFlags(PKG_EditorOnly))
    {
        Package->SetPackageFlags(PKG_EditorOnly);
        Package->MarkPackageDirty();
        if (OutChanged != nullptr)
        {
            *OutChanged = true;
        }
    }
    return true;
}

bool FDWCTransparencyIntermediateAssetPolicy::HasEditorOnlyPackageFlag(
    const FSoftObjectPath& AssetPath)
{
    if (AssetPath.IsNull())
    {
        return false;
    }

    const FString PackageName = AssetPath.GetLongPackageName();
    if (UPackage* LoadedPackage = FindPackage(nullptr, *PackageName))
    {
        return LoadedPackage->HasAnyPackageFlags(PKG_EditorOnly);
    }

    const FAssetData AssetData =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"))
            .Get()
            .GetAssetByObjectPath(AssetPath);
    return AssetData.IsValid() && AssetData.HasAnyPackageFlags(PKG_EditorOnly);
}

bool FDWCTransparencyIntermediateAssetPolicy::IsReferenceCookExcluded(
    const FSoftObjectPath& ArtifactPath,
    FString* OutReason)
{
    if (OutReason != nullptr)
    {
        OutReason->Reset();
    }
    if (ArtifactPath.IsNull())
    {
        return true;
    }
    if (!IsIntermediatePackagePath(ArtifactPath.GetLongPackageName()))
    {
        if (OutReason != nullptr)
        {
            *OutReason = FString::Printf(
                TEXT("Intermediate reference '%s' is outside Textures/Transparency/Temp."),
                *ArtifactPath.ToString());
        }
        return false;
    }

    const FString PackageName = ArtifactPath.GetLongPackageName();
    const bool bPackageLoaded = FindPackage(nullptr, *PackageName) != nullptr;
    const FAssetData AssetData =
        FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"))
            .Get()
            .GetAssetByObjectPath(ArtifactPath);
    if (!bPackageLoaded && !AssetData.IsValid())
    {
        // A missing stale cache cannot enter a cook. Missing-artifact validation
        // is handled by the stage-signature contract instead.
        return true;
    }
    if (!HasEditorOnlyPackageFlag(ArtifactPath))
    {
        if (OutReason != nullptr)
        {
            *OutReason = FString::Printf(
                TEXT("Intermediate package '%s' is not marked Editor Only."),
                *PackageName);
        }
        return false;
    }
    return true;
}

void FDWCTransparencyIntermediateAssetPolicy::RepairLoadedReferences(
    UWetClothingAsset& Asset,
    TArray<UPackage*>& OutChangedPackages,
    TArray<FString>& OutWarnings)
{
#if WITH_EDITORONLY_DATA
    for (const FDWCTransparencyMaterialColorCacheReference& Reference :
         Asset.Authored.TransparencyData.MaterialColorCache)
    {
        RepairLoadedObject(GetLoadedObject(Reference.Texture), OutChangedPackages, OutWarnings);
        RepairLoadedObject(GetLoadedObject(Reference.NormalTexture), OutChangedPackages, OutWarnings);
        RepairLoadedObject(GetLoadedObject(Reference.MetallicTexture), OutChangedPackages, OutWarnings);
    }

    for (const FWetClothingTransparencyLayerData& Layer :
         Asset.Authored.TransparencyData.TransparencyLayers)
    {
        for (const FDWCTransparencyTempArtifactReference& Reference :
             Layer.EditorStageCache.Artifacts)
        {
            RepairLoadedObject(GetLoadedObject(Reference.Texture), OutChangedPackages, OutWarnings);
        }
    }
#endif

    for (const FString& Warning : OutWarnings)
    {
        UE_LOG(LogDWCTransparencyCookPolicy, Warning, TEXT("%s"), *Warning);
    }
}
