//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "Async/DWCTask.h"
#include "HAL/CriticalSection.h"
#include "HAL/ThreadSafeBool.h"
#include "Templates/SharedPointer.h"

class DWC_API FDWCTaskQueue
{
  public:
    using FTaskRequestRef = TSharedRef<IDWCTaskRequest, ESPMode::ThreadSafe>;

    FDWCTaskQueue();
    ~FDWCTaskQueue();

    void Enqueue(FTaskRequestRef Request);
    void FlushGameThread();
    void Shutdown();

    int32 GetCompletedTaskCount() const;

  private:
    struct FSharedState
    {
        mutable FCriticalSection CompletedLock;
        TArray<FTaskRequestRef>  CompletedRequests;
        FThreadSafeBool          bAcceptingTasks = true;
    };

    TSharedRef<FSharedState, ESPMode::ThreadSafe> SharedState;
};
