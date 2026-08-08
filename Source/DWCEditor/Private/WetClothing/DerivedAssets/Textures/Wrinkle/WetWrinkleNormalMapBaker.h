// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UTexture;
class UTexture2D;
class UWetClothingAsset;
class FDWCEditorCancellationToken;

struct FWetWrinkleNormalMapBakeSettings
{
    int32 Resolution = 1024;
    int32 PaddingPixels = 8;
    bool  bIncludeDisabledPatches = false;
};

struct FWetWrinkleNormalMapBakeResult
{
    int32               BakedMapCount = 0;
    int32               BakedStampCount = 0;
    int32               BakedProceduralStrokeCount = 0;
    TArray<UTexture2D*> BakedNormalMaps;
    TArray<UTexture2D*> BakedMasks;
};

/** Immutable, UObject-free input captured on the game thread for a wrinkle bake. */
class FWetWrinkleNormalMapBakeSnapshot
{
  public:
    struct FImpl;

    FWetWrinkleNormalMapBakeSnapshot();
    ~FWetWrinkleNormalMapBakeSnapshot();
    FWetWrinkleNormalMapBakeSnapshot(FWetWrinkleNormalMapBakeSnapshot&&);
    FWetWrinkleNormalMapBakeSnapshot& operator=(FWetWrinkleNormalMapBakeSnapshot&&);

    FWetWrinkleNormalMapBakeSnapshot(const FWetWrinkleNormalMapBakeSnapshot&) = delete;
    FWetWrinkleNormalMapBakeSnapshot& operator=(const FWetWrinkleNormalMapBakeSnapshot&) = delete;

    bool   IsValid() const;
    int32  GetMaterialSlotIndex() const;
    uint64 GetEstimatedBytes() const;

  private:
    TUniquePtr<FImpl> Impl;

    friend class FWetWrinkleNormalMapBaker;
};

/** Pure CPU result. It contains no UObject references and is safe to move back to the game thread. */
struct FWetWrinkleNormalMapComputedResult
{
    bool           bSucceeded = false;
    bool           bCanceled = false;
    FString        Error;
    TArray<FColor> NormalPixels;
    TArray<uint8>  MaskPixels;
    int32          BakedStampCount = 0;
    int32          BakedProceduralStrokeCount = 0;
    uint64         ResultBytes = 0;
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
    static bool BuildMaterialSlotSnapshot(
        UWetClothingAsset*                      WetClothingAsset,
        int32                                   MaterialSlotIndex,
        const FWetWrinkleNormalMapBakeSettings& Settings,
        FWetWrinkleNormalMapBakeSession&        Session,
        FWetWrinkleNormalMapBakeSnapshot&       OutSnapshot,
        FString&                                OutErrorMessage);

    static FWetWrinkleNormalMapComputedResult ComputeSnapshot(
        const FWetWrinkleNormalMapBakeSnapshot& Snapshot,
        const FDWCEditorCancellationToken*      CancellationToken = nullptr);

    static bool CommitComputedResult(
        UWetClothingAsset*                      WetClothingAsset,
        const FWetWrinkleNormalMapBakeSnapshot& Snapshot,
        FWetWrinkleNormalMapComputedResult&&    ComputedResult,
        FWetWrinkleNormalMapBakeResult&         OutResult,
        FString&                                OutErrorMessage);

    static bool BakeMaterialSlot(
        UWetClothingAsset*                      WetClothingAsset,
        int32                                   MaterialSlotIndex,
        const FWetWrinkleNormalMapBakeSettings& Settings,
        FWetWrinkleNormalMapBakeResult&         OutResult,
        FString&                                OutErrorMessage);

    static bool BakeMaterialSlot(
        UWetClothingAsset*                      WetClothingAsset,
        int32                                   MaterialSlotIndex,
        const FWetWrinkleNormalMapBakeSettings& Settings,
        FWetWrinkleNormalMapBakeSession&        Session,
        FWetWrinkleNormalMapBakeResult&         OutResult,
        FString&                                OutErrorMessage);

    static bool IsMaterialSlotBakeCurrent(
        const UWetClothingAsset* WetClothingAsset,
        int32                    MaterialSlotIndex);

  private:
    struct FBakeGroup;

    static bool BuildGroupSnapshot(
        UWetClothingAsset&                      WetClothingAsset,
        const FBakeGroup&                       Group,
        const FWetWrinkleNormalMapBakeSettings& Settings,
        FWetWrinkleNormalMapBakeSession&        Session,
        FWetWrinkleNormalMapBakeSnapshot&       OutSnapshot,
        FString&                                OutErrorMessage);

    static FString MakeBuildSignature(
        const UWetClothingAsset&                WetClothingAsset,
        const FBakeGroup&                       Group,
        int32                                   Width,
        int32                                   Height,
        const FWetWrinkleNormalMapBakeSettings& Settings);

    static UTexture2D* CreateOrUpdateNormalTextureAsset(
        UWetClothingAsset&    WetClothingAsset,
        const FString&        ObjectSuffix,
        int32                 Width,
        int32                 Height,
        const TArray<FColor>& Pixels,
        UTexture2D*           ExistingTexture,
        FString&              OutErrorMessage);

    static UTexture2D* CreateOrUpdateMaskTextureAsset(
        UWetClothingAsset&   WetClothingAsset,
        const FString&       ObjectSuffix,
        int32                Width,
        int32                Height,
        const TArray<uint8>& Pixels,
        UTexture2D*          ExistingTexture,
        FString&             OutErrorMessage);
};
