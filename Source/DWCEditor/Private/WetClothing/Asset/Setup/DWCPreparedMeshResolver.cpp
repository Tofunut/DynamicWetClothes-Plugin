#include "DWCPreparedMeshResolver.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "DataAssets/WetClothingAsset.h"
#include "Engine/SkeletalMesh.h"
#include "IAssetTools.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "ObjectTools.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "UObject/UObjectIterator.h"

namespace DWCPreparedMeshResolverPrivate
{
    FDWCPreparedMeshResolveResult Failure(const FString& Message)
    {
        FDWCPreparedMeshResolveResult Result;
        Result.ErrorMessage = Message;
        return Result;
    }

    bool DeleteExistingGeneratedMesh(UWetClothingAsset& Asset, USkeletalMesh* ExistingMesh)
    {
        if (ExistingMesh == nullptr)
        {
            return false;
        }

        TArray<UObject*> ObjectsToReplace;
        ObjectsToReplace.Add(ExistingMesh);

        TSet<UObject*> ObjectsToReplaceWithin;
        ObjectsToReplaceWithin.Add(&Asset);

        UPackage* TransientPackage = GetTransientPackage();
        for (TObjectIterator<UObject> It; It; ++It)
        {
            UObject* Object = *It;
            if (Object != nullptr && Object->GetOutermost() == TransientPackage)
            {
                ObjectsToReplaceWithin.Add(Object);
            }
        }

        ObjectTools::ForceReplaceReferences(nullptr, ObjectsToReplace, ObjectsToReplaceWithin);

        TArray<UObject*> ObjectsToDelete;
        ObjectsToDelete.Add(ExistingMesh);
        return ObjectTools::DeleteObjects(ObjectsToDelete, false) > 0;
    }

    void ApplyExistingGeneratedMaterials(UWetClothingAsset& Asset, USkeletalMesh& Mesh)
    {
        for (const FWetClothingGeneratedWetMaterialOverride& MaterialOverride :
             Asset.Derived.Inline.GeneratedWetMaterialOverrides)
        {
            if (MaterialOverride.MaterialSlotIndex == INDEX_NONE ||
                MaterialOverride.GeneratedMaterialInstance == nullptr ||
                !Mesh.GetMaterials().IsValidIndex(MaterialOverride.MaterialSlotIndex))
            {
                continue;
            }

            UMaterialInterface* CurrentMaterial =
                Mesh.GetMaterials()[MaterialOverride.MaterialSlotIndex].MaterialInterface;
            const bool bIsExpectedSource = CurrentMaterial == nullptr ||
                CurrentMaterial == MaterialOverride.SourceMaterial ||
                CurrentMaterial == MaterialOverride.GeneratedMaterial ||
                CurrentMaterial == MaterialOverride.GeneratedMaterialInstance;
            if (!bIsExpectedSource)
            {
                continue;
            }

            UMaterialInterface* GeneratedMaterial = MaterialOverride.GeneratedMaterialInstance.Get();
            Mesh.GetMaterials()[MaterialOverride.MaterialSlotIndex].MaterialInterface = GeneratedMaterial;
        }
    }
}

