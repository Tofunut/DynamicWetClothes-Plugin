//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Spatial/DWCEditorIslandLocalGeodesicChartTypes.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSurfacePatchProjectionTypes.h"

class FDWCEditorCancellationToken;

/** Projects an authored patch over physical triangle adjacency, including Data UV seams. */
class FDWCEditorSurfacePatchProjector final
{
  public:
    static FDWCEditorSurfacePatchProjectionResult Project(
        const FDWCEditorSurfacePatchProjectionRequest& Request,
        const FDWCEditorCancellationToken* CancellationToken = nullptr);

    /** Rejects contradictory projection-mode flags before any chart or decal work begins. */
    static bool ValidateProjectionModeContract(
        const FDWCEditorSurfacePatchProjectionRequest& Request,
        FString* OutError = nullptr);

    /** Builds the rotation-independent chart request used by Non UV Seam projection. */
    static bool BuildIslandLocalChartRequest(
        const FDWCEditorSurfacePatchProjectionRequest& Request,
        FDWCEditorIslandLocalChartRequest& OutChartRequest,
        FString* OutError = nullptr);

    /** Converts an immutable shared-vertex chart into the final rotated patch fragments. */
    static FDWCEditorSurfacePatchProjectionResult ProjectFromIslandLocalChart(
        const FDWCEditorSurfacePatchProjectionRequest& Request,
        const FDWCEditorIslandLocalChartHandle& Chart,
        const FDWCEditorCancellationToken* CancellationToken = nullptr);

    /** Re-runs fragment continuity validation without projecting again. Intended for diagnostics/tests. */
    static void AnalyzeContinuityForDiagnostics(
        FDWCEditorSurfacePatchProjectionResult& Result,
        const FDWCEditorSpatialData& SpatialData);
};
