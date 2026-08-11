//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetClothingTransparencyData.h"
#include "WetClothing/Foundation/Jobs/DWCEditorWorkerJobTypes.h"
#include "WetClothing/Modes/Transparency/Brush/DWCTransparencyRevealColorTileStore.h"

class FDWCEditorCancellationToken;
struct FDWCTransparencySourcePayload;

struct FDWCTransparencyRevealCommitJobInput
{
    TSharedPtr<const FDWCTransparencySourcePayload> SourceResult;
    TArray<FDWCTransparencyRevealColorTilePayload> ModifiedTiles;
    TArray<FDWCTransparencyRevealColorStroke> FallbackStrokes;
    FLinearColor BaseRevealColor = FLinearColor::White;
    float RevealMetallicDarkeningStrength = 0.0f;
    bool bUseSparseTiles = false;
};

struct FDWCTransparencyRevealCommitJobResult final : FDWCEditorWorkerJobResult
{
    TArray<FColor> CorrectedRevealPixels;
    FIntPoint Resolution = FIntPoint::ZeroValue;
    FString SourceSignature;
    bool bUsedSparseTiles = false;
};

class FDWCTransparencyRevealCommitWorker final
{
  public:
    static FDWCEditorWorkerMemoryEstimate EstimateMemory(
        const FDWCTransparencyRevealCommitJobInput& Input);

    static TSharedPtr<FDWCTransparencyRevealCommitJobResult, ESPMode::ThreadSafe> Build(
        FDWCTransparencyRevealCommitJobInput&& Input,
        const TSharedRef<FDWCEditorCancellationToken, ESPMode::ThreadSafe>& CancellationToken);
};
