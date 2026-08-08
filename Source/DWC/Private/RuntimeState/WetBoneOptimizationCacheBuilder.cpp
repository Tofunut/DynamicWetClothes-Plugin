// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "RuntimeState/WetBoneOptimizationCacheBuilder.h"

#include "Engine/SkeletalMesh.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "ReferenceSkeleton.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkinWeightVertexBuffer.h"
#include "Utility/DWCError.h"

namespace WetClothingSkeletalMeshCacheBuilderInternal
{
    static bool AddValidUniqueBoneIndex(
        const int32    BoneIndex,
        const int32    BoneCount,
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

    static bool BoneHasCollisionShape(
        const UPhysicsAsset*      PhysicsAsset,
        const FReferenceSkeleton& RefSkeleton,
        const int32               BoneIndex)
    {
        if (PhysicsAsset == nullptr || BoneIndex < 0 || BoneIndex >= RefSkeleton.GetNum())
        {
            return false;
        }

        const int32 BodyIndex = PhysicsAsset->FindBodyIndex(RefSkeleton.GetBoneName(BoneIndex));
        if (!PhysicsAsset->SkeletalBodySetups.IsValidIndex(BodyIndex))
        {
            return false;
        }

        const USkeletalBodySetup* BodySetup = PhysicsAsset->SkeletalBodySetups[BodyIndex];
        return BodySetup != nullptr && BodySetup->AggGeom.GetElementCount() > 0;
    }

    static void CollectCollisionlessParentsRecursive(
        const int32               ChildBoneIndex,
        const UPhysicsAsset*      PhysicsAsset,
        const FReferenceSkeleton& RefSkeleton,
        TSet<int32>&              UniqueBoneIndices,
        TArray<int32>&            OutIncludedBoneIndices)
    {
        const int32 ParentBoneIndex = RefSkeleton.GetParentIndex(ChildBoneIndex);
        if (ParentBoneIndex == INDEX_NONE ||
            BoneHasCollisionShape(PhysicsAsset, RefSkeleton, ParentBoneIndex))
        {
            return;
        }

        AddValidUniqueBoneIndex(
            ParentBoneIndex,
            RefSkeleton.GetNum(),
            UniqueBoneIndices,
            OutIncludedBoneIndices);

        CollectCollisionlessParentsRecursive(
            ParentBoneIndex,
            PhysicsAsset,
            RefSkeleton,
            UniqueBoneIndices,
            OutIncludedBoneIndices);
    }

    static void CollectCollisionlessChildrenDepthFirst(
        const int32                  ParentBoneIndex,
        const UPhysicsAsset*         PhysicsAsset,
        const FReferenceSkeleton&    RefSkeleton,
        const TArray<TArray<int32>>& ChildBoneIndices,
        TSet<int32>&                 UniqueBoneIndices,
        TArray<int32>&               OutIncludedBoneIndices)
    {
        if (!ChildBoneIndices.IsValidIndex(ParentBoneIndex))
        {
            return;
        }

        for (const int32 ChildBoneIndex : ChildBoneIndices[ParentBoneIndex])
        {
            // A bone with a collision shape can be hit directly. Do not include it
            // under the current target and do not cross it during this DFS branch.
            if (BoneHasCollisionShape(PhysicsAsset, RefSkeleton, ChildBoneIndex))
            {
                continue;
            }

            AddValidUniqueBoneIndex(
                ChildBoneIndex,
                RefSkeleton.GetNum(),
                UniqueBoneIndices,
                OutIncludedBoneIndices);

            CollectCollisionlessChildrenDepthFirst(
                ChildBoneIndex,
                PhysicsAsset,
                RefSkeleton,
                ChildBoneIndices,
                UniqueBoneIndices,
                OutIncludedBoneIndices);
        }
    }

    static void BuildAutomaticIncludeRules(
        const USkeletalMesh*                 SkeletalMesh,
        TArray<FWetResolvedBoneIncludeRule>& OutResolvedIncludeRules)
    {
        OutResolvedIncludeRules.Reset();

        if (SkeletalMesh == nullptr)
        {
            return;
        }

        const FReferenceSkeleton& RefSkeleton = SkeletalMesh->GetRefSkeleton();
        const int32               BoneCount = RefSkeleton.GetNum();
        const UPhysicsAsset*      PhysicsAsset = SkeletalMesh->GetPhysicsAsset();

        if (BoneCount <= 0 || PhysicsAsset == nullptr)
        {
            return;
        }

        TArray<TArray<int32>> ChildBoneIndices;
        ChildBoneIndices.SetNum(BoneCount);
        for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
        {
            const int32 ParentBoneIndex = RefSkeleton.GetParentIndex(BoneIndex);
            if (ChildBoneIndices.IsValidIndex(ParentBoneIndex))
            {
                ChildBoneIndices[ParentBoneIndex].Add(BoneIndex);
            }
        }

        for (int32 TargetBoneIndex = 0; TargetBoneIndex < BoneCount; ++TargetBoneIndex)
        {
            // Runtime HitResult::BoneName can directly identify only collision bones.
            if (!BoneHasCollisionShape(PhysicsAsset, RefSkeleton, TargetBoneIndex))
            {
                continue;
            }

            FWetResolvedBoneIncludeRule ResolvedRule;
            ResolvedRule.TargetBoneIndex = TargetBoneIndex;

            TSet<int32> UniqueBoneIndices;
            AddValidUniqueBoneIndex(
                TargetBoneIndex,
                BoneCount,
                UniqueBoneIndices,
                ResolvedRule.IncludedBoneIndices);

            // The parent side is a single branch, but it is still resolved
            // recursively so both directions share the same precompute model.
            CollectCollisionlessParentsRecursive(
                TargetBoneIndex,
                PhysicsAsset,
                RefSkeleton,
                UniqueBoneIndices,
                ResolvedRule.IncludedBoneIndices);

            // Child branches are expanded recursively with DFS and flattened into
            // the same runtime include array.
            CollectCollisionlessChildrenDepthFirst(
                TargetBoneIndex,
                PhysicsAsset,
                RefSkeleton,
                ChildBoneIndices,
                UniqueBoneIndices,
                ResolvedRule.IncludedBoneIndices);

            OutResolvedIncludeRules.Add(MoveTemp(ResolvedRule));
        }
    }

    static bool BuildPrimaryVertexCache(
        const USkeletalMesh*              SkeletalMesh,
        const int32                       LODIndex,
        const FSkeletalMeshLODRenderData& LODData,
        FWetBonePrimaryVertexCache&       OutCache,
        FString*                          OutErrorMessage)
    {
        const FSkinWeightVertexBuffer* SkinWeightBuffer = LODData.GetSkinWeightVertexBuffer();
        if (SkinWeightBuffer == nullptr)
        {
            DWC::Error::SetMessage(OutErrorMessage, TEXT("DWC Data UV skin weight buffer is unavailable."));
            return false;
        }

        const int32 BoneCount = SkeletalMesh->GetRefSkeleton().GetNum();
        const int32 VertexCount = static_cast<int32>(LODData.GetNumVertices());
        if (BoneCount <= 0)
        {
            DWC::Error::SetMessage(OutErrorMessage, TEXT("DWC Data UV has no reference skeleton bones."));
            return false;
        }

        if (VertexCount <= 0)
        {
            DWC::Error::SetMessage(OutErrorMessage, TEXT("DWC Data UV LOD has no render vertices."));
            return false;
        }

        if (SkinWeightBuffer->GetMaxBoneInfluences() == 0)
        {
            DWC::Error::SetMessage(OutErrorMessage, TEXT("DWC Data UV skin weight buffer has no bone influences."));
            return false;
        }

        TArray<int32> PrimaryBoneByVertex;
        TArray<int32> VertexCountsByBone;
        PrimaryBoneByVertex.Init(INDEX_NONE, VertexCount);
        VertexCountsByBone.Init(0, BoneCount);

        constexpr uint32 PrimaryInfluenceIndex = 0;

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
                if (SkinWeightBuffer->GetBoneWeight(VertexIndex, PrimaryInfluenceIndex) == 0)
                {
                    continue;
                }

                const int32 SectionBoneIndex = static_cast<int32>(
                    SkinWeightBuffer->GetBoneIndex(VertexIndex, PrimaryInfluenceIndex));
                if (!Section.BoneMap.IsValidIndex(SectionBoneIndex))
                {
                    continue;
                }

                const int32 PrimaryBoneIndex = static_cast<int32>(Section.BoneMap[SectionBoneIndex]);
                if (PrimaryBoneIndex < 0 || PrimaryBoneIndex >= BoneCount)
                {
                    continue;
                }

                PrimaryBoneByVertex[VertexIndex] = PrimaryBoneIndex;
                ++VertexCountsByBone[PrimaryBoneIndex];
            }
        }

