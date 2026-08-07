//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

class USkeletalMesh;
class UTexture;
class UWetClothingAsset;
struct FDWCOriginalUVIslandTopology;

struct FWetClothingAssetUVTriangle
{
    int32     TriangleID = INDEX_NONE;
    int32     MaterialSlotIndex = INDEX_NONE;
    int32     UVIslandID = INDEX_NONE;
    FVector2D UVs[3];
    FVector   LocalPositions[3];
    int32     RenderVertexIndices[3] = { INDEX_NONE, INDEX_NONE, INDEX_NONE };
    FVector   LocalNormals[3] = { FVector::UpVector, FVector::UpVector, FVector::UpVector };
};

struct FWetClothingAssetUVIsland
{
    int32                               MaterialSlotIndex = INDEX_NONE;
    int32                               UVIslandID = INDEX_NONE;
    int32                               TriangleCount = 0;
    FBox2D                              UVBounds;
    FBox                                LocalBounds = FBox(ForceInit);
    double                              UVArea = 0.0;
    TArray<int32>                       TriangleIDs;
    TArray<FWetClothingAssetUVTriangle> UVTriangles;
};

class FWetClothingAssetMeshAnalyzer
{
  public:
    static int32 GetNumUVChannels(const USkeletalMesh* SkeletalMesh, int32 LODIndex = 0);

    static bool BuildMaterialSlotUVIslands(
        const USkeletalMesh*               SkeletalMesh,
        int32                              LODIndex,
        int32                              UVChannelIndex,
        int32                              MaterialSlotIndex,
        TArray<FWetClothingAssetUVIsland>& OutIslands,
        FString*                           OutErrorMessage = nullptr);

    /** Builds UV islands from the WCA-owned generated DWC UV Channel payload. */
    static bool BuildMaterialSlotDataUVIslands(
        const UWetClothingAsset&           WetClothingAsset,
        int32                              LODIndex,
        int32                              MaterialSlotIndex,
        TArray<FWetClothingAssetUVIsland>& OutIslands,
        FString*                           OutErrorMessage = nullptr);

    /** Rebuilds draw/hit-test triangles from persistent WCA island membership without rerunning island connectivity. */
    static bool BuildMaterialSlotUVIslandsFromTopology(
        const USkeletalMesh*                          SkeletalMesh,
        int32                                         LODIndex,
        int32                                         UVChannelIndex,
        int32                                         MaterialSlotIndex,
        const TArray<FDWCOriginalUVIslandTopology>&   Topology,
        TArray<FWetClothingAssetUVIsland>&            OutIslands,
        FString*                                      OutErrorMessage = nullptr);

    static void SetError(FString* OutErrorMessage, const TCHAR* InMessage);
};
