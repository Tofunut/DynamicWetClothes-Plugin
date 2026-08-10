//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "WetClothing/Modes/Wrinkle/Viewport/WetWrinkleAccumulatedPreviewWorker.h"

#include "WetClothing/Foundation/Jobs/DWCEditorCancellationToken.h"
#include "WetClothing/Foundation/Raster/DWCEditorNormalRasterCore.h"
#include "WetClothing/Foundation/Raster/DWCEditorRasterPostProcess.h"
#include "WetClothing/Foundation/Raster/DWCEditorSurfacePatchRasterBuilder.h"
#include "WetClothing/Modes/Wrinkle/Stroke/WetProceduralRidgeRasterizer.h"

TSharedPtr<FWetWrinkleAccumulatedPreviewJobResult, ESPMode::ThreadSafe>
FWetWrinkleAccumulatedPreviewWorker::Build(
    FWetWrinkleAccumulatedPreviewJobInput Input,
    const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& CancellationToken)
{
    TSharedPtr<FWetWrinkleAccumulatedPreviewJobResult, ESPMode::ThreadSafe> Result =
        MakeShared<FWetWrinkleAccumulatedPreviewJobResult, ESPMode::ThreadSafe>();
    Result->TextureSize = Input.TextureSize;
    Result->WorkingTextureSize = Input.WorkingTextureSize;
    Result->SpatialLeaseOwner = Input.SpatialLeaseOwner;
    if (!Result->WorkingSurface.Initialize(Input.WorkingTextureSize, false) ||
        Input.TextureSize.X <= 0 || Input.TextureSize.Y <= 0)
    {
        Result->bSucceeded = false;
        Result->Error = TEXT("The wrinkle preview resolution is invalid.");
        return Result;
    }

    if (Input.InvalidSurfacePatchCount > 0)
    {
        Result->bSucceeded = false;
        Result->InvalidSurfacePatchCount = Input.InvalidSurfacePatchCount;
        Result->FirstSurfacePatchError = MoveTemp(Input.FirstSurfacePatchError);
        Result->Error = FString::Printf(
            TEXT("%d enabled wrinkle patch(es) cannot build a valid surface projection: %s"),
            Result->InvalidSurfacePatchCount,
            Result->FirstSurfacePatchError.IsEmpty()
                ? TEXT("unknown authored patch error")
                : *Result->FirstSurfacePatchError);
        return Result;
    }

    for (const FWetWrinkleSurfacePatchPreviewInput& SurfacePatch : Input.SurfacePatches)
    {
        FDWCEditorProjectedNormalPatchCommand Command;
        FString ProjectionError;
        if (!FDWCEditorSurfacePatchRasterBuilder::BuildProjectedPatchCommand(
                SurfacePatch,
                Command,
                &ProjectionError,
                &CancellationToken.Get(),
                Input.SurfacePatchProjectionCache.Get(),
                EDWCEditorSurfacePatchCachePolicy::Persistent))
        {
            if (CancellationToken->IsCanceled())
            {
                Result->bSucceeded = false;
                Result->Error = TEXT("The wrinkle surface projection was canceled.");
                return Result;
            }
            Result->bSucceeded = false;
            Result->InvalidSurfacePatchCount = 1;
            Result->FirstSurfacePatchError = MoveTemp(ProjectionError);
            Result->Error = Result->FirstSurfacePatchError.IsEmpty()
                ? TEXT("A wrinkle patch could not build a surface projection.")
                : Result->FirstSurfacePatchError;
            return Result;
        }
        const FDWCEditorRasterResult RasterResult = FDWCEditorNormalRasterCore::RasterizeProjectedPatch(
            Command,
            Result->WorkingSurface,
            &CancellationToken.Get());
        if (RasterResult.bCanceled)
        {
            Result->bSucceeded = false;
            Result->Error = TEXT("The projected wrinkle preview raster was canceled.");
            return Result;
        }
    }
    for (const FWetProceduralRidgeStroke& Stroke : Input.RidgeStrokes)
    {
        if (CancellationToken->IsCanceled())
        {
            Result->bSucceeded = false;
            Result->Error = TEXT("The wrinkle preview job was canceled.");
            return Result;
        }
        const FWetProceduralRidgeRasterResult RasterResult = FWetProceduralRidgeRasterizer::RasterizeToSurface(
            Stroke,
            Result->WorkingSurface,
            nullptr,
            &CancellationToken.Get());
        if (RasterResult.bCanceled)
        {
            Result->bSucceeded = false;
            Result->Error = TEXT("The wrinkle preview job was canceled.");
            return Result;
        }
    }

    bool bEncoded = false;
    if (Input.WorkingTextureSize == Input.TextureSize)
    {
        FDWCEditorRasterPostProcess::EncodeNormalPixels(Result->WorkingSurface, Result->Pixels);
        bEncoded = true;
    }
    else
    {
        bEncoded = FDWCEditorRasterPostProcess::ResampleAndEncodeNormalPixels(
            Result->WorkingSurface,
            Input.TextureSize,
            Result->Pixels);
    }
    if (!bEncoded)
    {
        Result->bSucceeded = false;
        Result->Error = TEXT("Failed to resample the wrinkle preview surface.");
        return Result;
    }
    Result->ResultBytes = Result->WorkingSurface.GetAllocatedSizeBytes() + Result->Pixels.GetAllocatedSize();
    return Result;
}
