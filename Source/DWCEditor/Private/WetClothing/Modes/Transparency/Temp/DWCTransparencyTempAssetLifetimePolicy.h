// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetClothingTransparencyData.h"

/**
 * Lifetime rules for persistent, rebuildable Transparency editor artifacts.
 *
 * Stage checkpoints use two stable package slots. A commit always writes the
 * inactive slot and publishes its reference only after the complete batch
 * succeeds. This preserves the previous checkpoint on failure while bounding
 * future package and generated-manifest growth.
 */
class FDWCTransparencyTempAssetLifetimePolicy final
{
public:
    static constexpr int32 GenerationSlotCount = 2;

    static int32 SelectNextGenerationSlot(
        const FWetClothingTransparencyLayerData& Layer,
        EDWCTransparencyTempArtifactKind AnchorKind);

    static FString GetGenerationSlotToken(int32 GenerationSlot);

    /** Replaces every duplicate reference for Kind with one canonical current reference. */
    static void PublishCurrentReference(
        FWetClothingTransparencyLayerData& Layer,
        const FDWCTransparencyTempArtifactReference& CurrentReference);

    /** Removes cache metadata that can no longer participate in an exact-identity lookup. */
    static int32 PruneObsoleteMaterialSurfaceReferences(
        FWetClothingTransparencyData& TransparencyData);

private:
    static int32 ResolveGenerationSlot(const FSoftObjectPath& ArtifactPath);
};
