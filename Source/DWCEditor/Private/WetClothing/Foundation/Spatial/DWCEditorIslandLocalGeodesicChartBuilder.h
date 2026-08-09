//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Spatial/DWCEditorIslandLocalGeodesicChartTypes.h"

class FDWCEditorCancellationToken;

/** Builds one bounded, deterministic, shared-vertex chart without reading UObjects. */
class FDWCEditorIslandLocalGeodesicChartBuilder final
{
  public:
    static FDWCEditorIslandLocalChartResult Build(
        const FDWCEditorIslandLocalChartRequest& Request,
        const FDWCEditorCancellationToken* CancellationToken = nullptr);
};
