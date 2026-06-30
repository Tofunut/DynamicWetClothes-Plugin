#pragma once

#include "CoreMinimal.h"

class USkeletalMesh;

struct DYNAMICWETCLOTHES_API FDynamicWetQuantizedUV
{
    int64 U = 0;
    int64 V = 0;

    FDynamicWetQuantizedUV() = default;
    explicit FDynamicWetQuantizedUV(const FVector2D& UV);

    bool operator==(const FDynamicWetQuantizedUV& Other) const;
};

DYNAMICWETCLOTHES_API uint32 GetTypeHash(const FDynamicWetQuantizedUV& Value);

struct DYNAMICWETCLOTHES_API FDynamicWetUVEdgeKey
{
    FDynamicWetQuantizedUV A;
    FDynamicWetQuantizedUV B;

    FDynamicWetUVEdgeKey() = default;
    FDynamicWetUVEdgeKey(const FVector2D& InA, const FVector2D& InB);

    bool operator==(const FDynamicWetUVEdgeKey& Other) const;
};

DYNAMICWETCLOTHES_API uint32 GetTypeHash(const FDynamicWetUVEdgeKey& Key);

struct DYNAMICWETCLOTHES_API FDynamicWetUVTriangle
{
    int32 TriangleID = INDEX_NONE;
    int32 MaterialSlotIndex = INDEX_NONE;
    int32 IslandID = INDEX_NONE;
    int32 VertexIndices[3] = { INDEX_NONE, INDEX_NONE, INDEX_NONE };
    FVector2D UVs[3];
    FVector LocalPositions[3];
};

struct DYNAMICWETCLOTHES_API FDynamicWetUVIsland
{
    int32 MaterialSlotIndex = INDEX_NONE;
    int32 IslandID = INDEX_NONE;
    int32 TriangleCount = 0;
    FBox2D UVBounds;
    double UVArea = 0.0;
    TArray<int32> TriangleIDs;
    TArray<FDynamicWetUVTriangle> UVTriangles;
};

struct DYNAMICWETCLOTHES_API FDynamicWetVertexIslandMembership
{
    int32 MaterialSlotIndex = INDEX_NONE;
    int32 UVChannelIndex = INDEX_NONE;
    int32 IslandID = INDEX_NONE;
};

class DYNAMICWETCLOTHES_API FDynamicWetMeshAnalysis
{
public:
    static constexpr double UVQuantizeScale = 100000.0;

    static int32 GetNumUVChannels(const USkeletalMesh* SkeletalMesh, int32 LODIndex = 0);

    static bool BuildMaterialSlotUVIslands(
        const USkeletalMesh* SkeletalMesh,
        int32 LODIndex,
        int32 UVChannelIndex,
        int32 MaterialSlotIndex,
        TArray<FDynamicWetUVIsland>& OutIslands,
        FString* OutErrorMessage = nullptr);

    static bool BuildIslandVertexMap(
        const TArray<FDynamicWetUVIsland>& Islands,
        TMap<int32, TArray<int32>>& OutIslandVertices);

    static bool BuildVertexIslandMembership(
        const TArray<FDynamicWetUVIsland>& Islands,
        int32 VertexCount,
        int32 UVChannelIndex,
        TArray<FDynamicWetVertexIslandMembership>& OutVertexMembership);

    static void SetError(FString* OutErrorMessage, const TCHAR* InMessage);
};
