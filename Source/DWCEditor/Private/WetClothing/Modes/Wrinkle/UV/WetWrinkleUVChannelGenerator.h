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
    int32 UVChannelIndex = INDEX_NONE; //Result Saved Channel
    int32 MaterialSlotIndex = INDEX_NONE; //Material Slot
    int32 PackedIslandCount = 0;
    FString Message; //Succeed or Failed Description
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
        int32 TargetMaterialSlotIndex = INDEX_NONE,
        double TargetTexelsPerWorldUnit = 0.0);

    static bool CalculateSharedWorldTexelDensity(
        USkeletalMesh* SkeletalMesh,
        int32 LODIndex,
        int32 Resolution,
        int32 PaddingPixels,
        int32 SourceUVChannelIndex,
        const TArray<int32>& TargetMaterialSlotIndices,
        double& OutTexelsPerWorldUnit,
        FString& OutError);

    static FWetWrinkleUVChannelGenerationResult DeleteUVChannelForAsset(
        UWetClothingAsset* Asset,
        int32 LODIndex,
        int32 UVChannelIndex);
};
