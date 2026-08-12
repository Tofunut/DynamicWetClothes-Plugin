// Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

class USkeletalMesh;
class UWetClothingAsset;
struct FDWCWetClothingAssetSetupSettings;

/** Read-only facts shared by canonical WCA validation evaluators. */
struct FDWCEditorValidationEvaluationContext
{
    explicit FDWCEditorValidationEvaluationContext(
        const UWetClothingAsset& InAsset,
        bool bInDeepValidation);

    const UWetClothingAsset& Asset;
    const FDWCWetClothingAssetSetupSettings& Setup;
    USkeletalMesh* RuntimeMesh = nullptr;
    bool bDeepValidation = false;
    bool bAssetDirty = false;
    bool bHasWettableSlots = false;
    bool bCPUBackendEnabled = false;
    bool bGPUBackendEnabled = false;
    bool bDataUVReady = false;
    bool bOriginalUVTopologyReady = false;
    int32 RuntimeLODIndex = 0;
    TArray<int32> WettableMaterialSlotIndices;

    bool IsBakeOutputSavePending(int32 OutputMask) const;
};
