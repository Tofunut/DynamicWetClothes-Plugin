//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetClothingTransparencyData.h"
#include "WetClothing/Foundation/Authoring/State/DWCEditorSessionState.h"
#include "WetClothing/Foundation/Jobs/DWCEditorWorkerJobTypes.h"
#include "WetClothing/Foundation/Preview/Region/DWCEditorPreviewRegionTypes.h"
#include "WetClothing/Modes/Transparency/AutoMap/DWCTransparencyAutoMapGenerator.h"
#include "WetClothing/Modes/Transparency/Brush/DWCTransparencyAlphaTileStore.h"

class FDWCEditorCancellationToken;

struct FDWCTransparencyAlphaComposeTileSnapshot
{
    FIntPoint TileCoordinate = FIntPoint::ZeroValue;
    FIntRect Rect;
    TArray<FColor> RevealColor;
    TArray<uint8> OuterEdgeFeather;
};

struct FDWCTransparencyAlphaIncrementalJobInput
{
    TSharedPtr<const FDWCTransparencyAutoBakeResult> AutoResult;
    FDWCTransparencyBrushStroke Stroke;
    TArray<FDWCTransparencyBrushSample> Samples;
    TArray<FIntPoint> OutputTileCoordinates;
    TArray<FDWCTransparencyAlphaTilePayload> SnapshotTiles;
    TArray<FDWCTransparencyAlphaComposeTileSnapshot> ComposeTiles;
    EDWCTransparencyVisualizationMode VisualizationMode = EDWCTransparencyVisualizationMode::Final;
    uint64 ExpectedAlphaRevision = 0;
    FDWCEditorPreviewRegionTarget PreviewTarget;
};

struct FDWCTransparencyAlphaIncrementalJobResult final : FDWCEditorWorkerJobResult
{
    TArray<FDWCTransparencyAlphaTilePayload> AlphaTiles;
    TArray<FDWCEditorBGRA8RegionPayload> PreviewRegions;
    uint64 ExpectedAlphaRevision = 0;
    FDWCEditorPreviewRegionTarget PreviewTarget;
    bool bHasChanges = false;
};

class FDWCTransparencyAlphaIncrementalWorker final
{
  public:
    static FDWCEditorWorkerMemoryEstimate EstimateMemory(
        const TArray<FDWCTransparencyAlphaTilePayload>& SnapshotTiles,
        const TArray<FDWCTransparencyAlphaComposeTileSnapshot>& ComposeTiles,
        int32 OutputTileCount);

    static TSharedPtr<FDWCTransparencyAlphaIncrementalJobResult, ESPMode::ThreadSafe> Build(
        FDWCTransparencyAlphaIncrementalJobInput&& Input,
        const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& CancellationToken);
};
