#pragma once

#include "CoreMinimal.h"
class USkeletalMesh;

#include "RuntimeState/WetBoneOptimizationCache.h"

class DWC_API FWetBoneOptimizationCacheBuilder
{
  public:
    static bool Build(
        const USkeletalMesh*               SkeletalMesh,
        int32                              LODIndex,
        const TArray<FWetBoneIncludeRule>& IncludeRules,
        FWetBoneOptimizationCache&         OutCache,
        FString*                           OutErrorMessage = nullptr);
};
