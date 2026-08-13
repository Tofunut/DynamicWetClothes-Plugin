//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencySourcePayload.h"

class UWetClothingAsset;
class FDWCEditorCancellationToken;
struct FDWCTransparencyBlueprintHierarchy;
struct FWetClothingTransparencyLayerData;
struct FWetClothingTransparencyTargetSurface;

enum class EDWCTransparencyGenerationPhase : uint8
{
    PreparingTarget,
    RasterizingTarget,
    PreparingSources,
    BakingSourceMaterial,
    ProjectingSamples,
    ComposingResult,
    CommittingResult
};

struct FDWCTransparencyGenerationProgress
{
    EDWCTransparencyGenerationPhase Phase = EDWCTransparencyGenerationPhase::PreparingTarget;
    double OverallFraction = 0.0;
    int32 CompletedItems = 0;
    int32 TotalItems = 0;
    FName SourceName = NAME_None;
    int32 MaterialSlotIndex = INDEX_NONE;
};

using FDWCTransparencyGenerationProgressCallback =
    TFunction<void(const FDWCTransparencyGenerationProgress&)>;

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
        TArray<FString>& OutWarnings,
        const FDWCEditorCancellationToken* CancellationToken = nullptr,
        const FDWCTransparencyBlueprintHierarchy* BlueprintHierarchy = nullptr,
        const FDWCTransparencyGenerationProgressCallback* ProgressCallback = nullptr);

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

  private:
    static bool BuildProjectionSnapshotInternal(
        UWetClothingAsset& WetClothingAsset,
        const FWetClothingTransparencyLayerData& Layer,
        bool bResolveMaterialSurfaces,
        FDWCTransparencyAutoMapSnapshot& OutSnapshot,
        FString& OutErrorMessage,
        const FDWCTransparencyBlueprintHierarchy* BlueprintHierarchy = nullptr,
        const FDWCEditorCancellationToken* CancellationToken = nullptr,
        const FDWCTransparencyGenerationProgressCallback* ProgressCallback = nullptr);

    static FDWCTransparencyAutoMapComputedResult ComputeStreamingProjection(
        UWetClothingAsset& WetClothingAsset,
        FDWCTransparencyAutoMapSnapshot& Snapshot,
        const FDWCEditorCancellationToken* CancellationToken = nullptr,
        const FDWCTransparencyGenerationProgressCallback* ProgressCallback = nullptr);
};
