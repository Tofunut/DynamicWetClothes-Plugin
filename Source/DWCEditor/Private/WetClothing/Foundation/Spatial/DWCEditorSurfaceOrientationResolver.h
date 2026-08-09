//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSpatialQueryTypes.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSurfaceOrientationPolicy.h"

enum class EDWCEditorSurfaceOrientationSource : uint8
{
    PrimaryAxis,
    BlendedTopologyFallback,
    TopologyFallback,
    DeterministicSecondaryFallback
};

struct FDWCEditorResolvedSurfaceOrientation
{
    FVector3f FrameU = FVector3f::ZeroVector;
    FVector3f FrameV = FVector3f::ZeroVector;
    float PrimaryProjectionQuality = 0.0f;
    float FallbackWeight = 0.0f;
    EDWCEditorSurfaceOrientationSource Source =
        EDWCEditorSurfaceOrientationSource::PrimaryAxis;

    bool IsValid() const;
};

/** Resolves a path-independent authoring frame from the cached sparse topology field. */
class FDWCEditorSurfaceOrientationResolver final
{
  public:
    static bool Resolve(
        const FDWCEditorSpatialData& SpatialData,
        int32 TriangleIndex,
        const FVector3f& Barycentric,
        const FVector3f& SurfaceNormal,
        const FDWCEditorSurfaceOrientationPolicy& Policy,
        FDWCEditorResolvedSurfaceOrientation& OutResult);
};
