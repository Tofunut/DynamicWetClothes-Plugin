// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UMaterialInterface;

enum class EDWCTransparencyMaterialBakeResolutionSource : uint8
{
    MaterialPropertyTexture,
    ProceduralFallback,
    MissingMaterialFallback
};

/** Resolution used to evaluate one source material before ray projection. */
struct FDWCTransparencyResolvedMaterialBakeResolution
{
    int32 Resolution = 2048;
    EDWCTransparencyMaterialBakeResolutionSource Source =
        EDWCTransparencyMaterialBakeResolutionSource::ProceduralFallback;
    bool bUsedFallback = true;
    FString SourceDescription;
    FString Identity;
};

/**
 * Resolves source material evaluation independently from the target
 * Transparency output resolution. Base Color, Normal, and Metallic all share
 * one material bake resolution; uniform properties may still store a 1x1
 * physical payload.
 */
class FDWCTransparencyMaterialBakeResolutionResolver
{
  public:
    static constexpr int32 DefaultResolution = 2048;

    static FDWCTransparencyResolvedMaterialBakeResolution Resolve(
        UMaterialInterface* EffectiveMaterial);

    /** Pure policy entry point used by automation tests. */
    static int32 ResolveAutomaticResolutionFromDimensions(
        TConstArrayView<FIntPoint> CandidateDimensions);
};
