// Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

class UWetClothingAsset;
struct FWCAEditorValidationSnapshot;

class FDWCTransparencyCookValidationEvaluator
{
public:
    static void AppendToSnapshot(
        const UWetClothingAsset& Asset,
        FWCAEditorValidationSnapshot& InOutSnapshot);
};
