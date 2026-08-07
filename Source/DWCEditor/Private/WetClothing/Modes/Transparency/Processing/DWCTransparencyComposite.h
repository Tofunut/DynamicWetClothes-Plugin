//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Authoring/State/DWCEditorSessionState.h"
#include "WetClothing/Modes/Transparency/AutoMap/DWCTransparencyAutoMapGenerator.h"

class FDWCEditorCancellationToken;
class FDWCTransparencyAlphaTileStore;
class FDWCTransparencyRevealColorTileStore;

struct FDWCTransparencyPixelComposeContext
{
    const FDWCTransparencyAutoBakeResult* AutoResult = nullptr;
    // Optional authored reveal-color layer. When present it replaces the
    // immutable auto-bake inner color for display and bake composition.
    TConstArrayView<FColor> RevealColorBuffer;
    // Preferred sparse authoring representation. Missing tiles resolve to
    // AutoResult->InnerColorBuffer.
    const FDWCTransparencyRevealColorTileStore* RevealColorTileStore = nullptr;
    TConstArrayView<uint8> ManualPremultipliedBuffer;
    TConstArrayView<uint8> ManualWeightBuffer;
    // Preferred sparse authoring representation. Dense arrays remain only as
    // a compatibility input for bake/full-rebuild producers.
    const FDWCTransparencyAlphaTileStore* ManualAlphaTileStore = nullptr;
    TConstArrayView<uint8> WrinkleSuppressionBuffer;
    TConstArrayView<uint8> OuterEdgeFeatherBuffer;
    EDWCTransparencyVisualizationMode VisualizationMode = EDWCTransparencyVisualizationMode::Final;
    float TransparencyStrength = 0.4f;
    float WrinkleSuppressionStrength = 0.6f;
    float MaximumHitDistance = KINDA_SMALL_NUMBER;
    // Preview textures retain authored alpha and defer strength, suppression,
    // and suppression visualization to the transient preview material.
    bool bDeferPresentationToMaterial = false;

    bool IsValid() const;
};

class FDWCTransparencyComposite
{
  public:
    static float ComputeMaximumHitDistance(const FDWCTransparencyAutoBakeResult& AutoResult);

    static float ResolveEditedAlpha(
        const FDWCTransparencyPixelComposeContext& Context,
        int32 PixelIndex);

    static FColor ComposeVisualizationPixel(
        const FDWCTransparencyPixelComposeContext& Context,
        int32 PixelIndex,
        TOptional<float> EditedAlphaOverride = TOptional<float>(),
        TOptional<FColor> RevealColorOverride = TOptional<FColor>(),
        TOptional<uint8> WrinkleSuppressionOverride = TOptional<uint8>(),
        TOptional<uint8> OuterEdgeFeatherOverride = TOptional<uint8>());

    static bool ComposeVisualizationPixels(
        const FDWCTransparencyPixelComposeContext& Context,
        TArray<FColor>& OutPixels,
        const FDWCEditorCancellationToken* CancellationToken = nullptr);

    static float ResolveFinalAlpha(
        float EditedAlpha,
        float TransparencyStrength,
        float WrinkleSuppression,
        float WrinkleSuppressionStrength);

    static uint8 ResolveFinalAlpha8(
        float EditedAlpha,
        float TransparencyStrength,
        uint8 WrinkleSuppression,
        float WrinkleSuppressionStrength);

    static bool BuildCoverageEdgeFeatherBuffer(
        FIntPoint Resolution,
        const TArray<uint8>& OuterCoverage,
        float FeatherPixels,
        TArray<uint8>& OutBuffer);
};
