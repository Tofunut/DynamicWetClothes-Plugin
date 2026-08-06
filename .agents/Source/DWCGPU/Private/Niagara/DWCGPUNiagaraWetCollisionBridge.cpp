#include "Niagara/DWCGPUNiagaraWetCollisionBridge.h"

#include "RenderCore.h"

namespace
{
TMap<FNiagaraSystemInstanceID, FDWCGPUNiagaraWetCollisionBuffer> GWetCollisionBuffers;
constexpr uint64 GNiagaraWetCollisionActiveFrameTolerance = 2;
}

namespace DWCGPUNiagaraWetCollisionBridge
{
void RegisterBuffer_RenderThread(
    const FNiagaraSystemInstanceID SystemInstanceID,
    const int32 MaxContacts,
    const TRefCountPtr<FRDGPooledBuffer>& ContactBuffer,
    const TRefCountPtr<FRDGPooledBuffer>& ContactCountBuffer)
{
    if (!IsInRenderingThread() || !ContactBuffer.IsValid() || !ContactCountBuffer.IsValid())
    {
        return;
    }

    FDWCGPUNiagaraWetCollisionBuffer& Entry = GWetCollisionBuffers.FindOrAdd(SystemInstanceID);
    Entry.SystemInstanceID = SystemInstanceID;
    Entry.MaxContacts = MaxContacts;
    Entry.ContactBuffer = ContactBuffer;
    Entry.ContactCountBuffer = ContactCountBuffer;
    Entry.LastActiveRenderFrame = GFrameNumberRenderThread;
}

void MarkBufferActive_RenderThread(const FNiagaraSystemInstanceID SystemInstanceID)
{
    if (!IsInRenderingThread())
    {
        return;
    }

    FDWCGPUNiagaraWetCollisionBuffer* Entry = GWetCollisionBuffers.Find(SystemInstanceID);
    if (Entry == nullptr)
    {
        return;
    }

    Entry->LastActiveRenderFrame = GFrameNumberRenderThread;
}

void SetTargetReceiverGPUIds_GameThread(
    const FNiagaraSystemInstanceID SystemInstanceID,
    TArray<int32> TargetReceiverGPUIds)
{
    TargetReceiverGPUIds.RemoveAll([](const int32 ReceiverGPUId)
    {
        return ReceiverGPUId == 0;
    });

    ENQUEUE_RENDER_COMMAND(DWCSetNiagaraWetCollisionTargetReceivers)(
        [SystemInstanceID, TargetReceiverGPUIds = MoveTemp(TargetReceiverGPUIds)](FRHICommandListImmediate& RHICmdList) mutable
        {
            FDWCGPUNiagaraWetCollisionBuffer& Entry = GWetCollisionBuffers.FindOrAdd(SystemInstanceID);
            Entry.SystemInstanceID = SystemInstanceID;
            Entry.bRestrictToTargetReceiverGPUIds = true;
            Entry.TargetReceiverGPUIds = MoveTemp(TargetReceiverGPUIds);
        });
}

void ClearTargetReceiverGPUIds_GameThread(const FNiagaraSystemInstanceID SystemInstanceID)
{
    ENQUEUE_RENDER_COMMAND(DWCClearNiagaraWetCollisionTargetReceivers)(
        [SystemInstanceID](FRHICommandListImmediate& RHICmdList)
        {
            FDWCGPUNiagaraWetCollisionBuffer* Entry = GWetCollisionBuffers.Find(SystemInstanceID);
            if (Entry == nullptr)
            {
                return;
            }

            Entry->bRestrictToTargetReceiverGPUIds = false;
            Entry->TargetReceiverGPUIds.Reset();
        });
}

void UnregisterBuffer_RenderThread(const FNiagaraSystemInstanceID SystemInstanceID)
{
    if (IsInRenderingThread())
    {
        GWetCollisionBuffers.Remove(SystemInstanceID);
    }
}

void CollectBuffers_RenderThread(TArray<FDWCGPUNiagaraWetCollisionBuffer>& OutBuffers)
{
    if (!IsInRenderingThread())
    {
        return;
    }

    for (const TPair<FNiagaraSystemInstanceID, FDWCGPUNiagaraWetCollisionBuffer>& Pair : GWetCollisionBuffers)
    {
        const FDWCGPUNiagaraWetCollisionBuffer& Buffer = Pair.Value;
        if (Buffer.LastActiveRenderFrame > 0 &&
            GFrameNumberRenderThread > Buffer.LastActiveRenderFrame + GNiagaraWetCollisionActiveFrameTolerance)
        {
            continue;
        }

        if (Buffer.ContactBuffer.IsValid() && Buffer.ContactCountBuffer.IsValid())
        {
            OutBuffers.Add(Buffer);
        }
    }
}
} // namespace DWCGPUNiagaraWetCollisionBridge
