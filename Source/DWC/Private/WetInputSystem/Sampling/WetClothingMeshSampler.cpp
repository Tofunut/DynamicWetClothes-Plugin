// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "WetInputSystem/Sampling/WetClothingMeshSampler.h"
#include "Engine/SkeletalMesh.h"
#include "Utility/DWCLog.h"

uint64 FWetClothingMeshSampler::GetAllocatedMemoryBytes() const
{
    return sizeof(*this) +
           CachedSkinnedPositions.GetAllocatedSize() +
           CachedSkinnedNormals.GetAllocatedSize() +
           CachedRefToLocalMatrices.GetAllocatedSize();
}

#include "Async/ParallelFor.h"
#include "CoreGlobals.h"
#include "HAL/ThreadSafeBool.h"
#include "Runtime/Engine/Classes/Components/SkeletalMeshComponent.h"

#include "RuntimeState/WetClothingRuntimeData.h"
#include "Runtime/Engine/Public/Rendering/SkeletalMeshLODRenderData.h"
#include "Runtime/Engine/Public/Rendering/SkinWeightVertexBuffer.h"
#include "Runtime/Engine/Public/Rendering/SkeletalMeshRenderData.h"
#include "Utility/DWCProfiling.h"

namespace
{
    bool GetWetMeshSamplerLODRenderData(const USkeletalMeshComponent* TargetSkeletalMesh, const int32 LODIndex, FSkeletalMeshLODRenderData*& OutLODData)
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

bool FWetClothingMeshSampler::IsSkinningMatrixCacheValid(
    const USkeletalMeshComponent* TargetSkeletalMesh,
    const uint64                  FrameNumber) const
{
    return bCachedSkinningMatricesValid &&
           CachedSkinningMatrixMesh.Get() == TargetSkeletalMesh &&
           CachedSkinningMatrixFrameNumber == FrameNumber &&
           CachedRefToLocalMatrices.Num() > 0;
}

bool FWetClothingMeshSampler::IsSkinnedPositionCacheValid(
    const USkeletalMeshComponent* TargetSkeletalMesh,
    const int32                   LODIndex,
    const uint64                  FrameNumber) const
{
    return bCachedSkinnedPositionsValid &&
           CachedSkinnedPositionMesh.Get() == TargetSkeletalMesh &&
           CachedSkinnedPositionLODIndex == LODIndex &&
           CachedSkinnedPositionFrameNumber == FrameNumber &&
           CachedSkinnedPositions.Num() > 0;
}

bool FWetClothingMeshSampler::IsSkinnedNormalCacheValid(
    const USkeletalMeshComponent* TargetSkeletalMesh,
    const int32                   LODIndex,
    const uint64                  FrameNumber) const
{
    return bCachedSkinnedNormalsValid &&
           CachedSkinnedNormalMesh.Get() == TargetSkeletalMesh &&
           CachedSkinnedNormalLODIndex == LODIndex &&
           CachedSkinnedNormalFrameNumber == FrameNumber &&
           CachedSkinnedNormals.Num() > 0;
}

void FWetClothingMeshSampler::InvalidateSkinnedPositionCache()
{
    bCachedSkinnedPositionsValid = false;
    CachedSkinnedPositionMesh = nullptr;
    CachedSkinnedPositionFrameNumber = 0;
    CachedSkinnedPositionLODIndex = INDEX_NONE;
}

void FWetClothingMeshSampler::InvalidateSkinnedNormalCache()
{
    bCachedSkinnedNormalsValid = false;
    CachedSkinnedNormalMesh = nullptr;
    CachedSkinnedNormalFrameNumber = 0;
    CachedSkinnedNormalLODIndex = INDEX_NONE;
}

void FWetClothingMeshSampler::ResetPositions()
{
    CachedSkinnedPositions.Reset();
    InvalidateSkinnedPositionCache();
}

void FWetClothingMeshSampler::ResetNormals()
{
    CachedSkinnedNormals.Reset();
    InvalidateSkinnedNormalCache();
}

bool FWetClothingMeshSampler::UpdateSkinningMatrices(USkeletalMeshComponent* TargetSkeletalMesh)
{
    DWC_PROFILE_SCOPE(DWC_MeshSampler_UpdateSkinningMatrices);

    if (!TargetSkeletalMesh)
    {
        return false;
    }

    const uint64 FrameNumber = GFrameCounter;
    if (IsSkinningMatrixCacheValid(TargetSkeletalMesh, FrameNumber))
    {
        return true;
    }

    const bool bCacheOwnerChanged =
        CachedSkinningMatrixMesh.Get() != TargetSkeletalMesh ||
        CachedSkinningMatrixFrameNumber != FrameNumber;
    if (bCacheOwnerChanged)
    {
        InvalidateSkinnedPositionCache();
        InvalidateSkinnedNormalCache();
    }

    TargetSkeletalMesh->CacheRefToLocalMatrices(CachedRefToLocalMatrices);
    bCachedSkinningMatricesValid = CachedRefToLocalMatrices.Num() > 0;
    CachedSkinningMatrixMesh = TargetSkeletalMesh;
    CachedSkinningMatrixFrameNumber = FrameNumber;
    return bCachedSkinningMatricesValid;
}

bool FWetClothingMeshSampler::UpdateSkinnedPositions(USkeletalMeshComponent* TargetSkeletalMesh, const int32 LODIndex)
{
    DWC_PROFILE_SCOPE(DWC_MeshSampler_UpdateSkinnedPositions);

    if (!TargetSkeletalMesh)
    {
        return false;
    }

    const uint64 FrameNumber = GFrameCounter;
    if (IsSkinnedPositionCacheValid(TargetSkeletalMesh, LODIndex, FrameNumber))
    {
        return true;
    }

    FSkeletalMeshLODRenderData* LODData = nullptr;
    if (!GetWetMeshSamplerLODRenderData(TargetSkeletalMesh, LODIndex, LODData))
    {
        return false;
    }

    const FSkinWeightVertexBuffer* SkinWeightBuffer =
        TargetSkeletalMesh->GetSkinWeightBuffer(0);

    if (!SkinWeightBuffer)
    {
        UE_LOG(LogDWC, Warning, TEXT("DynamicWetClothesComponent: SkinWeightBuffer is null."));
        return false;
    }

    if (!UpdateSkinningMatrices(TargetSkeletalMesh))
    {
        return false;
    }

    CachedSkinnedPositions.Reset();
    InvalidateSkinnedPositionCache();

    {
        DWC_PROFILE_SCOPE(DWC_MeshSampler_ComputeSkinnedPositions_Engine);

        USkeletalMeshComponent::ComputeSkinnedPositions(
            TargetSkeletalMesh,
            CachedSkinnedPositions,
            CachedRefToLocalMatrices,
            *LODData,
            *SkinWeightBuffer);
    }

    bCachedSkinnedPositionsValid = CachedSkinnedPositions.Num() > 0;
    CachedSkinnedPositionMesh = TargetSkeletalMesh;
    CachedSkinnedPositionLODIndex = LODIndex;
    CachedSkinnedPositionFrameNumber = FrameNumber;
    return bCachedSkinnedPositionsValid;
}

bool FWetClothingMeshSampler::UpdateSkinnedPositionsDirect(USkeletalMeshComponent* TargetSkeletalMesh, const int32 LODIndex)
{
    DWC_PROFILE_SCOPE(DWC_MeshSampler_UpdateSkinnedPositionsDirect);

    if (!TargetSkeletalMesh)
    {
        return false;
    }

    const uint64 FrameNumber = GFrameCounter;
    if (IsSkinnedPositionCacheValid(TargetSkeletalMesh, LODIndex, FrameNumber))
    {
        return true;
    }

    FSkeletalMeshLODRenderData* LODData = nullptr;
    if (!GetWetMeshSamplerLODRenderData(TargetSkeletalMesh, LODIndex, LODData))
    {
        return false;
    }

    const FSkinWeightVertexBuffer* SkinWeightBuffer =
        TargetSkeletalMesh->GetSkinWeightBuffer(0);

    if (!SkinWeightBuffer)
    {
        UE_LOG(LogDWC, Warning, TEXT("DynamicWetClothesComponent: SkinWeightBuffer is null."));
        return false;
    }

    if (!UpdateSkinningMatrices(TargetSkeletalMesh))
    {
        return false;
    }

    const int32 VertexCount = LODData->GetNumVertices();
    if (VertexCount <= 0)
    {
        CachedSkinnedPositions.Reset();
        InvalidateSkinnedPositionCache();
        return false;
    }

    CachedSkinnedPositions.Reset();
    InvalidateSkinnedPositionCache();
    CachedSkinnedPositions.SetNumZeroed(VertexCount);

    {
        DWC_PROFILE_SCOPE(DWC_MeshSampler_UpdateSkinnedPositionsDirect_VertexLoop);

        FThreadSafeBool bFailed = false;
        ParallelFor(VertexCount, [this, LODData, SkinWeightBuffer, &bFailed](const int32 VertexIndex)
                    {
            if (bFailed)
            {
                return;
            }

            FVector3f SkinnedPosition = FVector3f::ZeroVector;
            if (!ComputeSkinnedPosition(*LODData, *SkinWeightBuffer, VertexIndex, SkinnedPosition))
            {
                bFailed = true;
                return;
            }

            CachedSkinnedPositions[VertexIndex] = SkinnedPosition; });

        if (bFailed)
        {
            CachedSkinnedPositions.Reset();
            InvalidateSkinnedPositionCache();
            return false;
        }
    }

    bCachedSkinnedPositionsValid = true;
    CachedSkinnedPositionMesh = TargetSkeletalMesh;
    CachedSkinnedPositionLODIndex = LODIndex;
    CachedSkinnedPositionFrameNumber = FrameNumber;
    return true;
}

bool FWetClothingMeshSampler::UpdateSkinnedNormals(USkeletalMeshComponent* TargetSkeletalMesh, const int32 LODIndex)
{
    DWC_PROFILE_SCOPE(DWC_MeshSampler_UpdateSkinnedNormals);

    if (!TargetSkeletalMesh)
    {
        return false;
    }

    const uint64 FrameNumber = GFrameCounter;
    if (IsSkinnedNormalCacheValid(TargetSkeletalMesh, LODIndex, FrameNumber))
    {
        return true;
    }

    FSkeletalMeshLODRenderData* LODData = nullptr;
    if (!GetWetMeshSamplerLODRenderData(TargetSkeletalMesh, LODIndex, LODData))
    {
        return false;
    }

    const FSkinWeightVertexBuffer* SkinWeightBuffer =
        TargetSkeletalMesh->GetSkinWeightBuffer(0);

    if (!SkinWeightBuffer)
    {
        UE_LOG(LogDWC, Warning, TEXT("DynamicWetClothesComponent: SkinWeightBuffer is null."));
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
        InvalidateSkinnedNormalCache();
        return false;
    }

    CachedSkinnedNormals.Reset();
    InvalidateSkinnedNormalCache();
    CachedSkinnedNormals.SetNumZeroed(VertexCount);

    const uint32 MaxInfluences = SkinWeightBuffer->GetMaxBoneInfluences();
    const float  BoneWeightScale = SkinWeightBuffer->GetBoneWeightByteSize() == 1 ? 255.0f : 65535.0f;

    {
        DWC_PROFILE_SCOPE(DWC_MeshSampler_UpdateSkinnedNormals_VertexLoop);

        ParallelFor(VertexCount, [this, LODData, SkinWeightBuffer, VertexCount, MaxInfluences, BoneWeightScale](const int32 VertexIndex)
                    {
            int32 SectionIndex = INDEX_NONE;
            int32 SectionVertexIndex = INDEX_NONE;
            LODData->GetSectionFromVertexIndex(VertexIndex, SectionIndex, SectionVertexIndex);
            if (!LODData->RenderSections.IsValidIndex(SectionIndex) || SectionVertexIndex < 0)
            {
                return;
            }

            const FSkelMeshRenderSection& Section = LODData->RenderSections[SectionIndex];
            const int32 BufferVertexIndex = Section.GetVertexBufferIndex() + SectionVertexIndex;
            if (BufferVertexIndex < 0 || BufferVertexIndex >= VertexCount)
            {
                return;
            }

            const FVector3f LocalNormal =
                LODData->StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(BufferVertexIndex).GetSafeNormal();

            if (LocalNormal.IsNearlyZero())
            {
                return;
            }

            FVector3f SkinnedNormal = FVector3f::ZeroVector;

            for (uint32 InfluenceIndex = 0; InfluenceIndex < MaxInfluences; ++InfluenceIndex)
            {
                const uint16 BoneWeight = SkinWeightBuffer->GetBoneWeight(BufferVertexIndex, InfluenceIndex);

                if (BoneWeight == 0)
                {
                    continue;
                }

                const int32 BoneMapIndex = static_cast<int32>(SkinWeightBuffer->GetBoneIndex(BufferVertexIndex, InfluenceIndex));
                if (!Section.BoneMap.IsValidIndex(BoneMapIndex))
                {
                    continue;
                }

                const int32 BoneIndex = Section.BoneMap[BoneMapIndex];
                if (!CachedRefToLocalMatrices.IsValidIndex(BoneIndex))
                {
                    continue;
                }

                const float     Weight = static_cast<float>(BoneWeight) / BoneWeightScale;
                const FVector4f SkinnedNormal4f = CachedRefToLocalMatrices[BoneIndex].TransformVector(LocalNormal);

                SkinnedNormal += FVector3f(SkinnedNormal4f.X, SkinnedNormal4f.Y, SkinnedNormal4f.Z) * Weight;
            }

            CachedSkinnedNormals[VertexIndex] = SkinnedNormal.GetSafeNormal(); });
    }

    bCachedSkinnedNormalsValid = CachedSkinnedNormals.Num() == VertexCount;
    CachedSkinnedNormalMesh = TargetSkeletalMesh;
    CachedSkinnedNormalLODIndex = LODIndex;
    CachedSkinnedNormalFrameNumber = FrameNumber;
    return bCachedSkinnedNormalsValid;
}

void FWetClothingMeshSampler::CommitSkinnedCacheFromTask(
    USkeletalMeshComponent* TargetSkeletalMesh,
    const int32             LODIndex,
    const uint64            FrameNumber,
    TArray<FVector3f>&&     SkinnedPositions,
    TArray<FVector3f>&&     SkinnedNormals)
{
    DWC_PROFILE_SCOPE(DWC_MeshSampler_CommitSkinnedCacheFromTask);

    if (!TargetSkeletalMesh)
    {
        return;
    }

    if (SkinnedPositions.Num() > 0)
    {
        CachedSkinnedPositions = MoveTemp(SkinnedPositions);
        bCachedSkinnedPositionsValid = true;
        CachedSkinnedPositionMesh = TargetSkeletalMesh;
        CachedSkinnedPositionLODIndex = LODIndex;
        CachedSkinnedPositionFrameNumber = FrameNumber;
    }

    if (SkinnedNormals.Num() > 0)
    {
        CachedSkinnedNormals = MoveTemp(SkinnedNormals);
        bCachedSkinnedNormalsValid = true;
        CachedSkinnedNormalMesh = TargetSkeletalMesh;
        CachedSkinnedNormalLODIndex = LODIndex;
        CachedSkinnedNormalFrameNumber = FrameNumber;
    }
}

bool FWetClothingMeshSampler::ComputeSkinnedPosition(
    const FSkeletalMeshLODRenderData& LODData,
    const FSkinWeightVertexBuffer&    SkinWeightBuffer,
    const uint32                      VertexIndex,
    FVector3f&                        OutPosition) const
{
    OutPosition = FVector3f::ZeroVector;

    if (VertexIndex >= static_cast<uint32>(LODData.GetNumVertices()))
    {
        return false;
    }

    int32 SectionIndex = INDEX_NONE;
    int32 SectionVertexIndex = INDEX_NONE;
    LODData.GetSectionFromVertexIndex(VertexIndex, SectionIndex, SectionVertexIndex);
    if (!LODData.RenderSections.IsValidIndex(SectionIndex) || SectionVertexIndex < 0)
    {
        return false;
    }

    const FSkelMeshRenderSection& Section = LODData.RenderSections[SectionIndex];
    const uint32                  BufferVertexIndex = Section.GetVertexBufferIndex() + SectionVertexIndex;
    if (BufferVertexIndex < 0 || BufferVertexIndex >= LODData.GetNumVertices())
    {
        return false;
    }

    const FVector3f LocalPosition =
        LODData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(BufferVertexIndex);
    const uint32 MaxInfluences = SkinWeightBuffer.GetMaxBoneInfluences();
    const float  BoneWeightScale = SkinWeightBuffer.GetBoneWeightByteSize() == 1 ? 255.0f : 65535.0f;

    const VectorRegister4Float LocalPositionRegister =
        MakeVectorRegister(LocalPosition.X, LocalPosition.Y, LocalPosition.Z, 1.0f);
    VectorRegister4Float SkinnedPositionRegister = VectorZeroFloat();
    float                WeightSum = 0.0f;
    for (uint32 InfluenceIndex = 0; InfluenceIndex < MaxInfluences; ++InfluenceIndex)
    {
        const uint16 BoneWeight = SkinWeightBuffer.GetBoneWeight(BufferVertexIndex, InfluenceIndex);
        if (BoneWeight == 0)
        {
            continue;
        }

        const int32 BoneMapIndex = static_cast<int32>(SkinWeightBuffer.GetBoneIndex(BufferVertexIndex, InfluenceIndex));
        if (!Section.BoneMap.IsValidIndex(BoneMapIndex))
        {
            continue;
        }

        const int32 BoneIndex = Section.BoneMap[BoneMapIndex];
        if (!CachedRefToLocalMatrices.IsValidIndex(BoneIndex))
        {
            continue;
        }

        const float                Weight = static_cast<float>(BoneWeight) / BoneWeightScale;
        const VectorRegister4Float WeightRegister = MakeVectorRegister(Weight, Weight, Weight, Weight);
        const VectorRegister4Float SkinnedPositionForInfluence =
            VectorTransformVector(LocalPositionRegister, &CachedRefToLocalMatrices[BoneIndex]);

        SkinnedPositionRegister =
            VectorMultiplyAdd(SkinnedPositionForInfluence, WeightRegister, SkinnedPositionRegister);
        WeightSum += Weight;
    }

    if (WeightSum <= KINDA_SMALL_NUMBER)
    {
        return false;
    }

    if (!FMath::IsNearlyEqual(WeightSum, 1.0f))
    {
        const float InvWeightSum = 1.0f / WeightSum;
        SkinnedPositionRegister = VectorMultiply(
            SkinnedPositionRegister,
            MakeVectorRegister(InvWeightSum, InvWeightSum, InvWeightSum, InvWeightSum));
    }

    VectorStoreFloat3(SkinnedPositionRegister, &OutPosition.X);
    return true;
}

bool FWetClothingMeshSampler::ComputeSkinnedNormal(
    const FSkeletalMeshLODRenderData& LODData,
    const FSkinWeightVertexBuffer&    SkinWeightBuffer,
    const uint32                      VertexIndex,
    FVector3f&                        OutNormal) const
{
    OutNormal = FVector3f::ZeroVector;

    if (VertexIndex >= static_cast<uint32>(LODData.GetNumVertices()))
    {
        return false;
    }

    int32 SectionIndex = INDEX_NONE;
    int32 SectionVertexIndex = INDEX_NONE;
    LODData.GetSectionFromVertexIndex(VertexIndex, SectionIndex, SectionVertexIndex);
    if (!LODData.RenderSections.IsValidIndex(SectionIndex) || SectionVertexIndex < 0)
    {
        return false;
    }

    const FSkelMeshRenderSection& Section = LODData.RenderSections[SectionIndex];
    const uint32                  BufferVertexIndex = Section.GetVertexBufferIndex() + SectionVertexIndex;
    if (BufferVertexIndex < 0 || BufferVertexIndex >= LODData.GetNumVertices())
    {
        return false;
    }

    const FVector3f LocalNormal = LODData.StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(BufferVertexIndex).GetSafeNormal();
    if (LocalNormal.IsNearlyZero())
    {
        return false;
    }

    const uint32 MaxInfluences = SkinWeightBuffer.GetMaxBoneInfluences();
    const float  BoneWeightScale = SkinWeightBuffer.GetBoneWeightByteSize() == 1 ? 255.0f : 65535.0f;

    for (uint32 InfluenceIndex = 0; InfluenceIndex < MaxInfluences; ++InfluenceIndex)
    {
        const uint16 BoneWeight = SkinWeightBuffer.GetBoneWeight(BufferVertexIndex, InfluenceIndex);
        if (BoneWeight == 0)
        {
            continue;
        }

        const int32 BoneMapIndex = static_cast<int32>(SkinWeightBuffer.GetBoneIndex(BufferVertexIndex, InfluenceIndex));
        if (!Section.BoneMap.IsValidIndex(BoneMapIndex))
        {
            continue;
        }

        const int32 BoneIndex = Section.BoneMap[BoneMapIndex];
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
