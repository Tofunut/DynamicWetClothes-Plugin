// Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

struct FDWCEditorValidationEvaluationContext;
struct FWCAEditorValidationSnapshot;

/** Builds per-slot generated-material validation from structured generator results. */
class FDWCGeneratedMaterialValidationEvaluator
{
  public:
    static void AppendToSnapshot(
        const FDWCEditorValidationEvaluationContext& Context,
        FWCAEditorValidationSnapshot& InOutSnapshot);
};
