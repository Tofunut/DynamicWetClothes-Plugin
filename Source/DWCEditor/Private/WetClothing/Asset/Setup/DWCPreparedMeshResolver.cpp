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

namespace DWCPreparedMeshResolverPrivate
{
    FDWCPreparedMeshResolveResult Failure(const FString& Message)
    {
        FDWCPreparedMeshResolveResult Result;
        Result.ErrorMessage = Message;
        return Result;
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
        if (!bForceNewAsset && ExistingMesh == Asset.GetDWCSkeletalMesh())
        {
            FDWCPreparedMeshResolveResult Result;
            Result.Mesh = ExistingMesh;
            return Result;
        }

        const FText Warning = FText::FromString(FString::Printf(
            TEXT("A Skeletal Mesh already exists at the deterministic DWC output path:\n\n%s\n\nReplacing it will permanently delete that asset before creating a new DWC mesh copy. Continue?"),
            *TargetObjectPath));
        if (FMessageDialog::Open(EAppMsgType::YesNo, Warning) != EAppReturnType::Yes)
        {
            return Failure(TEXT("DWC Skeletal Mesh creation was cancelled because the target path is occupied."));
        }

        if (!ObjectTools::DeleteSingleObject(ExistingMesh, false))
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
        return Failure(TEXT("Failed to duplicate the Source Mesh for DWC Data UV generation."));
    }

    FAssetRegistryModule::AssetCreated(PreparedMesh);
    PreparedMesh->MarkPackageDirty();

    FDWCPreparedMeshResolveResult Result;
    Result.Mesh = PreparedMesh;
    return Result;
}
