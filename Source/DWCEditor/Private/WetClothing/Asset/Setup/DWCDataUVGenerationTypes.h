//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "MeshDescription.h"

namespace DWCDataUVSafetyLimits
{
    /** Maximum visible 3D surface ratio that may be excluded without explicit user approval. */
    static constexpr double VisibleExclusionRatio = 0.005; // 0.5%
}

/** Transient triangle data read from one editable Skeletal Mesh LOD. */
struct FDWCDataUVTriangle
{
    FTriangleID TriangleID;
    int32 MaterialSlotIndex = INDEX_NONE;
    FVertexInstanceID VertexInstances[3];
    FVertexID Vertices[3];
    FVector Positions[3] = { FVector::ZeroVector, FVector::ZeroVector, FVector::ZeroVector };
    FVector2D SourceUVs[3] = { FVector2D::ZeroVector, FVector2D::ZeroVector, FVector2D::ZeroVector };
};

/** Transient packing unit in generated DWC UV Channel space. Not the persistent Original UV island record. */
struct FDWCDataUVChart
{
    int32 MaterialSlotIndex = INDEX_NONE;
    TArray<int32> TriangleIndices;
    TMap<int32, FVector2D> RawUVByVertexInstance;
    FBox2D RawBounds = FBox2D(ForceInit);
    double RawArea = 0.0;
};

enum class EDWCDataUVChartBuildFailureReason : uint8
{
    None,
    AnalysisBudgetExceeded
};

/** Structured details for a Source UV overlap-analysis failure. */
struct FDWCDataUVChartBuildFailure
{
    bool bIsValid = false;
    EDWCDataUVChartBuildFailureReason FailureReason = EDWCDataUVChartBuildFailureReason::None;
    int32 MaterialSlotIndex = INDEX_NONE;
    int32 SourceTriangleCount = 0;
    int64 TestedCandidatePairCount = 0;
    FString Reason;
};

/** Structured details for a final packed DWC UV validation failure. */
struct FDWCDataUVValidationFailure
{
    bool bIsValid = false;
    int32 MaterialSlotIndex = INDEX_NONE;
    int32 MeshTriangleID = INDEX_NONE;
    int32 GeneratorTriangleIndex = INDEX_NONE;
    int32 ChartIndex = INDEX_NONE;
    double PackedArea = 0.0;
    FString Reason;
    FVector2D PackedUVs[3] = { FVector2D::ZeroVector, FVector2D::ZeroVector, FVector2D::ZeroVector };
};

/** Non-fatal packed triangle excluded because its final packed UV area is below tolerance. */
struct FDWCDataUVValidationExclusion
{
    int32 MaterialSlotIndex = INDEX_NONE;
    int32 MeshTriangleID = INDEX_NONE;
    int32 GeneratorTriangleIndex = INDEX_NONE;
    int32 ChartIndex = INDEX_NONE;
    double PackedArea = 0.0;
    FVector2D PackedUVs[3] = { FVector2D::ZeroVector, FVector2D::ZeroVector, FVector2D::ZeroVector };
};
