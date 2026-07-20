#pragma once

#include "CoreMinimal.h"

class UObject;

class FDWCRevealBakeUtilities
{
  public:
    static const TCHAR* GetDefaultRevealBakePackagePath();
    static FString GetGeneratedPackagePath(const UObject& OwningAsset, const FString& RelativePath);
    static double GetElapsedMilliseconds(double StartTimeSeconds);
    static FString SanitizeAssetToken(const FString& InToken);
};
