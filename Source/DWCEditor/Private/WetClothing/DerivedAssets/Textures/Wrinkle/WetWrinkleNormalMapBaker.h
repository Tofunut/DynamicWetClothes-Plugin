#pragma once

#include "CoreMinimal.h"

class UTexture;
class UTexture2D;
class UWetClothingAsset;

struct FWetWrinkleNormalMapBakeSettings
{
    int32 Resolution = 1024;
    int32 PaddingPixels = 8;
    bool bIncludeDisabledPatches = false;
};

struct FWetWrinkleNormalMapBakeResult
{
    int32 BakedMapCount = 0;
    int32 BakedStampCount = 0;
    int32 BakedProceduralStrokeCount = 0;
    TArray<UTexture2D*> BakedNormalMaps;
    TArray<UTexture2D*> BakedMasks;
};

class FWetWrinkleNormalMapBakeSession
{
  public:
    struct FImpl;

    FWetWrinkleNormalMapBakeSession();
    ~FWetWrinkleNormalMapBakeSession();

    FWetWrinkleNormalMapBakeSession(FWetWrinkleNormalMapBakeSession&&);
    FWetWrinkleNormalMapBakeSession& operator=(FWetWrinkleNormalMapBakeSession&&);

    FWetWrinkleNormalMapBakeSession(const FWetWrinkleNormalMapBakeSession&) = delete;
    FWetWrinkleNormalMapBakeSession& operator=(const FWetWrinkleNormalMapBakeSession&) = delete;

  private:
    TUniquePtr<FImpl> Impl;

    friend class FWetWrinkleNormalMapBaker;
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

    static bool BakeMaterialSlot(
        UWetClothingAsset*                       WetClothingAsset,
        int32                                    MaterialSlotIndex,
        const FWetWrinkleNormalMapBakeSettings& Settings,
        FWetWrinkleNormalMapBakeSession&        Session,
        FWetWrinkleNormalMapBakeResult&         OutResult,
        FString&                                OutErrorMessage);

    static bool IsMaterialSlotBakeCurrent(
        const UWetClothingAsset* WetClothingAsset,
        int32                    MaterialSlotIndex);

  private:
    struct FBakeGroup;

    static bool BakeGroup(
        UWetClothingAsset&                       WetClothingAsset,
        const FBakeGroup&                        Group,
        const FWetWrinkleNormalMapBakeSettings& Settings,
        FWetWrinkleNormalMapBakeSession&        Session,
        FWetWrinkleNormalMapBakeResult&         InOutResult,
        FString&                                OutErrorMessage);

    static FString MakeBuildSignature(
        const UWetClothingAsset& WetClothingAsset,
        const FBakeGroup&        Group,
        int32                    Width,
        int32                    Height,
        const FWetWrinkleNormalMapBakeSettings& Settings);

    static UTexture2D* CreateOrUpdateNormalTextureAsset(
        UWetClothingAsset&    WetClothingAsset,
        const FString&        ObjectSuffix,
        int32                 Width,
        int32                 Height,
        const TArray<FColor>& Pixels,
        FString&              OutErrorMessage);

    static UTexture2D* CreateOrUpdateMaskTextureAsset(
        UWetClothingAsset&    WetClothingAsset,
        const FString&        ObjectSuffix,
        int32                 Width,
        int32                 Height,
        const TArray<uint8>&  Pixels,
        FString&              OutErrorMessage);
};
