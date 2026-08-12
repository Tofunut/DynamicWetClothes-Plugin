// Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetClothingAssetSetupData.h"
#include "WetClothing/Foundation/Validation/DWCEditorValidationTypes.h"

struct FDWCEditorValidationPresentation
{
    FText Title;
    FText Status;
    FText Detail;
    FText RequiredAction;
    FText ContextLabel;
};

struct FDWCEditorValidationDiagnostic
{
    FName Code;
    EDWCEditorValidationSeverity Severity = EDWCEditorValidationSeverity::Info;
    FDWCEditorValidationTargetKey Target;
    EDWCEditorValidationRemediation Remediation = EDWCEditorValidationRemediation::None;
    TOptional<EDWCEditorBuildAction> SuggestedAction;
    TArray<EDWCEditorBuildAction> BlockingActions;
    FDWCEditorValidationPresentation Presentation;
    bool bFailed = false;
};

struct FDWCEditorValidationNode
{
    FDWCEditorValidationTargetKey Key;
    EDWCEditorValidationIntentState Intent = EDWCEditorValidationIntentState::Enabled;
    EDWCEditorValidationInputState Input = EDWCEditorValidationInputState::Valid;
    EDWCEditorValidationDependencyState Dependency = EDWCEditorValidationDependencyState::Ready;
    EDWCEditorValidationArtifactState Artifact = EDWCEditorValidationArtifactState::Current;
    EDWCEditorValidationPersistenceState Persistence = EDWCEditorValidationPersistenceState::Saved;
    EDWCEditorValidationOperationState Operation = EDWCEditorValidationOperationState::Idle;
    TArray<int32> DiagnosticIndices;

    EDWCEditorValidationOverallState GetOverallState() const;
    bool RequiresRuntimeOutput() const;
};

struct FDWCEditorValidationActionState
{
    EDWCEditorBuildAction Action = EDWCEditorBuildAction::SaveAsset;
    EDWCEditorBuildActionState State = EDWCEditorBuildActionState::UpToDate;
    TArray<FDWCEditorValidationTargetKey> Targets;
    TArray<EDWCEditorBuildAction> BlockingActions;
};

struct FWCAEditorValidationSnapshot
{
    FString AssetPath;
    bool bAssetDirty = false;
    bool bDeepValidation = false;
    TArray<FDWCEditorValidationNode> Nodes;
    TArray<FDWCEditorValidationDiagnostic> Diagnostics;
    TMap<EDWCEditorBuildAction, FDWCEditorValidationActionState> Actions;
    FDWCTriangleValidationSummary TriangleDiagnostics;

    const FDWCEditorValidationNode* FindNode(const FDWCEditorValidationTargetKey& Key) const;
    TArray<const FDWCEditorValidationNode*> FindMaterialSlotNodes(int32 MaterialSlotIndex) const;
    const FDWCEditorValidationActionState* FindAction(EDWCEditorBuildAction Action) const;
    bool HasBlockingErrors() const;
};
