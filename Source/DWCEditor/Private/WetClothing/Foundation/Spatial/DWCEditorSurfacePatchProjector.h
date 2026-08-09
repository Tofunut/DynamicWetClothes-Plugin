//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSurfacePatchProjectionTypes.h"

class FDWCEditorCancellationToken;

/** Projects an authored patch over physical triangle adjacency, including Data UV seams. */
class FDWCEditorSurfacePatchProjector final
{
  public:
    /** Validates the algorithm and topology-boundary combination before admission. */
    static bool ValidateProjectionContract(
        const FDWCEditorSurfacePatchProjectionRequest& Request,
        FString* OutError = nullptr);

    /**
     * Computes a topology-bounded admission estimate without allocating projection output.
     * MaxWorkingSetBytes and MaxResultBytes remain hard safety limits, not reservation sizes.
     */
    static FDWCEditorSurfacePatchProjectionMemoryEstimate EstimateAdmissionMemory(
        const FDWCEditorSurfacePatchProjectionRequest& Request);

    static FDWCEditorSurfacePatchProjectionResult Project(
        const FDWCEditorSurfacePatchProjectionRequest& Request,
        const FDWCEditorCancellationToken* CancellationToken = nullptr);

    /** Re-runs fragment continuity validation without projecting again. Intended for diagnostics/tests. */
    static void AnalyzeContinuityForDiagnostics(
        FDWCEditorSurfacePatchProjectionResult& Result,
        const FDWCEditorSpatialData& SpatialData);
};
