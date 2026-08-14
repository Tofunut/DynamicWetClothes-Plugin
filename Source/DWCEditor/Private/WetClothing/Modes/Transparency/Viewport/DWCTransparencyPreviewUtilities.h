// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetClothingTransparencyData.h"
#include "WetClothing/Foundation/Jobs/DWCEditorWorkerJobTypes.h"
#include "WetClothing/Foundation/TextureWorkspace/DWCEditorTextureWorkspaceTypes.h"

class UWetClothingAsset;
struct FDWCTransparencySourcePayload;
class FDWCTransparencyRevealColorTileStore;

DECLARE_LOG_CATEGORY_EXTERN(LogWetTransparencyPreviewViewport, Log, All);

namespace UE::DWCEditor::TransparencyPreview
{
FDWCEditorTextureKey MakeTextureKey(
    const UWetClothingAsset* Asset,
    EDWCEditorTexturePurpose Purpose,
    int32 MaterialSlotIndex,
    const FGuid& LayerGuid);

FDWCEditorTextureDescriptor MakeColorDescriptor(
    const FIntPoint& Size,
    TextureAddress Address);

FDWCEditorTextureDescriptor MakeMaskDescriptor(
    const FIntPoint& Size,
    TextureAddress Address);

uint64 GetStrokeSnapshotBytes(
    const TArray<FDWCTransparencyBrushStroke>& Strokes,
    const TArray<FDWCTransparencyRevealColorStroke>& RevealColorStrokes);

FDWCEditorWorkerMemoryEstimate EstimateVisualizationMemory(
    const FDWCTransparencySourcePayload& SourcePayload,
    const FDWCTransparencyRevealColorTileStore& RevealColorTileStore,
    uint64 AlphaSnapshotBytes,
    const TArray<uint8>& OuterEdgeFeatherBuffer,
    const TArray<FDWCTransparencyRevealColorStroke>& RevealColorStrokes,
    bool bMaterializeAlpha,
    bool bRebuildRevealColor);
}
