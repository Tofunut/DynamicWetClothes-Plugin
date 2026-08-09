//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSpatialQueryTypes.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSurfaceOrientationPolicy.h"

/** Builds the immutable sparse topology fallback field stored by a spatial cache entry. */
class FDWCEditorSurfaceOrientationFieldBuilder final
{
  public:
    static bool Build(
        TConstArrayView<FDWCEditorSpatialTriangle> Triangles,
        const FDWCEditorSurfaceOrientationPolicy& Policy,
        FDWCEditorSurfaceOrientationField& OutField,
        FString* OutWarning = nullptr);
};
