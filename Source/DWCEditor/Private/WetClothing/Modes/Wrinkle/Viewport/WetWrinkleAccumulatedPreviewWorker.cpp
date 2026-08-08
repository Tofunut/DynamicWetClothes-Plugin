// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "WetClothing/Modes/Wrinkle/Viewport/WetWrinkleAccumulatedPreviewWorker.h"

#include "WetClothing/Foundation/Jobs/DWCEditorCancellationToken.h"
#include "WetClothing/Foundation/Raster/DWCEditorNormalRasterCore.h"
#include "WetClothing/Foundation/Raster/DWCEditorRasterPostProcess.h"
#include "WetClothing/Modes/Wrinkle/Stroke/WetProceduralRidgeRasterizer.h"

TSharedPtr<FWetWrinkleAccumulatedPreviewJobResult, ESPMode::ThreadSafe>
FWetWrinkleAccumulatedPreviewWorker::Build(
    FWetWrinkleAccumulatedPreviewJobInput                               Input,
    const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& CancellationToken)
{
    TSharedPtr<FWetWrinkleAccumulatedPreviewJobResult, ESPMode::ThreadSafe> Result =
        MakeShared<FWetWrinkleAccumulatedPreviewJobResult, ESPMode::ThreadSafe>();
    Result->TextureSize = Input.TextureSize;
    Result->WorkingTextureSize = Input.WorkingTextureSize;
    if (!Result->WorkingSurface.Initialize(Input.WorkingTextureSize, false) ||
        Input.TextureSize.X <= 0 || Input.TextureSize.Y <= 0)
    {
        Result->bSucceeded = false;
        Result->Error = TEXT("The wrinkle preview resolution is invalid.");
        return Result;
    }

    for (const FDWCEditorNormalStampCommand& Patch : Input.Patches)
    {
        const FDWCEditorRasterResult RasterResult = FDWCEditorNormalRasterCore::RasterizeStamp(
            Patch,
            Result->WorkingSurface,
            &CancellationToken.Get());
        if (RasterResult.bCanceled)
        {
            Result->bSucceeded = false;
            Result->Error = TEXT("The wrinkle preview job was canceled.");
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
