#include "WetSimulation/AbsorbedWetness/AbsorbedWetnessSimulationState.h"

uint64 FAbsorbedWetnessSimulationState::GetAllocatedMemoryBytes() const
{
    return sizeof(*this) +
           AbsorbedWetnessPerVertex.GetAllocatedSize() +
           UpdatingPendingWetnessAmounts.GetAllocatedSize() +
           WetnessDryHoldTimePerVertex.GetAllocatedSize() +
           UpdatingPendingWetnessVertexIndexQueue.GetAllocatedSize() +
           CurrentPendingWetnessVertexIndexQueue.GetAllocatedSize() +
           CurrentPendingWetnessAmounts.GetAllocatedSize() +
           bPendingWetnessQueued.GetAllocatedSize() +
           DirtyWetVertexIndices.GetAllocatedSize() +
           bDirtyWetVertexQueued.GetAllocatedSize();
}

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
    bDirtyWetVertexQueued.Reset();
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
    bDirtyWetVertexQueued.Init(false, VertexCount);
}

void FAbsorbedWetnessSimulationState::MarkWetVertexDirty(const int32 VertexIndex)
{
    if (!AbsorbedWetnessPerVertex.IsValidIndex(VertexIndex))
    {
        return;
    }

    if (bDirtyWetVertexQueued.Num() != AbsorbedWetnessPerVertex.Num())
    {
        bDirtyWetVertexQueued.Init(false, AbsorbedWetnessPerVertex.Num());
    }

    if (!bDirtyWetVertexQueued[VertexIndex])
    {
        bDirtyWetVertexQueued[VertexIndex] = true;
        DirtyWetVertexIndices.Add(VertexIndex);
    }
}

void FAbsorbedWetnessSimulationState::MarkAllWetVertexColorsDirty()
{
    DirtyWetVertexIndices.Reset();
    bDirtyWetVertexQueued.Init(false, AbsorbedWetnessPerVertex.Num());
    for (int32 VertexIndex = 0; VertexIndex < AbsorbedWetnessPerVertex.Num(); ++VertexIndex)
    {
        DirtyWetVertexIndices.Add(VertexIndex);
        bDirtyWetVertexQueued[VertexIndex] = true;
    }
}

void FAbsorbedWetnessSimulationState::ClearDirtyWetVertexIndices()
{
    for (const int32 VertexIndex : DirtyWetVertexIndices)
    {
        if (bDirtyWetVertexQueued.IsValidIndex(VertexIndex))
        {
            bDirtyWetVertexQueued[VertexIndex] = false;
        }
    }

    DirtyWetVertexIndices.Reset();
}
