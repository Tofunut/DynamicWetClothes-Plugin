#pragma once

#include "CoreMinimal.h"

class FDynamicWetReceiverSimulationState
{
public:
    void ResetAll();
    void ResetForVertexCount(int32 VertexCount);
    void MarkAllWetVertexColorsDirty();

    TArray<float> WetnessPerVertex;
    TArray<float> Updating_Pending_Wetness_Amounts;
    TArray<float> WetnessDryHoldTimePerVertex;

    TArray<int32> Updating_Pending_Wetness_Vertex_IndexQueue;
    TArray<int32> Current_Pending_Wetness_Vertex_IndexQueue;
    TArray<float> Current_Pending_Wetness_Amounts;
    TArray<bool> bPendingWetnessQueued;

    TArray<int32> DirtyWetVertexIndices;
};
