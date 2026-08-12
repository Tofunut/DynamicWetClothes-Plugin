// Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Foundation/Validation/DWCEditorValidationEvaluatorUtils.h"

namespace DWCEditorValidation
{
FDWCEditorValidationNode& FindOrAddNode(
    FWCAEditorValidationSnapshot& Snapshot,
    const FDWCEditorValidationTargetKey& Key)
{
    if (FDWCEditorValidationNode* Existing = Snapshot.Nodes.FindByPredicate(
            [&Key](const FDWCEditorValidationNode& Node) { return Node.Key == Key; }))
    {
        return *Existing;
    }
    FDWCEditorValidationNode& Added = Snapshot.Nodes.AddDefaulted_GetRef();
    Added.Key = Key;
    return Added;
}

void SetActionState(
    FWCAEditorValidationSnapshot& Snapshot,
    const EDWCEditorBuildAction Action,
    const EDWCEditorBuildActionState State,
    const FDWCEditorValidationTargetKey* Target,
    const TConstArrayView<EDWCEditorBuildAction> BlockingActions)
{
    FDWCEditorValidationActionState* Existing = Snapshot.Actions.Find(Action);
    if (Existing == nullptr)
    {
        FDWCEditorValidationActionState& Added = Snapshot.Actions.Add(Action);
        Added.Action = Action;
        Added.State = State;
        Existing = &Added;
    }
    FDWCEditorValidationActionState& ActionState = *Existing;
    auto Rank = [](const EDWCEditorBuildActionState Candidate)
    {
        switch (Candidate)
        {
        case EDWCEditorBuildActionState::Failed: return 5;
        case EDWCEditorBuildActionState::Blocked: return 4;
        case EDWCEditorBuildActionState::Required: return 3;
        case EDWCEditorBuildActionState::Running: return 2;
        case EDWCEditorBuildActionState::UpToDate: return 1;
        case EDWCEditorBuildActionState::Unavailable:
        default: return 0;
        }
    };
    if (Rank(State) >= Rank(ActionState.State))
    {
        ActionState.State = State;
    }
    if (Target != nullptr)
    {
        ActionState.Targets.AddUnique(*Target);
    }
    for (const EDWCEditorBuildAction BlockingAction : BlockingActions)
    {
        ActionState.BlockingActions.AddUnique(BlockingAction);
    }
}

void AddDiagnostic(
    FWCAEditorValidationSnapshot& Snapshot,
    FDWCEditorValidationNode& Node,
    const FName Code,
    const EDWCEditorValidationSeverity Severity,
    const FText& Title,
    const FText& Status,
    const FText& Detail,
    const FText& RequiredAction,
    const EDWCEditorValidationRemediation Remediation,
    const TOptional<EDWCEditorBuildAction> SuggestedAction,
    const bool bFailed,
    const FText& ContextLabel)
{
    FDWCEditorValidationDiagnostic& Diagnostic = Snapshot.Diagnostics.AddDefaulted_GetRef();
    Diagnostic.Code = Code;
    Diagnostic.Severity = Severity;
    Diagnostic.Target = Node.Key;
    Diagnostic.Remediation = Remediation;
    Diagnostic.SuggestedAction = SuggestedAction;
    Diagnostic.Presentation.Title = Title;
    Diagnostic.Presentation.Status = Status;
    Diagnostic.Presentation.Detail = Detail;
    Diagnostic.Presentation.RequiredAction = RequiredAction;
    Diagnostic.Presentation.ContextLabel = ContextLabel;
    Diagnostic.bFailed = bFailed;
    Node.DiagnosticIndices.Add(Snapshot.Diagnostics.Num() - 1);

    if (SuggestedAction.IsSet())
    {
        SetActionState(
            Snapshot,
            SuggestedAction.GetValue(),
            bFailed ? EDWCEditorBuildActionState::Failed : EDWCEditorBuildActionState::Required,
            &Node.Key);
    }
}
}
