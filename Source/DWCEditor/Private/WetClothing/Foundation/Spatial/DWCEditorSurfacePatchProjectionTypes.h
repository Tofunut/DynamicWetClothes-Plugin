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

/** Controls which physical topology boundaries a surface decal may traverse. */
enum class EDWCEditorSurfacePatchBoundaryPolicy : uint8
{
    Invalid,
    AnchorUVIslandOnly,
    CrossUVSeams
};

/** Canonical editor settings shared by wrinkle UI, authoring and raster requests. */
struct FDWCEditorSurfacePatchProjectionSettings
{
    EDWCEditorSurfacePatchBoundaryPolicy BoundaryPolicy =
        EDWCEditorSurfacePatchBoundaryPolicy::AnchorUVIslandOnly;
    float ProjectionDepthLocal = 3.0f;
    float MaxSurfaceAngleDegrees = 70.0f;
    float ProjectionDepthSoftness = 0.2f;
    float ProjectionAngleSoftness = 0.1f;

    void Normalize()
    {
        ProjectionDepthLocal = FMath::Clamp(ProjectionDepthLocal, 0.1f, 20.0f);
        MaxSurfaceAngleDegrees = FMath::Clamp(MaxSurfaceAngleDegrees, 1.0f, 89.0f);
        ProjectionDepthSoftness = FMath::Clamp(ProjectionDepthSoftness, 0.0f, 1.0f);
        ProjectionAngleSoftness = FMath::Clamp(ProjectionAngleSoftness, 0.0f, 1.0f);
        if (BoundaryPolicy != EDWCEditorSurfacePatchBoundaryPolicy::AnchorUVIslandOnly &&
            BoundaryPolicy != EDWCEditorSurfacePatchBoundaryPolicy::CrossUVSeams)
        {
            BoundaryPolicy = EDWCEditorSurfacePatchBoundaryPolicy::AnchorUVIslandOnly;
        }
    }

    bool IsValid() const
    {
        return
            (BoundaryPolicy == EDWCEditorSurfacePatchBoundaryPolicy::AnchorUVIslandOnly ||
             BoundaryPolicy == EDWCEditorSurfacePatchBoundaryPolicy::CrossUVSeams) &&
            FMath::IsFinite(ProjectionDepthLocal) && ProjectionDepthLocal > 0.0f &&
            FMath::IsFinite(MaxSurfaceAngleDegrees) && MaxSurfaceAngleDegrees > 0.0f &&
            MaxSurfaceAngleDegrees < 90.0f &&
            FMath::IsFinite(ProjectionDepthSoftness) && ProjectionDepthSoftness >= 0.0f &&
            ProjectionDepthSoftness <= 1.0f &&
            FMath::IsFinite(ProjectionAngleSoftness) && ProjectionAngleSoftness >= 0.0f &&
            ProjectionAngleSoftness <= 1.0f;
    }

