#include "DWCEditorUtils.h"

#include "DesktopPlatformModule.h"
#include "FileHelpers.h"
#include "Framework/Application/SlateApplication.h"
#include "IDesktopPlatform.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Object.h"
#include "UObject/Package.h"

namespace
{
    bool ConvertDirectoryUnderRootToContentPath(
        const FString& Directory,
        const FString& RootDirectory,
        const FString& RootContentPath,
        FString&       OutContentPath)
    {
        FString NormalizedRootDirectory = RootDirectory;
        FString NormalizedDirectory = Directory;
        FPaths::NormalizeDirectoryName(NormalizedRootDirectory);
        FPaths::NormalizeDirectoryName(NormalizedDirectory);

        if (!NormalizedDirectory.StartsWith(NormalizedRootDirectory))
        {
            return false;
        }

        FString RelativeDirectory = NormalizedDirectory.RightChop(NormalizedRootDirectory.Len());
        RelativeDirectory.RemoveFromStart(TEXT("/"));
        RelativeDirectory.RemoveFromStart(TEXT("\\"));
        RelativeDirectory.ReplaceInline(TEXT("\\"), TEXT("/"));

        OutContentPath = RelativeDirectory.IsEmpty()
                             ? RootContentPath
                             : FString::Printf(TEXT("%s/%s"), *RootContentPath, *RelativeDirectory);
        return true;
    }

    bool ConvertDirectoryToContentPath(const FString& Directory, FString& OutContentPath)
    {
        const FString ProjectContentDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());
        if (ConvertDirectoryUnderRootToContentPath(Directory, ProjectContentDirectory, TEXT("/Game"), OutContentPath))
        {
            return true;
        }

        for (const TSharedRef<IPlugin>& Plugin : IPluginManager::Get().GetEnabledPlugins())
        {
            if (!Plugin->CanContainContent())
            {
                continue;
            }

            const FString PluginContentDirectory = FPaths::ConvertRelativePathToFull(Plugin->GetContentDir());
            const FString PluginRootContentPath = FString::Printf(TEXT("/%s"), *Plugin->GetName());
            if (ConvertDirectoryUnderRootToContentPath(Directory, PluginContentDirectory, PluginRootContentPath, OutContentPath))
            {
                return true;
            }
        }

        return false;
    }
} // namespace

TArray<FString> DWCEditorUtils::BuildUniqueProfileSearchPaths(const TArray<FString>& AdditionalPaths)
{
    TArray<FString> Result;
    Result.Add(DefaultWetnessProfileLibraryPath);
    Result.Add(PluginWetnessProfileLibraryPath);

    for (const FString& AdditionalPath : AdditionalPaths)
    {
        if (AdditionalPath.IsEmpty())
        {
            continue;
        }

        if (!Result.Contains(AdditionalPath))
        {
            Result.Add(AdditionalPath);
        }
    }

    return Result;
}

bool DWCEditorUtils::PromptForContentFolder(FString& OutContentPath)
{
    IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
    if (DesktopPlatform == nullptr)
    {
        return false;
    }

    const void* ParentWindowHandle = FSlateApplication::IsInitialized()
                                         ? FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr)
                                         : nullptr;

    FString    SelectedDirectory;
    const bool bFolderSelected = DesktopPlatform->OpenDirectoryDialog(
        ParentWindowHandle,
        TEXT("Choose a folder inside this project's Content directory or an enabled plugin Content directory"),
        FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir()),
        SelectedDirectory);

    if (!bFolderSelected)
    {
        return false;
    }

    if (!ConvertDirectoryToContentPath(SelectedDirectory, OutContentPath))
    {
        FMessageDialog::Open(
            EAppMsgType::Ok,
            FText::FromString(TEXT("Please choose a folder inside this project's Content directory or an enabled plugin Content directory.")));
        return false;
    }

    return true;
}

bool DWCEditorUtils::SaveAsset(UObject* Asset)
{
    if (Asset == nullptr)
    {
        return false;
    }

    UPackage* Package = Asset->GetOutermost();
    if (Package == nullptr)
    {
        return false;
    }

    TArray<UPackage*> PackagesToSave;
    PackagesToSave.Add(Package);
    return FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSave, false, false) == FEditorFileUtils::PR_Success;
}
