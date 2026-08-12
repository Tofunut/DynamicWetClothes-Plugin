// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetClothingTransparencyData.h"

class UMaterialInterface;
class UTexture2D;
class UWetClothingAsset;

enum class EDWCTransparencyResolutionSource : uint8
{
    Override,
    TargetBaseColorTexture,
    ProceduralFallback,
    MissingTargetFallback
};

struct FDWCTransparencyResolvedOutputResolution
{
    int32 Size = 2048;
    EDWCTransparencyResolutionSource Source =
        EDWCTransparencyResolutionSource::ProceduralFallback;
    bool bUsedFallback = true;
    FString SourceDescription;
    FString Identity;

    FIntPoint GetExtent() const { return FIntPoint(Size, Size); }
    bool IsValid() const { return Size > 0 && !Identity.IsEmpty(); }
};

/** Canonical per-layer resolution policy shared by preview, signatures and final bake. */
class FDWCTransparencyResolutionResolver
{
  public:
    static constexpr int32 MinimumResolution = 256;
    static constexpr int32 DefaultResolution = 2048;
    static constexpr int32 MaximumResolution = 4096;

    static FDWCTransparencyResolvedOutputResolution Resolve(
        const UWetClothingAsset& Asset,
        const FWetClothingTransparencyLayerData& Layer);

    /** Resolves the material-driven branch even when the layer currently uses Override mode. */
    static FDWCTransparencyResolvedOutputResolution ResolveAutomatic(
        const UWetClothingAsset& Asset,
        const FWetClothingTransparencyLayerData& Layer);

    static int32 NormalizeResolution(int32 RequestedResolution);

    /** Pure policy entry point used by automation tests and non-material callers. */
    static int32 ResolveAutomaticResolutionFromDimensions(
        TConstArrayView<FIntPoint> CandidateDimensions);
};
