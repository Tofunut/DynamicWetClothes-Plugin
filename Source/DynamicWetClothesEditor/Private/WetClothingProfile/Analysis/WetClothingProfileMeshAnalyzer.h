#pragma once

#include "CoreMinimal.h"
#include "DynamicWet/DynamicWetMeshAnalysis.h"

class USkeletalMesh;
class UTexture;

using FWetClothingProfileUVTriangle = FDynamicWetUVTriangle;
using FWetClothingProfileUVIsland = FDynamicWetUVIsland;

class FWetClothingProfileMeshAnalyzer
{
  public:
    static int32 GetNumUVChannels(const USkeletalMesh* SkeletalMesh, int32 LODIndex = 0);

    static bool BuildMaterialSlotUVIslands(
        const USkeletalMesh*                 SkeletalMesh,
        int32                                LODIndex,
        int32                                UVChannelIndex,
        int32                                MaterialSlotIndex,
        TArray<FWetClothingProfileUVIsland>& OutIslands,
        FString*                             OutErrorMessage = nullptr);

    static void SetError(FString* OutErrorMessage, const TCHAR* InMessage);
};
