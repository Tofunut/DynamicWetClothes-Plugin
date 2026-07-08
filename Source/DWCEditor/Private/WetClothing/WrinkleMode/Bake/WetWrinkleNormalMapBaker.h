#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetClothingWrinkleData.h"
#include "WetClothing/Common/Analysis/WetClothingAssetMeshAnalyzer.h"

class UTexture;
class UTexture2D;
class UWetClothingAsset;

struct FWetWrinkleNormalMapBakeSettings
{
    int32 Resolution = 2048;
    int32 PaddingPixels = 8;
    int32 PreferredUVChannelIndex = INDEX_NONE;
    bool bIncludeDisabledPatchStrokes = false;
    bool bBakeNormalMap = true;
    bool bBakeMask = true;
};

struct FWetWrinkleNormalMapBakeResult
{
    int32 BakedMapCount = 0;
    int32 BakedStampCount = 0;
    TArray<int32> BakedUVChannelIndices;
    TArray<UTexture2D*> BakedNormalMaps;
    TArray<UTexture2D*> BakedMasks;
};

class FWetWrinkleNormalMapBaker
{
  public:
    struct FIntermediateBakeResult
    {
        int32 LODIndex = 0;
        int32 MaterialSlotIndex = INDEX_NONE;
        int32 UVChannelIndex = INDEX_NONE;
        int32 Resolution = 0;
        int32 Width = 0;
        int32 Height = 0;
        TObjectPtr<UTexture> SourceTexture = nullptr;
        TArray<const FWetWrinklePatchPlacement*> FilteredPatchPlacements;
        TArray<FWetClothingAssetUVIsland> UVIslands;
        TArray<FWetClothingAssetUVTriangle> UVTriangles;
        TArray<bool> IslandMask;
        TArray<FVector3f> NormalBuffer;
        TArray<float> PatchCoverageBuffer;

        bool IsInitialized() const
        {
            return Width > 0 &&
                   Height > 0 &&
                   NormalBuffer.Num() == Width * Height &&
                   IslandMask.Num() == Width * Height &&
                   PatchCoverageBuffer.Num() == Width * Height;
        }
    };

    static bool BakeMaterialSlot(
        UWetClothingAsset*                       WetClothingAsset,
        int32                                    MaterialSlotIndex,
        const FWetWrinkleNormalMapBakeSettings& Settings,
        FWetWrinkleNormalMapBakeResult&         OutResult,
        FString&                                OutErrorMessage);

  private:
    struct FBakeGroup;

    static bool ResolveBakeUVChannelIndices(
        const UWetClothingAsset&                 WetClothingAsset,
        int32                                    MaterialSlotIndex,
        const FWetWrinkleNormalMapBakeSettings& Settings,
        TArray<int32>&                           OutUVChannelIndices,
        FString&                                 OutErrorMessage);

    static bool BuildBakeGroupForMaterialSlot(
        const UWetClothingAsset&                 WetClothingAsset,
        int32                                    MaterialSlotIndex,
        int32                                    UVChannelIndex,
        int32                                    LODIndex,
        const FWetWrinkleNormalMapBakeSettings& Settings,
        FBakeGroup&                              OutGroup,
        FString&                                 OutErrorMessage);

    static bool BuildIntermediateBakeResult(
        UWetClothingAsset&                       WetClothingAsset,
        const FBakeGroup&                        Group,
        const FWetWrinkleNormalMapBakeSettings& Settings,
        FIntermediateBakeResult&                 OutIntermediateResult,
        FString&                                 OutErrorMessage);

    static bool BakeGroup(
        UWetClothingAsset&                       WetClothingAsset,
        const FBakeGroup&                        Group,
        const FWetWrinkleNormalMapBakeSettings& Settings,
        FWetWrinkleNormalMapBakeResult&         InOutResult,
        FString&                                OutErrorMessage);

    static void InitializeIntermediateBuffers(
        FIntermediateBakeResult& OutIntermediateResult,
        int32                    Width,
        int32                    Height,
        int32                    Resolution);

    static bool BuildTrianglesAndIslands(
        const UWetClothingAsset&               WetClothingAsset,
        int32                                  LODIndex,
        int32                                  MaterialSlotIndex,
        int32                                  UVChannelIndex,
        TArray<FWetClothingAssetUVIsland>&     OutIslands,
        TArray<FWetClothingAssetUVTriangle>&   OutTriangles,
        FString&                               OutErrorMessage);

    static int32 RasterizeTriangleMask(
        TArray<bool>&                       OutMask,
        int32                               Width,
        int32                               Height,
        const FWetClothingAssetUVTriangle& Triangle);

    static void RasterizeIslandMask(FIntermediateBakeResult& InOutIntermediateResult);

    static bool RasterizeSinglePatch(
        FIntermediateBakeResult&         InOutIntermediateResult,
        const FWetWrinklePatchPlacement& Patch,
        FString&                         OutErrorMessage);

    static void DilateBakedNormals(
        FIntermediateBakeResult& InOutIntermediateResult,
        int32                    PaddingPixels);

    static void ConvertIntermediateToPixels(
        const FIntermediateBakeResult& IntermediateResult,
        TArray<FColor>&                OutNormalPixels,
        TArray<FColor>&                OutMaskPixels);

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
