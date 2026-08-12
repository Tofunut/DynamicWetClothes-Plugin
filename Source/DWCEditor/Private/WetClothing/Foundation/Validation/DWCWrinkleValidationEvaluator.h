// Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

class UWetClothingAsset;
struct FWCAEditorValidationSnapshot;

class FDWCWrinkleValidationEvaluator
{
public:
    static void AppendToSnapshot(
        const UWetClothingAsset& Asset,
        bool bDeepValidation,
        FWCAEditorValidationSnapshot& InOutSnapshot);
};
