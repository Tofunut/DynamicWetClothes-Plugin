#pragma once

#include "CoreMinimal.h"

class USkeletalMesh;
class UTexture;

struct FWetClothingProfileUVTriangle
{
    int32     TriangleID = INDEX_NONE;
    int32     MaterialSlotIndex = INDEX_NONE;
    int32     IslandID = INDEX_NONE;
    FVector2D UVs[3];
    FVector   LocalPositions[3];
};

struct FWetClothingProfileUVIsland
{
    int32                                 MaterialSlotIndex = INDEX_NONE;
    int32                                 IslandID = INDEX_NONE;
    int32                                 TriangleCount = 0;
    FBox2D                                UVBounds;
    double                                UVArea = 0.0;
    TArray<int32>                         TriangleIDs;
    TArray<FWetClothingProfileUVTriangle> UVTriangles;
};

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