    bool IsEquivalent(const FDWCEditorSurfacePatchProjectionSettings& Other) const
    {
        return BoundaryPolicy == Other.BoundaryPolicy &&
            FMath::IsNearlyEqual(ProjectionDepthLocal, Other.ProjectionDepthLocal) &&
            FMath::IsNearlyEqual(MaxSurfaceAngleDegrees, Other.MaxSurfaceAngleDegrees) &&
            FMath::IsNearlyEqual(ProjectionDepthSoftness, Other.ProjectionDepthSoftness) &&
            FMath::IsNearlyEqual(ProjectionAngleSoftness, Other.ProjectionAngleSoftness);
    }
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
    int32 InteriorFootprintCandidateCount = 0;
    int32 InteriorDepthCandidateCount = 0;
    int32 DegenerateTangentFrameCount = 0;
    int32 SharedProjectorVertexMismatchCount = 0;
    float MaxSharedProjectorVertexError = 0.0f;
    float MaxCandidatePathError = 0.0f;
    float AverageCandidatePathError = 0.0f;
    float MaxSharedCoordinateError = 0.0f;
    float AverageSharedCoordinateError = 0.0f;
    double ProjectionMilliseconds = 0.0;
    double ContinuityValidationMilliseconds = 0.0;
    bool HasContinuityIssue() const
    {
        return InternalBoundaryEdgeCount > 0 || InternalBlockedEdgeCount > 0 ||
            FailedUnfoldEdgeCount > 0 || DiscontinuousCandidatePathCount > 0 ||
            DiscontinuousSharedEdgeCount > 0 || DegenerateFragmentCount > 0 ||
            FlippedFragmentCount > 0 || DegenerateTangentFrameCount > 0 ||
            SharedProjectorVertexMismatchCount > 0;
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
    EDWCEditorSurfacePatchBoundaryPolicy BoundaryPolicy =
        EDWCEditorSurfacePatchBoundaryPolicy::Invalid;
    bool bCollectDetailedDiagnostics = false;

    // Zero uses the number of triangles in the leased spatial payload.
    int32 MaxVisitedTriangles = 0;
    uint64 MaxWorkingSetBytes = 64ull * 1024ull * 1024ull;
    uint64 MaxResultBytes = 64ull * 1024ull * 1024ull;

    FDWCEditorSurfacePatchProjectionSettings GetSettings() const
    {
        FDWCEditorSurfacePatchProjectionSettings Settings;
        Settings.BoundaryPolicy = BoundaryPolicy;
        Settings.ProjectionDepthLocal = ProjectionDepthLocal;
        Settings.MaxSurfaceAngleDegrees = MaxSurfaceAngleDegrees;
        Settings.ProjectionDepthSoftness = ProjectionDepthSoftness;
        Settings.ProjectionAngleSoftness = ProjectionAngleSoftness;
        return Settings;
    }

    void ApplySettings(FDWCEditorSurfacePatchProjectionSettings Settings)
    {
        Settings.Normalize();
        BoundaryPolicy = Settings.BoundaryPolicy;
        ProjectionDepthLocal = Settings.ProjectionDepthLocal;
        MaxSurfaceAngleDegrees = Settings.MaxSurfaceAngleDegrees;
        ProjectionDepthSoftness = Settings.ProjectionDepthSoftness;
        ProjectionAngleSoftness = Settings.ProjectionAngleSoftness;
    }
};

/** Conservative private-memory bound used to admit one projection build. */
struct FDWCEditorSurfacePatchProjectionMemoryEstimate
{
    int32 TriangleUpperBound = 0;
    uint64 WorkingSetBytes = 0;
    uint64 IntermediateBytes = 0;
    uint64 ResultBytes = 0;

    uint64 GetTotalBytes() const
    {
        uint64 Total = WorkingSetBytes;
        Total = IntermediateBytes > MAX_uint64 - Total ? MAX_uint64 : Total + IntermediateBytes;
        return ResultBytes > MAX_uint64 - Total ? MAX_uint64 : Total + ResultBytes;
    }
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
    // Surface-decal projection attributes. Signed depth is affine over the
    // triangle, while the projector-space normal is normalized after raster
    // interpolation before depth/angle filtering is evaluated per texel.
    float SignedProjectionDepth[3] = { 0.0f, 0.0f, 0.0f };
    FVector3f SurfaceNormalInProjectorSpace[3] = {
        FVector3f(0.0f, 0.0f, 1.0f),
        FVector3f(0.0f, 0.0f, 1.0f),
        FVector3f(0.0f, 0.0f, 1.0f)
    };
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

/** Opaque ownership token for projection memory admitted to the shared cache budget. */
class IDWCEditorSurfacePatchProjectionResidency
{
  public:
    virtual ~IDWCEditorSurfacePatchProjectionResidency() = default;
    virtual uint64 GetResidentBytes() const = 0;
};

/**
 * Couples immutable projection geometry with the cache reservation that owns it.
 * An ephemeral lease has geometry only. A resident lease keeps the shared-cache
 * reservation alive even after the cache key is invalidated or its service exits.
 */
struct FDWCEditorSurfacePatchProjectionLease
{
    FDWCEditorSurfacePatchProjectionHandle Geometry;
    TSharedPtr<const IDWCEditorSurfacePatchProjectionResidency, ESPMode::ThreadSafe> Residency;

    bool IsValid() const { return Geometry.IsValid(); }
    bool IsCacheResident() const { return Geometry.IsValid() && Residency.IsValid(); }
    bool IsResidencyUnique() const { return Residency.IsValid() && Residency.IsUnique(); }

    const FDWCEditorSurfacePatchProjectionGeometry* Get() const { return Geometry.Get(); }
    const FDWCEditorSurfacePatchProjectionGeometry* operator->() const { return Geometry.Get(); }

    void Reset()
    {
        Geometry.Reset();
        Residency.Reset();
    }

    uint64 GetPrivateBytes() const
    {
        return IsValid() && !IsCacheResident()
            ? Geometry->GetAllocatedSizeBytes()
            : 0ull;
    }

    uint64 GetSharedResidentBytes() const
    {
        return IsCacheResident() ? Residency->GetResidentBytes() : 0ull;
    }
};
