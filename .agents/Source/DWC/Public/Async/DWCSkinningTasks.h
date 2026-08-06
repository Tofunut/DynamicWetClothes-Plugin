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
    FName ReceiverId = NAME_None;
    uint64 FrameNumber = 0;
    bool bComputePositions = true;
    bool bComputeNormals = false;

    TSharedPtr<const struct FDWCSkinningStaticData, ESPMode::ThreadSafe> StaticData;
    TArray<FMatrix44f> RefToLocalMatrices;
};

struct DWC_API FDWCSkinningStaticData
{
    FDWCVertexGeometryStaticData Geometry;
    UPTRINT SkinWeightBufferIdentity = 0;
    TArray<FDWCSkinningVertexSnapshot> Vertices;
    TArray<FDWCSkinningInfluenceSnapshot> Influences;

    uint64 GetAllocatedMemoryBytes() const
    {
        return sizeof(*this) +
               Geometry.GetAllocatedArrayMemoryBytes() +
               Vertices.GetAllocatedSize() +
               Influences.GetAllocatedSize();
    }

    bool IsValid() const
    {
        return Geometry.IsValid() &&
               Vertices.Num() == Geometry.VertexCount;
    }
};

struct DWC_API FDWCSkinningTaskResult
{
    FName ReceiverId = NAME_None;
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
    FName                         ReceiverId,
    const TSharedPtr<const FDWCSkinningStaticData, ESPMode::ThreadSafe>& StaticData,
    bool                          bComputePositions,
    bool                          bComputeNormals,
    FDWCSkinningTaskSnapshot&     OutSnapshot);

DWC_API TSharedPtr<const FDWCSkinningStaticData, ESPMode::ThreadSafe> BuildDWCSkinningStaticData(
    USkeletalMeshComponent* TargetSkeletalMesh);
