//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

class UWetClothingAsset;
struct FDWCTransparencySourcePayload;
struct FWetClothingTransparencyLayerData;

enum class EDWCTransparencyAffectedRebakeStatus : uint8
{
    Eligible,
    AlreadyCurrent,
    MissingLayer,
    MissingBakedMap,
    NonWrinkleInputsChanged,
    MissingCanonicalArtifact,
    InvalidCanonicalArtifact
};

struct FDWCTransparencyAffectedRebakeCandidate
{
    FGuid LayerGuid;
    int32 MaterialSlotIndex = INDEX_NONE;
    EDWCTransparencyAffectedRebakeStatus Status =
        EDWCTransparencyAffectedRebakeStatus::MissingLayer;
    FString Detail;

    bool IsEligible() const
    {
        return Status == EDWCTransparencyAffectedRebakeStatus::Eligible;
    }
};

/**
 * Small state contract used by affected Stage 4 batches. It prevents the next
 * full-resolution source payload from being restored until the previous one
 * has completed and records the resulting retained-memory high-water mark.
 */
class FDWCTransparencyAffectedRebakeSequence
{
  public:
    void Initialize(TArray<FGuid> InLayerGuids);
    bool TryBeginNext(FGuid& OutLayerGuid);
    void SetActivePayloadBytes(uint64 InBytes);
    void CompleteActive();
    void DiscardRemaining();

    bool HasActive() const { return bActive; }
    bool HasPending() const { return NextIndex < LayerGuids.Num(); }
    bool IsComplete() const { return !bActive && !HasPending(); }
    int32 GetPendingCount() const { return LayerGuids.Num() - NextIndex; }
    uint64 GetActivePayloadBytes() const { return ActivePayloadBytes; }
    uint64 GetPeakPayloadBytes() const { return PeakPayloadBytes; }

  private:
    TArray<FGuid> LayerGuids;
    int32 NextIndex = 0;
    bool bActive = false;
    uint64 ActivePayloadBytes = 0;
    uint64 PeakPayloadBytes = 0;
};

/**
 * Selects Stage 4 outputs whose only stale dependency is wrinkle coverage and
 * restores their canonical Stage 2 payload without repeating material bake or ray projection.
 */
class FDWCTransparencyAffectedStage4Rebake
{
  public:
    static void CollectCandidates(
        const UWetClothingAsset& Asset,
        TConstArrayView<int32> MaterialSlotIndices,
        TArray<FDWCTransparencyAffectedRebakeCandidate>& OutCandidates);

    static bool RestoreCanonicalSource(
        const UWetClothingAsset& Asset,
        const FGuid& LayerGuid,
        FDWCTransparencySourcePayload& OutResult,
        FString& OutError);

    /** Decodes persisted Stage 2 textures using an already-validated identity. */
    static bool RestoreCanonicalArtifacts(
        const FWetClothingTransparencyLayerData& Layer,
        const FDWCTransparencySourcePayload& Identity,
        FDWCTransparencySourcePayload& OutResult,
        FString& OutError);
};
