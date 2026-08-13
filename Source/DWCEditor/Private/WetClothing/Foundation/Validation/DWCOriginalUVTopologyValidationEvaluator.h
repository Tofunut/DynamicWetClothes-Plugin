// Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

struct FDWCEditorValidationEvaluationContext;
struct FWCAEditorValidationSnapshot;

/** Owns validation and build failure presentation for the saved Original UV topology. */
class FDWCOriginalUVTopologyValidationEvaluator
{
  public:
    static void AppendToSnapshot(
        const FDWCEditorValidationEvaluationContext& Context,
        FWCAEditorValidationSnapshot& InOutSnapshot);
};
