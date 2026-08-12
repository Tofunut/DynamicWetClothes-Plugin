// Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

struct FDWCEditorValidationEvaluationContext;
struct FWCAEditorValidationSnapshot;

/** Evaluates CPU data, GPU runtime data, and GPU map payloads from one read-only context. */
class FDWCRuntimeValidationEvaluator
{
  public:
    static void AppendToSnapshot(
        const FDWCEditorValidationEvaluationContext& Context,
        FWCAEditorValidationSnapshot& InOutSnapshot);
};
