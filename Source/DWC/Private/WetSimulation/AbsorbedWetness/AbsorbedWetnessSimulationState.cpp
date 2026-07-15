#include "WetSimulation/AbsorbedWetness/AbsorbedWetnessSimulationState.h"

void FAbsorbedWetnessSimulationState::ResetAll()
{
    AbsorbedWetnessPerVertex.Reset();
    UpdatingPendingWetnessAmounts.Reset();
    WetnessDryHoldTimePerVertex.Reset();
    UpdatingPendingWetnessVertexIndexQueue.Reset();
    CurrentPendingWetnessVertexIndexQueue.Reset();
    CurrentPendingWetnessAmounts.Reset();
    CurrentPendingWetnessReadIndex = 0;
    bPendingWetnessQueued.Reset();
    DirtyWetVertexIndices.Reset();
}

void FAbsorbedWetnessSimulationState::ResetForVertexCount(const int32 VertexCount)
{
    AbsorbedWetnessPerVertex.SetNumZeroed(VertexCount);
    UpdatingPendingWetnessAmounts.SetNumZeroed(VertexCount);
    WetnessDryHoldTimePerVertex.SetNumZeroed(VertexCount);
    UpdatingPendingWetnessVertexIndexQueue.Reset();
    CurrentPendingWetnessVertexIndexQueue.Reset();
    CurrentPendingWetnessAmounts.Reset();
    CurrentPendingWetnessReadIndex = 0;
    bPendingWetnessQueued.Init(false, VertexCount);
    DirtyWetVertexIndices.Reset();
}

void FAbsorbedWetnessSimulationState::MarkAllWetVertexColorsDirty()
{
    DirtyWetVertexIndices.Reset();
    for (int32 VertexIndex = 0; VertexIndex < AbsorbedWetnessPerVertex.Num(); ++VertexIndex)
    {
        DirtyWetVertexIndices.Add(VertexIndex);
    }
}
