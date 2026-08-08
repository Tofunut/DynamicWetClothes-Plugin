// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetClothingWrinkleData.h"
#include "WetClothing/Foundation/Jobs/DWCEditorWorkerJobTypes.h"
#include "WetClothing/Foundation/Preview/Region/DWCEditorPreviewRegionTypes.h"
#include "WetClothing/Foundation/Raster/DWCEditorRasterTypes.h"

class FDWCEditorCancellationToken;

enum class EWetWrinkleIncrementalCommandKind : uint8
{
    Patch,
    Ridge
};

struct FWetWrinkleIncrementalCommand
{
    EWetWrinkleIncrementalCommandKind Kind = EWetWrinkleIncrementalCommandKind::Patch;
    uint64                            Sequence = 0;
    FDWCEditorNormalStampCommand      Patch;
    FWetProceduralRidgeStroke         Ridge;
};

struct FWetWrinkleIncrementalRegionPlan
{
    FIntRect WorkingRect;
    FIntRect OutputRect;
};

struct FWetWrinkleIncrementalRegionSnapshot
{
    FWetWrinkleIncrementalRegionPlan Plan;
    FDWCEditorNormalRasterRegion     Region;
};

struct FWetWrinkleIncrementalPreviewJobInput
{
    FIntPoint                                    TextureSize = FIntPoint::ZeroValue;
    FIntPoint                                    WorkingTextureSize = FIntPoint::ZeroValue;
    TArray<FWetWrinkleIncrementalCommand>        Commands;
    TArray<FWetWrinkleIncrementalRegionSnapshot> Regions;
    FDWCEditorPreviewRegionTarget                Target;
    bool                                         bClearRegionsToFlat = false;
    uint64                                       FirstSequence = 0;
    uint64                                       LastSequence = 0;
};

struct FWetWrinkleIncrementalPreviewJobResult final : FDWCEditorWorkerJobResult
{
    TArray<FDWCEditorNormalRegionPayload> Regions;
    FDWCEditorPreviewRegionTarget         Target;
    uint64                                FirstSequence = 0;
    uint64                                LastSequence = 0;
    uint64                                AffectedPixelCount = 0;
};

class FWetWrinkleIncrementalPreviewWorker final
{
  public:
    static bool BuildRegionPlan(
        const TArray<FWetWrinkleIncrementalCommand>& Commands,
        FIntPoint                                    WorkingTextureSize,
        FIntPoint                                    TextureSize,
        TArray<FWetWrinkleIncrementalRegionPlan>&    OutPlan,
        const TArray<FIntRect>*                      AdditionalWorkingRects = nullptr);

    static FDWCEditorWorkerMemoryEstimate EstimateMemory(
        const TArray<FWetWrinkleIncrementalCommand>&    Commands,
        const TArray<FWetWrinkleIncrementalRegionPlan>& Plan,
        bool                                            bWithCoverage);

    static TSharedPtr<FWetWrinkleIncrementalPreviewJobResult, ESPMode::ThreadSafe> Build(
        FWetWrinkleIncrementalPreviewJobInput                               Input,
        const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& CancellationToken);
};
