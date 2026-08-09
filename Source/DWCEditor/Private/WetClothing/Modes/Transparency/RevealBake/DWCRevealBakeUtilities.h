//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

class UObject;

class FDWCRevealBakeUtilities
{
  public:
    static FString GetGeneratedPackagePath(const UObject& OwningAsset, const FString& RelativePath);
    static FString SanitizeAssetToken(const FString& InToken);
};
