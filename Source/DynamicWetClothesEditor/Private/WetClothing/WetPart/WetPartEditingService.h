/*
 *  Wet Clothing Asset의 Wet Part 편집 서비스 함수를 선언합니다.
 */

#pragma once

#include "CoreMinimal.h"
#include "WetClothingAsset.h"
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

    static bool EnsureDefaultWetPartForScope(UWetClothingAsset* Profile, const FWetPartScope& Scope);

    static int32 FindNextWetPartIDForScope(const UWetClothingAsset* Profile, const FWetPartScope& Scope);

    static FWetClothingAssetWetPartEntry*       FindMutableEntry(UWetClothingAsset* Profile, const FWetPartScope& Scope, int32 WetPartID);
    static const FWetClothingAssetWetPartEntry* FindEntry(const UWetClothingAsset* Profile, const FWetPartScope& Scope, int32 WetPartID);
    static const FWetClothingAssetWetPartEntry* FindEntryForIsland(const UWetClothingAsset* Profile, const FWetPartScope& Scope, int32 IslandID);
    static const FWetClothingAssetWetPartEntry* FindEffectiveEntryForIsland(const UWetClothingAsset* Profile, const FWetPartScope& Scope, int32 IslandID);

    static void BuildWetPartItemsForScope(
        const UWetClothingAsset*                           Profile,
        const FWetPartScope&                      Scope,
        TArray<TSharedPtr<FWetClothingAssetWetPartEntry>>& OutItems);

    static TSet<int32> GetIslandIDsForWetPart(
        const UWetClothingAsset*                             Profile,
        const FWetPartScope&                        Scope,
        const TArray<TSharedPtr<FWetClothingAssetUVIsland>>& Islands,
        int32                                                  WetPartID);

    static int32 GetEffectiveWetPartIDForIsland(const UWetClothingAsset* Profile, const FWetPartScope& Scope, int32 IslandID);

    static FLinearColor GetDefaultWetPartColor(int32 WetPartID);
    static FString      GetDefaultWetPartName(int32 WetPartID);
    static FString      GetWetPartDisplayName(const FWetClothingAssetWetPartEntry& Entry);
    static FString      GetAssignedProfileLabel(const FWetClothingAssetWetPartEntry& Entry);

    static TMap<int32, int32> BuildIslandWetPartIDMap(
        const UWetClothingAsset*                             Profile,
        const FWetPartScope&                        Scope,
        const TArray<TSharedPtr<FWetClothingAssetUVIsland>>& Islands);

    static TMap<int32, FLinearColor> BuildIslandColorMap(
        const UWetClothingAsset*                             Profile,
        const FWetPartScope&                        Scope,
        const TArray<TSharedPtr<FWetClothingAssetUVIsland>>& Islands);
};
