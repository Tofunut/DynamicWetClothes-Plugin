#include "DataAssets/WetClothingPrecomputedBoneOptimizationCache.h"

#include "Engine/SkeletalMesh.h"
#include "ReferenceSkeleton.h"
#include "Utility/DWCError.h"

namespace
{
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

void FWetClothingPrecomputedBoneOptimizationCache::Reset()
{
    *this = FWetClothingPrecomputedBoneOptimizationCache();
}

bool FWetClothingPrecomputedBoneOptimizationCache::BuildFromRuntimeCache(
    const USkeletalMesh*             SkeletalMesh,
    const FWetBoneOptimizationCache& RuntimeCache,
    const FString&                   InMeshBuildSignature,
    FString*                         OutErrorMessage)
{
    Reset();

    if (SkeletalMesh == nullptr)
    {
        DWC::Error::SetMessage(OutErrorMessage, TEXT("No skeletal mesh was provided for precomputed bone cache."));
        return false;
    }

    const FWetBonePrimaryVertexCache& PrimaryCache = RuntimeCache.PrimaryVertexCache;
    if (PrimaryCache.VertexCount <= 0 || PrimaryCache.BoneCount <= 0 || PrimaryCache.BoneStartOffsets.Num() != PrimaryCache.BoneCount + 1)
    {
        DWC::Error::SetMessage(OutErrorMessage, TEXT("Runtime bone optimization cache is invalid."));
        return false;
    }

    const FReferenceSkeleton& RefSkeleton = SkeletalMesh->GetRefSkeleton();
    if (RefSkeleton.GetNum() != PrimaryCache.BoneCount)
    {
        DWC::Error::SetMessage(OutErrorMessage, TEXT("Runtime bone optimization cache bone count does not match the skeletal mesh."));
        return false;
    }

    bIsValid = true;
    CacheFormatVersion = 2;
    LODIndex = PrimaryCache.LODIndex;
    VertexCount = PrimaryCache.VertexCount;
    BoneCount = PrimaryCache.BoneCount;
    MeshBuildSignature = InMeshBuildSignature;
    SkeletonSignature = MakeSkeletonSignature(SkeletalMesh);
    SkinWeightSignature = FString::Printf(TEXT("Format=2|PrimaryInfluence=0|LOD=%d|Vertices=%d|Bones=%d|Flat=%d"), LODIndex, VertexCount, BoneCount, PrimaryCache.FlatVertexIndices.Num());

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
        FWetClothingPrecomputedResolvedBoneIncludeRule PrecomputedRule;
        PrecomputedRule.TargetBoneIndex = RuntimeRule.TargetBoneIndex;
        PrecomputedRule.IncludedBoneIndices = RuntimeRule.IncludedBoneIndices;
        ResolvedIncludeRules.Add(MoveTemp(PrecomputedRule));
    }

    DWC::Error::SetMessage(OutErrorMessage, TEXT(""));
    return true;
}

bool FWetClothingPrecomputedBoneOptimizationCache::IsValidForMesh(
    const USkeletalMesh* SkeletalMesh,
    const int32          InLODIndex,
    const int32          InVertexCount,
    const FString&       InMeshBuildSignature) const
{
    if (!bIsValid || CacheFormatVersion != 2 || SkeletalMesh == nullptr)
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

    for (const FWetClothingPrecomputedResolvedBoneIncludeRule& Rule : ResolvedIncludeRules)
    {
        if (Rule.TargetBoneIndex < 0 || Rule.TargetBoneIndex >= BoneCount)
        {
            return false;
        }

        for (const int32 IncludedBoneIndex : Rule.IncludedBoneIndices)
        {
            if (IncludedBoneIndex < 0 || IncludedBoneIndex >= BoneCount)
            {
                return false;
            }
        }
    }

    return SkeletonSignature == MakeSkeletonSignature(SkeletalMesh);
}

bool FWetClothingPrecomputedBoneOptimizationCache::CopyToRuntimeCache(
    USkeletalMesh*             SkeletalMesh,
    FWetBoneOptimizationCache& OutRuntimeCache) const
{
    OutRuntimeCache = FWetBoneOptimizationCache();
    if (!bIsValid || CacheFormatVersion != 2 || SkeletalMesh == nullptr)
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
    for (const FWetClothingPrecomputedResolvedBoneIncludeRule& PrecomputedRule : ResolvedIncludeRules)
    {
        FWetResolvedBoneIncludeRule RuntimeRule;
        RuntimeRule.TargetBoneIndex = PrecomputedRule.TargetBoneIndex;
        RuntimeRule.IncludedBoneIndices = PrecomputedRule.IncludedBoneIndices;
        OutRuntimeCache.ResolvedIncludeRules.Add(MoveTemp(RuntimeRule));
    }

    return true;
}
