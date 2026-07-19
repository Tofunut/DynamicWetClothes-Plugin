#include "WetClothing/TransparencyBake/RevealBake/DWCRevealBakeUtilities.h"

#include "UObject/NameTypes.h"
#include "Misc/PackageName.h"
#include "UObject/Object.h"

const TCHAR* FDWCRevealBakeUtilities::GetDefaultRevealBakePackagePath()
{
    return TEXT("/Game/DWC/Reveals");
}

FString FDWCRevealBakeUtilities::GetGeneratedPackagePath(const UObject& OwningAsset, const FString& RelativePath)
{
    const FString OwningFolder = FPackageName::GetLongPackagePath(OwningAsset.GetOutermost()->GetName());
    if (OwningFolder.IsEmpty())
    {
        return FString();
    }

    FString Result = OwningFolder / TEXT("Generated") / OwningAsset.GetName();
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
