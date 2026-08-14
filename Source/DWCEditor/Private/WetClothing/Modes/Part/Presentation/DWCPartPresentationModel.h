// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetClothingPartData.h"

class UWetClothingAsset;
class FDWCEditorCacheStore;

/** Immutable, editor-only view of one authored Wet Part. */
struct FDWCPartPresentationItem
{
    int32                        WetPartID = 0;
    FString                      DisplayName;
    FLinearColor                 Color = FLinearColor::White;
    bool                         bViewEnabled = true;
    int32                        ProfileIndex = 0;
    FWetPartSurfaceWaterSettings SurfaceWater;

    FSoftObjectPath ProfilePath;
    FString         ProfileLabel;
    bool            bSurfaceWaterEnabled = false;
    bool            bSyntheticDefault = false;
};

using FDWCPartPresentationItemPtr = TSharedPtr<const FDWCPartPresentationItem>;

/**
 * Canonical read model consumed by the Part list, UV view, and preview viewport.
 * Building this snapshot never mutates, dirties, or synchronously loads the WCA.
 */
struct FDWCPartPresentationSnapshot
{
    int32 MaterialSlotIndex = INDEX_NONE;
    bool  bIsWettableSlot = false;

    TArray<FDWCPartPresentationItemPtr> Items;
    TMap<int32, FDWCPartPresentationItemPtr> ItemByPartID;
    TMap<int32, int32>              IslandToPartID;
    TMap<int32, TArray<int32>>      IslandIDsByPartID;
    TMap<int32, FLinearColor>       UVIslandColors;
    TMap<int32, FLinearColor>       PreviewIslandColors;
    TSet<int32>                     HiddenIslandIDs;
    uint32                          SemanticHash = 0;

    FDWCPartPresentationItemPtr FindItem(int32 WetPartID) const;
    TSet<int32> GetIslandIDsForPart(int32 WetPartID) const;
    int32 GetEffectivePartID(int32 UVIslandID) const;
    bool IsEquivalentTo(const FDWCPartPresentationSnapshot& Other) const;
};

/** Cached read-only state used by every Material Slot row. */
struct FDWCPartSlotPresentationItem
{
    int32 MaterialSlotIndex = INDEX_NONE;
    bool  bIsWettableSlot = false;
    bool  bDataUVIncluded = false;
    bool  bDataUVReady = false;
    bool  bDataUVFailed = false;
    bool  bDataUVHasWarnings = false;
    bool  bDataUVDiagnosticsComplete = false;
    bool  bPartMapComplete = false;
    bool  bNeedsPartMapAttention = false;
    int32 UVIslandCount = 0;
    int32 UnassignedUVIslandCount = 0;
    int32 MissingProfilePartCount = 0;
    uint32 SemanticHash = 0;

    bool IsEquivalentTo(const FDWCPartSlotPresentationItem& Other) const;
};

struct FDWCPartSlotPresentationSnapshot
{
    TMap<int32, FDWCPartSlotPresentationItem> ItemsByMaterialSlot;
    uint32 SemanticHash = 0;

    const FDWCPartSlotPresentationItem* Find(int32 MaterialSlotIndex) const;
    bool Update(FDWCPartSlotPresentationItem Item);
    bool IsEquivalentTo(const FDWCPartSlotPresentationSnapshot& Other) const;

  private:
    friend class FDWCPartSlotPresentationModel;
    void RebuildSemanticHash();
};

class FDWCPartPresentationModel
{
  public:
    static FDWCPartPresentationSnapshot Build(
        const UWetClothingAsset* WetClothingAsset,
        int32                    MaterialSlotIndex,
        TConstArrayView<int32>   UVIslandIDs);

    static FLinearColor GetUnassignedUVViewColor();
};

class FDWCPartSlotPresentationModel
{
  public:
    static FDWCPartSlotPresentationItem BuildSlot(
        const UWetClothingAsset* WetClothingAsset,
        int32                    MaterialSlotIndex,
        const TSet<int32>&       FailedDataUVMaterialSlotIndices,
        const TSharedPtr<FDWCEditorCacheStore>& CacheStore = nullptr);

    static FDWCPartSlotPresentationSnapshot BuildAll(
        const UWetClothingAsset* WetClothingAsset,
        const TSet<int32>&       FailedDataUVMaterialSlotIndices,
        const TSharedPtr<FDWCEditorCacheStore>& CacheStore = nullptr);
};
