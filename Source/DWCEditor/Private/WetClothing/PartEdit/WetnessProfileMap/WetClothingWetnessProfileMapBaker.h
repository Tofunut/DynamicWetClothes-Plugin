#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetClothingAsset.h"
#include "DataAssets/WetnessProfile.h"
#include "Engine/Texture.h"
#include "WetClothing/Common/Analysis/WetClothingAssetMeshAnalyzer.h"

class UTexture;
class UTexture2D;
class UWetClothingAsset;

struct FWetClothingWetnessProfileMapBakeSettings
{
    int32 Resolution = 512;
    int32 PaddingPixels = 4;
};

struct FWetClothingWetnessProfileMapBakeResult
{
    TObjectPtr<UTexture2D> WetnessProfileMap0 = nullptr;
    TArray<int32>          MaterialSlotIndices;
    int32                  PaintedPixelCount = 0;
};

class FWetClothingWetnessProfileMapBaker
{
  public:
    static FString MakeBuildSignature(
        const UWetClothingAsset* WetClothingAsset,
        const UTexture*          SourceTexture,
        int32                    UVChannelIndex,
        const TArray<int32>&     MaterialSlotIndices);

    static bool BakeWetnessProfileMap0(
        UWetClothingAsset*                               WetClothingAsset,
        UTexture*                                        SourceTexture,
        int32                                            UVChannelIndex,
        const TArray<int32>&                             MaterialSlotIndices,
        const FWetClothingWetnessProfileMapBakeSettings& Settings,
        FWetClothingWetnessProfileMapBakeResult&         OutResult,
        FString&                                         OutErrorMessage);

  private:
    static bool IsUVPointInsideTriangle(
        const FVector2D& Point,
        const FVector2D& A,
        const FVector2D& B,
        const FVector2D& C);

    static uint8 PackUnitFloat(float Value);
    static FColor EncodeProfileParameters(const FWetnessProfileParameters& Parameters);

    static void ApplyTextureAddressToIslands(
        TArray<FWetClothingAssetUVIsland>& Islands,
        TextureAddress                     AddressX,
        TextureAddress                     AddressY);

    static bool ResolveWetPartParameters(
        const FWetClothingWetPartEntry& WetPartEntry,
        FWetnessProfileParameters&           OutParameters);

    static void AppendProfileParametersSignature(FString& Signature, const FWetnessProfileParameters& Parameters);

    static const FWetClothingWetPartEntry* FindWetPartEntryForUVIsland(
        const UWetClothingAsset& WetClothingAsset,
        int32                    MaterialSlotIndex,
        int32                    UVChannelIndex,
        int32                    UVIslandID);

    static int32 PaintTriangle(
        TArray<FColor>&                    Pixels,
        TArray<bool>&                      PaintedMask,
        int32                              Width,
        int32                              Height,
        const FWetClothingAssetUVTriangle& Triangle,
        const FColor&                      Color);

    static void DilatePaintedPixels(
        TArray<FColor>& Pixels,
        TArray<bool>&   PaintedMask,
        int32           Width,
        int32           Height,
        int32           PaddingPixels);

    static FString BuildWetnessProfileMapObjectName(
        const UWetClothingAsset& WetClothingAsset,
        const UTexture&          SourceTexture,
        int32                    UVChannelIndex);

    static UTexture2D* CreateOrUpdateTextureAsset(
        UWetClothingAsset&    WetClothingAsset,
        UTexture&             SourceTexture,
        int32                 UVChannelIndex,
        int32                 Width,
        int32                 Height,
        const TArray<FColor>& Pixels,
        FString&              OutErrorMessage);
};