FDWCPreparedMeshResolveResult FDWCPreparedMeshResolver::Resolve(
    UWetClothingAsset& Asset,
    const bool bForceNewAsset)
{
    using namespace DWCPreparedMeshResolverPrivate;

    USkeletalMesh* SourceMesh = Asset.GetSourceSkeletalMesh();
    if (SourceMesh == nullptr)
    {
        return Failure(TEXT("The Wet Clothing Asset has no Source Skeletal Mesh."));
    }

    UPackage* SourcePackage = SourceMesh->GetOutermost();
    const bool bIsPreviewOnlyMesh = SourceMesh->HasAnyFlags(RF_Transient) || SourcePackage == GetTransientPackage() ||
        SourcePackage == nullptr || !FPackageName::IsValidLongPackageName(SourcePackage->GetName());
    if (bIsPreviewOnlyMesh)
    {
        return Failure(TEXT("The Source Skeletal Mesh is preview-only or unsaved. Assign a saved Skeletal Mesh asset before creating a WCA."));
    }

    const FSkeletalMeshRenderData* SourceRenderData = SourceMesh->GetResourceForRendering();
    if (SourceRenderData == nullptr || SourceRenderData->LODRenderData.Num() == 0)
    {
        return Failure(TEXT("The Source Skeletal Mesh has no render LOD data. Wait for the mesh to finish loading, then assign the saved asset again."));
    }

    if (!bForceNewAsset && Asset.GetDWCSkeletalMesh() != nullptr && Asset.GetDWCSkeletalMesh() != SourceMesh)
    {
        FDWCPreparedMeshResolveResult Result;
        Result.Mesh = Asset.GetDWCSkeletalMesh();
        return Result;
    }

    const FString AssetPackageName = Asset.GetOutermost() != nullptr
        ? Asset.GetOutermost()->GetName()
        : FString();
    if (!FPackageName::IsValidLongPackageName(AssetPackageName))
    {
        return Failure(TEXT("The Wet Clothing Asset must be saved before DWC can create a mesh copy."));
    }

    const FString WCAFolder = FPackageName::GetLongPackagePath(AssetPackageName);
    const FString GeneratedMeshFolder = WCAFolder / TEXT("Generated") / Asset.GetName() / TEXT("Mesh");
    const FString SourceAssetName = SourceMesh->GetName();
    const FString DWCAssetName = SourceAssetName.EndsWith(TEXT("_DWC"))
        ? SourceAssetName
        : SourceAssetName + TEXT("_DWC");
    const FString TargetPackageName = GeneratedMeshFolder / DWCAssetName;
    const FString TargetObjectPath = TargetPackageName + TEXT(".") + DWCAssetName;

    if (USkeletalMesh* ExistingMesh = LoadObject<USkeletalMesh>(nullptr, *TargetObjectPath))
    {
        const FText Warning = FText::FromString(FString::Printf(
            TEXT("A Skeletal Mesh already exists at the deterministic DWC output path:\n\n%s\n\nReplacing it will permanently delete that asset before creating a new DWC mesh copy. Continue?"),
            *TargetObjectPath));
        if (FMessageDialog::Open(EAppMsgType::YesNo, Warning) != EAppReturnType::Yes)
        {
            return Failure(TEXT("DWC Skeletal Mesh creation was cancelled because the target path is occupied."));
        }

        if (!DeleteExistingGeneratedMesh(Asset, ExistingMesh))
        {
            return Failure(TEXT("Failed to remove the existing asset at the DWC Skeletal Mesh output path."));
        }
    }
    else if (UObject* ConflictingObject = LoadObject<UObject>(nullptr, *TargetObjectPath))
    {
        return Failure(FString::Printf(
            TEXT("A non-Skeletal-Mesh asset already exists at the DWC output path: %s"),
            *GetPathNameSafe(ConflictingObject)));
    }

    FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
    UObject* DuplicatedObject = AssetToolsModule.Get().DuplicateAsset(
        DWCAssetName,
        GeneratedMeshFolder,
        SourceMesh);
    USkeletalMesh* PreparedMesh = Cast<USkeletalMesh>(DuplicatedObject);
    if (PreparedMesh == nullptr)
    {
        return Failure(TEXT("Failed to duplicate the Source Mesh for DWC UV Channel generation."));
    }

    // Reuse generated material instances on a newly created DWC mesh without
    // replacing an unrelated material explicitly assigned by the user.
    ApplyExistingGeneratedMaterials(Asset, *PreparedMesh);

    FAssetRegistryModule::AssetCreated(PreparedMesh);
    PreparedMesh->MarkPackageDirty();

    FDWCPreparedMeshResolveResult Result;
    Result.Mesh = PreparedMesh;
    return Result;
}
