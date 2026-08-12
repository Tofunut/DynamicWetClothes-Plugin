// Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Foundation/Operations/DWCEditorOperationPhaseGraph.h"

#include "HAL/PlatformTime.h"

namespace
{
    uint64 SumDemands(
        const TArray<FDWCEditorOperationPhaseResourceDemand>& Demands,
        const TOptional<EDWCEditorResourcePool> Pool)
    {
        uint64 Total = 0;
        for (const FDWCEditorOperationPhaseResourceDemand& Demand : Demands)
        {
            if (Pool.IsSet() && Demand.Pool != Pool.GetValue())
            {
                continue;
            }
            if (Demand.Bytes > MAX_uint64 - Total)
            {
                return MAX_uint64;
            }
            Total += Demand.Bytes;
        }
        return Total;
    }

    void AddDemand(
        TArray<FDWCEditorOperationPhaseResourceDemand>& Demands,
        const EDWCEditorResourcePool Pool,
        const uint64 Bytes)
    {
        if (Bytes == 0)
        {
            return;
        }
        if (FDWCEditorOperationPhaseResourceDemand* Existing = Demands.FindByPredicate(
                [Pool](const FDWCEditorOperationPhaseResourceDemand& Demand)
                {
                    return Demand.Pool == Pool;
                }))
        {
            Existing->Bytes = Bytes > MAX_uint64 - Existing->Bytes
                ? MAX_uint64
                : Existing->Bytes + Bytes;
            return;
        }
        FDWCEditorOperationPhaseResourceDemand& Demand = Demands.AddDefaulted_GetRef();
        Demand.Pool = Pool;
        Demand.Bytes = Bytes;
    }
}

void FDWCEditorOperationPhaseResourcePlan::AddPeak(
    const EDWCEditorResourcePool Pool,
    const uint64 Bytes)
{
    AddDemand(Peak, Pool, Bytes);
}

void FDWCEditorOperationPhaseResourcePlan::AddRetained(
    const EDWCEditorResourcePool Pool,
    const uint64 Bytes)
{
    AddDemand(Retained, Pool, Bytes);
}

uint64 FDWCEditorOperationPhaseResourcePlan::GetPeakBytes(
    const EDWCEditorResourcePool Pool) const
{
    return SumDemands(Peak, Pool);
}

uint64 FDWCEditorOperationPhaseResourcePlan::GetRetainedBytes(
    const EDWCEditorResourcePool Pool) const
{
    return SumDemands(Retained, Pool);
}

uint64 FDWCEditorOperationPhaseResourcePlan::GetTotalPeakBytes() const
{
    return SumDemands(Peak, {});
}

uint64 FDWCEditorOperationPhaseResourcePlan::GetTotalRetainedBytes() const
{
    return SumDemands(Retained, {});
}

bool FDWCEditorOperationPhaseResourcePlan::IsValid(FString* OutError) const
{
    for (const FDWCEditorOperationPhaseResourceDemand& Demand : Retained)
    {
        if (Demand.Bytes > GetPeakBytes(Demand.Pool))
        {
            if (OutError != nullptr)
            {
                *OutError = TEXT("A phase cannot retain more bytes than its peak ownership in the same pool.");
            }
            return false;
        }
    }
    return GetTotalPeakBytes() != MAX_uint64 && GetTotalRetainedBytes() != MAX_uint64;
}

bool FDWCEditorOperationPhaseDescriptor::IsValid(FString* OutError) const
{
    if (Name.IsNone())
    {
        if (OutError != nullptr) *OutError = TEXT("An operation phase requires a name.");
        return false;
    }
    if (Dependencies.Contains(Name))
    {
        if (OutError != nullptr) *OutError = TEXT("An operation phase cannot depend on itself.");
        return false;
    }
    return Resources.IsValid(OutError);
}

