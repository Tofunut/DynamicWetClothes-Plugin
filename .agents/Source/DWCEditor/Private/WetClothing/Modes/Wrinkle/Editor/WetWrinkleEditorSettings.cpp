#include "WetWrinkleEditorSettings.h"

FString UWetWrinkleEditorSettings::NormalizeContentPath(const FString& InPath)
{
    FString Result = InPath.TrimStartAndEnd();
    Result.ReplaceInline(TEXT("\\"), TEXT("/"));
    while (Result.Len() > 1 && Result.EndsWith(TEXT("/")))
    {
        Result.LeftChopInline(1);
    }
    return Result;
}

void UWetWrinkleEditorSettings::GetNormalTextureSearchPaths(TArray<FString>& OutPaths) const
{
    OutPaths.Reset();
    OutPaths.Add(DefaultNormalTexturePath);
    for (const FDirectoryPath& Directory : AdditionalNormalTexturePaths)
    {
        const FString Normalized = NormalizeContentPath(Directory.Path);
        if (Normalized.StartsWith(TEXT("/")))
        {
            OutPaths.AddUnique(Normalized);
        }
    }
}

bool UWetWrinkleEditorSettings::AddNormalTextureSearchPath(const FString& InPath)
{
    const FString Normalized = NormalizeContentPath(InPath);
    if (!Normalized.StartsWith(TEXT("/")) || Normalized == DefaultNormalTexturePath)
    {
        return false;
    }

    if (AdditionalNormalTexturePaths.ContainsByPredicate(
            [&Normalized](const FDirectoryPath& Existing)
            {
                return NormalizeContentPath(Existing.Path).Equals(Normalized, ESearchCase::IgnoreCase);
            }))
    {
        return false;
    }

    FDirectoryPath& Added = AdditionalNormalTexturePaths.AddDefaulted_GetRef();
    Added.Path = Normalized;
    SaveConfig();
    return true;
}

void UWetWrinkleEditorSettings::RemoveNormalTextureSearchPath(const FString& InPath)
{
    const FString Normalized = NormalizeContentPath(InPath);
    const int32 Removed = AdditionalNormalTexturePaths.RemoveAll(
        [&Normalized](const FDirectoryPath& Existing)
        {
            return NormalizeContentPath(Existing.Path).Equals(Normalized, ESearchCase::IgnoreCase);
        });
    if (Removed > 0)
    {
        SaveConfig();
    }
}

bool UWetWrinkleEditorSettings::IsNormalTextureHidden(const FSoftObjectPath& TexturePath) const
{
    return TexturePath.IsValid() && HiddenNormalTexturePaths.Contains(TexturePath);
}

void UWetWrinkleEditorSettings::SetNormalTextureHidden(const FSoftObjectPath& TexturePath, const bool bHidden)
{
    if (!TexturePath.IsValid())
    {
        return;
    }

    if (bHidden)
    {
        HiddenNormalTexturePaths.AddUnique(TexturePath);
    }
    else
    {
        HiddenNormalTexturePaths.Remove(TexturePath);
    }
    SaveConfig();
}
