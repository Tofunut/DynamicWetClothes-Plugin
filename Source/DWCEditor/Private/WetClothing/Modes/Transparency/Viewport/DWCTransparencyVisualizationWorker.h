#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Authoring/State/DWCEditorSessionState.h"
#include "WetClothing/Foundation/Jobs/DWCEditorWorkerJobTypes.h"
#include "WetClothing/Modes/Transparency/AutoMap/DWCTransparencyAutoMapGenerator.h"
#include "DataAssets/WetClothingTransparencyData.h"

class FDWCEditorCancellationToken;

struct FDWCTransparencyVisualizationJobInput
{
    // The auto bake result is immutable while a visualization job is running.
    TSharedPtr<const FDWCTransparencyAutoBakeResult> AutoResult;
    TArray<FColor> RevealColorBuffer;
    TArray<uint8> ManualPremultipliedBuffer;
    TArray<uint8> ManualWeightBuffer;
    TArray<uint8> WrinkleSuppressionBuffer;
    TArray<uint8> OuterEdgeFeatherBuffer;
    bool bRebuildManualOverridesFromStrokes = false;
    bool bRebuildRevealColorFromStrokes = false;
    // Only the replay inputs are captured. Copying the full layer also copies
    // unrelated baked metadata and source settings into every worker request.
    TArray<FDWCTransparencyBrushStroke> EditableStrokes;
    TArray<FDWCTransparencyRevealColorStroke> RevealColorPaintStrokes;
    int32 BaselineStrokeCount = 0;
    FLinearColor BaseRevealColor = FLinearColor::White;
    int32 MaterialSlotIndex = INDEX_NONE;
    int32 UVChannelIndex = INDEX_NONE;
    EDWCTransparencyVisualizationMode VisualizationMode = EDWCTransparencyVisualizationMode::Final;
    float TransparencyPreviewStrength = 0.4f;
    float WrinkleSuppressionStrength = 0.6f;
};

struct FDWCTransparencyVisualizationJobResult final : FDWCEditorWorkerJobResult
{
    FIntPoint Resolution = FIntPoint::ZeroValue;
    TArray<FColor> Pixels;
    TArray<uint8> RebuiltManualPremultipliedBuffer;
    TArray<uint8> RebuiltManualWeightBuffer;
    bool bIncludesRebuiltManualOverrides = false;
    TArray<FColor> RebuiltRevealColorBuffer;
    bool bIncludesRebuiltRevealColor = false;
};

class FDWCTransparencyVisualizationWorker
{
  public:
    static TSharedPtr<FDWCTransparencyVisualizationJobResult, ESPMode::ThreadSafe> Build(
        FDWCTransparencyVisualizationJobInput&& Input,
        const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& CancellationToken);
};
