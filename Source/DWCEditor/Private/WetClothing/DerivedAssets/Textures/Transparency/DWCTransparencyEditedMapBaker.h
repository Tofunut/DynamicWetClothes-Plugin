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

enum class EDWCTransparencyStage4RevealSource : uint8
{
    CorrectedCheckpoint,
    CanonicalReplay
};

/** Conservative reservation made before Stage 4 snapshot preparation starts. */
struct FDWCTransparencyStage4MemoryPlan
{
    uint64 ResidentSharedBytes = 0;
    /** Request inputs retained only while the game-thread snapshot is prepared. */
    uint64 PrepareInputBytes = 0;
    uint64 SnapshotBytes = 0;
    uint64 OutputBytes = 0;
    uint64 ScratchBytes = 0;
    /** Snapshot bytes transferred into the output rather than duplicated. */
    uint64 TransferableSnapshotBytes = 0;

    uint64 GetTotalBytes() const;
    uint64 GetPreparePeakBytes() const;
    uint64 GetWorkerPeakBytes() const;
};

struct FDWCTransparencyEditedMapBakeResult
{
    TObjectPtr<UTexture2D> TransparencyMap = nullptr;
    /** Optional runtime payload: coverage-weighted outer-tangent normal in RG. */
    TObjectPtr<UTexture2D> RevealNormalMap = nullptr;
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
    FIntPoint GetSourceResolution() const;
    const FString& GetSourceBuildSignature() const;
    int32 GetSourceValidHitCount() const;
    int32 GetSourceNoHitCount() const;
    /** Bytes uniquely owned by this immutable bake snapshot. */
    uint64 GetEstimatedPrivateBytes() const;
    /** Bytes returned as final/rebuilt color and Reveal Normal payloads. */
    uint64 GetEstimatedOutputBytes() const;
    /** Snapshot storage that becomes output storage during worker execution. */
    uint64 GetEstimatedTransferableBytes() const;
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
    TArray<FColor> FinalRevealNormalPixels;
    bool bContainsRevealNormal = false;
    int32 AppliedStrokeCount = 0;
    int32 AppliedSampleCount = 0;
    int32 IgnoredNoHitOverridePixelCount = 0;
    bool bAppliedWrinkleSuppression = false;
    /** Created only when Stage 4 had to rebuild a missing/stale Stage 3 checkpoint. */
    bool bRebuiltCorrectedRevealCheckpoint = false;
    /** Stage 2 alpha retained separately; checkpoint RGB is shared with FinalPixels. */
    TArray<uint8> RebuiltCorrectedRevealAlpha;
    FString WarningMessage;
    uint64 ResultBytes = 0;
};

class FDWCTransparencyEditedMapBaker
{
  public:
    /** Estimates a worst-case prepare/worker peak before any full-resolution allocation. */
    static bool BuildMemoryPlan(
        FIntPoint Resolution,
        uint64 SourcePayloadBytes,
        uint64 AuthoringInputBytes,
        bool bRestoresCanonicalArtifacts,
        FDWCTransparencyStage4MemoryPlan& OutPlan,
        FString& OutErrorMessage);

    static bool BuildMemoryPlan(
        FIntPoint Resolution,
        uint64 SourcePayloadBytes,
        uint64 AuthoringInputBytes,
        bool bRestoresCanonicalArtifacts,
        bool bRequiresRevealNormal,
        bool bRequiresOuterIslandID,
        FDWCTransparencyStage4MemoryPlan& OutPlan,
        FString& OutErrorMessage);

    /** Estimates the canonical Stage 2 payload without restoring its Temp artifacts. */
    static uint64 EstimateCanonicalSourcePayloadBytes(FIntPoint Resolution);

    static uint64 EstimateStage4SourcePayloadBytes(
        FIntPoint Resolution,
        bool bRequiresRevealSurface,
        bool bRequiresOuterIslandID);

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
        FDWCTransparencyEditedMapBakeSnapshot& Snapshot,
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
