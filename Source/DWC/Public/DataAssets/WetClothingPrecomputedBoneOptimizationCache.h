//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "RuntimeState/WetBoneOptimizationCache.h"
#include "WetClothingPrecomputedBoneOptimizationCache.generated.h"

class USkeletalMesh;

USTRUCT()
struct DWC_API FWetClothingPrecomputedResolvedBoneIncludeRule
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category = "Precomputed Bone Cache")
    int32 TargetBoneIndex = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, Category = "Precomputed Bone Cache")
    TArray<int32> IncludedBoneIndices;
};

USTRUCT()
struct DWC_API FWetClothingPrecomputedBoneOptimizationCache
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category = "Precomputed Bone Cache")
    bool bIsValid = false;

    UPROPERTY(VisibleAnywhere, Category = "Precomputed Bone Cache")
    int32 CacheFormatVersion = 0;

    UPROPERTY(VisibleAnywhere, Category = "Precomputed Bone Cache")
    int32 LODIndex = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, Category = "Precomputed Bone Cache")
    int32 VertexCount = 0;

    UPROPERTY(VisibleAnywhere, Category = "Precomputed Bone Cache")
    int32 BoneCount = 0;

    UPROPERTY(VisibleAnywhere, Category = "Precomputed Bone Cache")
    TArray<FName> BoneNames;

    UPROPERTY(VisibleAnywhere, Category = "Precomputed Bone Cache")
    TArray<int32> BoneStartOffsets;

    UPROPERTY(VisibleAnywhere, Category = "Precomputed Bone Cache")
    TArray<int32> FlatVertexIndices;

    UPROPERTY(VisibleAnywhere, Category = "Precomputed Bone Cache")
    TArray<FWetClothingPrecomputedResolvedBoneIncludeRule> ResolvedIncludeRules;

    UPROPERTY(VisibleAnywhere, Category = "Precomputed Bone Cache")
    FString MeshBuildSignature;

    UPROPERTY(VisibleAnywhere, Category = "Precomputed Bone Cache")
    FString SkeletonSignature;

    UPROPERTY(VisibleAnywhere, Category = "Precomputed Bone Cache")
    FString SkinWeightSignature;

    void Reset();
    bool BuildFromRuntimeCache(
        const USkeletalMesh*             SkeletalMesh,
        const FWetBoneOptimizationCache& RuntimeCache,
        const FString&                   InMeshBuildSignature,
        FString*                         OutErrorMessage = nullptr);
    bool IsValidForMesh(const USkeletalMesh* SkeletalMesh, int32 InLODIndex, int32 InVertexCount, const FString& InMeshBuildSignature) const;
    bool CopyToRuntimeCache(USkeletalMesh* SkeletalMesh, FWetBoneOptimizationCache& OutRuntimeCache) const;
};
