//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetClothingTransparencyData.h"
#include "WetClothing/Modes/Transparency/Brush/DWCTransparencyAlphaTileStore.h"
#include "WetClothing/Modes/Transparency/Pipeline/DWCTransparencyStageContracts.h"
#include "WetClothing/Modes/Transparency/Processing/DWCWrinkleSuppressionCoverageService.h"

class UWetClothingAsset;
struct FDWCTransparencySourcePayload;

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

/** Complete immutable input contract for Stage 4 preview/bake preparation. */
struct FDWCTransparencyFinalWorkingSet
{
    FDWCTransparencyStageIdentity Identity;
    TSharedPtr<const FDWCTransparencySourcePayload> SourcePayload;
    FDWCTransparencyAlphaWorkingSnapshot Alpha;
    FDWCTransparencyFinalSettingsSnapshot Settings;
    FDWCWrinkleSuppressionDependencySnapshot WrinkleDependency;
    FString SourceSignature;
    FString RevealSignature;
    FString AlphaAuthoringSignature;
    FString SuppressionSettingsSignature;
    FString FinalSignature;
    uint64 AuthoringRevision = 0;
    uint64 OwnedBytes = 0;
    uint64 RetainedBytes = 0;

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
