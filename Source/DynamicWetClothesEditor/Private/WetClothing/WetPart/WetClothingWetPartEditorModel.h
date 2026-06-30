/*
 *  Wet Clothing Asset의 Wet Part 편집 모델과 관련 유틸리티 함수를 선언합니다.
 */

#pragma once

#include "CoreMinimal.h"
#include "WetClothingAsset.h"
#include "WetClothing/Analysis/WetClothingAssetMeshAnalyzer.h"

struct FWetClothingWetPartScope
{
    int32 MaterialSlotIndex = INDEX_NONE;
    int32 UVChannelIndex = 0;

    bool IsValid() const
    {
        return MaterialSlotIndex != INDEX_NONE;
    }
};

class FWetClothingWetPartEditorModel
{
  public:
    static FWetClothingWetPartScope MakeScope(int32 MaterialSlotIndex, int32 UVChannelIndex);

    static bool MatchesScope(const FWetClothingAssetWetPartEntry& Entry, const FWetClothingWetPartScope& Scope);

    static bool EnsureDefaultWetPartForScope(UWetClothingAsset* Profile, const FWetClothingWetPartScope& Scope);

    static int32 FindNextWetPartIDForScope(const UWetClothingAsset* Profile, const FWetClothingWetPartScope& Scope);

    static FWetClothingAssetWetPartEntry*       FindMutableEntry(UWetClothingAsset* Profile, const FWetClothingWetPartScope& Scope, int32 WetPartID);
    static const FWetClothingAssetWetPartEntry* FindEntry(const UWetClothingAsset* Profile, const FWetClothingWetPartScope& Scope, int32 WetPartID);
    static const FWetClothingAssetWetPartEntry* FindEntryForIsland(const UWetClothingAsset* Profile, const FWetClothingWetPartScope& Scope, int32 IslandID);
    static const FWetClothingAssetWetPartEntry* FindEffectiveEntryForIsland(const UWetClothingAsset* Profile, const FWetClothingWetPartScope& Scope, int32 IslandID);

    static void BuildWetPartItemsForScope(
        const UWetClothingAsset*                           Profile,
        const FWetClothingWetPartScope&                      Scope,
        TArray<TSharedPtr<FWetClothingAssetWetPartEntry>>& OutItems);

    static TSet<int32> GetIslandIDsForWetPart(
        const UWetClothingAsset*                             Profile,
        const FWetClothingWetPartScope&                        Scope,
        const TArray<TSharedPtr<FWetClothingAssetUVIsland>>& Islands,
        int32                                                  WetPartID);

    static int32 GetEffectiveWetPartIDForIsland(const UWetClothingAsset* Profile, const FWetClothingWetPartScope& Scope, int32 IslandID);

    static FLinearColor GetDefaultWetPartColor(int32 WetPartID);
    static FString      GetDefaultWetPartName(int32 WetPartID);
    static FString      GetWetPartDisplayName(const FWetClothingAssetWetPartEntry& Entry);
    static FString      GetAssignedProfileLabel(const FWetClothingAssetWetPartEntry& Entry);

    static TMap<int32, int32> BuildIslandWetPartIDMap(
        const UWetClothingAsset*                             Profile,
        const FWetClothingWetPartScope&                        Scope,
        const TArray<TSharedPtr<FWetClothingAssetUVIsland>>& Islands);

    static TMap<int32, FLinearColor> BuildIslandColorMap(
        const UWetClothingAsset*                             Profile,
        const FWetClothingWetPartScope&                        Scope,
        const TArray<TSharedPtr<FWetClothingAssetUVIsland>>& Islands);
};
