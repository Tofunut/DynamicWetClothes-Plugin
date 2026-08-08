// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "Async/DWCSkinningTasks.h"

#include "Async/ParallelFor.h"
#include "Components/DynamicWetClothesComponent.h"
#include "CoreGlobals.h"
#include "DataAssets/WetClothingAsset.h"
#include "Engine/SkeletalMesh.h"
#include "Runtime/Engine/Classes/Components/SkeletalMeshComponent.h"
#include "Runtime/Engine/Public/Rendering/SkeletalMeshLODRenderData.h"
#include "Runtime/Engine/Public/Rendering/SkeletalMeshRenderData.h"
#include "Runtime/Engine/Public/Rendering/SkinWeightVertexBuffer.h"
#include "Utility/DWCProfiling.h"

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

FDWCCpuSkinningTask::FDWCCpuSkinningTask(
    TWeakObjectPtr<UDynamicWetClothesComponent> InOwner,
    FDWCSkinningTaskSnapshot&&                  InSnapshot)
    : Owner(InOwner), Snapshot(MoveTemp(InSnapshot))
{
}

void FDWCCpuSkinningTask::ExecuteWorker()
{
    DWC_PROFILE_SCOPE(DWC_CpuSkinningTask_ExecuteWorker);

    SetStatus(EDWCTaskStatus::Running);

    Result.ReceiverId = Snapshot.ReceiverId;
    Result.FrameNumber = Snapshot.FrameNumber;
    Result.SkinnedPositions.Reset();
    Result.SkinnedNormals.Reset();

    const int32 VertexCount = Snapshot.StaticData.IsValid() ? Snapshot.StaticData->Geometry.VertexCount : 0;
    if (VertexCount <= 0 ||
        !Snapshot.StaticData.IsValid() ||
        !Snapshot.StaticData->IsValid())
    {
        SetStatus(EDWCTaskStatus::Failed);
        return;
    }

    if (Snapshot.bComputePositions)
    {
        Result.SkinnedPositions.SetNumZeroed(VertexCount);
    }

    if (Snapshot.bComputeNormals)
    {
        Result.SkinnedNormals.SetNumZeroed(VertexCount);
    }

    ParallelFor(VertexCount, [this](const int32 VertexIndex)
                {
        const FDWCSkinningStaticData& StaticData = *Snapshot.StaticData;
        const FDWCSkinningVertexSnapshot& Vertex = StaticData.Vertices[VertexIndex];
        if (Vertex.InfluenceOffset < 0 || Vertex.InfluenceCount <= 0)
        {
            return;
        }

        FVector3f SkinnedPosition = FVector3f::ZeroVector;
        FVector3f SkinnedNormal = FVector3f::ZeroVector;
        float WeightSum = 0.0f;

        for (int32 InfluenceIndex = 0; InfluenceIndex < Vertex.InfluenceCount; ++InfluenceIndex)
        {
            const int32 InfluenceArrayIndex = Vertex.InfluenceOffset + InfluenceIndex;
            if (!StaticData.Influences.IsValidIndex(InfluenceArrayIndex))
            {
                continue;
            }

            const FDWCSkinningInfluenceSnapshot& Influence = StaticData.Influences[InfluenceArrayIndex];
            if (!Snapshot.RefToLocalMatrices.IsValidIndex(Influence.BoneIndex) || Influence.Weight <= 0.0f)
            {
                continue;
            }

            const FMatrix44f& BoneMatrix = Snapshot.RefToLocalMatrices[Influence.BoneIndex];
            if (Snapshot.bComputePositions)
            {
                const FVector4f Position4f = BoneMatrix.TransformPosition(StaticData.Geometry.LocalPositions[VertexIndex]);
                SkinnedPosition += FVector3f(Position4f.X, Position4f.Y, Position4f.Z) * Influence.Weight;
            }

            if (Snapshot.bComputeNormals)
            {
                const FVector4f Normal4f = BoneMatrix.TransformVector(StaticData.Geometry.LocalNormals[VertexIndex]);
                SkinnedNormal += FVector3f(Normal4f.X, Normal4f.Y, Normal4f.Z) * Influence.Weight;
            }

            WeightSum += Influence.Weight;
        }

        if (WeightSum <= KINDA_SMALL_NUMBER)
        {
            return;
        }

        if (!FMath::IsNearlyEqual(WeightSum, 1.0f))
        {
            const float InvWeightSum = 1.0f / WeightSum;
            SkinnedPosition *= InvWeightSum;
            SkinnedNormal *= InvWeightSum;
        }

        if (Snapshot.bComputePositions)
        {
            Result.SkinnedPositions[VertexIndex] = SkinnedPosition;
        }

        if (Snapshot.bComputeNormals)
        {
            Result.SkinnedNormals[VertexIndex] = SkinnedNormal.GetSafeNormal();
        } });

    SetStatus(EDWCTaskStatus::Completed);
}

void FDWCCpuSkinningTask::CommitGameThread()
{
    DWC_PROFILE_SCOPE(DWC_CpuSkinningTask_CommitGameThread);

    if (!Owner.IsValid())
    {
        return;
    }

    Owner->CommitCpuSkinningTaskResult(MoveTemp(Result));
}

