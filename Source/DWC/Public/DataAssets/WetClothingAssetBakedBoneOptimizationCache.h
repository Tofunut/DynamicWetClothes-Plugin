#pragma once

#include "CoreMinimal.h"
#include "RuntimeData/WetBoneOptimizationCache.h"
#include "WetClothingAssetBakedBoneOptimizationCache.generated.h"

class USkeletalMesh;

USTRUCT(BlueprintType)
struct DWC_API FWetClothingAssetBakedResolvedBoneIncludeRule
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category = "Baked Bone Cache")
    int32 TargetBoneIndex = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, Category = "Baked Bone Cache")
    TArray<int32> IncludedBoneIndices;
};

USTRUCT(BlueprintType)
struct DWC_API FWetClothingAssetBakedBoneOptimizationCache
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, Category = "Baked Bone Cache")
    bool bIsValid = false;

    UPROPERTY(VisibleAnywhere, Category = "Baked Bone Cache")
    int32 LODIndex = INDEX_NONE;

    UPROPERTY(VisibleAnywhere, Category = "Baked Bone Cache")
    int32 VertexCount = 0;

    UPROPERTY(VisibleAnywhere, Category = "Baked Bone Cache")
    int32 BoneCount = 0;

    UPROPERTY(VisibleAnywhere, Category = "Baked Bone Cache")
    TArray<FName> BoneNames;

    UPROPERTY(VisibleAnywhere, Category = "Baked Bone Cache")
    TArray<int32> BoneStartOffsets;

    UPROPERTY(VisibleAnywhere, Category = "Baked Bone Cache")
    TArray<int32> FlatVertexIndices;

    UPROPERTY(VisibleAnywhere, Category = "Baked Bone Cache")
    TArray<FWetClothingAssetBakedResolvedBoneIncludeRule> ResolvedIncludeRules;

    UPROPERTY(VisibleAnywhere, Category = "Baked Bone Cache")
    FString MeshBuildSignature;

    UPROPERTY(VisibleAnywhere, Category = "Baked Bone Cache")
    FString SkeletonSignature;

    UPROPERTY(VisibleAnywhere, Category = "Baked Bone Cache")
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
