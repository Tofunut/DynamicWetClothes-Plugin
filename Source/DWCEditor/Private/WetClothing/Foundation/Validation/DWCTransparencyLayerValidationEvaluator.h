// Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

struct FWCAEditorValidationSnapshot;
class UWetClothingAsset;

/** Builds canonical, side-effect-free validation state for every Transparency target slot. */
class FDWCTransparencyLayerValidationEvaluator
{
  public:
    static void AppendToSnapshot(
        const UWetClothingAsset& Asset,
        bool bDeepValidation,
        FWCAEditorValidationSnapshot& InOutSnapshot);
};
