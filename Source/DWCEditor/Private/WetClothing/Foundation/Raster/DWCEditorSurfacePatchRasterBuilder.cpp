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

    FDWCEditorSurfacePatchProjectionLease ProjectionLease;
    if (ProjectionCache != nullptr)
    {
        if (!ProjectionCache->Resolve(
                Input.Projection,
                CachePolicy,
                ProjectionLease,
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
        ProjectionLease.Geometry = MoveTemp(NewGeometry);
    }

    if (ProjectionLease.IsValid() && ProjectionLease->Diagnostics.bDetailed &&
        ProjectionLease->Diagnostics.HasContinuityIssue())
    {
        const FDWCEditorSurfacePatchProjectionDiagnostics& Diagnostics =
            ProjectionLease->Diagnostics;
        UE_LOG(LogDWCWrinkleProjectionDiagnostics, Warning,
            TEXT("Surface projection discontinuity: slot=%d anchor=%d visited=%d fragments=%d "
                 "regular=%d seam=%d boundary=%d blocked=%d internalBoundary=%d internalBlocked=%d "
                 "failedUnfold=%d pathMismatch=%d/%d maxPathError=%.6g "
                 "sharedMismatch=%d/%d maxSharedError=%.6g degenerate=%d flipped=%d "
                 "interior={footprint:%d,depth:%d} tangentDegenerate=%d "
                 "projectorVertexMismatch=%d maxProjectorVertexError=%.6g "
                 "projectionMs=%.3f validationMs=%.3f"),
            Input.Projection.MaterialSlotIndex,
            Input.Projection.AnchorTriangleID,
            ProjectionLease->VisitedTriangleCount,
            ProjectionLease->Fragments.Num(),
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
            Diagnostics.InteriorFootprintCandidateCount,
            Diagnostics.InteriorDepthCandidateCount,
            Diagnostics.DegenerateTangentFrameCount,
            Diagnostics.SharedProjectorVertexMismatchCount,
            Diagnostics.MaxSharedProjectorVertexError,
            Diagnostics.ProjectionMilliseconds,
            Diagnostics.ContinuityValidationMilliseconds);
    }

    OutCommand.ProjectionLease = MoveTemp(ProjectionLease);
    OutCommand.NormalSource = Input.NormalSource;
    OutCommand.CoverageSource = Input.CoverageSource;
    OutCommand.Strength = Input.Strength;
    OutCommand.Falloff = Input.Falloff;
    OutCommand.bUseSurfaceProjectionFilter = true;
    OutCommand.ProjectionDepthLocal = Input.Projection.ProjectionDepthLocal;
    OutCommand.MaxSurfaceAngleRadians =
        FMath::DegreesToRadians(Input.Projection.MaxSurfaceAngleDegrees);
    OutCommand.ProjectionDepthSoftness = Input.Projection.ProjectionDepthSoftness;
    OutCommand.ProjectionAngleSoftness = Input.Projection.ProjectionAngleSoftness;
    return true;
}
