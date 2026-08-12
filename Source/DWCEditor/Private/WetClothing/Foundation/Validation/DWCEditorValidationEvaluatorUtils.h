// Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Validation/DWCEditorValidationSnapshot.h"

namespace DWCEditorValidation
{
FDWCEditorValidationNode& FindOrAddNode(
    FWCAEditorValidationSnapshot& Snapshot,
    const FDWCEditorValidationTargetKey& Key);

void SetActionState(
    FWCAEditorValidationSnapshot& Snapshot,
    EDWCEditorBuildAction Action,
    EDWCEditorBuildActionState State,
    const FDWCEditorValidationTargetKey* Target = nullptr,
    TConstArrayView<EDWCEditorBuildAction> BlockingActions = {});

void AddDiagnostic(
    FWCAEditorValidationSnapshot& Snapshot,
    FDWCEditorValidationNode& Node,
    FName Code,
    EDWCEditorValidationSeverity Severity,
    const FText& Title,
    const FText& Status,
    const FText& Detail,
    const FText& RequiredAction,
    EDWCEditorValidationRemediation Remediation,
    TOptional<EDWCEditorBuildAction> SuggestedAction = {},
    bool bFailed = false,
    const FText& ContextLabel = FText::GetEmpty());
}
