#pragma once

#include "CoreMinimal.h"
#include "RuntimeData/WetBoneOptimizationCache.h"

class UWetClothingAsset;
class USkeletalMesh;

class DWC_API FWetBakedRuntimeDataBridge
{
  public:
    static bool TryCopyBakedBoneOptimizationCache(
        const UWetClothingAsset*   WetClothingAsset,
        USkeletalMesh*             SkeletalMesh,
        int32                      LODIndex,
        FWetBoneOptimizationCache& OutRuntimeCache,
        FString*                   OutErrorMessage = nullptr);
};
