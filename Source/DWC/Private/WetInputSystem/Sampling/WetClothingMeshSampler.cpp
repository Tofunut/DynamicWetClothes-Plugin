#include "WetInputSystem/Sampling/WetClothingMeshSampler.h"

#include "Runtime/Engine/Classes/Components/SkeletalMeshComponent.h"

#include "RuntimeData/WetClothingRuntimeData.h"
#include "Runtime/Engine/Public/Rendering/SkeletalMeshLODRenderData.h"
#include "Runtime/Engine/Public/Rendering/SkinWeightVertexBuffer.h"
#include "Runtime/Engine/Public/Rendering/SkeletalMeshRenderData.h"

namespace
{
    bool GetLODRenderData(const USkeletalMeshComponent* TargetSkeletalMesh, const int32 LODIndex, FSkeletalMeshLODRenderData*& OutLODData)
    {
        OutLODData = nullptr;
        if (!TargetSkeletalMesh)
        {
            return false;
        }

        const USkeletalMesh* SkeletalMesh = TargetSkeletalMesh->GetSkeletalMeshAsset();
        if (!SkeletalMesh)
        {
            return false;
        }

        FSkeletalMeshRenderData* RenderData = SkeletalMesh->GetResourceForRendering();
        if (!RenderData || !RenderData->LODRenderData.IsValidIndex(LODIndex))
        {
            return false;
        }

        OutLODData = &RenderData->LODRenderData[LODIndex];
        return true;
    }
} // namespace

void FWetClothingMeshSampler::ResetPositions()
{
    CachedSkinnedPositions.Reset();
}

void FWetClothingMeshSampler::ResetNormals()
{
    CachedSkinnedNormals.Reset();
}

bool FWetClothingMeshSampler::UpdateSkinningMatrices(USkeletalMeshComponent* TargetSkeletalMesh)
{
    if (!TargetSkeletalMesh)
    {
        return false;
    }

    TargetSkeletalMesh->CacheRefToLocalMatrices(CachedRefToLocalMatrices);
    return CachedRefToLocalMatrices.Num() > 0;
}

bool FWetClothingMeshSampler::UpdateSkinnedPositions(USkeletalMeshComponent* TargetSkeletalMesh, const int32 LODIndex)
{
    if (!TargetSkeletalMesh)
    {
        return false;
    }

    FSkeletalMeshLODRenderData* LODData = nullptr;
    if (!GetLODRenderData(TargetSkeletalMesh, LODIndex, LODData))
    {
        return false;
    }

    const FSkinWeightVertexBuffer* SkinWeightBuffer =
        TargetSkeletalMesh->GetSkinWeightBuffer(0);

    if (!SkinWeightBuffer)
    {
        UE_LOG(LogTemp, Warning, TEXT("DynamicWetClothesComponent: SkinWeightBuffer is null."));
        return false;
    }

    if (!UpdateSkinningMatrices(TargetSkeletalMesh))
    {
        return false;
    }

    CachedSkinnedPositions.Reset();

    USkeletalMeshComponent::ComputeSkinnedPositions(
        TargetSkeletalMesh,
        CachedSkinnedPositions,
        CachedRefToLocalMatrices,
        *LODData,
        *SkinWeightBuffer);

    return CachedSkinnedPositions.Num() > 0;
}

