#pragma once

#include "CoreMinimal.h"

class UTexture;
class UTexture2D;
class UWetClothingAsset;

struct FWetWrinkleNormalMapBakeSettings
{
    int32 Resolution = 1024;
    int32 PaddingPixels = 4;
    bool bIncludeDisabledPatchStrokes = false;
    bool bBakeNormalMap = true;
    bool bBakeMask = true;
};

struct FWetWrinkleNormalMapBakeResult
{
    int32 BakedMapCount = 0;
    int32 BakedStampCount = 0;
    TArray<TObjectPtr<UTexture2D>> BakedNormalMaps;
    TArray<TObjectPtr<UTexture2D>> BakedMasks;
};

class FWetWrinkleNormalMapBaker
{
  public:
    static bool BakeMaterialSlot(
        UWetClothingAsset*                       WetClothingAsset,
        int32                                    MaterialSlotIndex,
        const FWetWrinkleNormalMapBakeSettings& Settings,
        FWetWrinkleNormalMapBakeResult&         OutResult,
        FString&                                OutErrorMessage);

  private:
    struct FBakeGroup;

    static bool BakeGroup(
        UWetClothingAsset&                       WetClothingAsset,
        const FBakeGroup&                        Group,
        const FWetWrinkleNormalMapBakeSettings& Settings,
        FWetWrinkleNormalMapBakeResult&         InOutResult,
        FString&                                OutErrorMessage);

    static FString MakeBuildSignature(
        const UWetClothingAsset& WetClothingAsset,
        const FBakeGroup&        Group,
        int32                    Width,
        int32                    Height);

    static UTexture2D* CreateOrUpdateTextureAsset(
        UWetClothingAsset&    WetClothingAsset,
        const FString&        ObjectSuffix,
        int32                 Width,
        int32                 Height,
        const TArray<FColor>& Pixels,
        bool                  bNormalMap,
        FString&              OutErrorMessage);
};
