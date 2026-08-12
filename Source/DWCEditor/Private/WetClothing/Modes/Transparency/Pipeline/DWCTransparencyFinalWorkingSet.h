//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetClothingTransparencyData.h"
#include "WetClothing/Modes/Transparency/Brush/DWCTransparencyAlphaTileStore.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyStageContracts.h"
#include "WetClothing/Modes/Transparency/Processing/DWCWrinkleSuppressionCoverageService.h"

class UWetClothingAsset;
struct FDWCTransparencySourcePayload;

/**
 * Immutable Stage 4 domain extracted from the canonical Stage 2/3 payload.
 *
 * Stage 4 edits alpha only. Keeping color, reveal-surface, hit-distance, and
 * source-priority buffers out of this contract prevents final-alpha jobs from
 * retaining the much larger authoring payload merely to clip brush samples.
 */
struct FDWCTransparencyAlphaDomainSnapshot
{
    FGuid LayerGuid;
    int32 MaterialSlotIndex = INDEX_NONE;
    FIntPoint Resolution = FIntPoint::ZeroValue;
    FString OutputResolutionIdentity;
    FString SourceSignature;
    TArray<uint8> BaseAlpha;
    TArray<uint8> OuterCoverage;
    TArray<uint16> OuterIslandIDs;
    TBitArray<> ValidSource;

    static TSharedPtr<const FDWCTransparencyAlphaDomainSnapshot> Create(
        const FDWCTransparencySourcePayload& SourcePayload,
        FString* OutError = nullptr,
        bool bIncludeOuterIslandIDs = true);

    bool IsValid(FString* OutError = nullptr) const;
    uint64 GetAllocatedBytes() const;
    int32 ResolveOuterIslandIDAtUV(
        const FVector2D& PositionUV,
        int32 FallbackUVIslandID,
        bool bWrap) const;
    bool MatchesOuterIslandID(int32 PixelIndex, int32 UVIslandID) const;
};

struct FDWCTransparencyFinalSettingsSnapshot
{
    float TransparencyStrength = 0.4f;
    float WrinkleSuppressionStrength = 0.6f;
    float WrinkleMaskThreshold = 0.15f;
    float WrinkleMaskSoftness = 0.05f;
    int32 PaddingPixels = 8;
    float EdgeFeatherPixels = 4.0f;

    static FDWCTransparencyFinalSettingsSnapshot FromAuthoredData(
        const FWetClothingTransparencyData& Data);
    bool IsValid(FString* OutError = nullptr) const;
};

enum class EDWCTransparencyAlphaSnapshotMode : uint8
{
    SparseTiles,
    StrokeReplay
};

/** Immutable, worker-safe copy of the authored alpha state. */
struct FDWCTransparencyAlphaWorkingSnapshot
{
    EDWCTransparencyAlphaSnapshotMode Mode = EDWCTransparencyAlphaSnapshotMode::StrokeReplay;
    FIntPoint Resolution = FIntPoint::ZeroValue;
    uint64 StoreRevision = 0;
    int32 BaselineStrokeCount = 0;
    int32 AuthoredStrokeCount = 0;
    int32 AppliedSampleCount = 0;
    TArray<FDWCTransparencyAlphaTilePayload> ModifiedTiles;
    TArray<FDWCTransparencyBrushStroke> FallbackStrokes;

    bool IsValid(FString* OutError = nullptr) const;
    uint64 GetAllocatedBytes() const;
};

/** Immutable alpha-only input contract for Stage 4 preview/bake preparation. */
struct FDWCTransparencyFinalWorkingSet
{
    FDWCTransparencyStageIdentity Identity;
    TSharedPtr<const FDWCTransparencyAlphaDomainSnapshot> AlphaDomain;
    FDWCTransparencyAlphaWorkingSnapshot Alpha;
    FDWCTransparencyFinalSettingsSnapshot Settings;
    FDWCWrinkleSuppressionDependencySnapshot WrinkleDependency;
    FString SourceSignature;
    FString RevealSignature;
    /** Runtime Reveal Normal identity. It is independent of Stage 3 color and Stage 4 alpha edits. */
    FString RevealNormalSignature;
    FString AlphaAuthoringSignature;
    FString SuppressionSettingsSignature;
    FString FinalAlphaSignature;
    FString FinalSignature;
    uint64 AuthoringRevision = 0;
    uint64 OwnedBytes = 0;
    uint64 RetainedBytes = 0;
    /** Stage 2/3 must author a Reveal Surface payload for this source type. */
    bool bRequiresRevealSurface = false;
    /** Runtime material binding currently requires the baked Reveal Normal payload. */
    bool bRequiresRuntimeRevealNormal = false;

    bool IsValid(FString* OutError = nullptr) const;
};

struct FDWCTransparencyFinalCurrentness
{
    EDWCTransparencyStaleReason Reason = EDWCTransparencyStaleReason::None;
    FString Detail;

    bool IsCurrent() const { return Reason == EDWCTransparencyStaleReason::None; }
};

class FDWCTransparencyFinalWorkingSetBuilder
{
  public:
    static bool Build(
        const UWetClothingAsset& Asset,
        const FWetClothingTransparencyLayerData& Layer,
        TSharedPtr<const FDWCTransparencySourcePayload> SourcePayload,
        const FDWCTransparencyFinalSettingsSnapshot& Settings,
        FDWCTransparencyAlphaWorkingSnapshot AlphaSnapshot,
        FDWCWrinkleSuppressionDependencySnapshot WrinkleDependency,
        uint64 AuthoringRevision,
        FDWCTransparencyFinalWorkingSet& OutWorkingSet,
        FString& OutError);

    static FDWCTransparencyFinalCurrentness EvaluateCurrentness(
        const FWetClothingBakedTransparencyMap* BakedMap,
        const FDWCTransparencyFinalWorkingSet& WorkingSet);
};
