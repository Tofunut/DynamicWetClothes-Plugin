#pragma once

#include "CoreMinimal.h"

class USkeletalMesh;

struct DWC_API FWetQuantizedUV
{
    int64 U = 0;
    int64 V = 0;

    FWetQuantizedUV() = default;
    explicit FWetQuantizedUV(const FVector2D& UV);

    bool operator==(const FWetQuantizedUV& Other) const;
};

DWC_API uint32 GetTypeHash(const FWetQuantizedUV& Value);

struct DWC_API FWetUVEdgeKey
{
    FWetQuantizedUV A;
    FWetQuantizedUV B;

    FWetUVEdgeKey() = default;
    FWetUVEdgeKey(const FVector2D& InA, const FVector2D& InB);

    bool operator==(const FWetUVEdgeKey& Other) const;
};

DWC_API uint32 GetTypeHash(const FWetUVEdgeKey& Key);

struct DWC_API FWetUVTriangle
{
    int32     TriangleID = INDEX_NONE;
    int32     MaterialSlotIndex = INDEX_NONE;
    int32     UVIslandID = INDEX_NONE;
    int32     VertexIndices[3] = { INDEX_NONE, INDEX_NONE, INDEX_NONE };
    FVector2D UVs[3];
    FVector   LocalPositions[3];
};

struct DWC_API FWetUVIsland
{
    int32                  MaterialSlotIndex = INDEX_NONE;
    int32                  UVIslandID = INDEX_NONE;
    int32                  TriangleCount = 0;
    FBox2D                 UVBounds;
    double                 UVArea = 0.0;
    TArray<int32>          TriangleIDs;
    TArray<FWetUVTriangle> UVTriangles;
};

struct DWC_API FWetVertexIslandMembership
{
    int32 MaterialSlotIndex = INDEX_NONE;
    int32 UVChannelIndex = INDEX_NONE;
    int32 UVIslandID = INDEX_NONE;
};

class DWC_API FWetMeshAnalysis
{
  public:
    static constexpr double UVQuantizeScale = 100000.0;

    static int32 GetNumUVChannels(const USkeletalMesh* SkeletalMesh, int32 LODIndex = 0);

    static bool BuildMaterialSlotUVIslands(
        const USkeletalMesh*  SkeletalMesh,
        int32                 LODIndex,
        int32                 UVChannelIndex,
        int32                 MaterialSlotIndex,
        TArray<FWetUVIsland>& OutIslands,
        FString*              OutErrorMessage = nullptr);

    static bool BuildUVIslandVertexMap(
        const TArray<FWetUVIsland>& Islands,
        TMap<int32, TArray<int32>>& OutIslandVertices);

    static bool BuildVertexIslandMembership(
        const TArray<FWetUVIsland>&         Islands,
        int32                               VertexCount,
        int32                               UVChannelIndex,
        TArray<FWetVertexIslandMembership>& OutVertexMembership);

    static void SetError(FString* OutErrorMessage, const TCHAR* InMessage);
};
