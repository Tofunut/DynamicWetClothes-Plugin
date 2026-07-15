#pragma once

#include "CoreMinimal.h"

class FAbsorbedWetnessSimulationState
{
  public:
    void ResetAll();
    void ResetForVertexCount(int32 VertexCount);
    void MarkWetVertexDirty(int32 VertexIndex);
    void MarkAllWetVertexColorsDirty();
    void ClearDirtyWetVertexIndices();

    TArray<float> AbsorbedWetnessPerVertex;
    TArray<float> UpdatingPendingWetnessAmounts;
    TArray<float> WetnessDryHoldTimePerVertex;

    TArray<int32> UpdatingPendingWetnessVertexIndexQueue;
    TArray<int32> CurrentPendingWetnessVertexIndexQueue;
    TArray<float> CurrentPendingWetnessAmounts;
    int32 CurrentPendingWetnessReadIndex = 0;
    TArray<bool>  bPendingWetnessQueued;

    TArray<int32> DirtyWetVertexIndices;
    TArray<bool>  bDirtyWetVertexQueued;
};
