//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetClothingWrinkleData.h"
#include "WetClothing/Foundation/Jobs/DWCEditorWorkerJobTypes.h"
#include "WetClothing/Foundation/Raster/DWCEditorRasterTypes.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSurfacePatchProjectionTypes.h"

class FDWCEditorCancellationToken;
class FDWCEditorSurfacePatchProjectionCacheService;

using FWetWrinkleSurfacePatchPreviewInput = FDWCEditorSurfaceNormalPatchInput;

struct FWetWrinkleSpatialLeaseOwner
{
    FDWCEditorSpatialLease Lease;
};

struct FWetWrinkleAccumulatedPreviewJobInput
{
    FIntPoint TextureSize = FIntPoint::ZeroValue;
    FIntPoint WorkingTextureSize = FIntPoint::ZeroValue;
    TArray<FWetWrinkleSurfacePatchPreviewInput> SurfacePatches;
    TArray<FWetProceduralRidgeStroke> RidgeStrokes;
    int32 InvalidSurfacePatchCount = 0;
    FString FirstSurfacePatchError;
    TSharedPtr<FWetWrinkleSpatialLeaseOwner, ESPMode::ThreadSafe> SpatialLeaseOwner;
    TSharedPtr<FDWCEditorSurfacePatchProjectionCacheService> SurfacePatchProjectionCache;
};

struct FWetWrinkleAccumulatedPreviewJobResult final : FDWCEditorWorkerJobResult
{
    FIntPoint TextureSize = FIntPoint::ZeroValue;
    FIntPoint WorkingTextureSize = FIntPoint::ZeroValue;
    TArray<FColor> Pixels;
    FDWCEditorNormalRasterSurface WorkingSurface;
    int32 InvalidSurfacePatchCount = 0;
    FString FirstSurfacePatchError;
    TSharedPtr<FWetWrinkleSpatialLeaseOwner, ESPMode::ThreadSafe> SpatialLeaseOwner;
};

class FWetWrinkleAccumulatedPreviewWorker
{
  public:
    static TSharedPtr<FWetWrinkleAccumulatedPreviewJobResult, ESPMode::ThreadSafe> Build(
        FWetWrinkleAccumulatedPreviewJobInput Input,
        const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& CancellationToken);
};
