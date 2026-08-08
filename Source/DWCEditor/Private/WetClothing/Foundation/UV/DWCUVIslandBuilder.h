// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** Minimal, source-agnostic triangle input used by the single Original-UV island policy. */
struct FDWCUVIslandBuildTriangle
{
    int32     TriangleID = INDEX_NONE;
    int32     MaterialSlotIndex = INDEX_NONE;
    FVector2D UVs[3] = { FVector2D::ZeroVector, FVector2D::ZeroVector, FVector2D::ZeroVector };
};

/** Transient result. Persistent ownership is decided by the caller (for example WCA topology metadata). */
struct FDWCOriginalUVIslandBuildResult
{
    int32         MaterialSlotIndex = INDEX_NONE;
    int32         IslandID = INDEX_NONE;
    TArray<int32> TriangleInputIndices;
    TArray<int32> TriangleIDs;
    FBox2D        UVBounds = FBox2D(ForceInit);
    double        UVArea = 0.0;
};

/**
 * The only implementation of DWC Original-UV island connectivity.
 *
 * Policy:
 * - material slots are isolated from each other;
 * - connectivity uses quantized UV edge endpoints only;
 * - every triangle reusing the same UV edge is connected;
 * - 3D position and render-vertex identity are intentionally ignored.
 */
class FDWCUVIslandBuilder
{
  public:
    static void Build(
        const TArray<FDWCUVIslandBuildTriangle>& Triangles,
        TArray<FDWCOriginalUVIslandBuildResult>& OutIslands);
};
