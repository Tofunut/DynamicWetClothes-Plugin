#pragma once

#include "CoreMinimal.h"

class USkeletalMesh;

struct DWC_API FWetBoneIncludeRule
{
    FName         TargetBoneName;
    TArray<FName> ParentBoneNamesToInclude;
    TArray<FName> ChildBoneNamesToInclude;
};

struct DWC_API FWetResolvedBoneIncludeRule
{
    int32         TargetBoneIndex = INDEX_NONE;
    TArray<int32> IncludedBoneIndices;
};

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
