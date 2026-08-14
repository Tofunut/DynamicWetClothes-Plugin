// Copyright 2026 Team Tofunut. All Rights Reserved.

/*
 * Declares editing services for Wet Parts in a Wet Clothing Asset.
 */

#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetClothingAsset.h"

struct FWetPartScope
{
    int32 MaterialSlotIndex = INDEX_NONE;

    bool IsValid() const
    {
        return MaterialSlotIndex != INDEX_NONE;
    }
};

class FWetPartEditingService
{
  public:
    static FWetPartScope MakeScope(int32 MaterialSlotIndex);

    /** Mutation helper. The caller owns transaction and dirty-state policy. */
    static bool EnsureDefaultWetPartForScope(
        FWetClothingEditableWetPartData& EditableData,
        const FWetPartScope& Scope);
    static int32 FindNextWetPartIDForScope(const UWetClothingAsset* WetClothingAsset, const FWetPartScope& Scope);

    static FWetClothingWetPartEntry*       FindMutableEntry(UWetClothingAsset* WetClothingAsset, const FWetPartScope& Scope, int32 WetPartID);
    static const FWetClothingWetPartEntry* FindEntry(const UWetClothingAsset* WetClothingAsset, const FWetPartScope& Scope, int32 WetPartID);
    static const FWetClothingWetPartEntry* FindEntryForUVIsland(const UWetClothingAsset* WetClothingAsset, const FWetPartScope& Scope, int32 UVIslandID);
    static const FWetClothingWetPartEntry* FindEffectiveEntryForUVIsland(const UWetClothingAsset* WetClothingAsset, const FWetPartScope& Scope, int32 UVIslandID);

    static int32 GetEffectiveWetPartIDForUVIsland(const UWetClothingAsset* WetClothingAsset, const FWetPartScope& Scope, int32 UVIslandID);

    static FLinearColor GetDefaultWetPartColor(int32 WetPartID);
    static FString      GetDefaultWetPartName(int32 WetPartID);
    static FString      GetWetPartDisplayName(const FWetClothingWetPartEntry& Entry);
};
