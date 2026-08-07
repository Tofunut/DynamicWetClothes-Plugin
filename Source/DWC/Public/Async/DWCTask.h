//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

enum class EDWCTaskStatus : uint8
{
    Created,
    Running,
    Completed,
    Failed,
    Canceled
};

struct DWC_API FDWCVertexGeometryStaticData
{
    UPTRINT SkeletalMeshIdentity = 0;
    UPTRINT VertexDataIdentity = 0;
    int32 VertexCount = 0;
    TArray<FVector3f> LocalPositions;
    TArray<FVector3f> LocalNormals;

    uint64 GetAllocatedMemoryBytes() const
    {
        return sizeof(*this) + GetAllocatedArrayMemoryBytes();
    }

    uint64 GetAllocatedArrayMemoryBytes() const
    {
        return LocalPositions.GetAllocatedSize() +
               LocalNormals.GetAllocatedSize();
    }

    bool IsValid() const
    {
        return SkeletalMeshIdentity != 0 &&
               VertexDataIdentity != 0 &&
               VertexCount > 0 &&
               LocalPositions.Num() == VertexCount &&
               LocalNormals.Num() == VertexCount;
    }
};

class DWC_API IDWCTaskRequest
{
  public:
    virtual ~IDWCTaskRequest() = default;

    virtual void ExecuteWorker() = 0;
    virtual void CommitGameThread() = 0;

    EDWCTaskStatus GetStatus() const
    {
        return Status;
    }

  protected:
    void SetStatus(const EDWCTaskStatus NewStatus)
    {
        Status = NewStatus;
    }

  private:
    EDWCTaskStatus Status = EDWCTaskStatus::Created;
};
