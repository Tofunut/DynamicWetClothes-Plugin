//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSpatialQueryTypes.h"

enum class EDWCEditorSurfacePatchProjectionStatus : uint8
{
    Succeeded,
    InvalidSpatialHandle,
    InvalidRequest,
    AnchorNotFound,
    DegenerateSurface,
    TraversalBudgetExceeded,
    ResultBudgetExceeded,
    Canceled
};

/** Optional projection diagnostics. Detailed fields are populated only on explicit requests. */
struct FDWCEditorSurfacePatchProjectionDiagnostics
{
    bool bDetailed = false;
    int32 CandidateTriangleCount = 0;
    int32 EmittedFragmentCount = 0;
    int32 RegularEdgeCount = 0;
    int32 UVSeamEdgeCount = 0;
    int32 BoundaryEdgeCount = 0;
    int32 BlockedEdgeCount = 0;
    int32 InternalBoundaryEdgeCount = 0;
    int32 InternalBlockedEdgeCount = 0;
    int32 FailedUnfoldEdgeCount = 0;
    int32 CandidatePathComparisonCount = 0;
    int32 DiscontinuousCandidatePathCount = 0;
    int32 SharedEdgeComparisonCount = 0;
    int32 DiscontinuousSharedEdgeCount = 0;
    int32 DegenerateFragmentCount = 0;
    int32 FlippedFragmentCount = 0;
    float MaxCandidatePathError = 0.0f;
    float AverageCandidatePathError = 0.0f;
    float MaxSharedCoordinateError = 0.0f;
    float AverageSharedCoordinateError = 0.0f;
    double ProjectionMilliseconds = 0.0;
    double ContinuityValidationMilliseconds = 0.0;
    bool bIslandChartBuildAttempted = false;
    bool bIslandChartBuildSucceeded = false;
    uint8 IslandChartStatus = 0;
    int32 IslandChartVertexCount = 0;
    int32 IslandChartTriangleCount = 0;
    int32 IslandChartLoopMismatchCount = 0;
    float IslandChartMaxLoopResidual = 0.0f;
    double IslandChartBuildMilliseconds = 0.0;
    uint64 IslandChartPeakWorkingSetBytes = 0;
    uint64 IslandChartResultBytes = 0;

    bool HasContinuityIssue() const
    {
        return InternalBoundaryEdgeCount > 0 || InternalBlockedEdgeCount > 0 ||
            FailedUnfoldEdgeCount > 0 || DiscontinuousCandidatePathCount > 0 ||
            DiscontinuousSharedEdgeCount > 0 || DegenerateFragmentCount > 0 ||
            FlippedFragmentCount > 0;
    }
};

/** Immutable, worker-safe input for projecting one authored patch over mesh topology. */
struct FDWCEditorSurfacePatchProjectionRequest
{
    FDWCEditorSpatialHandle SpatialHandle;
    int32 MaterialSlotIndex = INDEX_NONE;
    int32 AnchorTriangleID = INDEX_NONE;
    FVector3f AnchorBarycentric = FVector3f(1.0f, 0.0f, 0.0f);
    FVector3f SurfaceFrameU = FVector3f(1.0f, 0.0f, 0.0f);
    FVector3f SurfaceFrameV = FVector3f(0.0f, 1.0f, 0.0f);
    FVector2f SurfaceHalfExtentLocal = FVector2f::ZeroVector;
    float RotationRadians = 0.0f;
    FVector2f Scale = FVector2f(1.0f, 1.0f);
    float ProjectionDepthLocal = 3.0f;
    float MaxSurfaceAngleDegrees = 70.0f;
    float ProjectionDepthSoftness = 0.2f;
    float ProjectionAngleSoftness = 0.1f;
    bool bUseSurfaceDecalProjection = false;
    // Non-UV-seam mode builds one shared-vertex chart through regular topology
    // edges only. Surface-decal mode may cross physical UV-seam adjacency.
    bool bAllowUVSeamTraversal = false;
    bool bCollectDetailedDiagnostics = false;

