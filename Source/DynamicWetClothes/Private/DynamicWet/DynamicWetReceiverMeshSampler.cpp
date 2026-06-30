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

    Receiver.TargetSkeletalMesh->CacheRefToLocalMatrices(
        CachedRefToLocalMatrices);

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

    Receiver.TargetSkeletalMesh->CacheRefToLocalMatrices(
        CachedRefToLocalMatrices);

    const int32 VertexCount = LODData->GetNumVertices();
    if (VertexCount <= 0)
    {
        CachedSkinnedNormals.Reset();
        return false;
    }

    CachedSkinnedNormals.Reset();
    CachedSkinnedNormals.SetNumZeroed(VertexCount);

    const uint32 MaxInfluences = SkinWeightBuffer->GetMaxBoneInfluences();
    const float BoneWeightScale =
        SkinWeightBuffer->GetBoneWeightByteSize() == 1 ? 255.0f : 65535.0f;

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
            const uint16 BoneWeight =
                SkinWeightBuffer->GetBoneWeight(VertexIndex, InfluenceIndex);

            if (BoneWeight == 0)
            {
                continue;
            }

            const uint32 BoneIndex =
                SkinWeightBuffer->GetBoneIndex(VertexIndex, InfluenceIndex);

            if (!CachedRefToLocalMatrices.IsValidIndex(BoneIndex))
            {
                continue;
            }

            const float Weight = static_cast<float>(BoneWeight) / BoneWeightScale;
            const FVector4f SkinnedNormal4f =
                CachedRefToLocalMatrices[BoneIndex].TransformVector(LocalNormal);

            SkinnedNormal += FVector3f(
                                  SkinnedNormal4f.X,
                                  SkinnedNormal4f.Y,
                                  SkinnedNormal4f.Z) *
                              Weight;
        }

        CachedSkinnedNormals[VertexIndex] = SkinnedNormal.GetSafeNormal();
    }

    return CachedSkinnedNormals.Num() == VertexCount;
}
