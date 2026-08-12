// Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Async/DWCEditorAsyncOperationTypes.h"

enum class EDWCEditorOperationPhaseThread : uint8
{
    GameThread,
    WorkerThread,
    RenderThread
};

enum class EDWCEditorOperationPhaseState : uint8
{
    WaitingDependencies,
    WaitingAdmission,
    Running,
    Retiring,
    Completed,
    Failed,
    Canceled
};

struct FDWCEditorOperationPhaseResourceDemand
{
    EDWCEditorResourcePool Pool = EDWCEditorResourcePool::WorkerPrivateCPU;
    uint64 Bytes = 0;
};

/** Peak phase ownership and the subset that remains live after the phase finishes. */
struct FDWCEditorOperationPhaseResourcePlan
{
    TArray<FDWCEditorOperationPhaseResourceDemand> Peak;
    TArray<FDWCEditorOperationPhaseResourceDemand> Retained;

    void AddPeak(EDWCEditorResourcePool Pool, uint64 Bytes);
    void AddRetained(EDWCEditorResourcePool Pool, uint64 Bytes);
    uint64 GetPeakBytes(EDWCEditorResourcePool Pool) const;
    uint64 GetRetainedBytes(EDWCEditorResourcePool Pool) const;
    uint64 GetTotalPeakBytes() const;
    uint64 GetTotalRetainedBytes() const;
    bool IsValid(FString* OutError = nullptr) const;
};

struct FDWCEditorOperationPhaseDescriptor
{
    FName Name;
    TArray<FName> Dependencies;
    EDWCEditorOperationPhaseThread Thread = EDWCEditorOperationPhaseThread::GameThread;
    FDWCEditorOperationPhaseResourcePlan Resources;
    FString DebugName;

    bool IsValid(FString* OutError = nullptr) const;
};

struct FDWCEditorOperationPhaseSnapshot
{
    FName Name;
    EDWCEditorOperationPhaseThread Thread = EDWCEditorOperationPhaseThread::GameThread;
    EDWCEditorOperationPhaseState State = EDWCEditorOperationPhaseState::WaitingDependencies;
    FDWCEditorOperationPhaseResourcePlan Resources;
    double AdmissionWaitSeconds = 0.0;
    double RunSeconds = 0.0;
    FString Error;
};

struct FDWCEditorOperationPhaseGraphSnapshot
{
    TArray<FDWCEditorOperationPhaseSnapshot> Phases;
    bool bCanceled = false;
    bool bTerminal = false;
};
