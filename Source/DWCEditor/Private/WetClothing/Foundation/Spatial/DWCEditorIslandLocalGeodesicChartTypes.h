//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSpatialQueryTypes.h"

enum class EDWCEditorIslandLocalChartStatus : uint8
{
    Succeeded,
    InvalidSpatialHandle,
    InvalidRequest,
    AnchorNotFound,
    InvalidTopology,
    DegenerateSurface,
    NoUsableNeighborhood,
    TraversalBudgetExceeded,
    ResultBudgetExceeded,
    Canceled
};

/** Worker-safe request for a bounded chart inside one material-slot UV island. */
struct FDWCEditorIslandLocalChartRequest
{
    FDWCEditorSpatialHandle SpatialHandle;
    int32 MaterialSlotIndex = INDEX_NONE;
    int32 AnchorTriangleID = INDEX_NONE;
    FVector3f AnchorBarycentric = FVector3f(1.0f, 0.0f, 0.0f);
    FVector3f SurfaceFrameU = FVector3f(1.0f, 0.0f, 0.0f);
    FVector3f SurfaceFrameV = FVector3f(0.0f, 1.0f, 0.0f);
    float GeodesicRadiusLocal = 0.0f;
    float NeighborhoodMarginLocal = 0.0f;
    int32 MaxVisitedTriangles = 0;
    uint64 MaxWorkingSetBytes = 64ull * 1024ull * 1024ull;
    uint64 MaxResultBytes = 64ull * 1024ull * 1024ull;
};

struct FDWCEditorIslandLocalChartVertex
{
    int64 TopologyVertexID = INDEX_NONE;
    FVector3f LocalPosition = FVector3f::ZeroVector;
    FVector2f ChartCoordinate = FVector2f::ZeroVector;
    float GeodesicDistance = TNumericLimits<float>::Max();
    int64 PredecessorTopologyVertexID = INDEX_NONE;
    bool bBoundary = false;
};

/** A triangle references shared chart vertices instead of owning independent coordinates. */
struct FDWCEditorIslandLocalChartTriangle
{
    int32 SpatialTriangleIndex = INDEX_NONE;
    int32 TriangleID = INDEX_NONE;
    int32 ChartVertexIndices[3] = { INDEX_NONE, INDEX_NONE, INDEX_NONE };
    FVector2f TargetUVs[3] = {
        FVector2f::ZeroVector,
        FVector2f::ZeroVector,
        FVector2f::ZeroVector
    };
    EDWCEditorSpatialEdgeType EdgeTypes[3] = {
        EDWCEditorSpatialEdgeType::Boundary,
        EDWCEditorSpatialEdgeType::Boundary,
        EDWCEditorSpatialEdgeType::Boundary
    };
    FBox2f ChartBounds = FBox2f(ForceInit);
    FBox2f TargetUVBounds = FBox2f(ForceInit);
};

struct FDWCEditorIslandLocalChartDiagnostics
{
    int32 CandidateTriangleCount = 0;
    int32 CandidateVertexCount = 0;
    int32 EmittedTriangleCount = 0;
    int32 EmittedVertexCount = 0;
    int32 RegularEdgeCount = 0;
    int32 UVSeamEdgeCount = 0;
    int32 BoundaryEdgeCount = 0;
    int32 BlockedEdgeCount = 0;
    int32 LoopClosureComparisonCount = 0;
    int32 DiscontinuousLoopClosureCount = 0;
    int32 DegenerateTriangleCount = 0;
    float MaxLoopClosureResidual = 0.0f;
    float AverageLoopClosureResidual = 0.0f;
    double NeighborhoodMilliseconds = 0.0;
    double GeodesicMilliseconds = 0.0;
    double ChartMilliseconds = 0.0;
    double TotalMilliseconds = 0.0;
    uint64 PeakWorkingSetBytes = 0;
    uint64 ResultBytes = 0;
};

struct FDWCEditorIslandLocalGeodesicChart
{
    int32 MaterialSlotIndex = INDEX_NONE;
    int32 UVIslandID = INDEX_NONE;
    int32 AnchorTriangleID = INDEX_NONE;
    FVector3f AnchorBarycentric = FVector3f(1.0f, 0.0f, 0.0f);
    FVector3f SurfaceFrameU = FVector3f(1.0f, 0.0f, 0.0f);
    FVector3f SurfaceFrameV = FVector3f(0.0f, 1.0f, 0.0f);
    FVector3f AnchorLocalPosition = FVector3f::ZeroVector;
    float GeodesicRadiusLocal = 0.0f;
    float NeighborhoodMarginLocal = 0.0f;
    TArray<FDWCEditorIslandLocalChartVertex> Vertices;
    TArray<FDWCEditorIslandLocalChartTriangle> Triangles;
    FDWCEditorIslandLocalChartDiagnostics Diagnostics;

    uint64 GetAllocatedSizeBytes() const
    {
        return sizeof(*this) + Vertices.GetAllocatedSize() + Triangles.GetAllocatedSize();
    }
};

using FDWCEditorIslandLocalChartHandle =
    TSharedPtr<const FDWCEditorIslandLocalGeodesicChart, ESPMode::ThreadSafe>;

struct FDWCEditorIslandLocalChartResult
{
    EDWCEditorIslandLocalChartStatus Status = EDWCEditorIslandLocalChartStatus::InvalidRequest;
    FString Error;
    FDWCEditorIslandLocalChartHandle Chart;

    bool IsSuccess() const
    {
        return Status == EDWCEditorIslandLocalChartStatus::Succeeded && Chart.IsValid();
    }
};