        OutCache.SourceMesh = const_cast<USkeletalMesh*>(SkeletalMesh);
        OutCache.LODIndex = LODIndex;
        OutCache.BoneCount = BoneCount;
        OutCache.VertexCount = VertexCount;
        OutCache.BoneStartOffsets.SetNumZeroed(BoneCount + 1);

        for (int32 BoneIndex = 0; BoneIndex < BoneCount; ++BoneIndex)
        {
            OutCache.BoneStartOffsets[BoneIndex + 1] =
                OutCache.BoneStartOffsets[BoneIndex] + VertexCountsByBone[BoneIndex];
        }

        const int32 AssignedVertexCount = OutCache.BoneStartOffsets.Last();
        OutCache.FlatVertexIndices.SetNumUninitialized(AssignedVertexCount);

        TArray<int32> WriteOffsets = OutCache.BoneStartOffsets;
        for (int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
        {
            const int32 PrimaryBoneIndex = PrimaryBoneByVertex[VertexIndex];
            if (PrimaryBoneIndex == INDEX_NONE)
            {
                continue;
            }

            OutCache.FlatVertexIndices[WriteOffsets[PrimaryBoneIndex]++] = VertexIndex;
        }

        return true;
    }
} // namespace WetClothingSkeletalMeshCacheBuilderInternal

