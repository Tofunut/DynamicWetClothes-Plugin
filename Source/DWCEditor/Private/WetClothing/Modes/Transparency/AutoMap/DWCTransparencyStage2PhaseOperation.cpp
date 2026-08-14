// Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Modes/Transparency/AutoMap/DWCTransparencyStage2PhaseOperation.h"

namespace
{
    constexpr EDWCTransparencyStage2OperationPhase OrderedPhases[] = {
        EDWCTransparencyStage2OperationPhase::PrepareTarget,
        EDWCTransparencyStage2OperationPhase::RasterizeTarget,
        EDWCTransparencyStage2OperationPhase::PrepareSources,
        EDWCTransparencyStage2OperationPhase::StreamProjection,
        EDWCTransparencyStage2OperationPhase::ComposeResult,
        EDWCTransparencyStage2OperationPhase::TransferResult
    };

    uint64 GetDesiredBytes(
        const FDWCTransparencyStage2PhaseResources& Resources,
        const EDWCEditorResourcePool Pool,
        const bool bRetained)
    {
        if (Pool == EDWCEditorResourcePool::WorkerPrivateCPU)
        {
            return bRetained ? Resources.WorkerRetainedBytes : Resources.WorkerPeakBytes;
        }
        if (Pool == EDWCEditorResourcePool::PreviewWorkspaceCPU)
        {
            return bRetained ? Resources.PreviewRetainedBytes : Resources.PreviewPeakBytes;
        }
        return 0;
    }
}

FDWCTransparencyStage2PhaseOperation::FDWCTransparencyStage2PhaseOperation(
    TSharedPtr<FDWCEditorResourceGovernor> InResourceGovernor,
    FDWCEditorAsyncOperationIdentity InIdentity,
    const bool bInResourcesOwnedByCaller)
    : ResourceGovernor(MoveTemp(InResourceGovernor))
    , Identity(MoveTemp(InIdentity))
    , bResourcesOwnedByCaller(bInResourcesOwnedByCaller)
{
    FName Previous;
    for (const EDWCTransparencyStage2OperationPhase Phase : OrderedPhases)
    {
        FDWCEditorOperationPhaseDescriptor Descriptor;
        Descriptor.Name = GetPhaseName(Phase);
        Descriptor.DebugName = Descriptor.Name.ToString();
        Descriptor.Thread = EDWCEditorOperationPhaseThread::GameThread;
        if (!Previous.IsNone())
        {
            Descriptor.Dependencies.Add(Previous);
        }
        verify(Graph.AddPhase(MoveTemp(Descriptor)));
        Previous = GetPhaseName(Phase);
    }
    verify(Graph.Validate());
}

FName FDWCTransparencyStage2PhaseOperation::GetPhaseName(
    const EDWCTransparencyStage2OperationPhase Phase)
{
    switch (Phase)
    {
    case EDWCTransparencyStage2OperationPhase::PrepareTarget:
        return TEXT("Stage2.PrepareTarget");
    case EDWCTransparencyStage2OperationPhase::RasterizeTarget:
        return TEXT("Stage2.RasterizeTarget");
    case EDWCTransparencyStage2OperationPhase::PrepareSources:
        return TEXT("Stage2.PrepareSources");
    case EDWCTransparencyStage2OperationPhase::StreamProjection:
        return TEXT("Stage2.StreamProjection");
    case EDWCTransparencyStage2OperationPhase::ComposeResult:
        return TEXT("Stage2.ComposeResult");
    case EDWCTransparencyStage2OperationPhase::TransferResult:
        return TEXT("Stage2.TransferResult");
    }
    return NAME_None;
}

bool FDWCTransparencyStage2PhaseOperation::ResizePool(
    const EDWCEditorResourcePool Pool,
    const uint64 Bytes,
    const FString& DebugName,
    FString& OutError)
{
    if (bResourcesOwnedByCaller || !ResourceGovernor.IsValid())
    {
        return true;
    }
    TUniquePtr<FDWCEditorMemoryLease>* Existing = Leases.Find(Pool);
    if (Bytes == 0)
    {
        if (Existing != nullptr)
        {
            Existing->Reset();
            Leases.Remove(Pool);
        }
        return true;
    }
    if (Existing != nullptr && Existing->IsValid() && (*Existing)->IsValid())
    {
        return (*Existing)->TryResize(Bytes, &OutError);
    }

    FDWCEditorResourceReservationRequest Request;
    Request.Pool = Pool;
    Request.Bytes = Bytes;
    Request.Owner = Identity;
    Request.DebugName = DebugName;
    FDWCEditorMemoryLease Lease = ResourceGovernor->TryAcquire(Request, &OutError);
    if (!Lease.IsValid())
    {
        return false;
    }
    Leases.Add(Pool, MakeUnique<FDWCEditorMemoryLease>(MoveTemp(Lease)));
    return true;
}

void FDWCTransparencyStage2PhaseOperation::ReleasePoolsNotIn(
    const FDWCTransparencyStage2PhaseResources& Resources,
    const bool bRetained)
{
    TArray<EDWCEditorResourcePool> Pools;
    Leases.GetKeys(Pools);
    for (const EDWCEditorResourcePool Pool : Pools)
    {
        if (GetDesiredBytes(Resources, Pool, bRetained) == 0)
        {
            Leases.Remove(Pool);
        }
    }
}

