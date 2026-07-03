#pragma once

#include "CoreMinimal.h"

class FAbsorbedWetnessSimulationState
{
  public:
    void ResetAll();
    void ResetForVertexCount(int32 VertexCount);
    void MarkAllWetVertexColorsDirty();

    TArray<float> AbsorbedWetnessPerVertex;
    TArray<float> UpdatingPendingWetnessAmounts;
    TArray<float> WetnessDryHoldTimePerVertex;

    TArray<int32> UpdatingPendingWetnessVertexIndexQueue;
    TArray<int32> CurrentPendingWetnessVertexIndexQueue;
    TArray<float> CurrentPendingWetnessAmounts;
    TArray<bool>  bPendingWetnessQueued;

    TArray<int32> DirtyWetVertexIndices;
};
