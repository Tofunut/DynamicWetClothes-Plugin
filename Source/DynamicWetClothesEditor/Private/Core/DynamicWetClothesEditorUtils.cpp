#include "DynamicWetClothesEditorUtils.h"

#include "DesktopPlatformModule.h"
#include "FileHelpers.h"
#include "Framework/Application/SlateApplication.h"
#include "IDesktopPlatform.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Object.h"
#include "UObject/Package.h"

namespace
{
    bool ConvertDirectoryToContentPath(const FString& Directory, FString& OutContentPath)
    {
        FString ProjectContentDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());
        FString NormalizedProjectContentDirectory = ProjectContentDirectory;
        FString NormalizedDirectory = Directory;
        FPaths::NormalizeDirectoryName(NormalizedProjectContentDirectory);
        FPaths::NormalizeDirectoryName(NormalizedDirectory);

        if (!NormalizedDirectory.StartsWith(NormalizedProjectContentDirectory))
        {
            return false;
        }

        FString RelativeDirectory = NormalizedDirectory.RightChop(NormalizedProjectContentDirectory.Len());
        RelativeDirectory.RemoveFromStart(TEXT("/"));
        RelativeDirectory.RemoveFromStart(TEXT("\\"));
        RelativeDirectory.ReplaceInline(TEXT("\\"), TEXT("/"));

        OutContentPath = RelativeDirectory.IsEmpty()
                             ? TEXT("/Game")
                             : FString::Printf(TEXT("/Game/%s"), *RelativeDirectory);
        return true;
    }
} // namespace

TArray<FString> DynamicWetClothesEditorUtils::BuildUniqueProfileSearchPaths(const TArray<FString>& AdditionalPaths)
{
    TArray<FString> Result;
    Result.Add(DefaultWetnessProfileLibraryPath);

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

bool DynamicWetClothesEditorUtils::PromptForContentFolder(FString& OutContentPath)
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
        TEXT("Choose a folder inside this project's Content directory"),
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
            FText::FromString(TEXT("Please choose a folder inside this project's Content directory.")));
        return false;
    }

    return true;
}

bool DynamicWetClothesEditorUtils::SaveAsset(UObject* Asset)
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
