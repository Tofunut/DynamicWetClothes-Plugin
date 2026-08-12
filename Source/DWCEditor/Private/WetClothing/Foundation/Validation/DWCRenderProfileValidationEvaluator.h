// Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

class UWetClothingAsset;
struct FWCAEditorValidationSnapshot;

/** Converts the structured Render Profile service state into canonical validation nodes. */
class FDWCRenderProfileValidationEvaluator
{
public:
    static void AppendToSnapshot(
        const UWetClothingAsset& Asset,
        FWCAEditorValidationSnapshot& InOutSnapshot);
};
