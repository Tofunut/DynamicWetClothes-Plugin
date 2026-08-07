//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

#if WITH_EDITOR

class USkeletalMesh;
class FSkeletalMeshLODRenderData;
struct FDWCEditorUVTopologyData;

struct FDWCRuntimeTopologyTriangle
{
    int32 TriangleID = INDEX_NONE;
    int32 VertexIndices[3] = { INDEX_NONE, INDEX_NONE, INDEX_NONE };
};

struct FDWCRuntimeOriginalUVIsland
{
    int32 UVIslandID = INDEX_NONE;
    TSet<int32> VertexIndices;
};

/** Adapts WCA-owned persistent Original UV topology for CPU precomputed runtime-data building. */
class FDWCOriginalUVRuntimeTopologyAdapter
{
public:
    static bool ReadMaterialSlotTriangles(
        const USkeletalMesh* SkeletalMesh,
        const FSkeletalMeshLODRenderData& LODData,
        const TArray<uint32>& IndexBuffer,
        int32 UVChannelIndex,
        int32 MaterialSlotIndex,
        TArray<FDWCRuntimeTopologyTriangle>& OutTriangles,
        FString* OutErrorMessage = nullptr);

    static bool BuildIslands(
        const TArray<FDWCRuntimeTopologyTriangle>& RawTriangles,
        const FDWCEditorUVTopologyData& Topology,
        int32 MaterialSlotIndex,
        TArray<FDWCRuntimeOriginalUVIsland>& OutIslands,
        FString* OutErrorMessage = nullptr);
};

#endif // WITH_EDITOR
