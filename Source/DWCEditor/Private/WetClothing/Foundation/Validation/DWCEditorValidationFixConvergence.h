// Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Build/DWCEditorBuildActionTypes.h"

struct FWCAEditorValidationSnapshot;

enum class EDWCEditorValidationFixDecision : uint8
{
    Complete,
    ExecuteStep,
    Blocked,
    NoProgress,
    IterationLimit
};

struct FDWCEditorValidationFixDecisionResult
{
    EDWCEditorValidationFixDecision Decision = EDWCEditorValidationFixDecision::Complete;
    TOptional<FDWCEditorBuildPlanStep> Step;
    FString Failure;
};

/**
 * Pure convergence policy shared by the automatic-fix workflow and its tests.
 * Presentation strings are deliberately excluded from the state fingerprint.
 */
class FDWCEditorValidationFixConvergence
{
  public:
    explicit FDWCEditorValidationFixConvergence(
        int32 InMaxObservations = 32,
        int32 InMaxIdenticalObservations = 3);

    FDWCEditorValidationFixDecisionResult Observe(
        const FWCAEditorValidationSnapshot& Validation,
        const FDWCEditorBuildStatusSnapshot& BuildStatus,
        const FDWCEditorBuildPlan& Plan);

    static FString BuildStateFingerprint(
        const FWCAEditorValidationSnapshot& Validation,
        const FDWCEditorBuildStatusSnapshot& BuildStatus,
        const FDWCEditorBuildPlan& Plan);

  private:
    int32 MaxObservations = 32;
    int32 MaxIdenticalObservations = 3;
    int32 ObservationCount = 0;
    int32 IdenticalObservationCount = 0;
    FString LastFingerprint;
};
