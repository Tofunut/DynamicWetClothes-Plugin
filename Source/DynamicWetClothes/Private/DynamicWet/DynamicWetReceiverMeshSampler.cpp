#include "DynamicWet/DynamicWetReceiverMeshSampler.h"

#include "Runtime/Engine/Classes/Components/SkeletalMeshComponent.h"
#include "DynamicWet/DynamicWetReceiverContext.h"
#include "DynamicWet/DynamicWetReceiverRuntimeData.h"
#include "Runtime/Engine/Public/Rendering/SkeletalMeshLODRenderData.h"
#include "Runtime/Engine/Public/Rendering/SkinWeightVertexBuffer.h"

void FDynamicWetReceiverMeshSampler::ResetPositions()
{
    CachedSkinnedPositions.Reset();
}

void FDynamicWetReceiverMeshSampler::ResetNormals()
{
    CachedSkinnedNormals.Reset();
}

bool FDynamicWetReceiverMeshSampler::UpdateSkinningMatrices(FDynamicWetReceiverContext& Receiver)
{
    if (!Receiver.TargetSkeletalMesh)
    {
        return false;
    }

    Receiver.TargetSkeletalMesh->CacheRefToLocalMatrices(CachedRefToLocalMatrices);
    return CachedRefToLocalMatrices.Num() > 0;
}

bool FDynamicWetReceiverMeshSampler::UpdateSkinnedPositions(FDynamicWetReceiverContext& Receiver)
{
    if (!Receiver.TargetSkeletalMesh)
    {
        return false;
    }

    FSkeletalMeshLODRenderData* LODData = nullptr;
    if (!Receiver.RuntimeDataBuilder.GetLODRenderData(Receiver, 0, LODData))
    {
        return false;
    }

    const FSkinWeightVertexBuffer* SkinWeightBuffer =
        Receiver.TargetSkeletalMesh->GetSkinWeightBuffer(0);

    if (!SkinWeightBuffer)
    {
        UE_LOG(LogTemp, Warning, TEXT("DynamicWetReceiverComponent: SkinWeightBuffer is null."));
        return false;
    }

    if (!UpdateSkinningMatrices(Receiver))
    {
        return false;
    }

    CachedSkinnedPositions.Reset();

    USkeletalMeshComponent::ComputeSkinnedPositions(
        Receiver.TargetSkeletalMesh,
        CachedSkinnedPositions,
        CachedRefToLocalMatrices,
        *LODData,
        *SkinWeightBuffer);

    return CachedSkinnedPositions.Num() > 0;
}

bool FDynamicWetReceiverMeshSampler::UpdateSkinnedNormals(FDynamicWetReceiverContext& Receiver)
{
    if (!Receiver.TargetSkeletalMesh)
    {
        return false;
    }

    FSkeletalMeshLODRenderData* LODData = nullptr;
    if (!Receiver.RuntimeDataBuilder.GetLODRenderData(Receiver, 0, LODData))
    {
        return false;
    }

    const FSkinWeightVertexBuffer* SkinWeightBuffer =
        Receiver.TargetSkeletalMesh->GetSkinWeightBuffer(0);

    if (!SkinWeightBuffer)
    {
        UE_LOG(LogTemp, Warning, TEXT("DynamicWetReceiverComponent: SkinWeightBuffer is null."));
        return false;
    }

    if (!UpdateSkinningMatrices(Receiver))
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
    const float BoneWeightScale = SkinWeightBuffer->GetBoneWeightByteSize() == 1 ? 255.0f : 65535.0f;

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

            const float Weight = static_cast<float>(BoneWeight) / BoneWeightScale;
            const FVector4f SkinnedNormal4f = CachedRefToLocalMatrices[BoneIndex].TransformVector(LocalNormal);

            SkinnedNormal += FVector3f(SkinnedNormal4f.X, SkinnedNormal4f.Y, SkinnedNormal4f.Z) * Weight;
        }

        CachedSkinnedNormals[VertexIndex] = SkinnedNormal.GetSafeNormal();
    }

    return CachedSkinnedNormals.Num() == VertexCount;
}

bool FDynamicWetReceiverMeshSampler::ComputeSkinnedPosition(
    const FSkeletalMeshLODRenderData& LODData,
    const FSkinWeightVertexBuffer& SkinWeightBuffer,
    const uint32 VertexIndex,
    FVector3f& OutPosition) const
{
    OutPosition = FVector3f::ZeroVector;

    if (VertexIndex < 0 || VertexIndex >= LODData.GetNumVertices())
    {
        return false;
    }

    const FVector3f LocalPosition =
        LODData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(VertexIndex);
    const uint32 MaxInfluences = SkinWeightBuffer.GetMaxBoneInfluences();
    const float BoneWeightScale = SkinWeightBuffer.GetBoneWeightByteSize() == 1 ? 255.0f : 65535.0f;

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

        const float Weight = static_cast<float>(BoneWeight) / BoneWeightScale;
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

bool FDynamicWetReceiverMeshSampler::ComputeSkinnedNormal(
    const FSkeletalMeshLODRenderData& LODData,
    const FSkinWeightVertexBuffer& SkinWeightBuffer,
    const uint32 VertexIndex,
    FVector3f& OutNormal) const
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
    const float BoneWeightScale = SkinWeightBuffer.GetBoneWeightByteSize() == 1 ? 255.0f : 65535.0f;

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

        const float Weight = static_cast<float>(BoneWeight) / BoneWeightScale;
        const FVector4f SkinnedNormal4f = CachedRefToLocalMatrices[BoneIndex].TransformVector(LocalNormal);

        OutNormal += FVector3f(SkinnedNormal4f.X, SkinnedNormal4f.Y, SkinnedNormal4f.Z) * Weight;
    }

    OutNormal = OutNormal.GetSafeNormal();
    return !OutNormal.IsNearlyZero();
}
