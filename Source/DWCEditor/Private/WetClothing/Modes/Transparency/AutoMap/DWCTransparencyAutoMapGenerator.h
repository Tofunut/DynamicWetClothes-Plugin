// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetClothingTransparencyData.h"

class UWetClothingAsset;
class FDWCEditorCancellationToken;
struct FWetClothingTransparencyLayerData;
struct FWetClothingTransparencyTargetSurface;

struct FDWCTransparencySourceHitStats
{
    int32 PriorityIndex = INDEX_NONE;
    int32 MaterialSlotIndex = INDEX_NONE;
    FName MaterialSlotName;
    int32 HitCount = 0;
};

struct FDWCTransparencyAutoBakeResult
{
    static constexpr uint16 InvalidOuterIslandID = MAX_uint16;

    static bool CanEncodeOuterIslandID(const int32 IslandID)
    {
        return IslandID >= 0 && IslandID < static_cast<int32>(InvalidOuterIslandID);
    }

    static uint16 EncodeOuterIslandID(const int32 IslandID)
    {
        return CanEncodeOuterIslandID(IslandID)
                   ? static_cast<uint16>(IslandID)
                   : InvalidOuterIslandID;
    }

    static int32 DecodeOuterIslandID(const uint16 IslandID)
    {
        return IslandID != InvalidOuterIslandID
                   ? static_cast<int32>(IslandID)
                   : INDEX_NONE;
    }

    static bool MatchesOuterIslandID(const uint16 EncodedIslandID, const int32 IslandID)
    {
        return IslandID == INDEX_NONE ||
               (CanEncodeOuterIslandID(IslandID) && EncodedIslandID == static_cast<uint16>(IslandID));
    }

    /** Resolves the texture-space island used consistently by hover, live paint, and stroke replay. */
    int32 ResolveOuterIslandIDAtUV(
        const FVector2D& PositionUV,
        int32            FallbackUVIslandID,
        bool             bWrap) const;

    FGuid          LayerGuid;
    int32          MaterialSlotIndex = INDEX_NONE;
    int32          UVChannelIndex = 0;
    int32          LODIndex = 0;
    FIntPoint      Resolution = FIntPoint(0, 0);
    FString        BuildSignature;
    int32          OuterSampleCount = 0;
    int32          ValidHitCount = 0;
    int32          NoHitCount = 0;
    int32          OverlappedUVPixelCount = 0;
    TArray<FColor> InnerColorBuffer;
    TArray<uint8>  AutoAlphaBuffer;
    // Target-slot UV coverage is separate from ray-hit validity. It is used to
    // feather island edges and dilate only outside the target surface.
    TArray<uint8> OuterCoverageBuffer;
    // Editor working data. Used to clip brush edits to the UV island under the
    // cursor so painting across texture seams does not bleed into neighboring
    // islands.
    TArray<uint16>                         OuterIslandIDBuffer;
    TBitArray<>                            ValidHitBuffer;
    TArray<float>                          HitDistanceBuffer;
    TArray<int16>                          SourcePriorityBuffer;
    TArray<FDWCTransparencySourceHitStats> SourceStats;

    // A generated result contains pre-final auto alpha. A baked baseline already
    // contains authoring strength, wrinkle suppression, feathering, and padding.
    bool  bIsFinalBakedBaseline = false;
    int32 BaselineStrokeCount = 0;
    FGuid BaselineBakeGuid;

