// Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Foundation/Validation/DWCEditorValidationSnapshot.h"

bool FDWCEditorValidationNode::RequiresRuntimeOutput() const
{
    return Intent == EDWCEditorValidationIntentState::Enabled;
}

EDWCEditorValidationOverallState FDWCEditorValidationNode::GetOverallState() const
{
    if (Operation == EDWCEditorValidationOperationState::Failed)
    {
        return EDWCEditorValidationOverallState::Failed;
    }
    if (Operation == EDWCEditorValidationOperationState::Cancelled)
    {
        return EDWCEditorValidationOverallState::Cancelled;
    }
    if (Operation == EDWCEditorValidationOperationState::Running)
    {
        return EDWCEditorValidationOverallState::Running;
    }

    switch (Intent)
    {
    case EDWCEditorValidationIntentState::NotApplicable:
        return EDWCEditorValidationOverallState::NotApplicable;
    case EDWCEditorValidationIntentState::NotConfigured:
        return EDWCEditorValidationOverallState::NotConfigured;
    case EDWCEditorValidationIntentState::Draft:
        return EDWCEditorValidationOverallState::Draft;
    case EDWCEditorValidationIntentState::Disabled:
        return EDWCEditorValidationOverallState::Disabled;
    case EDWCEditorValidationIntentState::Enabled:
    default:
        break;
    }

    if (Dependency == EDWCEditorValidationDependencyState::Blocked)
    {
        return EDWCEditorValidationOverallState::Blocked;
    }
    if (Input == EDWCEditorValidationInputState::Invalid || Artifact == EDWCEditorValidationArtifactState::Invalid)
    {
        return EDWCEditorValidationOverallState::Invalid;
    }
    if (Input == EDWCEditorValidationInputState::Missing || Artifact == EDWCEditorValidationArtifactState::Missing)
    {
        return EDWCEditorValidationOverallState::Missing;
    }
    if (Artifact == EDWCEditorValidationArtifactState::Stale)
    {
        return EDWCEditorValidationOverallState::Stale;
    }
    if (Artifact == EDWCEditorValidationArtifactState::Partial)
    {
        return EDWCEditorValidationOverallState::Partial;
    }
    if (Persistence == EDWCEditorValidationPersistenceState::SavePending)
    {
        return EDWCEditorValidationOverallState::SavePending;
    }
    return EDWCEditorValidationOverallState::Current;
}

const FDWCEditorValidationNode* FWCAEditorValidationSnapshot::FindNode(
    const FDWCEditorValidationTargetKey& Key) const
{
    return Nodes.FindByPredicate([&Key](const FDWCEditorValidationNode& Node) { return Node.Key == Key; });
}

TArray<const FDWCEditorValidationNode*> FWCAEditorValidationSnapshot::FindMaterialSlotNodes(
    const int32 MaterialSlotIndex) const
{
    TArray<const FDWCEditorValidationNode*> Result;
    for (const FDWCEditorValidationNode& Node : Nodes)
    {
        if (Node.Key.MaterialSlotIndex == MaterialSlotIndex)
        {
            Result.Add(&Node);
        }
    }
    return Result;
}

const FDWCEditorValidationActionState* FWCAEditorValidationSnapshot::FindAction(
    const EDWCEditorBuildAction Action) const
{
    return Actions.Find(Action);
}

bool FWCAEditorValidationSnapshot::HasBlockingErrors() const
{
    return Diagnostics.ContainsByPredicate(
        [](const FDWCEditorValidationDiagnostic& Diagnostic)
        {
            return Diagnostic.Severity == EDWCEditorValidationSeverity::Error;
        });
}