bool FDWCEditorOperationPhaseGraph::AddPhase(
    FDWCEditorOperationPhaseDescriptor Descriptor,
    FString* OutError)
{
    check(IsInGameThread());
    if (!Descriptor.IsValid(OutError) || Phases.Contains(Descriptor.Name))
    {
        if (OutError != nullptr && OutError->IsEmpty())
        {
            *OutError = TEXT("The operation phase name is already registered.");
        }
        return false;
    }
    const FName Name = Descriptor.Name;
    FPhaseState& State = Phases.Add(Name);
    State.Descriptor = MoveTemp(Descriptor);
    State.State = State.Descriptor.Dependencies.IsEmpty()
        ? EDWCEditorOperationPhaseState::WaitingAdmission
        : EDWCEditorOperationPhaseState::WaitingDependencies;
    InsertionOrder.Add(Name);
    return true;
}

bool FDWCEditorOperationPhaseGraph::Validate(FString* OutError) const
{
    check(IsInGameThread());
    if (Phases.IsEmpty())
    {
        if (OutError != nullptr) *OutError = TEXT("An operation phase graph must contain at least one phase.");
        return false;
    }
    TMap<FName, int32> InDegree;
    TMap<FName, TArray<FName>> Dependents;
    for (const TPair<FName, FPhaseState>& Pair : Phases)
    {
        InDegree.Add(Pair.Key, Pair.Value.Descriptor.Dependencies.Num());
        for (const FName Dependency : Pair.Value.Descriptor.Dependencies)
        {
            if (!Phases.Contains(Dependency))
            {
                if (OutError != nullptr)
                {
                    *OutError = FString::Printf(
                        TEXT("Operation phase '%s' references missing dependency '%s'."),
                        *Pair.Key.ToString(), *Dependency.ToString());
                }
                return false;
            }
            Dependents.FindOrAdd(Dependency).Add(Pair.Key);
        }
    }
    TArray<FName> Ready;
    for (const TPair<FName, int32>& Pair : InDegree)
    {
        if (Pair.Value == 0) Ready.Add(Pair.Key);
    }
    int32 Visited = 0;
    while (!Ready.IsEmpty())
    {
        const FName Name = Ready.Pop(EAllowShrinking::No);
        ++Visited;
        for (const FName Dependent : Dependents.FindRef(Name))
        {
            int32& Degree = InDegree.FindChecked(Dependent);
            if (--Degree == 0) Ready.Add(Dependent);
        }
    }
    if (Visited != Phases.Num())
    {
        if (OutError != nullptr) *OutError = TEXT("The operation phase graph contains a dependency cycle.");
        return false;
    }
    return true;
}

bool FDWCEditorOperationPhaseGraph::UpdateResourcePlan(
    const FName PhaseName,
    FDWCEditorOperationPhaseResourcePlan ResourcePlan,
    FString* OutError)
{
    check(IsInGameThread());
    FPhaseState* Phase = Phases.Find(PhaseName);
    if (Phase == nullptr || Phase->State == EDWCEditorOperationPhaseState::Running ||
        Phase->State == EDWCEditorOperationPhaseState::Retiring ||
        Phase->State == EDWCEditorOperationPhaseState::Completed)
    {
        if (OutError != nullptr) *OutError = TEXT("The phase resource plan can only change before execution.");
        return false;
    }
    if (!ResourcePlan.IsValid(OutError))
    {
        return false;
    }
    Phase->Descriptor.Resources = MoveTemp(ResourcePlan);
    return true;
}

bool FDWCEditorOperationPhaseGraph::AreDependenciesComplete(const FPhaseState& Phase) const
{
    for (const FName Dependency : Phase.Descriptor.Dependencies)
    {
        const FPhaseState* DependencyState = Phases.Find(Dependency);
        if (DependencyState == nullptr ||
            DependencyState->State != EDWCEditorOperationPhaseState::Completed)
        {
            return false;
        }
    }
    return true;
}

