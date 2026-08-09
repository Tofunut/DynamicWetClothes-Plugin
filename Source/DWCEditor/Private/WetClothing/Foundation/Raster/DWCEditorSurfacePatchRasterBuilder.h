//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Raster/DWCEditorRasterTypes.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSurfacePatchProjectionCacheService.h"

class FDWCEditorCancellationToken;

/** Converts a canonical surface patch into target-UV fragments for the shared raster core. */
class FDWCEditorSurfacePatchRasterBuilder final
{
  public:
    static bool BuildProjectedPatchCommand(
        const FDWCEditorSurfaceNormalPatchInput& Input,
        FDWCEditorProjectedNormalPatchCommand& OutCommand,
        FString* OutError = nullptr,
        const FDWCEditorCancellationToken* CancellationToken = nullptr,
        FDWCEditorSurfacePatchProjectionCacheService* ProjectionCache = nullptr,
        EDWCEditorSurfacePatchCachePolicy CachePolicy =
            EDWCEditorSurfacePatchCachePolicy::Ephemeral);
};
