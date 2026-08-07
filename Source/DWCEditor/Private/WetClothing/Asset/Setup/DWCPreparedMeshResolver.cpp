//Copyright 2026 Team Tofunut. All Rights Reserved.
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
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInterface.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "UObject/UObjectIterator.h"

namespace DWCPreparedMeshResolverPrivate
{
    struct FDeterministicMeshPath
    {
        FString PackageName;
        FString ObjectName;
        FString ObjectPath;
    };

    FDWCPreparedMeshResolveResult Failure(const FString& Message)
    {
        FDWCPreparedMeshResolveResult Result;
        Result.ErrorMessage = Message;
        return Result;
    }

    FDWCPreparedMeshPreflightResult PreflightFailure(
        const EDWCPreparedMeshPreflightAction Action,
        const FString& Message)
    {
        FDWCPreparedMeshPreflightResult Result;
        Result.Action = Action;
        Result.ErrorMessage = Message;
        return Result;
    }

    bool IsSourceMeshUsable(USkeletalMesh* SourceMesh, FString& OutErrorMessage)
    {
        if (SourceMesh == nullptr)
        {
            OutErrorMessage = TEXT("The Wet Clothing Asset has no Source Skeletal Mesh.");
            return false;
        }

        UPackage* SourcePackage = SourceMesh->GetOutermost();
        const bool bIsPreviewOnlyMesh = SourceMesh->HasAnyFlags(RF_Transient) || SourcePackage == GetTransientPackage() ||
            SourcePackage == nullptr || !FPackageName::IsValidLongPackageName(SourcePackage->GetName());
        if (bIsPreviewOnlyMesh)
        {
            OutErrorMessage = TEXT("The Source Skeletal Mesh is preview-only or unsaved. Assign a saved Skeletal Mesh asset before creating a WCA.");
            return false;
        }

        const FSkeletalMeshRenderData* SourceRenderData = SourceMesh->GetResourceForRendering();
        if (SourceRenderData == nullptr || SourceRenderData->LODRenderData.Num() == 0)
        {
            OutErrorMessage = TEXT("The Source Skeletal Mesh has no render LOD data. Wait for the mesh to finish loading, then assign the saved asset again.");
            return false;
        }

        return true;
    }

    bool ResolveDeterministicMeshPath(
        UWetClothingAsset& Asset,
        const USkeletalMesh& SourceMesh,
        FDeterministicMeshPath& OutPath,
        FString& OutErrorMessage)
    {
        const FString AssetPackageName = Asset.GetOutermost() != nullptr
            ? Asset.GetOutermost()->GetName()
            : FString();
        if (!FPackageName::IsValidLongPackageName(AssetPackageName))
        {
            OutErrorMessage = TEXT("The Wet Clothing Asset must be saved before DWC can create a mesh copy.");
            return false;
        }

        const FString WCAFolder = FPackageName::GetLongPackagePath(AssetPackageName);
        const FString GeneratedMeshFolder = WCAFolder / TEXT("Generated") / Asset.GetName() / TEXT("Mesh");
        const FString SourceAssetName = SourceMesh.GetName();
        OutPath.ObjectName = SourceAssetName.EndsWith(TEXT("_DWC"))
            ? SourceAssetName
            : SourceAssetName + TEXT("_DWC");
        OutPath.PackageName = GeneratedMeshFolder / OutPath.ObjectName;
        OutPath.ObjectPath = OutPath.PackageName + TEXT(".") + OutPath.ObjectName;
        return true;
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

    bool IsSameMaterialFamily(UMaterialInterface* A, UMaterialInterface* B)
    {
        if (A == nullptr || B == nullptr)
        {
            return A == B;
        }

        UMaterial* ABase = A->GetMaterial();
        UMaterial* BBase = B->GetMaterial();
        return A == B || (ABase != nullptr && ABase == BBase);
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
            UMaterialInterface* SourceMaterial = MaterialOverride.SourceMaterial.Get();
            UMaterialInterface* GeneratedMaterial = MaterialOverride.GeneratedMaterial.Get();
            UMaterialInterface* GeneratedMaterialInstance = MaterialOverride.GeneratedMaterialInstance.Get();
            const bool bIsExpectedSource = CurrentMaterial == nullptr ||
                CurrentMaterial == SourceMaterial ||
                CurrentMaterial == GeneratedMaterial ||
                CurrentMaterial == GeneratedMaterialInstance ||
                IsSameMaterialFamily(CurrentMaterial, SourceMaterial);
            if (!bIsExpectedSource)
            {
                continue;
            }

            Mesh.GetMaterials()[MaterialOverride.MaterialSlotIndex].MaterialInterface = GeneratedMaterialInstance;
        }
    }

    void PreserveImportedTangentBasisForPreparedMesh(USkeletalMesh& Mesh)
    {
        const int32 LODCount = Mesh.GetLODNum();
        for (int32 LODIndex = 0; LODIndex < LODCount; ++LODIndex)
        {
            FSkeletalMeshLODInfo* LODInfo = Mesh.GetLODInfo(LODIndex);
            if (LODInfo == nullptr)
            {
                continue;
            }

            LODInfo->BuildSettings.bRecomputeNormals = false;
            LODInfo->BuildSettings.bRecomputeTangents = false;
        }
    }
}

