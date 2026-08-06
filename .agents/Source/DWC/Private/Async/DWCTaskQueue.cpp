#include "Async/DWCTaskQueue.h"

#include "Async/Async.h"
#include "Misc/ScopeLock.h"
#include "Utility/DWCProfiling.h"

FDWCTaskQueue::FDWCTaskQueue()
    : SharedState(MakeShared<FSharedState, ESPMode::ThreadSafe>())
{
}

FDWCTaskQueue::~FDWCTaskQueue()
{
    Shutdown();
}

void FDWCTaskQueue::Enqueue(FTaskRequestRef Request)
{
    if (!SharedState->bAcceptingTasks)
    {
        return;
    }

    const TSharedRef<FSharedState, ESPMode::ThreadSafe> State = SharedState;
    Async(EAsyncExecution::ThreadPool,
          [State, Request]()
          {
              DWC_PROFILE_SCOPE(DWC_TaskQueue_ExecuteWorker);

              Request->ExecuteWorker();

              if (!State->bAcceptingTasks)
              {
                  return;
              }

              FScopeLock Lock(&State->CompletedLock);
              State->CompletedRequests.Add(Request);
          });
}

void FDWCTaskQueue::FlushGameThread()
{
    DWC_PROFILE_SCOPE(DWC_TaskQueue_FlushGameThread);

    TArray<FTaskRequestRef> RequestsToCommit;
    {
        FScopeLock Lock(&SharedState->CompletedLock);
        Swap(RequestsToCommit, SharedState->CompletedRequests);
    }

    for (const FTaskRequestRef& Request : RequestsToCommit)
    {
        Request->CommitGameThread();
    }
}

void FDWCTaskQueue::Shutdown()
{
    SharedState->bAcceptingTasks = false;

    FScopeLock Lock(&SharedState->CompletedLock);
    SharedState->CompletedRequests.Reset();
}

int32 FDWCTaskQueue::GetCompletedTaskCount() const
{
    FScopeLock Lock(&SharedState->CompletedLock);
    return SharedState->CompletedRequests.Num();
}
