#include "Core/DWCGeneratedAssetRelocator.h"

#include "AssetToolsModule.h"
#include "ContentBrowserMenuContexts.h"
#include "DataAssets/WetClothingAsset.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture.h"
#include "IAssetTools.h"
#include "Framework/Commands/UIAction.h"
#include "Materials/Material.h"
#include "Materials/MaterialFunctionInterface.h"
#include "Materials/MaterialInstance.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "DWCGeneratedAssetRelocator"

namespace DWCGeneratedAssetRelocatorPrivate
{
    FString ResolveSubfolder(const UObject& Asset)
    {
        if (Asset.IsA<USkeletalMesh>())
        {
            return TEXT("Mesh");
        }
        if (Asset.IsA<UMaterial>() || Asset.IsA<UMaterialInstance>() || Asset.IsA<UMaterialFunctionInterface>())
        {
            return TEXT("Materials");
        }
        if (Asset.IsA<UTexture>())
        {
            const FString CurrentPath = Asset.GetOutermost()->GetName();
            const FString AssetName = Asset.GetName();
            if (CurrentPath.Contains(TEXT("Wrinkle"), ESearchCase::IgnoreCase) ||
                AssetName.Contains(TEXT("Wrinkle"), ESearchCase::IgnoreCase))
            {
                return TEXT("Textures/Wrinkles");
            }
            if (CurrentPath.Contains(TEXT("Transparency"), ESearchCase::IgnoreCase) ||
                AssetName.Contains(TEXT("Transparency"), ESearchCase::IgnoreCase))
            {
                return TEXT("Textures/Transparency");
            }
            return TEXT("Textures/Wetness");
        }
        return TEXT("Misc");
    }

    void ExecuteRelocation(TWeakObjectPtr<UWetClothingAsset> WeakAsset)
    {
        UWetClothingAsset* Asset = WeakAsset.Get();
        if (Asset == nullptr)
        {
            return;
        }

        FString Message;
        if (!FDWCGeneratedAssetRelocator::RelocateGeneratedAssets(*Asset, Message))
        {
            FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Message));
            return;
        }
        FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Message));
    }
}

bool FDWCGeneratedAssetRelocator::RelocateGeneratedAssets(
    UWetClothingAsset& Asset,
    FString& OutMessage)
{
    OutMessage.Reset();
    const UPackage* WcaPackage = Asset.GetOutermost();
    if (WcaPackage == nullptr || !FPackageName::IsValidLongPackageName(WcaPackage->GetName()))
    {
        OutMessage = TEXT("Save the Wet Clothing Asset before relocating its generated assets.");
        return false;
    }

    TArray<UObject*> GeneratedAssets;
    Asset.GetOwnedGeneratedAssets(GeneratedAssets);
    if (GeneratedAssets.IsEmpty())
    {
        OutMessage = TEXT("No owned generated assets were found in this WCA's manifest.");
        return false;
    }

    const FString WcaFolder = FPackageName::GetLongPackagePath(WcaPackage->GetName());
    const FString TargetRoot = WcaFolder / TEXT("Generated") / Asset.GetName();
    TArray<FAssetRenameData> RenameData;
    RenameData.Reserve(GeneratedAssets.Num());

    for (UObject* GeneratedAsset : GeneratedAssets)
    {
        if (GeneratedAsset == nullptr ||
            !Asset.IsGeneratedAssetOwnedByThisWCA(GeneratedAsset) ||
            GeneratedAsset == &Asset)
        {
            continue;
        }

        const FString TargetFolder = TargetRoot / DWCGeneratedAssetRelocatorPrivate::ResolveSubfolder(*GeneratedAsset);
        const FString CurrentFolder = FPackageName::GetLongPackagePath(GeneratedAsset->GetOutermost()->GetName());
        if (CurrentFolder == TargetFolder)
        {
            continue;
        }

        RenameData.Emplace(GeneratedAsset, TargetFolder, GeneratedAsset->GetName());
    }

    if (RenameData.IsEmpty())
    {
        OutMessage = TEXT("All owned generated assets are already under the current WCA Generated folder.");
        return true;
    }

    const FText Confirmation = FText::Format(
        LOCTEXT(
            "RelocateGeneratedAssetsConfirmation",
            "Move {0} generated asset(s) under:\n\n{1}\n\nUnreal will preserve object references and create redirectors. Continue?"),
        FText::AsNumber(RenameData.Num()),
        FText::FromString(TargetRoot));
    if (FMessageDialog::Open(EAppMsgType::YesNo, Confirmation) != EAppReturnType::Yes)
    {
        OutMessage = TEXT("Generated asset relocation was cancelled.");
        return false;
    }

    FAssetToolsModule& AssetToolsModule =
        FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
    if (!AssetToolsModule.Get().RenameAssets(RenameData))
    {
        OutMessage = TEXT("One or more generated assets could not be moved. Check source-control status and output-path conflicts.");
        return false;
    }

    for (UObject* GeneratedAsset : GeneratedAssets)
    {
        if (GeneratedAsset != nullptr && Asset.IsGeneratedAssetOwnedByThisWCA(GeneratedAsset))
        {
            Asset.TagGeneratedAsset(GeneratedAsset);
        }
    }
    Asset.MarkPackageDirty();
    OutMessage = FString::Printf(
        TEXT("Moved %d generated asset(s) under %s. Redirectors were left for normal Unreal reference repair."),
        RenameData.Num(),
        *TargetRoot);
    return true;
}

void FDWCGeneratedAssetRelocator::RegisterContentBrowserMenu(void* Owner)
{
    if (Owner == nullptr)
    {
        return;
    }

    UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateLambda([Owner]()
    {
        FToolMenuOwnerScoped OwnerScoped(Owner);
        UToolMenu* ToolMenu = UToolMenus::Get()->ExtendMenu(
            TEXT("ContentBrowser.AssetContextMenu.WetClothingAsset"));
        if (ToolMenu == nullptr)
        {
            return;
        }

        FToolMenuSection& Section = ToolMenu->FindOrAddSection(TEXT("GetAssetActions"));
        Section.AddDynamicEntry(
            TEXT("DWCRelocateGeneratedAssets"),
            FNewToolMenuSectionDelegate::CreateLambda([](FToolMenuSection& InSection)
            {
                const UContentBrowserAssetContextMenuContext* Context =
                    InSection.FindContext<UContentBrowserAssetContextMenuContext>();
                if (Context == nullptr || !Context->bCanBeModified || Context->SelectedAssets.Num() != 1)
                {
                    return;
                }

                UWetClothingAsset* Asset =
                    Cast<UWetClothingAsset>(Context->SelectedAssets[0].GetAsset());
                if (Asset == nullptr)
                {
                    return;
                }

                InSection.AddMenuEntry(
                    TEXT("DWCRelocateGeneratedAssetsEntry"),
                    LOCTEXT("RelocateGeneratedAssets", "Move Generated Assets..."),
                    LOCTEXT(
                        "RelocateGeneratedAssetsTooltip",
                        "Move all generated outputs owned by this WCA under its current Content Browser folder."),
                    FSlateIcon(),
                    FUIAction(FExecuteAction::CreateStatic(
                        &DWCGeneratedAssetRelocatorPrivate::ExecuteRelocation,
                        TWeakObjectPtr<UWetClothingAsset>(Asset))));
            }));
    }));
}

#undef LOCTEXT_NAMESPACE