FDWCPreparedMeshPreflightResult FDWCPreparedMeshResolver::Preflight(
    UWetClothingAsset& Asset,
    const bool bForceNewAsset)
{
    using namespace DWCPreparedMeshResolverPrivate;

    USkeletalMesh* SourceMesh = Asset.GetSourceSkeletalMesh();
    FString ErrorMessage;
    if (!IsSourceMeshUsable(SourceMesh, ErrorMessage))
    {
        return PreflightFailure(EDWCPreparedMeshPreflightAction::InvalidSourceMesh, ErrorMessage);
    }

    if (!bForceNewAsset && Asset.GetDWCSkeletalMesh() != nullptr && Asset.GetDWCSkeletalMesh() != SourceMesh)
    {
        FDWCPreparedMeshPreflightResult Result;
        Result.bCanProceed = true;
        Result.Action = EDWCPreparedMeshPreflightAction::ReuseReferencedMesh;
        Result.ResolvedMesh = Asset.GetDWCSkeletalMesh();
        return Result;
    }

    FDeterministicMeshPath TargetPath;
    if (!ResolveDeterministicMeshPath(Asset, *SourceMesh, TargetPath, ErrorMessage))
    {
        return PreflightFailure(EDWCPreparedMeshPreflightAction::InvalidSourceMesh, ErrorMessage);
    }

    FDWCPreparedMeshPreflightResult Result;
    Result.bCanProceed = true;
    Result.TargetPackageName = TargetPath.PackageName;
    Result.TargetObjectPath = TargetPath.ObjectPath;

    if (USkeletalMesh* ExistingMesh = LoadObject<USkeletalMesh>(nullptr, *TargetPath.ObjectPath))
    {
        Result.Action = bForceNewAsset
            ? EDWCPreparedMeshPreflightAction::ForceReplaceDeterministicMesh
            : EDWCPreparedMeshPreflightAction::ReuseDeterministicMesh;
        Result.ResolvedMesh = ExistingMesh;
        return Result;
    }

    if (UObject* ConflictingObject = LoadObject<UObject>(nullptr, *TargetPath.ObjectPath))
    {
        return PreflightFailure(
            EDWCPreparedMeshPreflightAction::OwnershipConflict,
            FString::Printf(
                TEXT("A non-Skeletal-Mesh asset already exists at the DWC output path: %s"),
                *GetPathNameSafe(ConflictingObject)));
    }

    Result.Action = EDWCPreparedMeshPreflightAction::CreateNewMesh;
    return Result;
}

FDWCPreparedMeshResolveResult FDWCPreparedMeshResolver::Resolve(
    UWetClothingAsset& Asset,
    const bool bForceNewAsset)
{
    using namespace DWCPreparedMeshResolverPrivate;

    USkeletalMesh* SourceMesh = Asset.GetSourceSkeletalMesh();
    FString ErrorMessage;
    if (!IsSourceMeshUsable(SourceMesh, ErrorMessage))
    {
        return Failure(ErrorMessage);
    }

    if (!bForceNewAsset && Asset.GetDWCSkeletalMesh() != nullptr && Asset.GetDWCSkeletalMesh() != SourceMesh)
    {
        PreserveImportedTangentBasisForPreparedMesh(*Asset.GetDWCSkeletalMesh());
        FDWCPreparedMeshResolveResult Result;
        Result.Mesh = Asset.GetDWCSkeletalMesh();
        return Result;
    }

    FDeterministicMeshPath TargetPath;
    if (!ResolveDeterministicMeshPath(Asset, *SourceMesh, TargetPath, ErrorMessage))
    {
        return Failure(ErrorMessage);
    }

    if (USkeletalMesh* ExistingMesh = LoadObject<USkeletalMesh>(nullptr, *TargetPath.ObjectPath))
    {
        const FText Warning = FText::FromString(FString::Printf(
            TEXT("A Skeletal Mesh already exists at the deterministic DWC output path:\n\n%s\n\nReplacing it will permanently delete that asset before creating a new DWC mesh copy. Continue?"),
            *TargetPath.ObjectPath));
        if (FMessageDialog::Open(EAppMsgType::YesNo, Warning) != EAppReturnType::Yes)
        {
            return Failure(TEXT("DWC Skeletal Mesh creation was cancelled because the target path is occupied."));
        }

        if (!DeleteExistingGeneratedMesh(Asset, ExistingMesh))
        {
            return Failure(TEXT("Failed to remove the existing asset at the DWC Skeletal Mesh output path."));
        }
    }
    else if (UObject* ConflictingObject = LoadObject<UObject>(nullptr, *TargetPath.ObjectPath))
    {
        return Failure(FString::Printf(
            TEXT("A non-Skeletal-Mesh asset already exists at the DWC output path: %s"),
            *GetPathNameSafe(ConflictingObject)));
    }

    FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
    UObject* DuplicatedObject = AssetToolsModule.Get().DuplicateAsset(
        TargetPath.ObjectName,
        FPackageName::GetLongPackagePath(TargetPath.PackageName),
        SourceMesh);
    USkeletalMesh* PreparedMesh = Cast<USkeletalMesh>(DuplicatedObject);
    if (PreparedMesh == nullptr)
    {
        return Failure(TEXT("Failed to duplicate the Source Mesh for DWC UV Channel generation."));
    }

    PreserveImportedTangentBasisForPreparedMesh(*PreparedMesh);

    // Reuse generated material instances on a newly created DWC mesh without
    // replacing an unrelated material explicitly assigned by the user.
    ApplyExistingGeneratedMaterials(Asset, *PreparedMesh);

    FAssetRegistryModule::AssetCreated(PreparedMesh);
    PreparedMesh->MarkPackageDirty();

    FDWCPreparedMeshResolveResult Result;
    Result.Mesh = PreparedMesh;
    return Result;
}