bool BuildDWCSkinningTaskSnapshot(
    USkeletalMeshComponent*                                              TargetSkeletalMesh,
    const FName                                                          ReceiverId,
    const TSharedPtr<const FDWCSkinningStaticData, ESPMode::ThreadSafe>& StaticData,
    const bool                                                           bComputePositions,
    const bool                                                           bComputeNormals,
    FDWCSkinningTaskSnapshot&                                            OutSnapshot)
{
    DWC_PROFILE_SCOPE(DWC_BuildCpuSkinningTaskSnapshot);

    OutSnapshot = FDWCSkinningTaskSnapshot();

    if (!TargetSkeletalMesh || !StaticData.IsValid() || (!bComputePositions && !bComputeNormals))
    {
        return false;
    }

    constexpr int32                RuntimeLODIndex = UWetClothingAsset::RuntimeSimulationLODIndex;
    const USkeletalMesh*           CurrentMesh = TargetSkeletalMesh->GetSkeletalMeshAsset();
    const FSkinWeightVertexBuffer* CurrentSkinWeightBuffer = TargetSkeletalMesh->GetSkinWeightBuffer(RuntimeLODIndex);
    if (CurrentMesh == nullptr ||
        CurrentSkinWeightBuffer == nullptr ||
        StaticData->Geometry.SkeletalMeshIdentity != reinterpret_cast<UPTRINT>(CurrentMesh) ||
        StaticData->SkinWeightBufferIdentity != reinterpret_cast<UPTRINT>(CurrentSkinWeightBuffer))
    {
        return false;
    }

    TArray<FMatrix44f> RefToLocalMatrices;
    TargetSkeletalMesh->CacheRefToLocalMatrices(RefToLocalMatrices);
    if (RefToLocalMatrices.Num() == 0)
    {
        return false;
    }

    const int32 VertexCount = StaticData->Geometry.VertexCount;
    if (VertexCount <= 0)
    {
        return false;
    }

    if (!StaticData->IsValid())
    {
        return false;
    }

    OutSnapshot.ReceiverId = ReceiverId;
    OutSnapshot.FrameNumber = GFrameCounter;
    OutSnapshot.bComputePositions = bComputePositions;
    OutSnapshot.bComputeNormals = bComputeNormals;
    OutSnapshot.StaticData = StaticData;
    OutSnapshot.RefToLocalMatrices = MoveTemp(RefToLocalMatrices);

    return true;
}

TSharedPtr<const FDWCSkinningStaticData, ESPMode::ThreadSafe> BuildDWCSkinningStaticData(
    USkeletalMeshComponent* TargetSkeletalMesh)
{
    DWC_PROFILE_SCOPE(DWC_BuildCpuSkinningStaticData);

    if (!TargetSkeletalMesh)
    {
        return nullptr;
    }

    constexpr int32             RuntimeLODIndex = UWetClothingAsset::RuntimeSimulationLODIndex;
    FSkeletalMeshLODRenderData* LODData = nullptr;
    if (!GetLODRenderData(TargetSkeletalMesh, RuntimeLODIndex, LODData))
    {
        return nullptr;
    }

    const FSkinWeightVertexBuffer* SkinWeightBuffer = TargetSkeletalMesh->GetSkinWeightBuffer(RuntimeLODIndex);
    if (!SkinWeightBuffer)
    {
        return nullptr;
    }

    const int32 VertexCount = LODData->GetNumVertices();
    if (VertexCount <= 0)
    {
        return nullptr;
    }

    TSharedRef<FDWCSkinningStaticData, ESPMode::ThreadSafe> StaticData =
        MakeShared<FDWCSkinningStaticData, ESPMode::ThreadSafe>();
    StaticData->Geometry.SkeletalMeshIdentity = reinterpret_cast<UPTRINT>(TargetSkeletalMesh->GetSkeletalMeshAsset());
    StaticData->Geometry.VertexDataIdentity = reinterpret_cast<UPTRINT>(LODData);
    StaticData->SkinWeightBufferIdentity = reinterpret_cast<UPTRINT>(SkinWeightBuffer);
    StaticData->Geometry.VertexCount = VertexCount;
    StaticData->Geometry.LocalPositions.SetNumZeroed(VertexCount);
    StaticData->Geometry.LocalNormals.SetNumZeroed(VertexCount);
    StaticData->Vertices.SetNumZeroed(VertexCount);

    const uint32 MaxInfluences = SkinWeightBuffer->GetMaxBoneInfluences();
    const float  BoneWeightScale = SkinWeightBuffer->GetBoneWeightByteSize() == 1 ? 255.0f : 65535.0f;
    StaticData->Influences.Reserve(VertexCount * static_cast<int32>(MaxInfluences));

    for (int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
    {
        int32 SectionIndex = INDEX_NONE;
        int32 SectionVertexIndex = INDEX_NONE;
        LODData->GetSectionFromVertexIndex(VertexIndex, SectionIndex, SectionVertexIndex);
        if (!LODData->RenderSections.IsValidIndex(SectionIndex) || SectionVertexIndex < 0)
        {
            continue;
        }

        const FSkelMeshRenderSection& Section = LODData->RenderSections[SectionIndex];
        const int32                   BufferVertexIndex = Section.GetVertexBufferIndex() + SectionVertexIndex;
        if (BufferVertexIndex < 0 || BufferVertexIndex >= VertexCount)
        {
            continue;
        }

        FDWCSkinningVertexSnapshot& Vertex = StaticData->Vertices[VertexIndex];
        Vertex.BufferVertexIndex = BufferVertexIndex;
        Vertex.InfluenceOffset = StaticData->Influences.Num();

        StaticData->Geometry.LocalPositions[VertexIndex] =
            LODData->StaticVertexBuffers.PositionVertexBuffer.VertexPosition(BufferVertexIndex);

        StaticData->Geometry.LocalNormals[VertexIndex] =
            LODData->StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(BufferVertexIndex).GetSafeNormal();

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
            StaticData->Influences.Add({ BoneIndex, static_cast<float>(BoneWeight) / BoneWeightScale });
            ++Vertex.InfluenceCount;
        }

        if (Vertex.InfluenceCount == 0)
        {
            Vertex.InfluenceOffset = INDEX_NONE;
        }
    }

    return StaticData;
}
