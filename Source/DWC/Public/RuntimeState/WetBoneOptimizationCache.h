// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class USkeletalMesh;

/*
A target bone and the fully resolved bones that must be searched with it.
The included array is flattened during asset precomputation. Runtime code does
not recurse through the reference skeleton.
*/
struct DWC_API FWetResolvedBoneIncludeRule
{
    int32         TargetBoneIndex = INDEX_NONE;
    TArray<int32> IncludedBoneIndices;
};

/*
Primary-bone vertex buckets for one skeletal-mesh LOD.
Each render vertex is assigned only from skin-weight Influence 0.
*/
struct DWC_API FWetBonePrimaryVertexCache
{
    USkeletalMesh* SourceMesh = nullptr;
    int32          LODIndex = INDEX_NONE;
    int32          BoneCount = 0;
    int32          VertexCount = 0;
    TArray<int32>  BoneStartOffsets;
    TArray<int32>  FlatVertexIndices;
};

struct DWC_API FWetBoneOptimizationCache
{
    FWetBonePrimaryVertexCache          PrimaryVertexCache;
    TArray<FWetResolvedBoneIncludeRule> ResolvedIncludeRules;
};