bool FWetBoneOptimizationCacheBuilder::Build(
    const USkeletalMesh*       SkeletalMesh,
    const int32                LODIndex,
    FWetBoneOptimizationCache& OutCache,
    FString*                   OutErrorMessage)
{
    using namespace WetClothingSkeletalMeshCacheBuilderInternal;

    OutCache = FWetBoneOptimizationCache();

    if (SkeletalMesh == nullptr)
    {
        DWC::Error::SetMessage(OutErrorMessage, TEXT("No DWC Data UV is assigned."));
        return false;
    }

    const FSkeletalMeshRenderData* RenderData = SkeletalMesh->GetResourceForRendering();
    if (RenderData == nullptr)
    {
        DWC::Error::SetMessage(OutErrorMessage, TEXT("DWC Data UV render data is unavailable."));
        return false;
    }

    if (!RenderData->LODRenderData.IsValidIndex(LODIndex))
    {
        DWC::Error::SetMessage(OutErrorMessage, TEXT("Requested DWC Data UV LOD render data is unavailable."));
        return false;
    }

    const FSkeletalMeshLODRenderData& LODData = RenderData->LODRenderData[LODIndex];
    if (!BuildPrimaryVertexCache(SkeletalMesh, LODIndex, LODData, OutCache.PrimaryVertexCache, OutErrorMessage))
    {
        return false;
    }

    BuildAutomaticIncludeRules(SkeletalMesh, OutCache.ResolvedIncludeRules);
    DWC::Error::SetMessage(OutErrorMessage, TEXT(""));
    return true;
}
