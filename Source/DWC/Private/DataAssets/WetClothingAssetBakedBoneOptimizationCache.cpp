#include "DataAssets/WetClothingAssetBakedBoneOptimizationCache.h"

#include "Engine/SkeletalMesh.h"
#include "ReferenceSkeleton.h"

namespace
{
    void SetError(FString* OutErrorMessage, const TCHAR* Message)
    {
        if (OutErrorMessage != nullptr)
        {
            *OutErrorMessage = Message;
        }
    }

    FString MakeSkeletonSignature(const USkeletalMesh* SkeletalMesh)
    {
        if (SkeletalMesh == nullptr)
        {
            return FString();
        }

        const FReferenceSkeleton& RefSkeleton = SkeletalMesh->GetRefSkeleton();
        FString                   Signature = FString::Printf(TEXT("Bones=%d"), RefSkeleton.GetNum());
        for (int32 BoneIndex = 0; BoneIndex < RefSkeleton.GetNum(); ++BoneIndex)
        {
            Signature += FString::Printf(
                TEXT("|%d:%s:%d"),
                BoneIndex,
                *RefSkeleton.GetBoneName(BoneIndex).ToString(),
                RefSkeleton.GetParentIndex(BoneIndex));
        }
        return Signature;
    }
} // namespace

void FWetClothingAssetBakedBoneOptimizationCache::Reset()
{
    *this = FWetClothingAssetBakedBoneOptimizationCache();
}

bool FWetClothingAssetBakedBoneOptimizationCache::BuildFromRuntimeCache(
    const USkeletalMesh*             SkeletalMesh,
    const FWetBoneOptimizationCache& RuntimeCache,
    const FString&                   InMeshBuildSignature,
    FString*                         OutErrorMessage)
{
    Reset();

    if (SkeletalMesh == nullptr)
    {
        SetError(OutErrorMessage, TEXT("No skeletal mesh was provided for baked bone cache."));
        return false;
    }

    const FWetBonePrimaryVertexCache& PrimaryCache = RuntimeCache.PrimaryVertexCache;
    if (PrimaryCache.VertexCount <= 0 || PrimaryCache.BoneCount <= 0 || PrimaryCache.BoneStartOffsets.Num() != PrimaryCache.BoneCount + 1)
    {
        SetError(OutErrorMessage, TEXT("Runtime bone optimization cache is invalid."));
        return false;
    }

    const FReferenceSkeleton& RefSkeleton = SkeletalMesh->GetRefSkeleton();
    if (RefSkeleton.GetNum() != PrimaryCache.BoneCount)
    {
        SetError(OutErrorMessage, TEXT("Runtime bone optimization cache bone count does not match the skeletal mesh."));
        return false;
    }

    bIsValid = true;
    LODIndex = PrimaryCache.LODIndex;
    VertexCount = PrimaryCache.VertexCount;
    BoneCount = PrimaryCache.BoneCount;
    MeshBuildSignature = InMeshBuildSignature;
    SkeletonSignature = MakeSkeletonSignature(SkeletalMesh);
    SkinWeightSignature = FString::Printf(TEXT("LOD=%d|Vertices=%d|Bones=%d|Flat=%d"), LODIndex, VertexCount, BoneCount, PrimaryCache.FlatVertexIndices.Num());

    BoneNames.SetNum(BoneCount);
    for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
    {
        BoneNames[BoneIndex] = RefSkeleton.GetBoneName(BoneIndex);
    }

    BoneStartOffsets = PrimaryCache.BoneStartOffsets;
    FlatVertexIndices = PrimaryCache.FlatVertexIndices;

    ResolvedIncludeRules.Reset();
    for (const FWetResolvedBoneIncludeRule& RuntimeRule : RuntimeCache.ResolvedIncludeRules)
    {
        FWetClothingAssetBakedResolvedBoneIncludeRule BakedRule;
        BakedRule.TargetBoneIndex = RuntimeRule.TargetBoneIndex;
        BakedRule.IncludedBoneIndices = RuntimeRule.IncludedBoneIndices;
        ResolvedIncludeRules.Add(MoveTemp(BakedRule));
    }

    SetError(OutErrorMessage, TEXT(""));
    return true;
}

bool FWetClothingAssetBakedBoneOptimizationCache::IsValidForMesh(
    const USkeletalMesh* SkeletalMesh,
    const int32          InLODIndex,
    const int32          InVertexCount,
    const FString&       InMeshBuildSignature) const
{
    if (!bIsValid || SkeletalMesh == nullptr)
    {
        return false;
    }

    const FReferenceSkeleton& RefSkeleton = SkeletalMesh->GetRefSkeleton();
    if (LODIndex != InLODIndex || VertexCount != InVertexCount || BoneCount != RefSkeleton.GetNum())
    {
        return false;
    }

    if (BoneNames.Num() != BoneCount || BoneStartOffsets.Num() != BoneCount + 1 || MeshBuildSignature != InMeshBuildSignature)
    {
        return false;
    }

    for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
    {
        if (BoneNames[BoneIndex] != RefSkeleton.GetBoneName(BoneIndex))
        {
            return false;
        }
    }

    return SkeletonSignature == MakeSkeletonSignature(SkeletalMesh);
}

bool FWetClothingAssetBakedBoneOptimizationCache::CopyToRuntimeCache(
    USkeletalMesh*             SkeletalMesh,
    FWetBoneOptimizationCache& OutRuntimeCache) const
{
    OutRuntimeCache = FWetBoneOptimizationCache();
    if (!bIsValid || SkeletalMesh == nullptr)
    {
        return false;
    }

    OutRuntimeCache.PrimaryVertexCache.SourceMesh = SkeletalMesh;
    OutRuntimeCache.PrimaryVertexCache.LODIndex = LODIndex;
    OutRuntimeCache.PrimaryVertexCache.BoneCount = BoneCount;
    OutRuntimeCache.PrimaryVertexCache.VertexCount = VertexCount;
    OutRuntimeCache.PrimaryVertexCache.BoneStartOffsets = BoneStartOffsets;
    OutRuntimeCache.PrimaryVertexCache.FlatVertexIndices = FlatVertexIndices;

    OutRuntimeCache.ResolvedIncludeRules.Reset();
    for (const FWetClothingAssetBakedResolvedBoneIncludeRule& BakedRule : ResolvedIncludeRules)
    {
        FWetResolvedBoneIncludeRule RuntimeRule;
        RuntimeRule.TargetBoneIndex = BakedRule.TargetBoneIndex;
        RuntimeRule.IncludedBoneIndices = BakedRule.IncludedBoneIndices;
        OutRuntimeCache.ResolvedIncludeRules.Add(MoveTemp(RuntimeRule));
    }

    return true;
}
