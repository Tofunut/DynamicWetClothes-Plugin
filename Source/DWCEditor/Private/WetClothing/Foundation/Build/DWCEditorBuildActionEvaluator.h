//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Build/DWCEditorBuildActionTypes.h"

class FDWCEditorBuildActionEvaluator
{
  public:
    /** Captures only cheap WCA state. Service-owned probes remain explicit. */
    static FDWCEditorBuildEvaluationInput CaptureAssetState(
        const UWetClothingAsset& Asset,
        EDWCEditorBuildSurfaceMode SurfaceMode,
        FDWCEditorBuildEvaluationInput ServiceState = {});

    static FDWCEditorBuildStatusSnapshot Evaluate(const FDWCEditorBuildEvaluationInput& Input);
};

