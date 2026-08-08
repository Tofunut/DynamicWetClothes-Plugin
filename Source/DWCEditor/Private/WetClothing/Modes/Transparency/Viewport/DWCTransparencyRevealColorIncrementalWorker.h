// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetClothingTransparencyData.h"
#include "WetClothing/Foundation/Authoring/State/DWCEditorSessionState.h"
#include "WetClothing/Foundation/Jobs/DWCEditorWorkerJobTypes.h"
#include "WetClothing/Foundation/Preview/Region/DWCEditorPreviewRegionTypes.h"
#include "WetClothing/Modes/Transparency/AutoMap/DWCTransparencyAutoMapGenerator.h"
#include "WetClothing/Modes/Transparency/Brush/DWCTransparencyAlphaTileStore.h"
#include "WetClothing/Modes/Transparency/Brush/DWCTransparencyRevealColorTileStore.h"

class FDWCEditorCancellationToken;

struct FDWCTransparencyRevealColorComposeTileSnapshot
{
    FIntPoint     TileCoordinate = FIntPoint::ZeroValue;
    FIntRect      Rect;
    TArray<uint8> ManualPremultiplied;
    TArray<uint8> ManualWeight;
    TArray<uint8> OuterEdgeFeather;
};

struct FDWCTransparencyRevealColorIncrementalJobInput
{
    TSharedPtr<const FDWCTransparencyAutoBakeResult>       AutoResult;
    FDWCTransparencyRevealColorStroke                      Stroke;
    TArray<FDWCTransparencyBrushSample>                    Samples;
    FLinearColor                                           BaseRevealColor = FLinearColor::White;
    TArray<FIntPoint>                                      OutputTileCoordinates;
    TArray<FDWCTransparencyRevealColorTilePayload>         SnapshotTiles;
    TArray<FDWCTransparencyRevealColorComposeTileSnapshot> ComposeTiles;
    EDWCTransparencyVisualizationMode                      VisualizationMode = EDWCTransparencyVisualizationMode::Final;
    uint64                                                 ExpectedRevealRevision = 0;
    FDWCEditorPreviewRegionTarget                          PreviewTarget;
};

struct FDWCTransparencyRevealColorIncrementalJobResult final : FDWCEditorWorkerJobResult
{
    TArray<FDWCTransparencyRevealColorTilePayload> RevealTiles;
    TArray<FDWCEditorBGRA8RegionPayload>           PreviewRegions;
    uint64                                         ExpectedRevealRevision = 0;
    FDWCEditorPreviewRegionTarget                  PreviewTarget;
    bool                                           bHasChanges = false;
};

class FDWCTransparencyRevealColorIncrementalWorker final
{
  public:
    static FDWCEditorWorkerMemoryEstimate EstimateMemory(
        const TArray<FDWCTransparencyRevealColorTilePayload>&         SnapshotTiles,
        const TArray<FDWCTransparencyRevealColorComposeTileSnapshot>& ComposeTiles,
        int32                                                         OutputTileCount);

    static TSharedPtr<FDWCTransparencyRevealColorIncrementalJobResult, ESPMode::ThreadSafe> Build(
        FDWCTransparencyRevealColorIncrementalJobInput&&                    Input,
        const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& CancellationToken);
};