TArray<FName> FDWCEditorOperationPhaseGraph::GetReadyPhases() const
{
    check(IsInGameThread());
    TArray<FName> Result;
    if (bCanceled) return Result;
    for (const FName Name : InsertionOrder)
    {
        const FPhaseState& Phase = Phases.FindChecked(Name);
        if ((Phase.State == EDWCEditorOperationPhaseState::WaitingDependencies ||
             Phase.State == EDWCEditorOperationPhaseState::WaitingAdmission) &&
            AreDependenciesComplete(Phase))
        {
            Result.Add(Name);
        }
    }
    return Result;
}

bool FDWCEditorOperationPhaseGraph::Transition(
    const FName PhaseName,
    const EDWCEditorOperationPhaseState Expected,
    const EDWCEditorOperationPhaseState Next,
    FString* OutError)
{
    FPhaseState* Phase = Phases.Find(PhaseName);
    if (Phase == nullptr || Phase->State != Expected)
    {
        if (OutError != nullptr)
        {
            *OutError = FString::Printf(TEXT("Operation phase '%s' is not in the expected state."),
                *PhaseName.ToString());
        }
        return false;
    }
    Phase->State = Next;
    return true;
}

bool FDWCEditorOperationPhaseGraph::MarkWaitingAdmission(FName PhaseName, FString* OutError)
{
    check(IsInGameThread());
    FPhaseState* Phase = Phases.Find(PhaseName);
    if (Phase == nullptr || !AreDependenciesComplete(*Phase) ||
        (Phase->State != EDWCEditorOperationPhaseState::WaitingDependencies &&
         Phase->State != EDWCEditorOperationPhaseState::WaitingAdmission))
    {
        if (OutError != nullptr) *OutError = TEXT("The phase dependencies are not complete.");
        return false;
    }
    Phase->State = EDWCEditorOperationPhaseState::WaitingAdmission;
    if (Phase->AdmissionStartedSeconds == 0.0)
    {
        Phase->AdmissionStartedSeconds = FPlatformTime::Seconds();
    }
    return true;
}

bool FDWCEditorOperationPhaseGraph::MarkRunning(FName PhaseName, FString* OutError)
{
    check(IsInGameThread());
    if (!MarkWaitingAdmission(PhaseName, OutError)) return false;
    FPhaseState& Phase = Phases.FindChecked(PhaseName);
    const double Now = FPlatformTime::Seconds();
    Phase.AdmissionWaitSeconds += Phase.AdmissionStartedSeconds > 0.0
        ? FMath::Max(Now - Phase.AdmissionStartedSeconds, 0.0) : 0.0;
    Phase.AdmissionStartedSeconds = 0.0;
    Phase.RunStartedSeconds = Now;
    Phase.State = EDWCEditorOperationPhaseState::Running;
    return true;
}

bool FDWCEditorOperationPhaseGraph::MarkRetiring(FName PhaseName, FString* OutError)
{
    check(IsInGameThread());
    FPhaseState* Phase = Phases.Find(PhaseName);
    if (Phase == nullptr || Phase->State != EDWCEditorOperationPhaseState::Running)
    {
        if (OutError != nullptr) *OutError = TEXT("Only a running phase can retire.");
        return false;
    }
    Phase->RunSeconds += FMath::Max(FPlatformTime::Seconds() - Phase->RunStartedSeconds, 0.0);
    Phase->RunStartedSeconds = 0.0;
    Phase->State = EDWCEditorOperationPhaseState::Retiring;
    return true;
}

bool FDWCEditorOperationPhaseGraph::MarkCompleted(FName PhaseName, FString* OutError)
{
    check(IsInGameThread());
    FPhaseState* Phase = Phases.Find(PhaseName);
    if (Phase == nullptr || (Phase->State != EDWCEditorOperationPhaseState::Running &&
                             Phase->State != EDWCEditorOperationPhaseState::Retiring))
    {
        if (OutError != nullptr) *OutError = TEXT("Only a running or retiring phase can complete.");
        return false;
    }
    if (Phase->State == EDWCEditorOperationPhaseState::Running)
    {
        Phase->RunSeconds += FMath::Max(FPlatformTime::Seconds() - Phase->RunStartedSeconds, 0.0);
        Phase->RunStartedSeconds = 0.0;
    }
    Phase->State = EDWCEditorOperationPhaseState::Completed;
    return true;
}

