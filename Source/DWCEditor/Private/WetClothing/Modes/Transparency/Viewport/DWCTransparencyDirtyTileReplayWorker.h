#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetClothingTransparencyData.h"
#include "WetClothing/Foundation/Jobs/DWCEditorWorkerJobTypes.h"
#include "WetClothing/Foundation/Preview/Region/DWCEditorPreviewRegionTypes.h"
#include "WetClothing/Modes/Transparency/AutoMap/DWCTransparencyAutoMapGenerator.h"
#include "WetClothing/Modes/Transparency/Viewport/DWCTransparencyAlphaIncrementalWorker.h"
#include "WetClothing/Modes/Transparency/Viewport/DWCTransparencyRevealColorIncrementalWorker.h"

class FDWCEditorCancellationToken;

enum class EDWCTransparencyDirtyReplayTarget : uint8
{
    Alpha,
    RevealColor
};

struct FDWCTransparencyDirtyTileReplayJobInput
{
    EDWCTransparencyDirtyReplayTarget Target = EDWCTransparencyDirtyReplayTarget::Alpha;
    TSharedPtr<const FDWCTransparencyAutoBakeResult> AutoResult;
    TArray<FDWCTransparencyBrushStroke> AlphaStrokes;
    TArray<FDWCTransparencyRevealColorStroke> RevealColorStrokes;
    int32 BaselineStrokeCount = 0;
    int32 MaterialSlotIndex = INDEX_NONE;
    FLinearColor BaseRevealColor = FLinearColor::White;
    TArray<FIntPoint> DirtyTileCoordinates;
    TArray<FDWCTransparencyAlphaComposeTileSnapshot> AlphaComposeTiles;
    TArray<FDWCTransparencyRevealColorComposeTileSnapshot> RevealComposeTiles;
    EDWCTransparencyVisualizationMode VisualizationMode = EDWCTransparencyVisualizationMode::Final;
    uint64 ExpectedStoreRevision = 0;
    FDWCEditorPreviewRegionTarget PreviewTarget;
};

struct FDWCTransparencyDirtyTileReplayJobResult final : FDWCEditorWorkerJobResult
{
    EDWCTransparencyDirtyReplayTarget Target = EDWCTransparencyDirtyReplayTarget::Alpha;
    TArray<FDWCTransparencyAlphaTilePayload> AlphaTiles;
    TArray<FDWCTransparencyRevealColorTilePayload> RevealColorTiles;
    TArray<FDWCEditorBGRA8RegionPayload> PreviewRegions;
    uint64 ExpectedStoreRevision = 0;
    FDWCEditorPreviewRegionTarget PreviewTarget;
};

class FDWCTransparencyDirtyTileReplayWorker final
{
public:
    static FDWCEditorWorkerMemoryEstimate EstimateMemory(
        const FDWCTransparencyDirtyTileReplayJobInput& Input);

    static TSharedPtr<FDWCTransparencyDirtyTileReplayJobResult, ESPMode::ThreadSafe> Build(
        FDWCTransparencyDirtyTileReplayJobInput&& Input,
        const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& CancellationToken);
};
