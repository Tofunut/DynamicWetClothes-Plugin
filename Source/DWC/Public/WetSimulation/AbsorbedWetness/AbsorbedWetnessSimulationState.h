// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class DWC_API FAbsorbedWetnessSimulationState
{
  public:
    void   ResetAll();
    void   ResetForVertexCount(int32 VertexCount);
    void   MarkWetVertexDirty(int32 VertexIndex);
    void   MarkAllWetVertexColorsDirty();
    void   ClearDirtyWetVertexIndices();
    uint64 GetAllocatedMemoryBytes() const;

    TArray<float> AbsorbedWetnessPerVertex;
    TArray<float> UpdatingPendingWetnessAmounts;
    TArray<float> WetnessDryHoldTimePerVertex;

    TArray<int32> UpdatingPendingWetnessVertexIndexQueue;
    TArray<int32> CurrentPendingWetnessVertexIndexQueue;
    TArray<float> CurrentPendingWetnessAmounts;
    int32         CurrentPendingWetnessReadIndex = 0;
    TArray<bool>  bPendingWetnessQueued;

    TArray<int32> DirtyWetVertexIndices;
    TArray<bool>  bDirtyWetVertexQueued;
};