bool FDWCEditorOperationPhaseGraph::MarkFailed(
    FName PhaseName,
    const FString& Error,
    FString* OutError)
{
    check(IsInGameThread());
    FPhaseState* Phase = Phases.Find(PhaseName);
    if (Phase == nullptr || Phase->State == EDWCEditorOperationPhaseState::Completed)
    {
        if (OutError != nullptr) *OutError = TEXT("A missing or completed phase cannot fail.");
        return false;
    }
    if (Phase->State == EDWCEditorOperationPhaseState::Running)
    {
        Phase->RunSeconds += FMath::Max(FPlatformTime::Seconds() - Phase->RunStartedSeconds, 0.0);
        Phase->RunStartedSeconds = 0.0;
    }
    Phase->State = EDWCEditorOperationPhaseState::Failed;
    Phase->Error = Error;
    return true;
}

void FDWCEditorOperationPhaseGraph::CancelOutstanding(const FString& Reason)
{
    check(IsInGameThread());
    bCanceled = true;
    for (TPair<FName, FPhaseState>& Pair : Phases)
    {
        FPhaseState& Phase = Pair.Value;
        if (Phase.State != EDWCEditorOperationPhaseState::Completed &&
            Phase.State != EDWCEditorOperationPhaseState::Failed)
        {
            Phase.State = EDWCEditorOperationPhaseState::Canceled;
            Phase.Error = Reason;
        }
    }
}

const FDWCEditorOperationPhaseDescriptor* FDWCEditorOperationPhaseGraph::FindDescriptor(
    const FName PhaseName) const
{
    const FPhaseState* Phase = Phases.Find(PhaseName);
    return Phase != nullptr ? &Phase->Descriptor : nullptr;
}

EDWCEditorOperationPhaseState FDWCEditorOperationPhaseGraph::GetState(const FName PhaseName) const
{
    const FPhaseState* Phase = Phases.Find(PhaseName);
    return Phase != nullptr ? Phase->State : EDWCEditorOperationPhaseState::Failed;
}

bool FDWCEditorOperationPhaseGraph::IsTerminal() const
{
    if (Phases.IsEmpty()) return false;
    for (const TPair<FName, FPhaseState>& Pair : Phases)
    {
        const EDWCEditorOperationPhaseState State = Pair.Value.State;
        if (State != EDWCEditorOperationPhaseState::Completed &&
            State != EDWCEditorOperationPhaseState::Failed &&
            State != EDWCEditorOperationPhaseState::Canceled)
        {
            return false;
        }
    }
    return true;
}

FDWCEditorOperationPhaseGraphSnapshot FDWCEditorOperationPhaseGraph::GetSnapshot() const
{
    FDWCEditorOperationPhaseGraphSnapshot Result;
    Result.bCanceled = bCanceled;
    Result.bTerminal = IsTerminal();
    for (const FName Name : InsertionOrder)
    {
        const FPhaseState& Phase = Phases.FindChecked(Name);
        FDWCEditorOperationPhaseSnapshot& Snapshot = Result.Phases.AddDefaulted_GetRef();
        Snapshot.Name = Name;
        Snapshot.Thread = Phase.Descriptor.Thread;
        Snapshot.State = Phase.State;
        Snapshot.Resources = Phase.Descriptor.Resources;
        Snapshot.AdmissionWaitSeconds = Phase.AdmissionWaitSeconds;
        Snapshot.RunSeconds = Phase.RunSeconds;
        Snapshot.Error = Phase.Error;
    }
    return Result;
}
