// Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

class UWetClothingAsset;
struct FDWCEditorValidationEvaluationContext;
struct FWCAEditorValidationSnapshot;

/** Canonical asset, persistence, Data UV, and unowned failure validation. */
class FDWCAssetValidationEvaluator
{
public:
    static void AppendAssetAndDataUV(
        const FDWCEditorValidationEvaluationContext& Context,
        FWCAEditorValidationSnapshot& InOutSnapshot);

    static void AppendUnownedFailure(
        const UWetClothingAsset& Asset,
        FWCAEditorValidationSnapshot& InOutSnapshot);
};
