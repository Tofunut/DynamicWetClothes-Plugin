// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RuntimeState/WetBoneOptimizationCache.h"

class USkeletalMesh;

class DWC_API FWetBoneOptimizationCacheBuilder
{
  public:
    /*
    Builds the complete precomputed cache:
    - vertex buckets use skin-weight Influence 0 only;
    - include rules are generated automatically from the Physics Asset;
    - collisionless parent/child chains are flattened with DFS.
    */
    static bool Build(
        const USkeletalMesh*       SkeletalMesh,
        int32                      LODIndex,
        FWetBoneOptimizationCache& OutCache,
        FString*                   OutErrorMessage = nullptr);
};
