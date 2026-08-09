//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

/** Canonical versions shared by cache keys, baked signatures, and regression tests. */
namespace DWCEditorSurfacePatchProjectionVersion
{
    inline constexpr uint32 SurfaceProjection = 10;
    inline constexpr uint32 ProjectedRaster = 3;
    // Preserves the historical CoreMode token in baked build signatures.
    inline constexpr int32 SurfaceDecalSignatureId = 1;
}

namespace DWCEditorSurfaceOrientationVersion
{
    inline constexpr uint32 Policy = 1;
    inline constexpr uint32 FieldLayout = 1;
    inline constexpr uint32 Resolver = 1;
}
