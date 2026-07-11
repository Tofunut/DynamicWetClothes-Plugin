#pragma once

#include "CoreMinimal.h"

enum class EDWCTaskKind : uint8
{
    Unknown,
    CpuSkinning,
    WetSurfaceInput,
    WetAreaInput,
    WetContactInput,
    VertexColorBuild,
    WetPropagation
};

enum class EDWCTaskStatus : uint8
{
    Created,
    Running,
    Completed,
    Failed,
    Canceled
};

struct DWC_API FDWCTaskTargetSnapshot
{
    FName TargetId = NAME_None;
    int32 TargetGeneration = 0;
};

struct DWC_API FDWCVertexTaskSnapshot
{
    FDWCTaskTargetSnapshot Target;
    int32 LODIndex = 0;
    int32 VertexCount = 0;
};

class DWC_API IDWCTaskRequest
{
  public:
    virtual ~IDWCTaskRequest() = default;

    virtual EDWCTaskKind GetKind() const = 0;
    virtual FName        GetDebugName() const = 0;

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
