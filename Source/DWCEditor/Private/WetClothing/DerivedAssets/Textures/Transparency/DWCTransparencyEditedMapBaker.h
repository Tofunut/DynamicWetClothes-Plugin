//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

class UTexture2D;
class UWetClothingAsset;
class FDWCEditorCancellationToken;
class FDWCWrinkleSuppressionCoverageService;
struct FDWCTransparencySourcePayload;
struct FDWCTransparencyAlphaWorkingSnapshot;
struct FDWCTransparencyFinalSettingsSnapshot;
struct FWetClothingTransparencyLayerData;

struct FDWCTransparencyEditedMapBakeResult
{
    TObjectPtr<UTexture2D> TransparencyMap = nullptr;
    /** Optional linear runtime payload: RG outer-tangent normal, B metallic, A source coverage. */
    TObjectPtr<UTexture2D> RevealSurfaceMap = nullptr;
    int32 AppliedStrokeCount = 0;
    int32 AppliedSampleCount = 0;
    int32 IgnoredNoHitOverridePixelCount = 0;
    bool bAppliedWrinkleSuppression = false;
    FString WarningMessage;
};

class FDWCTransparencyEditedMapBakeSnapshot
{
  public:
    struct FImpl;

    FDWCTransparencyEditedMapBakeSnapshot();
    ~FDWCTransparencyEditedMapBakeSnapshot();
    FDWCTransparencyEditedMapBakeSnapshot(FDWCTransparencyEditedMapBakeSnapshot&&);
    FDWCTransparencyEditedMapBakeSnapshot& operator=(FDWCTransparencyEditedMapBakeSnapshot&&);

    FDWCTransparencyEditedMapBakeSnapshot(const FDWCTransparencyEditedMapBakeSnapshot&) = delete;
    FDWCTransparencyEditedMapBakeSnapshot& operator=(const FDWCTransparencyEditedMapBakeSnapshot&) = delete;

    bool IsValid() const;
    int32 GetMaterialSlotIndex() const;
    FGuid GetLayerGuid() const;
    /** Bytes uniquely owned by this immutable bake snapshot. */
    uint64 GetEstimatedPrivateBytes() const;
    /** Bytes returned as final/rebuilt color and Reveal Surface payloads. */
    uint64 GetEstimatedOutputBytes() const;
    /** Peak scratch used by alpha feathering and dilation. */
    uint64 GetEstimatedScratchBytes() const;
    /** Total worker-private estimate retained for compatibility with existing callers. */
    uint64 GetEstimatedBytes() const;

  private:
    TUniquePtr<FImpl> Impl;
    friend class FDWCTransparencyEditedMapBaker;
};

struct FDWCTransparencyEditedMapComputedResult
{
    bool bSucceeded = false;
    bool bCanceled = false;
    FString Error;
    TArray<FColor> FinalPixels;
    TArray<FColor> FinalRevealSurfacePixels;
    bool bContainsRevealSurface = false;
    int32 AppliedStrokeCount = 0;
    int32 AppliedSampleCount = 0;
    int32 IgnoredNoHitOverridePixelCount = 0;
    bool bAppliedWrinkleSuppression = false;
    /** Created only when Stage 4 had to rebuild a missing/stale Stage 3 checkpoint. */
    bool bRebuiltCorrectedRevealCheckpoint = false;
    TArray<FColor> RebuiltCorrectedRevealPixels;
    FString WarningMessage;
    uint64 ResultBytes = 0;
};

class FDWCTransparencyEditedMapBaker
{
  public:
    static bool BuildSnapshot(
        const UWetClothingAsset& WetClothingAsset,
        const FWetClothingTransparencyLayerData& Layer,
        const FDWCTransparencySourcePayload& SourcePayload,
        FDWCTransparencyEditedMapBakeSnapshot& OutSnapshot,
        FString& OutErrorMessage);

    static bool BuildSnapshot(
        const UWetClothingAsset& WetClothingAsset,
        const FWetClothingTransparencyLayerData& Layer,
        TSharedRef<const FDWCTransparencySourcePayload> SourcePayload,
        FDWCTransparencyEditedMapBakeSnapshot& OutSnapshot,
        FString& OutErrorMessage);

    static bool BuildSnapshot(
        const UWetClothingAsset& WetClothingAsset,
        const FWetClothingTransparencyLayerData& Layer,
        TSharedRef<const FDWCTransparencySourcePayload> SourcePayload,
        TSharedPtr<FDWCWrinkleSuppressionCoverageService> CoverageService,
        FDWCTransparencyEditedMapBakeSnapshot& OutSnapshot,
        FString& OutErrorMessage);

    static bool BuildSnapshot(
        const UWetClothingAsset& WetClothingAsset,
        const FWetClothingTransparencyLayerData& Layer,
        TSharedRef<const FDWCTransparencySourcePayload> SourcePayload,
        FDWCTransparencyAlphaWorkingSnapshot AlphaSnapshot,
        FDWCTransparencyEditedMapBakeSnapshot& OutSnapshot,
        FString& OutErrorMessage);

    static bool BuildSnapshot(
        const UWetClothingAsset& WetClothingAsset,
        const FWetClothingTransparencyLayerData& Layer,
        TSharedRef<const FDWCTransparencySourcePayload> SourcePayload,
        FDWCTransparencyAlphaWorkingSnapshot AlphaSnapshot,
        TSharedPtr<FDWCWrinkleSuppressionCoverageService> CoverageService,
        FDWCTransparencyEditedMapBakeSnapshot& OutSnapshot,
        FString& OutErrorMessage);

    /** Uses an immutable Stage 4 settings snapshot captured at the bake request boundary. */
    static bool BuildSnapshot(
        const UWetClothingAsset& WetClothingAsset,
        const FWetClothingTransparencyLayerData& Layer,
        TSharedRef<const FDWCTransparencySourcePayload> SourcePayload,
        FDWCTransparencyAlphaWorkingSnapshot AlphaSnapshot,
        TSharedPtr<FDWCWrinkleSuppressionCoverageService> CoverageService,
        const FDWCTransparencyFinalSettingsSnapshot& FinalSettings,
        FDWCTransparencyEditedMapBakeSnapshot& OutSnapshot,
        FString& OutErrorMessage);

    static FDWCTransparencyEditedMapComputedResult ComputeSnapshot(
        const FDWCTransparencyEditedMapBakeSnapshot& Snapshot,
        const FDWCEditorCancellationToken* CancellationToken = nullptr);

    static bool CommitComputedResult(
        UWetClothingAsset& WetClothingAsset,
        const FDWCTransparencyEditedMapBakeSnapshot& Snapshot,
        FDWCTransparencyEditedMapComputedResult&& ComputedResult,
        FDWCTransparencyEditedMapBakeResult& OutResult,
        FString& OutErrorMessage);

    static bool IsAutoResultCompatible(
        const FWetClothingTransparencyLayerData& Layer,
        const FDWCTransparencySourcePayload& SourcePayload,
        FString& OutReason);

    static bool IsLayerBakeCurrent(
        const UWetClothingAsset& WetClothingAsset,
        const FWetClothingTransparencyLayerData& Layer,
        FString* OutReason = nullptr);

    static bool Bake(
        UWetClothingAsset& WetClothingAsset,
        FWetClothingTransparencyLayerData& Layer,
        const FDWCTransparencySourcePayload& SourcePayload,
        FDWCTransparencyEditedMapBakeResult& OutResult,
        FString& OutErrorMessage);
};
