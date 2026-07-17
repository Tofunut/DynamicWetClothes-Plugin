#pragma once

#include "CoreMinimal.h"

class USkeletalMesh;

struct FDWCDataUVGenerationResult
{
    bool bSucceeded = false;
    int32 UVChannelIndex = INDEX_NONE;
    int32 MaterialSlotIndex = INDEX_NONE;
    /** Number of connected islands found in the source UV before self-overlap splitting. */
    int32 SourceIslandCount = 0;

    /** Number of non-overlapping DWC charts packed into the generated channel. */
    int32 IslandCount = 0;

    int32 SplitSourceIslandCount = 0;
    int32 SelfOverlapPairCount = 0;
    int32 Degenerate3DTriangleCount = 0;
    int32 DegenerateSourceUVTriangleCount = 0;
    int32 InvalidSourceUVTriangleCount = 0;
    int32 TriangleFallbackChartCount = 0;
    int32 RenderVertexCount = 0;
    TArray<FVector2f> DataUVs;

    /** 3D degenerate triangles are excluded automatically and are informational only. */
    int32 GetWarningCount() const
    {
        return SplitSourceIslandCount + DegenerateSourceUVTriangleCount + InvalidSourceUVTriangleCount +
               TriangleFallbackChartCount;
    }

    bool HasWarnings() const
    {
        return GetWarningCount() > 0;
    }

    FString Message;
};

/**
 * Generates read-only DWC Data UV payloads from Skeletal Mesh render data.
 * This utility never owns WCA state and is not exposed as an editor-mode command.
 */
class FDWCDataUVGenerator
{
public:
    static FDWCDataUVGenerationResult GenerateForSkeletalMeshRenderData(
        const USkeletalMesh* SkeletalMesh,
        int32 LODIndex,
        int32 SourceUVChannelIndex,
        int32 TargetMaterialSlotIndex = INDEX_NONE);

    static FDWCDataUVGenerationResult GenerateForSkeletalMesh(
        USkeletalMesh* SkeletalMesh,
        int32 LODIndex,
        int32 SourceUVChannelIndex,
        int32 PreferredUVChannelIndex,
        bool bAllowOverwriteExistingChannel = false,
        int32 TargetMaterialSlotIndex = INDEX_NONE);
};