bool FWetClothingMeshSampler::UpdateSkinnedNormals(USkeletalMeshComponent* TargetSkeletalMesh, const int32 LODIndex)
{
    if (!TargetSkeletalMesh)
    {
        return false;
    }

    FSkeletalMeshLODRenderData* LODData = nullptr;
    if (!GetLODRenderData(TargetSkeletalMesh, LODIndex, LODData))
    {
        return false;
    }

    const FSkinWeightVertexBuffer* SkinWeightBuffer =
        TargetSkeletalMesh->GetSkinWeightBuffer(0);

    if (!SkinWeightBuffer)
    {
        UE_LOG(LogTemp, Warning, TEXT("DynamicWetClothesComponent: SkinWeightBuffer is null."));
        return false;
    }

    if (!UpdateSkinningMatrices(TargetSkeletalMesh))
    {
        return false;
    }

    const int32 VertexCount = LODData->GetNumVertices();
    if (VertexCount <= 0)
    {
        CachedSkinnedNormals.Reset();
        return false;
    }

    CachedSkinnedNormals.Reset();
    CachedSkinnedNormals.SetNumZeroed(VertexCount);

    const uint32 MaxInfluences = SkinWeightBuffer->GetMaxBoneInfluences();
    const float  BoneWeightScale = SkinWeightBuffer->GetBoneWeightByteSize() == 1 ? 255.0f : 65535.0f;

    for (int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
    {
        const FVector3f LocalNormal =
            LODData->StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(VertexIndex).GetSafeNormal();

        if (LocalNormal.IsNearlyZero())
        {
            continue;
        }

        FVector3f SkinnedNormal = FVector3f::ZeroVector;

        for (uint32 InfluenceIndex = 0; InfluenceIndex < MaxInfluences; ++InfluenceIndex)
        {
            const uint16 BoneWeight = SkinWeightBuffer->GetBoneWeight(VertexIndex, InfluenceIndex);

            if (BoneWeight == 0)
            {
                continue;
            }

            const uint32 BoneIndex = SkinWeightBuffer->GetBoneIndex(VertexIndex, InfluenceIndex);

            if (!CachedRefToLocalMatrices.IsValidIndex(BoneIndex))
            {
                continue;
            }

            const float     Weight = static_cast<float>(BoneWeight) / BoneWeightScale;
            const FVector4f SkinnedNormal4f = CachedRefToLocalMatrices[BoneIndex].TransformVector(LocalNormal);

            SkinnedNormal += FVector3f(SkinnedNormal4f.X, SkinnedNormal4f.Y, SkinnedNormal4f.Z) * Weight;
        }

        CachedSkinnedNormals[VertexIndex] = SkinnedNormal.GetSafeNormal();
    }

    return CachedSkinnedNormals.Num() == VertexCount;
}

bool FWetClothingMeshSampler::ComputeSkinnedPosition(
    const FSkeletalMeshLODRenderData& LODData,
    const FSkinWeightVertexBuffer&    SkinWeightBuffer,
    const uint32                      VertexIndex,
    FVector3f&                        OutPosition) const
{
    OutPosition = FVector3f::ZeroVector;

    if (VertexIndex < 0 || VertexIndex >= LODData.GetNumVertices())
    {
        return false;
    }

    const FVector3f LocalPosition =
        LODData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(VertexIndex);
    const uint32 MaxInfluences = SkinWeightBuffer.GetMaxBoneInfluences();
    const float  BoneWeightScale = SkinWeightBuffer.GetBoneWeightByteSize() == 1 ? 255.0f : 65535.0f;

    float WeightSum = 0.0f;
    for (uint32 InfluenceIndex = 0; InfluenceIndex < MaxInfluences; ++InfluenceIndex)
    {
        const uint16 BoneWeight = SkinWeightBuffer.GetBoneWeight(VertexIndex, InfluenceIndex);
        if (BoneWeight == 0)
        {
            continue;
        }

        const uint32 BoneIndex = SkinWeightBuffer.GetBoneIndex(VertexIndex, InfluenceIndex);
        if (!CachedRefToLocalMatrices.IsValidIndex(BoneIndex))
        {
            continue;
        }

        const float     Weight = static_cast<float>(BoneWeight) / BoneWeightScale;
        const FVector4f SkinnedPosition4f = CachedRefToLocalMatrices[BoneIndex].TransformFVector4(
            FVector4f(LocalPosition.X, LocalPosition.Y, LocalPosition.Z, 1.0f));

        OutPosition += FVector3f(SkinnedPosition4f.X, SkinnedPosition4f.Y, SkinnedPosition4f.Z) * Weight;
        WeightSum += Weight;
    }

    if (WeightSum <= KINDA_SMALL_NUMBER)
    {
        return false;
    }

    if (!FMath::IsNearlyEqual(WeightSum, 1.0f))
    {
        OutPosition /= WeightSum;
    }

    return true;
}

bool FWetClothingMeshSampler::ComputeSkinnedNormal(
    const FSkeletalMeshLODRenderData& LODData,
    const FSkinWeightVertexBuffer&    SkinWeightBuffer,
    const uint32                      VertexIndex,
    FVector3f&                        OutNormal) const
{
    OutNormal = FVector3f::ZeroVector;

    if (VertexIndex < 0 || VertexIndex >= LODData.GetNumVertices())
    {
        return false;
    }

    const FVector3f LocalNormal = LODData.StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(VertexIndex).GetSafeNormal();
    if (LocalNormal.IsNearlyZero())
    {
        return false;
    }

    const uint32 MaxInfluences = SkinWeightBuffer.GetMaxBoneInfluences();
    const float  BoneWeightScale = SkinWeightBuffer.GetBoneWeightByteSize() == 1 ? 255.0f : 65535.0f;

    for (uint32 InfluenceIndex = 0; InfluenceIndex < MaxInfluences; ++InfluenceIndex)
    {
        const uint16 BoneWeight = SkinWeightBuffer.GetBoneWeight(VertexIndex, InfluenceIndex);
        if (BoneWeight == 0)
        {
            continue;
        }

        const uint32 BoneIndex = SkinWeightBuffer.GetBoneIndex(VertexIndex, InfluenceIndex);
        if (!CachedRefToLocalMatrices.IsValidIndex(BoneIndex))
        {
            continue;
        }

        const float     Weight = static_cast<float>(BoneWeight) / BoneWeightScale;
        const FVector4f SkinnedNormal4f = CachedRefToLocalMatrices[BoneIndex].TransformVector(LocalNormal);

        OutNormal += FVector3f(SkinnedNormal4f.X, SkinnedNormal4f.Y, SkinnedNormal4f.Z) * Weight;
    }

    OutNormal = OutNormal.GetSafeNormal();
    return !OutNormal.IsNearlyZero();
}
