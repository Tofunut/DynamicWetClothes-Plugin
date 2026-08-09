//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Foundation/Raster/DWCEditorSurfacePatchRasterBuilder.h"

#include "WetClothing/Foundation/Jobs/DWCEditorCancellationToken.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSurfacePatchProjector.h"

DEFINE_LOG_CATEGORY_STATIC(LogDWCWrinkleProjectionDiagnostics, Log, All);

bool FDWCEditorSurfacePatchRasterBuilder::BuildProjectedPatchCommand(
    const FDWCEditorSurfaceNormalPatchInput& Input,
    FDWCEditorProjectedNormalPatchCommand& OutCommand,
    FString* OutError,
    const FDWCEditorCancellationToken* CancellationToken,
    FDWCEditorSurfacePatchProjectionCacheService* ProjectionCache,
    const EDWCEditorSurfacePatchCachePolicy CachePolicy)
{
    OutCommand = FDWCEditorProjectedNormalPatchCommand();
    if (!Input.IsValid())
    {
        if (OutError != nullptr)
        {
            *OutError = TEXT("The surface patch has no usable anchor, normal source, or strength.");
        }
        return false;
    }

    FDWCEditorSurfacePatchProjectionHandle Geometry;
    if (ProjectionCache != nullptr)
    {
        if (!ProjectionCache->Resolve(
                Input.Projection,
                CachePolicy,
                Geometry,
                OutError,
                CancellationToken))
        {
            return false;
        }
    }
    else
    {
        FDWCEditorSurfacePatchProjectionResult Projection =
            FDWCEditorSurfacePatchProjector::Project(Input.Projection, CancellationToken);
        if (!Projection.IsSuccess() || Projection.Fragments.IsEmpty())
        {
            if (OutError != nullptr)
            {
                *OutError = Projection.Error.IsEmpty()
                    ? TEXT("The surface patch did not project onto any target triangles.")
                    : MoveTemp(Projection.Error);
            }
            return false;
        }
        TSharedRef<FDWCEditorSurfacePatchProjectionGeometry, ESPMode::ThreadSafe> NewGeometry =
            MakeShared<FDWCEditorSurfacePatchProjectionGeometry, ESPMode::ThreadSafe>();
        NewGeometry->Fragments = MoveTemp(Projection.Fragments);
        NewGeometry->AffectedUVIslandIDs = MoveTemp(Projection.AffectedUVIslandIDs);
        NewGeometry->VisitedTriangleCount = Projection.VisitedTriangleCount;
        NewGeometry->TraversedSeamCount = Projection.TraversedSeamCount;
        NewGeometry->PeakWorkingSetBytes = Projection.PeakWorkingSetBytes;
        NewGeometry->Diagnostics = Projection.Diagnostics;
        Geometry = MoveTemp(NewGeometry);
    }

    if (Geometry.IsValid() && Geometry->Diagnostics.bDetailed &&
        Geometry->Diagnostics.HasContinuityIssue())
    {
        const FDWCEditorSurfacePatchProjectionDiagnostics& Diagnostics = Geometry->Diagnostics;
        UE_LOG(LogDWCWrinkleProjectionDiagnostics, Warning,
            TEXT("Surface projection discontinuity: slot=%d anchor=%d visited=%d fragments=%d "
                 "regular=%d seam=%d boundary=%d blocked=%d internalBoundary=%d internalBlocked=%d "
                 "failedUnfold=%d pathMismatch=%d/%d maxPathError=%.6g "
                 "sharedMismatch=%d/%d maxSharedError=%.6g degenerate=%d flipped=%d "
                 "projectionMs=%.3f validationMs=%.3f "
                 "chart={attempted:%s,success:%s,status:%u,vertices:%d,triangles:%d,"
                 "loopMismatch:%d,maxResidual:%.6g,buildMs:%.3f,workingBytes:%llu,resultBytes:%llu}"),
            Input.Projection.MaterialSlotIndex,
            Input.Projection.AnchorTriangleID,
            Geometry->VisitedTriangleCount,
            Geometry->Fragments.Num(),
            Diagnostics.RegularEdgeCount,
            Diagnostics.UVSeamEdgeCount,
            Diagnostics.BoundaryEdgeCount,
            Diagnostics.BlockedEdgeCount,
            Diagnostics.InternalBoundaryEdgeCount,
            Diagnostics.InternalBlockedEdgeCount,
            Diagnostics.FailedUnfoldEdgeCount,
            Diagnostics.DiscontinuousCandidatePathCount,
            Diagnostics.CandidatePathComparisonCount,
            Diagnostics.MaxCandidatePathError,
            Diagnostics.DiscontinuousSharedEdgeCount,
            Diagnostics.SharedEdgeComparisonCount,
            Diagnostics.MaxSharedCoordinateError,
            Diagnostics.DegenerateFragmentCount,
            Diagnostics.FlippedFragmentCount,
            Diagnostics.ProjectionMilliseconds,
            Diagnostics.ContinuityValidationMilliseconds,
            Diagnostics.bIslandChartBuildAttempted ? TEXT("true") : TEXT("false"),
            Diagnostics.bIslandChartBuildSucceeded ? TEXT("true") : TEXT("false"),
            Diagnostics.IslandChartStatus,
            Diagnostics.IslandChartVertexCount,
            Diagnostics.IslandChartTriangleCount,
            Diagnostics.IslandChartLoopMismatchCount,
            Diagnostics.IslandChartMaxLoopResidual,
            Diagnostics.IslandChartBuildMilliseconds,
            Diagnostics.IslandChartPeakWorkingSetBytes,
            Diagnostics.IslandChartResultBytes);
    }

    OutCommand.SharedProjection = MoveTemp(Geometry);
    OutCommand.NormalSource = Input.NormalSource;
    OutCommand.CoverageSource = Input.CoverageSource;
    OutCommand.Strength = Input.Strength;
    OutCommand.Falloff = Input.Falloff;
    return true;
}
