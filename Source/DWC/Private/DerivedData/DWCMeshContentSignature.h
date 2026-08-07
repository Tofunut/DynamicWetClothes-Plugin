//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

class USkeletalMesh;
class FSkeletalMeshLODRenderData;

/** Builds deterministic signatures for persistent derived-data dependency validation. */
class FDWCMeshContentSignature
{
public:
    static FString BuildStructure(
        const USkeletalMesh* SkeletalMesh,
        int32 LODIndex);

    static FString BuildStructure(
        const USkeletalMesh* SkeletalMesh,
        const FSkeletalMeshLODRenderData& LODData,
        int32 LODIndex);

    static FString BuildUVContent(
        const USkeletalMesh* SkeletalMesh,
        int32 LODIndex,
        int32 UVChannelIndex);
};
