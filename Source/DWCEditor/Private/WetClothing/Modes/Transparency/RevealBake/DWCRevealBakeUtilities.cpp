//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Modes/Transparency/RevealBake/DWCRevealBakeUtilities.h"
#include "Core/DWCGeneratedAssetPaths.h"

#include "UObject/NameTypes.h"
#include "Misc/PackageName.h"
#include "UObject/Object.h"

FString FDWCRevealBakeUtilities::GetGeneratedPackagePath(const UObject& OwningAsset, const FString& RelativePath)
{
    const FString OwningFolder = FPackageName::GetLongPackagePath(OwningAsset.GetOutermost()->GetName());
    if (OwningFolder.IsEmpty())
    {
        return FString();
    }

    FString Result = DWCGeneratedAssetPaths::MakeAssetRoot(OwningFolder, OwningAsset.GetName());
    if (!RelativePath.IsEmpty())
    {
        Result /= RelativePath;
    }
    return Result;
}

double FDWCRevealBakeUtilities::GetElapsedMilliseconds(const double StartTimeSeconds)
{
    return (FPlatformTime::Seconds() - StartTimeSeconds) * 1000.0;
}

FString FDWCRevealBakeUtilities::SanitizeAssetToken(const FString& InToken)
{
    FString Result = InToken;
    const TCHAR* InvalidChars = INVALID_OBJECTNAME_CHARACTERS;
    while (*InvalidChars)
    {
        Result.ReplaceCharInline(*InvalidChars, TEXT('_'));
        ++InvalidChars;
    }

    Result.ReplaceInline(TEXT(" "), TEXT("_"));
    return Result;
}
