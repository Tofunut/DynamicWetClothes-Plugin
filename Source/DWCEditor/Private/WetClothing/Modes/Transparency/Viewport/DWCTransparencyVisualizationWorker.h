//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Authoring/State/DWCEditorSessionState.h"
#include "WetClothing/Foundation/Jobs/DWCEditorWorkerJobTypes.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencySourcePayload.h"
#include "WetClothing/Modes/Transparency/Brush/DWCTransparencyAlphaTileStore.h"
#include "WetClothing/Modes/Transparency/Brush/DWCTransparencyRevealColorTileStore.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyFinalWorkingSet.h"
#include "DataAssets/WetClothingTransparencyData.h"

class FDWCEditorCancellationToken;

struct FDWCTransparencyVisualizationJobInput
{
    // The auto bake result is immutable while a visualization job is running.
    TSharedPtr<const FDWCTransparencySourcePayload> SourcePayload;
    FDWCTransparencyRevealColorTileStore RevealColorTileStore;
    FDWCTransparencyAlphaWorkingSnapshot AlphaSnapshot;
    TArray<uint8> OuterEdgeFeatherBuffer;
    bool bRebuildRevealColorFromStrokes = false;
    // Only the replay inputs are captured. Copying the full layer also copies
    // unrelated baked metadata and source settings into every worker request.
    TArray<FDWCTransparencyRevealColorStroke> RevealColorPaintStrokes;
    int32 BaselineStrokeCount = 0;
    FLinearColor BaseRevealColor = FLinearColor::White;
    int32 MaterialSlotIndex = INDEX_NONE;
    int32 UVChannelIndex = INDEX_NONE;
    EDWCTransparencyVisualizationMode VisualizationMode = EDWCTransparencyVisualizationMode::Final;
};

struct FDWCTransparencyVisualizationJobResult final : FDWCEditorWorkerJobResult
{
    FIntPoint Resolution = FIntPoint::ZeroValue;
    TArray<FColor> Pixels;
    FDWCTransparencyAlphaWorkingSnapshot MaterializedAlphaSnapshot;
    bool bIncludesMaterializedAlphaSnapshot = false;
    FDWCTransparencyRevealColorTileStore RebuiltRevealColorTileStore;
    bool bIncludesRebuiltRevealColor = false;
};

class FDWCTransparencyVisualizationWorker
{
  public:
    static TSharedPtr<FDWCTransparencyVisualizationJobResult, ESPMode::ThreadSafe> Build(
        FDWCTransparencyVisualizationJobInput&& Input,
        const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& CancellationToken);
};
