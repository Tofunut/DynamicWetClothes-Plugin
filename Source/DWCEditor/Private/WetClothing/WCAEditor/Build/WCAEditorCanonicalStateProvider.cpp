// Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/WCAEditor/Build/WCAEditorCanonicalStateProvider.h"

#include "DataAssets/WetClothingAsset.h"
#include "WetClothing/WCAEditor/Build/WCAEditorBuildStatusProvider.h"
#include "WetClothing/WCAEditor/WCAValidationReport.h"

FWCAEditorCanonicalStateSnapshot FWCAEditorCanonicalStateProvider::Build(
    UWetClothingAsset& Asset,
    const SWCAEditorPanel* EditorPanel,
    const EDWCEditorBuildSurfaceMode SurfaceMode,
    const bool bDeepValidation,
    const bool bRefreshAssetState)
{
    FWCAEditorCanonicalStateSnapshot Result;
    Result.Validation = BuildWCAValidationSnapshot(
        Asset,
        bDeepValidation ? EWCAValidationMode::Deep : EWCAValidationMode::Fast,
        bRefreshAssetState);
    Result.BuildStatus = FWCAEditorBuildStatusProvider::BuildSnapshot(
        Asset,
        EditorPanel,
        SurfaceMode,
        bDeepValidation,
        &Result.Validation);
    return Result;
}
