#pragma once

#include "CoreMinimal.h"
#include "WetClothingAsset.h"

class USkeletalMesh;

struct DYNAMICWETCLOTHES_API FWetClothingBoneIncludeRule
{
    FName        TargetBoneName;
    TArray<FName> ParentBoneNamesToInclude;
    TArray<FName> ChildBoneNamesToInclude;
};

struct DYNAMICWETCLOTHES_API FWetClothingResolvedBoneIncludeRule
{
    int32        TargetBoneIndex = INDEX_NONE;
    TArray<int32> IncludedBoneIndices;
};

struct DYNAMICWETCLOTHES_API FWetClothingBonePrimaryVertexCache
{
    USkeletalMesh* SourceMesh = nullptr;
    int32          LODIndex = INDEX_NONE;
    int32          BoneCount = 0;
    int32          VertexCount = 0;
    TArray<int32>  BoneStartOffsets;
    TArray<int32>  FlatVertexIndices;
};

struct DYNAMICWETCLOTHES_API FWetClothingSkeletalMeshOptimizationCache
{
    FWetClothingBonePrimaryVertexCache          PrimaryVertexCache;
    TArray<FWetClothingResolvedBoneIncludeRule> ResolvedIncludeRules;
};

class DYNAMICWETCLOTHES_API FWetClothingSkeletalMeshCacheBuilder
{
  public:
    static bool Build(
        const USkeletalMesh*                       SkeletalMesh,
        int32                                      LODIndex,
        const TArray<FWetClothingBoneIncludeRule>& IncludeRules,
        FWetClothingSkeletalMeshOptimizationCache& OutCache,
        FString*                                   OutErrorMessage = nullptr);
};
