// Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Validation/DWCEditorValidationTypes.h"

class USkeletalMesh;
class UWetClothingAsset;
struct FDWCWetClothingAssetSetupSettings;

/** Read-only facts shared by canonical WCA validation evaluators. */
struct FDWCEditorValidationEvaluationContext
{
    explicit FDWCEditorValidationEvaluationContext(
        const UWetClothingAsset& InAsset,
        EDWCEditorValidationAccess InAccess);

    const UWetClothingAsset& Asset;
    const FDWCWetClothingAssetSetupSettings& Setup;
    USkeletalMesh* RuntimeMesh = nullptr;
    EDWCEditorValidationAccess Access = EDWCEditorValidationAccess::MetadataOnly;
    bool bDeepValidation = false;
    bool bAssetDirty = false;
    bool bHasWettableSlots = false;
    bool bCPUBackendEnabled = false;
    bool bGPUBackendEnabled = false;
    bool bDataUVReady = false;
    bool bOriginalUVTopologyReady = false;
    bool bHasOriginalUVTopologyPayload = false;
    bool bOriginalUVTopologyMetadataValid = false;
    bool bOriginalUVTopologySignatureCurrent = false;
    int32 RuntimeLODIndex = 0;
    TArray<int32> WettableMaterialSlotIndices;

    bool AllowsPayloadAccess() const
    {
        return Access == EDWCEditorValidationAccess::ExactPayload;
    }

    bool IsBakeOutputSavePending(int32 OutputMask) const;
};
