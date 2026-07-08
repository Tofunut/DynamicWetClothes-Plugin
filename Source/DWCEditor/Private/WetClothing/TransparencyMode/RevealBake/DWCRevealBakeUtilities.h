#pragma once

#include "CoreMinimal.h"

class FDWCRevealBakeUtilities
{
  public:
    static const TCHAR* GetDefaultRevealBakePackagePath();
    static double GetElapsedMilliseconds(double StartTimeSeconds);
    static FString SanitizeAssetToken(const FString& InToken);
};
