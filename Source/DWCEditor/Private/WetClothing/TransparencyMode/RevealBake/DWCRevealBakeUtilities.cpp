#include "WetClothing/TransparencyMode/RevealBake/DWCRevealBakeUtilities.h"

#include "UObject/NameTypes.h"

const TCHAR* FDWCRevealBakeUtilities::GetDefaultRevealBakePackagePath()
{
    return TEXT("/Game/DWC/Reveals");
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
