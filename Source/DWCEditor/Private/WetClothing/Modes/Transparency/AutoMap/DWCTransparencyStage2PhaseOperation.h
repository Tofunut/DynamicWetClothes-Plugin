// Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Async/DWCEditorResourceGovernor.h"
#include "WetClothing/Foundation/Operations/DWCEditorOperationPhaseGraph.h"

enum class EDWCTransparencyStage2OperationPhase : uint8
{
    PrepareTarget,
    RasterizeTarget,
    PrepareSources,
    StreamProjection,
    ComposeResult,
    TransferResult
};

struct FDWCTransparencyStage2PhaseResources
{
    uint64 WorkerPeakBytes = 0;
    uint64 WorkerRetainedBytes = 0;
    uint64 PreviewPeakBytes = 0;
    uint64 PreviewRetainedBytes = 0;
};

/**
 * Serial resource-aware lifetime for Transparency Stage 2. The phase graph is
 * the state authority; leases represent the buffers that are actually live at
 * each boundary. A caller-owned reservation can disable local admission while
 * retaining the same phase and cancellation contract.
 */
class FDWCTransparencyStage2PhaseOperation final
{
public:
    FDWCTransparencyStage2PhaseOperation(
        TSharedPtr<FDWCEditorResourceGovernor> InResourceGovernor,
        FDWCEditorAsyncOperationIdentity InIdentity,
        bool bInResourcesOwnedByCaller);

    bool BeginPhase(
        EDWCTransparencyStage2OperationPhase Phase,
        const FDWCTransparencyStage2PhaseResources& Resources,
        FString& OutError);
    bool Complete(FString& OutError);
    void Fail(const FString& Error);
    void Cancel(const FString& Reason);

    FDWCEditorMemoryLease TakeRetainedLease(EDWCEditorResourcePool Pool);
    FDWCEditorOperationPhaseGraphSnapshot GetSnapshot() const;

    static FName GetPhaseName(EDWCTransparencyStage2OperationPhase Phase);

private:
    bool FinishCurrentPhase(FString& OutError);
    bool ResizePool(
        EDWCEditorResourcePool Pool,
        uint64 Bytes,
        const FString& DebugName,
        FString& OutError);
    void ReleasePoolsNotIn(const FDWCTransparencyStage2PhaseResources& Resources, bool bRetained);

    TSharedPtr<FDWCEditorResourceGovernor> ResourceGovernor;
    FDWCEditorAsyncOperationIdentity Identity;
    FDWCEditorOperationPhaseGraph Graph;
    TMap<EDWCEditorResourcePool, TUniquePtr<FDWCEditorMemoryLease>> Leases;
    TOptional<EDWCTransparencyStage2OperationPhase> CurrentPhase;
    FDWCTransparencyStage2PhaseResources CurrentResources;
    bool bResourcesOwnedByCaller = false;
    bool bTerminal = false;
};
