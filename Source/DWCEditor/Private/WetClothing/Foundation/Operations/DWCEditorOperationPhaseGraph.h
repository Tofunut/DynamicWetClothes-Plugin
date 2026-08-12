// Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Operations/DWCEditorOperationPhaseTypes.h"

/** Game-thread state authority for one validated resource-aware operation DAG. */
class FDWCEditorOperationPhaseGraph
{
public:
    bool AddPhase(FDWCEditorOperationPhaseDescriptor Descriptor, FString* OutError = nullptr);
    bool Validate(FString* OutError = nullptr) const;
    bool UpdateResourcePlan(
        FName PhaseName,
        FDWCEditorOperationPhaseResourcePlan ResourcePlan,
        FString* OutError = nullptr);

    TArray<FName> GetReadyPhases() const;
    bool MarkWaitingAdmission(FName PhaseName, FString* OutError = nullptr);
    bool MarkRunning(FName PhaseName, FString* OutError = nullptr);
    bool MarkRetiring(FName PhaseName, FString* OutError = nullptr);
    bool MarkCompleted(FName PhaseName, FString* OutError = nullptr);
    bool MarkFailed(FName PhaseName, const FString& Error, FString* OutError = nullptr);
    void CancelOutstanding(const FString& Reason = FString());

    const FDWCEditorOperationPhaseDescriptor* FindDescriptor(FName PhaseName) const;
    EDWCEditorOperationPhaseState GetState(FName PhaseName) const;
    bool IsTerminal() const;
    FDWCEditorOperationPhaseGraphSnapshot GetSnapshot() const;

private:
    struct FPhaseState
    {
        FDWCEditorOperationPhaseDescriptor Descriptor;
        EDWCEditorOperationPhaseState State = EDWCEditorOperationPhaseState::WaitingDependencies;
        double AdmissionStartedSeconds = 0.0;
        double AdmissionWaitSeconds = 0.0;
        double RunStartedSeconds = 0.0;
        double RunSeconds = 0.0;
        FString Error;
    };

    bool AreDependenciesComplete(const FPhaseState& Phase) const;
    bool Transition(
        FName PhaseName,
        EDWCEditorOperationPhaseState Expected,
        EDWCEditorOperationPhaseState Next,
        FString* OutError);

    TMap<FName, FPhaseState> Phases;
    TArray<FName> InsertionOrder;
    bool bCanceled = false;
};
