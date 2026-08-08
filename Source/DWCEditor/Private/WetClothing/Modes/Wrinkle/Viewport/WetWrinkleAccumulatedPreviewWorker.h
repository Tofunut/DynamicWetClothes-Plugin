// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetClothingWrinkleData.h"
#include "WetClothing/Foundation/Jobs/DWCEditorWorkerJobTypes.h"
#include "WetClothing/Foundation/Raster/DWCEditorRasterTypes.h"

class FDWCEditorCancellationToken;

struct FWetWrinkleAccumulatedPreviewJobInput
{
    FIntPoint                            TextureSize = FIntPoint::ZeroValue;
    FIntPoint                            WorkingTextureSize = FIntPoint::ZeroValue;
    TArray<FDWCEditorNormalStampCommand> Patches;
    TArray<FWetProceduralRidgeStroke>    RidgeStrokes;
};

struct FWetWrinkleAccumulatedPreviewJobResult final : FDWCEditorWorkerJobResult
{
    FIntPoint                     TextureSize = FIntPoint::ZeroValue;
    FIntPoint                     WorkingTextureSize = FIntPoint::ZeroValue;
    TArray<FColor>                Pixels;
    FDWCEditorNormalRasterSurface WorkingSurface;
};

class FWetWrinkleAccumulatedPreviewWorker
{
  public:
    static TSharedPtr<FWetWrinkleAccumulatedPreviewJobResult, ESPMode::ThreadSafe> Build(
        FWetWrinkleAccumulatedPreviewJobInput                               Input,
        const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& CancellationToken);
};
