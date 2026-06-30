#include "DynamicWet/DynamicWetReceiverSimulationState.h"

void FDynamicWetReceiverSimulationState::ResetAll()
{
    WetnessPerVertex.Reset();
    Updating_Pending_Wetness_Amounts.Reset();
    WetnessDryHoldTimePerVertex.Reset();
    Updating_Pending_Wetness_Vertex_IndexQueue.Reset();
    Current_Pending_Wetness_Vertex_IndexQueue.Reset();
    Current_Pending_Wetness_Amounts.Reset();
    bPendingWetnessQueued.Reset();
    DirtyWetVertexIndices.Reset();
}

void FDynamicWetReceiverSimulationState::ResetForVertexCount(const int32 VertexCount)
{
    WetnessPerVertex.SetNumZeroed(VertexCount);
    Updating_Pending_Wetness_Amounts.SetNumZeroed(VertexCount);
    WetnessDryHoldTimePerVertex.SetNumZeroed(VertexCount);
    Updating_Pending_Wetness_Vertex_IndexQueue.Reset();
    Current_Pending_Wetness_Vertex_IndexQueue.Reset();
    Current_Pending_Wetness_Amounts.Reset();
    bPendingWetnessQueued.Init(false, VertexCount);
    DirtyWetVertexIndices.Reset();
}

void FDynamicWetReceiverSimulationState::MarkAllWetVertexColorsDirty()
{
    DirtyWetVertexIndices.Reset();
    for (int32 VertexIndex = 0; VertexIndex < WetnessPerVertex.Num(); ++VertexIndex)
    {
        DirtyWetVertexIndices.Add(VertexIndex);
    }
}