    // Zero uses the number of triangles in the leased spatial payload.
    int32 MaxVisitedTriangles = 0;
    uint64 MaxWorkingSetBytes = 64ull * 1024ull * 1024ull;
    uint64 MaxResultBytes = 64ull * 1024ull * 1024ull;
};

/** One physical triangle expressed both in target Data UV and patch-local coordinates. */
struct FDWCEditorSurfacePatchFragment
{
    int32 TriangleIndex = INDEX_NONE;
    int32 TriangleID = INDEX_NONE;
    int32 UVIslandID = INDEX_NONE;
    FVector2f TargetUVs[3] = {
        FVector2f::ZeroVector,
        FVector2f::ZeroVector,
        FVector2f::ZeroVector
    };
    // Normalized patch coordinates. The patch footprint is the unit circle and
    // source texture UV is PatchCoordinates * 0.5 + 0.5.
    FVector2f PatchCoordinates[3] = {
        FVector2f::ZeroVector,
        FVector2f::ZeroVector,
        FVector2f::ZeroVector
    };
    // Columns of the patch-frame to render tangent-frame XY transform at each
    // corner. Rasterization interpolates these so a smooth render tangent basis
    // remains continuous across triangles and rotated UV islands.
    FVector2f PatchAxisUInTargetTangent[3] = {
        FVector2f(1.0f, 0.0f),
        FVector2f(1.0f, 0.0f),
        FVector2f(1.0f, 0.0f)
    };
    FVector2f PatchAxisVInTargetTangent[3] = {
        FVector2f(0.0f, 1.0f),
        FVector2f(0.0f, 1.0f),
        FVector2f(0.0f, 1.0f)
    };
    float ProjectionInfluence[3] = { 1.0f, 1.0f, 1.0f };
    FBox2f TargetUVBounds = FBox2f(ForceInit);
};

struct FDWCEditorSurfacePatchProjectionResult
{
    EDWCEditorSurfacePatchProjectionStatus Status =
        EDWCEditorSurfacePatchProjectionStatus::InvalidRequest;
    FString Error;
    TArray<FDWCEditorSurfacePatchFragment> Fragments;
    TArray<int32> AffectedUVIslandIDs;
    int32 VisitedTriangleCount = 0;
    int32 TraversedSeamCount = 0;
    bool bTouchesUVSeam = false;
    uint64 PeakWorkingSetBytes = 0;
    FDWCEditorSurfacePatchProjectionDiagnostics Diagnostics;

    bool IsSuccess() const
    {
        return Status == EDWCEditorSurfacePatchProjectionStatus::Succeeded;
    }

    uint64 GetAllocatedSizeBytes() const
    {
        return Fragments.GetAllocatedSize() +
            AffectedUVIslandIDs.GetAllocatedSize();
    }
};

/** Immutable geometry shared by committed preview and bake raster commands. */
struct FDWCEditorSurfacePatchProjectionGeometry
{
    TArray<FDWCEditorSurfacePatchFragment> Fragments;
    TArray<int32> AffectedUVIslandIDs;
    int32 VisitedTriangleCount = 0;
    int32 TraversedSeamCount = 0;
    bool bTouchesUVSeam = false;
    uint64 PeakWorkingSetBytes = 0;
    FDWCEditorSurfacePatchProjectionDiagnostics Diagnostics;

    bool IsValid() const { return !Fragments.IsEmpty(); }

    uint64 GetAllocatedSizeBytes() const
    {
        return static_cast<uint64>(sizeof(*this)) +
            static_cast<uint64>(Fragments.GetAllocatedSize()) +
            static_cast<uint64>(AffectedUVIslandIDs.GetAllocatedSize());
    }
};

using FDWCEditorSurfacePatchProjectionHandle =
    TSharedPtr<const FDWCEditorSurfacePatchProjectionGeometry, ESPMode::ThreadSafe>;
