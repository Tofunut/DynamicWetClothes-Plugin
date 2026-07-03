#include "RuntimeData/WetBoneOptimizationCacheBuilder.h"

#include "Engine/SkeletalMesh.h"
#include "ReferenceSkeleton.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkinWeightVertexBuffer.h"
#include "Utility/DWCError.h"

namespace WetClothingSkeletalMeshCacheBuilderInternal
{
    static bool AddValidUniqueBoneIndex(
        int32          BoneIndex,
        int32          BoneCount,
        TSet<int32>&   UniqueBoneIndices,
        TArray<int32>& OutBoneIndices)
    {
        if (BoneIndex == INDEX_NONE || BoneIndex < 0 || BoneIndex >= BoneCount || UniqueBoneIndices.Contains(BoneIndex))
        {
            return false;
        }

        UniqueBoneIndices.Add(BoneIndex);
        OutBoneIndices.Add(BoneIndex);
        return true;
    }

    static bool BuildPrimaryVertexCache(
        const USkeletalMesh*              SkeletalMesh,
        int32                             LODIndex,
        const FSkeletalMeshLODRenderData& LODData,
        FWetBonePrimaryVertexCache&       OutCache,
        FString*                          OutErrorMessage)
    {
        const FSkinWeightVertexBuffer* SkinWeightBuffer = LODData.GetSkinWeightVertexBuffer();
        if (SkinWeightBuffer == nullptr)
        {
            DWC::Error::SetMessage(OutErrorMessage, TEXT("TargetMesh skin weight buffer is unavailable."));
            return false;
        }

        const int32 BoneCount = SkeletalMesh->GetRefSkeleton().GetNum();
        const int32 VertexCount = static_cast<int32>(LODData.GetNumVertices());
        if (BoneCount <= 0)
        {
            DWC::Error::SetMessage(OutErrorMessage, TEXT("TargetMesh has no reference skeleton bones."));
            return false;
        }

        if (VertexCount <= 0)
        {
            DWC::Error::SetMessage(OutErrorMessage, TEXT("TargetMesh LOD has no render vertices."));
            return false;
        }

        const uint32 MaxInfluences = SkinWeightBuffer->GetMaxBoneInfluences();
        if (MaxInfluences == 0)
        {
            DWC::Error::SetMessage(OutErrorMessage, TEXT("TargetMesh skin weight buffer has no bone influences."));
            return false;
        }

        TArray<int32> PrimaryBoneByVertex;
        TArray<int32> VertexCountsByBone;
        PrimaryBoneByVertex.Init(INDEX_NONE, VertexCount);
        VertexCountsByBone.Init(0, BoneCount);

        for (const FSkelMeshRenderSection& Section : LODData.RenderSections)
        {
            if (!Section.IsValid())
            {
                continue;
            }

            const int32 SectionVertexStart = Section.GetVertexBufferIndex();
            const int32 SectionVertexEnd = FMath::Min(SectionVertexStart + Section.GetNumVertices(), VertexCount);
            for (int32 VertexIndex = SectionVertexStart; VertexIndex < SectionVertexEnd; ++VertexIndex)
            {
                uint16 MaxBoneWeight = 0;
                int32  PrimaryBoneIndex = INDEX_NONE;

                for (uint32 InfluenceIndex = 0; InfluenceIndex < MaxInfluences; ++InfluenceIndex)
                {
                    const uint16 BoneWeight = SkinWeightBuffer->GetBoneWeight(VertexIndex, InfluenceIndex);
                    if (BoneWeight == 0 || BoneWeight <= MaxBoneWeight)
                    {
                        continue;
                    }

                    const int32 SectionBoneIndex = static_cast<int32>(SkinWeightBuffer->GetBoneIndex(VertexIndex, InfluenceIndex));
                    if (!Section.BoneMap.IsValidIndex(SectionBoneIndex))
                    {
                        continue;
                    }

                    const int32 BoneIndex = static_cast<int32>(Section.BoneMap[SectionBoneIndex]);
                    if (BoneIndex < 0 || BoneIndex >= BoneCount)
                    {
                        continue;
                    }

                    MaxBoneWeight = BoneWeight;
                    PrimaryBoneIndex = BoneIndex;
                }

                if (PrimaryBoneIndex != INDEX_NONE)
                {
                    PrimaryBoneByVertex[VertexIndex] = PrimaryBoneIndex;
                    ++VertexCountsByBone[PrimaryBoneIndex];
                }
            }
        }

        OutCache.SourceMesh = const_cast<USkeletalMesh*>(SkeletalMesh);
        OutCache.LODIndex = LODIndex;
        OutCache.BoneCount = BoneCount;
        OutCache.VertexCount = VertexCount;
        OutCache.BoneStartOffsets.Reset();
        OutCache.BoneStartOffsets.SetNumZeroed(BoneCount + 1);

        for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
        {
            OutCache.BoneStartOffsets[BoneIndex + 1] = OutCache.BoneStartOffsets[BoneIndex] + VertexCountsByBone[BoneIndex];
        }

        const int32 AssignedVertexCount = OutCache.BoneStartOffsets.Last();
        OutCache.FlatVertexIndices.Reset();
        OutCache.FlatVertexIndices.SetNumUninitialized(AssignedVertexCount);

        TArray<int32> WriteOffsets = OutCache.BoneStartOffsets;
        for (int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
        {
            const int32 PrimaryBoneIndex = PrimaryBoneByVertex[VertexIndex];
            if (PrimaryBoneIndex == INDEX_NONE)
            {
                continue;
            }

            const int32 WriteIndex = WriteOffsets[PrimaryBoneIndex]++;
            OutCache.FlatVertexIndices[WriteIndex] = VertexIndex;
        }

        return true;
    }

    static void ResolveIncludeRules(
        const USkeletalMesh*                 SkeletalMesh,
        const TArray<FWetBoneIncludeRule>&   IncludeRules,
        TArray<FWetResolvedBoneIncludeRule>& OutResolvedIncludeRules)
    {
        OutResolvedIncludeRules.Reset();

        const FReferenceSkeleton& RefSkeleton = SkeletalMesh->GetRefSkeleton();
        const int32               BoneCount = RefSkeleton.GetNum();

        for (const FWetBoneIncludeRule& IncludeRule : IncludeRules)
        {
            const int32 TargetBoneIndex = RefSkeleton.FindBoneIndex(IncludeRule.TargetBoneName);
            if (TargetBoneIndex == INDEX_NONE)
            {
                continue;
            }

            FWetResolvedBoneIncludeRule ResolvedRule;
            ResolvedRule.TargetBoneIndex = TargetBoneIndex;

            TSet<int32> UniqueBoneIndices;
            AddValidUniqueBoneIndex(TargetBoneIndex, BoneCount, UniqueBoneIndices, ResolvedRule.IncludedBoneIndices);

            for (const FName ParentBoneName : IncludeRule.ParentBoneNamesToInclude)
            {
                AddValidUniqueBoneIndex(RefSkeleton.FindBoneIndex(ParentBoneName), BoneCount, UniqueBoneIndices, ResolvedRule.IncludedBoneIndices);
            }

            for (const FName ChildBoneName : IncludeRule.ChildBoneNamesToInclude)
            {
                AddValidUniqueBoneIndex(RefSkeleton.FindBoneIndex(ChildBoneName), BoneCount, UniqueBoneIndices, ResolvedRule.IncludedBoneIndices);
            }

            OutResolvedIncludeRules.Add(ResolvedRule);
        }
    }
} // namespace WetClothingSkeletalMeshCacheBuilderInternal

bool FWetBoneOptimizationCacheBuilder::Build(
    const USkeletalMesh*               SkeletalMesh,
    int32                              LODIndex,
    const TArray<FWetBoneIncludeRule>& IncludeRules,
    FWetBoneOptimizationCache&         OutCache,
    FString*                           OutErrorMessage)
{
    using namespace WetClothingSkeletalMeshCacheBuilderInternal;

    OutCache = FWetBoneOptimizationCache();

    if (SkeletalMesh == nullptr)
    {
        DWC::Error::SetMessage(OutErrorMessage, TEXT("No TargetMesh is assigned."));
        return false;
    }

    const FSkeletalMeshRenderData* RenderData = SkeletalMesh->GetResourceForRendering();
    if (RenderData == nullptr)
    {
        DWC::Error::SetMessage(OutErrorMessage, TEXT("TargetMesh render data is unavailable."));
        return false;
    }

    if (!RenderData->LODRenderData.IsValidIndex(LODIndex))
    {
        DWC::Error::SetMessage(OutErrorMessage, TEXT("Requested TargetMesh LOD render data is unavailable."));
        return false;
    }

    const FSkeletalMeshLODRenderData& LODData = RenderData->LODRenderData[LODIndex];
    if (!BuildPrimaryVertexCache(SkeletalMesh, LODIndex, LODData, OutCache.PrimaryVertexCache, OutErrorMessage))
    {
        return false;
    }

    ResolveIncludeRules(SkeletalMesh, IncludeRules, OutCache.ResolvedIncludeRules);
    DWC::Error::SetMessage(OutErrorMessage, TEXT(""));
    return true;
}
