// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "NiagaraCore.h"
#include "RenderGraphResources.h"

struct FDWCGPUNiagaraWetCollisionBuffer
{
    FNiagaraSystemInstanceID       SystemInstanceID = 0;
    int32                          MaxContacts = 0;
    bool                           bRestrictToTargetReceiverGPUIds = false;
    TArray<int32>                  TargetReceiverGPUIds;
    TRefCountPtr<FRDGPooledBuffer> ContactBuffer;
    TRefCountPtr<FRDGPooledBuffer> ContactCountBuffer;
    uint64                         LastActiveRenderFrame = 0;
};

namespace DWCGPUNiagaraWetCollisionBridge
{
    DWCGPU_API void RegisterBuffer_RenderThread(
        FNiagaraSystemInstanceID              SystemInstanceID,
        int32                                 MaxContacts,
        const TRefCountPtr<FRDGPooledBuffer>& ContactBuffer,
        const TRefCountPtr<FRDGPooledBuffer>& ContactCountBuffer);

    DWCGPU_API void MarkBufferActive_RenderThread(FNiagaraSystemInstanceID SystemInstanceID);

    DWCGPU_API void SetTargetReceiverGPUIds_GameThread(
        FNiagaraSystemInstanceID SystemInstanceID,
        TArray<int32>            TargetReceiverGPUIds);

    DWCGPU_API void ClearTargetReceiverGPUIds_GameThread(FNiagaraSystemInstanceID SystemInstanceID);

    DWCGPU_API void UnregisterBuffer_RenderThread(FNiagaraSystemInstanceID SystemInstanceID);

    DWCGPU_API void CollectBuffers_RenderThread(TArray<FDWCGPUNiagaraWetCollisionBuffer>& OutBuffers);
} // namespace DWCGPUNiagaraWetCollisionBridge
