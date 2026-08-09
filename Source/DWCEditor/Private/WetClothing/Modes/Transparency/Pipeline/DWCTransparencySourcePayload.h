//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

struct FDWCTransparencySourceHitStats
{
    int32 PriorityIndex = INDEX_NONE;
    int32 MaterialSlotIndex = INDEX_NONE;
    FName MaterialSlotName;
    int32 HitCount = 0;
};

/**
 * Canonical immutable output of Transparency Stage 2.
 *
 * Auto-map generation, reveal correction, final alpha authoring, preview, and
 * bake all share this payload. Keeping the contract in Pipeline prevents
 * downstream systems from depending on the AutoMap producer implementation.
 */
struct FDWCTransparencySourcePayload
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
        int32 FallbackUVIslandID,
        bool bWrap) const;

    FGuid LayerGuid;
    int32 MaterialSlotIndex = INDEX_NONE;
    int32 UVChannelIndex = 0;
    int32 LODIndex = 0;
    FIntPoint Resolution = FIntPoint::ZeroValue;
    FString BuildSignature;
    int32 OuterSampleCount = 0;
    int32 ValidHitCount = 0;
    int32 NoHitCount = 0;
    int32 OverlappedUVPixelCount = 0;
    TArray<FColor> InnerColorBuffer;
    TArray<uint8> AutoAlphaBuffer;
    TArray<uint8> OuterCoverageBuffer;
    TArray<uint16> OuterIslandIDBuffer;
    TBitArray<> ValidHitBuffer;
    TArray<float> HitDistanceBuffer;
    TArray<int16> SourcePriorityBuffer;
    TArray<FDWCTransparencySourceHitStats> SourceStats;

    // A restored baked baseline contains final authoring state. Generated
    // source payloads contain the pre-final automatic alpha instead.
    bool bIsFinalBakedBaseline = false;
    int32 BaselineStrokeCount = 0;
    FGuid BaselineBakeGuid;

    uint64 GetAllocatedBytes() const
    {
        return static_cast<uint64>(sizeof(FDWCTransparencySourcePayload)) +
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
