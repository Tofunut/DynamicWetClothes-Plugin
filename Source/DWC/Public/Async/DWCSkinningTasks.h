#pragma once

#include "Async/DWCTask.h"

class UDynamicWetClothesComponent;
class USkeletalMeshComponent;
class FSkeletalMeshLODRenderData;
class FSkinWeightVertexBuffer;

struct DWC_API FDWCSkinningInfluenceSnapshot
{
    int32 BoneIndex = INDEX_NONE;
    float Weight = 0.0f;
};

struct DWC_API FDWCSkinningVertexSnapshot
{
    int32 BufferVertexIndex = INDEX_NONE;
    int32 InfluenceOffset = INDEX_NONE;
    int32 InfluenceCount = 0;
};

struct DWC_API FDWCSkinningTaskSnapshot
{
    FDWCVertexTaskSnapshot VertexTarget;
    uint64 FrameNumber = 0;
    bool bComputePositions = true;
    bool bComputeNormals = false;

    TSharedPtr<const struct FDWCSkinningStaticData, ESPMode::ThreadSafe> StaticData;
    TArray<FMatrix44f> RefToLocalMatrices;
};

struct DWC_API FDWCSkinningStaticData
{
    UPTRINT SkeletalMeshIdentity = 0;
    UPTRINT SkinWeightBufferIdentity = 0;
    int32 LODIndex = INDEX_NONE;
    int32 VertexCount = 0;
    TArray<FVector3f> LocalPositions;
    TArray<FVector3f> LocalNormals;
    TArray<FDWCSkinningVertexSnapshot> Vertices;
    TArray<FDWCSkinningInfluenceSnapshot> Influences;

    bool IsValidFor(const FDWCVertexTaskSnapshot& InVertexTarget) const
    {
        return LODIndex == InVertexTarget.LODIndex &&
               VertexCount == InVertexTarget.VertexCount &&
               Vertices.Num() == InVertexTarget.VertexCount &&
               LocalPositions.Num() == InVertexTarget.VertexCount &&
               LocalNormals.Num() == InVertexTarget.VertexCount;
    }
};

struct DWC_API FDWCSkinningTaskResult
{
    FDWCVertexTaskSnapshot VertexTarget;
    uint64 FrameNumber = 0;
    TArray<FVector3f> SkinnedPositions;
    TArray<FVector3f> SkinnedNormals;

    bool HasPositions() const
    {
        return SkinnedPositions.Num() > 0;
    }

    bool HasNormals() const
    {
        return SkinnedNormals.Num() > 0;
    }
};

class DWC_API FDWCCpuSkinningTask final : public IDWCTaskRequest
{
  public:
    FDWCCpuSkinningTask(
        TWeakObjectPtr<UDynamicWetClothesComponent> InOwner,
        FDWCSkinningTaskSnapshot&&                  InSnapshot);

    virtual EDWCTaskKind GetKind() const override;
    virtual FName        GetDebugName() const override;
    virtual void         ExecuteWorker() override;
    virtual void         CommitGameThread() override;

    const FDWCSkinningTaskResult& GetResult() const
    {
        return Result;
    }

  private:
    TWeakObjectPtr<UDynamicWetClothesComponent> Owner;
    FDWCSkinningTaskSnapshot Snapshot;
    FDWCSkinningTaskResult Result;
};

DWC_API bool BuildDWCSkinningTaskSnapshot(
    USkeletalMeshComponent*       TargetSkeletalMesh,
    int32                         LODIndex,
    const FDWCTaskTargetSnapshot& Target,
    const TSharedPtr<const FDWCSkinningStaticData, ESPMode::ThreadSafe>& StaticData,
    bool                          bComputePositions,
    bool                          bComputeNormals,
    FDWCSkinningTaskSnapshot&     OutSnapshot);

DWC_API TSharedPtr<const FDWCSkinningStaticData, ESPMode::ThreadSafe> BuildDWCSkinningStaticData(
    USkeletalMeshComponent* TargetSkeletalMesh,
    int32 LODIndex);