    /**
     * Returns the heap storage retained by this immutable result. This is
     * intentionally explicit because visualization jobs share the result
     * instead of making a deep copy, but the scheduler still needs to account
     * for the resident snapshot while the job is active.
     */
    uint64 GetAllocatedBytes() const
    {
        return static_cast<uint64>(sizeof(FDWCTransparencyAutoBakeResult)) +
               static_cast<uint64>(BuildSignature.GetAllocatedSize()) +
               static_cast<uint64>(InnerColorBuffer.GetAllocatedSize()) +
               static_cast<uint64>(AutoAlphaBuffer.GetAllocatedSize()) +
               static_cast<uint64>(OuterCoverageBuffer.GetAllocatedSize()) +
               static_cast<uint64>(OuterIslandIDBuffer.GetAllocatedSize()) +
               static_cast<uint64>(ValidHitBuffer.GetAllocatedSize()) +
               static_cast<uint64>(HitDistanceBuffer.GetAllocatedSize()) +
               static_cast<uint64>(SourcePriorityBuffer.GetAllocatedSize()) +
               static_cast<uint64>(SourceStats.GetAllocatedSize());
    }
};

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

    bool   IsValid() const;
    int32  GetMaterialSlotIndex() const;
    FGuid  GetLayerGuid() const;
    uint64 GetEstimatedBytes() const;

  private:
    TUniquePtr<FImpl> Impl;
    friend class FDWCTransparencyAutoMapGenerator;
};

struct FDWCTransparencyAutoMapComputedResult
{
    bool                           bSucceeded = false;
    bool                           bCanceled = false;
    FString                        Error;
    FString                        Summary;
    TArray<FString>                Warnings;
    FDWCTransparencyAutoBakeResult AutoResult;
    uint64                         ResultBytes = 0;
};

class FDWCTransparencyAutoMapGenerator
{
  public:
    /** Builds only the metadata/signature needed for stale checks; no geometry or pixel buffers are generated. */
    static bool BuildSignatureOnlyResult(
        const UWetClothingAsset&                 WetClothingAsset,
        const FWetClothingTransparencyLayerData& Layer,
        FDWCTransparencyAutoBakeResult&          OutResult,
        FString&                                 OutErrorMessage);

    static bool BuildSameMeshSnapshot(
        const UWetClothingAsset&                 WetClothingAsset,
        const FWetClothingTransparencyLayerData& Layer,
        FDWCTransparencyAutoMapSnapshot&         OutSnapshot,
        FString&                                 OutErrorMessage);

    static FDWCTransparencyAutoMapComputedResult ComputeSameMeshSnapshot(
        FDWCTransparencyAutoMapSnapshot&   Snapshot,
        const FDWCEditorCancellationToken* CancellationToken = nullptr);

    static bool BuildTargetSurfaceBuffers(
        const UWetClothingAsset&                     WetClothingAsset,
        const FWetClothingTransparencyTargetSurface& TargetSurface,
        int32                                        LODIndex,
        FIntPoint                                    Resolution,
        TArray<uint8>&                               OutCoverageBuffer,
        TArray<uint16>&                              OutIslandIDBuffer,
        int32*                                       OutOuterSampleCount,
        int32*                                       OutOverlappedPixelCount,
        FString&                                     OutErrorMessage);

    static bool GenerateSameMesh(
        const UWetClothingAsset&                 WetClothingAsset,
        const FWetClothingTransparencyLayerData& Layer,
        FDWCTransparencyAutoBakeResult&          OutResult,
        FString&                                 OutSummary,
        TArray<FString>&                         OutWarnings);

    static bool GenerateBaseRevealColorMap(
        const UWetClothingAsset&                 WetClothingAsset,
        const FWetClothingTransparencyLayerData& Layer,
        FDWCTransparencyAutoBakeResult&          OutResult,
        FString&                                 OutSummary,
        TArray<FString>&                         OutWarnings);

    /** Replays authored reveal-color strokes into a separate color layer. The
     *  auto-bake result remains an immutable base for preview workers. */
    static void ApplyRevealColorPaintStrokes(
        const FDWCTransparencyAutoBakeResult&            AutoResult,
        const TArray<FDWCTransparencyRevealColorStroke>& Strokes,
        int32                                            MaterialSlotIndex,
        const FLinearColor&                              BaseRevealColor,
        TArray<FColor>&                                  InOutRevealColorBuffer);
};
