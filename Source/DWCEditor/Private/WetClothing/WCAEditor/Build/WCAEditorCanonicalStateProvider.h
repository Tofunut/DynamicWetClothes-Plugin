// Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Build/DWCEditorBuildActionTypes.h"
#include "WetClothing/Foundation/Validation/DWCEditorValidationSnapshot.h"

class SWCAEditorPanel;
class UWetClothingAsset;

struct FWCAEditorCanonicalStateSnapshot
{
    FWCAEditorValidationSnapshot Validation;
    FDWCEditorBuildStatusSnapshot BuildStatus;
};

class FWCAEditorCanonicalStateProvider
{
  public:
    static FWCAEditorCanonicalStateSnapshot Build(
        UWetClothingAsset& Asset,
        const SWCAEditorPanel* EditorPanel,
        EDWCEditorBuildSurfaceMode SurfaceMode,
        bool bDeepValidation);
};
