#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Authoring/State/DWCEditorSessionState.h"
#include "WetClothing/Modes/Transparency/AutoMap/DWCTransparencyAutoMapGenerator.h"

class FDWCEditorCancellationToken;

struct FDWCTransparencyPixelComposeContext
{
    const FDWCTransparencyAutoBakeResult* AutoResult = nullptr;
    // Optional authored reveal-color layer. When present it replaces the
    // immutable auto-bake inner color for display and bake composition.
    TConstArrayView<FColor> RevealColorBuffer;
    TConstArrayView<uint8> ManualPremultipliedBuffer;
    TConstArrayView<uint8> ManualWeightBuffer;
    TConstArrayView<uint8> WrinkleSuppressionBuffer;
    TConstArrayView<uint8> OuterEdgeFeatherBuffer;
    EDWCTransparencyVisualizationMode VisualizationMode = EDWCTransparencyVisualizationMode::Final;
    float TransparencyStrength = 0.4f;
    float WrinkleSuppressionStrength = 0.6f;
    float MaximumHitDistance = KINDA_SMALL_NUMBER;

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
        TOptional<float> EditedAlphaOverride = TOptional<float>());

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
