//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencySourcePayload.h"

class UWetClothingAsset;
class FDWCEditorCancellationToken;
struct FWetClothingTransparencyLayerData;
struct FWetClothingTransparencyTargetSurface;

/** Game-thread capture for same-mesh projection. Worker code only reads copied geometry and texture pixels. */
class FDWCTransparencyAutoMapSnapshot
{
  public:
    struct FImpl;

    FDWCTransparencyAutoMapSnapshot();
    ~FDWCTransparencyAutoMapSnapshot();
    FDWCTransparencyAutoMapSnapshot(FDWCTransparencyAutoMapSnapshot&&);
    FDWCTransparencyAutoMapSnapshot& operator=(FDWCTransparencyAutoMapSnapshot&&);

    FDWCTransparencyAutoMapSnapshot(const FDWCTransparencyAutoMapSnapshot&) = delete;
    FDWCTransparencyAutoMapSnapshot& operator=(const FDWCTransparencyAutoMapSnapshot&) = delete;

    bool IsValid() const;
    int32 GetMaterialSlotIndex() const;
    FGuid GetLayerGuid() const;
    uint64 GetEstimatedBytes() const;

  private:
    TUniquePtr<FImpl> Impl;
    friend class FDWCTransparencyAutoMapGenerator;
};

struct FDWCTransparencyAutoMapComputedResult
{
    bool bSucceeded = false;
    bool bCanceled = false;
    FString Error;
    FString Summary;
    TArray<FString> Warnings;
    FDWCTransparencySourcePayload SourcePayload;
    uint64 ResultBytes = 0;
};

class FDWCTransparencyAutoMapGenerator
{
  public:
    /** Builds only the metadata/signature needed for stale checks; no geometry or pixel buffers are generated. */
    static bool BuildSignatureOnlyResult(
        const UWetClothingAsset& WetClothingAsset,
        const FWetClothingTransparencyLayerData& Layer,
        FDWCTransparencySourcePayload& OutResult,
        FString& OutErrorMessage);

    static bool BuildSameMeshSnapshot(
        UWetClothingAsset& WetClothingAsset,
        const FWetClothingTransparencyLayerData& Layer,
        FDWCTransparencyAutoMapSnapshot& OutSnapshot,
        FString& OutErrorMessage);

    /** Dispatches Type 1/2/3 ray-projected sources into the common snapshot contract. */
    static bool BuildProjectionSnapshot(
        UWetClothingAsset& WetClothingAsset,
        const FWetClothingTransparencyLayerData& Layer,
        FDWCTransparencyAutoMapSnapshot& OutSnapshot,
        FString& OutErrorMessage);

    static FDWCTransparencyAutoMapComputedResult ComputeSameMeshSnapshot(
        FDWCTransparencyAutoMapSnapshot& Snapshot,
        const FDWCEditorCancellationToken* CancellationToken = nullptr);

    static bool BuildTargetSurfaceBuffers(
        const UWetClothingAsset& WetClothingAsset,
        const FWetClothingTransparencyTargetSurface& TargetSurface,
        int32 LODIndex,
        FIntPoint Resolution,
        TArray<uint8>& OutCoverageBuffer,
        TArray<uint16>& OutIslandIDBuffer,
        int32* OutOuterSampleCount,
        int32* OutOverlappedPixelCount,
        FString& OutErrorMessage);

    static bool GenerateSameMesh(
        UWetClothingAsset& WetClothingAsset,
        const FWetClothingTransparencyLayerData& Layer,
        FDWCTransparencySourcePayload& OutResult,
        FString& OutSummary,
        TArray<FString>& OutWarnings);

    static bool GenerateBaseRevealColorMap(
        const UWetClothingAsset& WetClothingAsset,
        const FWetClothingTransparencyLayerData& Layer,
        FDWCTransparencySourcePayload& OutResult,
        FString& OutSummary,
        TArray<FString>& OutWarnings);

    /** Replays authored reveal-color strokes into a separate color layer. The
     *  auto-bake result remains an immutable base for preview workers. */
    static void ApplyRevealColorPaintStrokes(
        const FDWCTransparencySourcePayload& SourcePayload,
        const TArray<FDWCTransparencyRevealColorStroke>& Strokes,
        int32 MaterialSlotIndex,
        const FLinearColor& BaseRevealColor,
        TArray<FColor>& InOutRevealColorBuffer);
};
