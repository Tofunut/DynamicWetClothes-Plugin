//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Build/DWCEditorBuildActionTypes.h"

struct FDWCEditorValidationEvaluationContext;

class FDWCEditorBuildActionEvaluator
{
  public:
    /** Captures WCA state without exceeding the validation context's access policy. */
    static FDWCEditorBuildEvaluationInput CaptureAssetState(
        const FDWCEditorValidationEvaluationContext& Context,
        EDWCEditorBuildSurfaceMode SurfaceMode,
        FDWCEditorBuildEvaluationInput ServiceState = {});

    static FDWCEditorBuildStatusSnapshot Evaluate(const FDWCEditorBuildEvaluationInput& Input);
};
