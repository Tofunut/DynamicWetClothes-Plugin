//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Build/DWCEditorBuildActionTypes.h"

class FDWCEditorBuildPlanResolver
{
  public:
    static FDWCEditorBuildPlan ResolveRequired(const FDWCEditorBuildStatusSnapshot& Snapshot);
    static FDWCEditorBuildPlan ResolveActions(
        const FDWCEditorBuildStatusSnapshot& Snapshot,
        TConstArrayView<EDWCEditorBuildAction> RequestedActions);
};

