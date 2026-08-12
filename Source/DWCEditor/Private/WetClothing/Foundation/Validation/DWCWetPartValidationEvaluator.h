// Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

struct FDWCEditorValidationEvaluationContext;
struct FWCAEditorValidationSnapshot;

/** Validates authored Wet Part ownership and topology without modifying the WCA. */
class FDWCWetPartValidationEvaluator
{
  public:
    static void AppendToSnapshot(
        const FDWCEditorValidationEvaluationContext& Context,
        FWCAEditorValidationSnapshot& InOutSnapshot);
};
