#pragma once

#include "CoreMinimal.h"

class UWetClothingAsset;
class USkeletalMesh;

struct FWetWrinkleUVChannelGenerationSettings
{
    int32 LODIndex = 0;
    int32 Resolution = 1024;
    int32 PaddingPixels = 8;
    int32 SourceUVChannelIndex = 0;
    int32 PreferredUVChannelIndex = 2;
    int32 TargetMaterialSlotIndex = INDEX_NONE;
    bool bAllowOverwriteExistingGeneratedChannel = true;
};

struct FWetWrinkleUVChannelGenerationResult
{
    bool bSucceeded = false;
    int32 UVChannelIndex = INDEX_NONE;
    int32 MaterialSlotIndex = INDEX_NONE;
    int32 IslandCount = 0;
    FString Message;
};

class FWetWrinkleUVChannelGenerator
{
  public:
    static FWetWrinkleUVChannelGenerationResult GenerateForAsset(
        UWetClothingAsset* Asset,
        const FWetWrinkleUVChannelGenerationSettings& Settings = FWetWrinkleUVChannelGenerationSettings());

    static FWetWrinkleUVChannelGenerationResult GenerateForSkeletalMesh(
        USkeletalMesh* SkeletalMesh,
        int32 LODIndex,
        int32 Resolution,
        int32 PaddingPixels,
        int32 SourceUVChannelIndex = 0,
        int32 PreferredUVChannelIndex = 2,
        bool bAllowOverwriteExistingChannel = false,
        int32 TargetMaterialSlotIndex = INDEX_NONE);

    static FWetWrinkleUVChannelGenerationResult DeleteUVChannelForAsset(
        UWetClothingAsset* Asset,
        int32 LODIndex,
        int32 UVChannelIndex);
};
