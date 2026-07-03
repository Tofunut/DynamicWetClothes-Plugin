/*
 *  Wet Clothing Asset의 Wet Part 편집 서비스 함수를 선언합니다.
 */

#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetClothingAsset.h"
#include "WetClothing/Analysis/WetClothingAssetMeshAnalyzer.h"

struct FWetPartScope
{
    int32 MaterialSlotIndex = INDEX_NONE;
    int32 UVChannelIndex = 0;

    bool IsValid() const
    {
        return MaterialSlotIndex != INDEX_NONE;
    }
};

class FWetPartEditingService
{
  public:
    static FWetPartScope MakeScope(int32 MaterialSlotIndex, int32 UVChannelIndex);

    static bool MatchesScope(const FWetClothingAssetWetPartEntry& Entry, const FWetPartScope& Scope);

    static bool EnsureDefaultWetPartForScope(UWetClothingAsset* WetClothingAsset, const FWetPartScope& Scope);

    static int32 FindNextWetPartIDForScope(const UWetClothingAsset* WetClothingAsset, const FWetPartScope& Scope);

    static FWetClothingAssetWetPartEntry*       FindMutableEntry(UWetClothingAsset* WetClothingAsset, const FWetPartScope& Scope, int32 WetPartID);
    static const FWetClothingAssetWetPartEntry* FindEntry(const UWetClothingAsset* WetClothingAsset, const FWetPartScope& Scope, int32 WetPartID);
    static const FWetClothingAssetWetPartEntry* FindEntryForUVIsland(const UWetClothingAsset* WetClothingAsset, const FWetPartScope& Scope, int32 UVIslandID);
    static const FWetClothingAssetWetPartEntry* FindEffectiveEntryForUVIsland(const UWetClothingAsset* WetClothingAsset, const FWetPartScope& Scope, int32 UVIslandID);

    static void BuildWetPartItemsForScope(
        const UWetClothingAsset*                           WetClothingAsset,
        const FWetPartScope&                               Scope,
        TArray<TSharedPtr<FWetClothingAssetWetPartEntry>>& OutItems);

    static TSet<int32> GetUVIslandIDsForWetPart(
        const UWetClothingAsset*                             WetClothingAsset,
        const FWetPartScope&                                 Scope,
        const TArray<TSharedPtr<FWetClothingAssetUVIsland>>& Islands,
        int32                                                WetPartID);

    static int32 GetEffectiveWetPartIDForUVIsland(const UWetClothingAsset* WetClothingAsset, const FWetPartScope& Scope, int32 UVIslandID);

    static FLinearColor GetDefaultWetPartColor(int32 WetPartID);
    static FString      GetDefaultWetPartName(int32 WetPartID);
    static FString      GetWetPartDisplayName(const FWetClothingAssetWetPartEntry& Entry);
    static FString      GetAssignedProfileLabel(const FWetClothingAssetWetPartEntry& Entry);

    static TMap<int32, int32> BuildUVIslandWetPartIDMap(
        const UWetClothingAsset*                             WetClothingAsset,
        const FWetPartScope&                                 Scope,
        const TArray<TSharedPtr<FWetClothingAssetUVIsland>>& Islands);

    static TMap<int32, FLinearColor> BuildUVIslandColorMap(
        const UWetClothingAsset*                             WetClothingAsset,
        const FWetPartScope&                                 Scope,
        const TArray<TSharedPtr<FWetClothingAssetUVIsland>>& Islands);
};