bool FDWCTransparencyStage2PhaseOperation::FinishCurrentPhase(FString& OutError)
{
    if (!CurrentPhase.IsSet())
    {
        return true;
    }
    const FName PhaseName = GetPhaseName(CurrentPhase.GetValue());
    if (!Graph.MarkRetiring(PhaseName, &OutError))
    {
        return false;
    }
    if (!ResizePool(
            EDWCEditorResourcePool::WorkerPrivateCPU,
            CurrentResources.WorkerRetainedBytes,
            PhaseName.ToString() + TEXT(" retained worker buffers"),
            OutError) ||
        !ResizePool(
            EDWCEditorResourcePool::PreviewWorkspaceCPU,
            CurrentResources.PreviewRetainedBytes,
            PhaseName.ToString() + TEXT(" retained preview payload"),
            OutError))
    {
        Graph.MarkFailed(PhaseName, OutError);
        Graph.CancelOutstanding(OutError);
        Leases.Reset();
        bTerminal = true;
        return false;
    }
    ReleasePoolsNotIn(CurrentResources, true);
    if (!Graph.MarkCompleted(PhaseName, &OutError))
    {
        return false;
    }
    CurrentPhase.Reset();
    return true;
}

bool FDWCTransparencyStage2PhaseOperation::BeginPhase(
    const EDWCTransparencyStage2OperationPhase Phase,
    const FDWCTransparencyStage2PhaseResources& Resources,
    FString& OutError)
{
    check(IsInGameThread());
    if (bTerminal || !FinishCurrentPhase(OutError))
    {
        return false;
    }

    const FName PhaseName = GetPhaseName(Phase);
    FDWCEditorOperationPhaseResourcePlan Plan;
    Plan.AddPeak(EDWCEditorResourcePool::WorkerPrivateCPU, Resources.WorkerPeakBytes);
    Plan.AddRetained(EDWCEditorResourcePool::WorkerPrivateCPU, Resources.WorkerRetainedBytes);
    Plan.AddPeak(EDWCEditorResourcePool::PreviewWorkspaceCPU, Resources.PreviewPeakBytes);
    Plan.AddRetained(EDWCEditorResourcePool::PreviewWorkspaceCPU, Resources.PreviewRetainedBytes);
    if (!Graph.UpdateResourcePlan(PhaseName, MoveTemp(Plan), &OutError) ||
        !Graph.MarkWaitingAdmission(PhaseName, &OutError))
    {
        Graph.CancelOutstanding(OutError);
        Leases.Reset();
        bTerminal = true;
        return false;
    }

    if (!ResizePool(
            EDWCEditorResourcePool::WorkerPrivateCPU,
            Resources.WorkerPeakBytes,
            PhaseName.ToString() + TEXT(" worker buffers"),
            OutError) ||
        !ResizePool(
            EDWCEditorResourcePool::PreviewWorkspaceCPU,
            Resources.PreviewPeakBytes,
            PhaseName.ToString() + TEXT(" preview payload"),
            OutError))
    {
        Graph.MarkFailed(PhaseName, OutError);
        Graph.CancelOutstanding(OutError);
        Leases.Reset();
        bTerminal = true;
        return false;
    }
    ReleasePoolsNotIn(Resources, false);
    if (!Graph.MarkRunning(PhaseName, &OutError))
    {
        Graph.CancelOutstanding(OutError);
        Leases.Reset();
        bTerminal = true;
        return false;
    }
    CurrentPhase = Phase;
    CurrentResources = Resources;
    return true;
}

bool FDWCTransparencyStage2PhaseOperation::Complete(FString& OutError)
{
    check(IsInGameThread());
    if (bTerminal)
    {
        OutError = TEXT("The Transparency Stage 2 phase operation is already terminal.");
        return false;
    }
    if (!FinishCurrentPhase(OutError) || !Graph.IsTerminal())
    {
        return false;
    }
    bTerminal = true;
    return true;
}

void FDWCTransparencyStage2PhaseOperation::Fail(const FString& Error)
{
    check(IsInGameThread());
    if (CurrentPhase.IsSet())
    {
        Graph.MarkFailed(GetPhaseName(CurrentPhase.GetValue()), Error);
    }
    Graph.CancelOutstanding(Error);
    Leases.Reset();
    CurrentPhase.Reset();
    bTerminal = true;
}

void FDWCTransparencyStage2PhaseOperation::Cancel(const FString& Reason)
{
    check(IsInGameThread());
    Graph.CancelOutstanding(Reason);
    Leases.Reset();
    CurrentPhase.Reset();
    bTerminal = true;
}

FDWCEditorMemoryLease FDWCTransparencyStage2PhaseOperation::TakeRetainedLease(
    const EDWCEditorResourcePool Pool)
{
    TUniquePtr<FDWCEditorMemoryLease> Lease;
    if (Leases.RemoveAndCopyValue(Pool, Lease) && Lease.IsValid())
    {
        return MoveTemp(*Lease);
    }
    return {};
}

FDWCEditorOperationPhaseGraphSnapshot FDWCTransparencyStage2PhaseOperation::GetSnapshot() const
{
    return Graph.GetSnapshot();
}
