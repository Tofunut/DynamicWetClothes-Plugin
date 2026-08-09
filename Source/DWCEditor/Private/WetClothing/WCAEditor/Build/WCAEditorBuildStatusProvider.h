//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Build/DWCEditorBuildActionTypes.h"

class SWCAEditorPanel;
class UWetClothingAsset;

class FWCAEditorBuildStatusProvider
{
  public:
    static FDWCEditorBuildStatusSnapshot BuildSnapshot(
        UWetClothingAsset& Asset,
        const SWCAEditorPanel* EditorPanel,
        EDWCEditorBuildSurfaceMode SurfaceMode,
        bool bDeepValidation);
};

