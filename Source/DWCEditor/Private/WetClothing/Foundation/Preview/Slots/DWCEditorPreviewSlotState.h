//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

class UMaterialInterface;
class UTexture2D;
class UWetClothingAsset;

enum class EDWCEditorPreviewSlotIssue : uint8
{
    None,
    InvalidAsset,
    MissingPreparedMesh,
    InvalidMaterialSlot,
    MissingSourceMaterial,
    UnsupportedMaterialGraph,
    NotWettable,
    MissingDataUV,
    WetPartDataMissing,
    WetPartDataOutOfDate,
    SlotTextureMissing,
    SlotTextureOutOfDate
};

struct FDWCEditorPreviewSlotState
{
    int32 MaterialSlotIndex = INDEX_NONE;
    FName MaterialSlotName = NAME_None;
    TWeakObjectPtr<UMaterialInterface> SourceMaterial;
    TWeakObjectPtr<UTexture2D> WetPartDataTexture;
    bool bWettable = false;
    bool bPreviewReady = false;
    EDWCEditorPreviewSlotIssue Issue = EDWCEditorPreviewSlotIssue::InvalidAsset;
};

struct FDWCEditorPreviewSlotCollection
{
    // Dense mesh material-slot order: Slots[MaterialSlotIndex] owns that slot's state.
    TArray<FDWCEditorPreviewSlotState> Slots;
    TArray<int32> ReadyWettableSlotIndices;
    int32 WettableSlotCount = 0;
    int32 SkippedWettableSlotCount = 0;
    uint32 StateSignature = 0;

    const FDWCEditorPreviewSlotState* Find(int32 MaterialSlotIndex) const;
    bool IsReady(int32 MaterialSlotIndex) const;
};

/** Resolves the material slots that have all data required by an editor preview session. */
class FDWCEditorPreviewSlotResolver
{
public:
    static FDWCEditorPreviewSlotCollection Resolve(const UWetClothingAsset* WetClothingAsset);
    static FText GetIssueText(EDWCEditorPreviewSlotIssue Issue);
    static FText GetAggregateTooltip(const FDWCEditorPreviewSlotCollection& Collection);
};
