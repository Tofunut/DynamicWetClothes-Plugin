#pragma once

#include "CoreMinimal.h"

struct FDynamicWetReceiverContext;
class FSkeletalMeshLODRenderData;
class FSkinWeightVertexBuffer;

class FDynamicWetReceiverMeshSampler
{
public:
    void ResetPositions();
    void ResetNormals();

    bool UpdateSkinningMatrices(FDynamicWetReceiverContext& Receiver);
    bool UpdateSkinnedPositions(FDynamicWetReceiverContext& Receiver);
    bool UpdateSkinnedNormals(FDynamicWetReceiverContext& Receiver);
    bool ComputeSkinnedPosition(
        const FSkeletalMeshLODRenderData& LODData,
        const FSkinWeightVertexBuffer& SkinWeightBuffer,
        uint32 VertexIndex,
        FVector3f& OutPosition) const;
    bool ComputeSkinnedNormal(
        const FSkeletalMeshLODRenderData& LODData,
        const FSkinWeightVertexBuffer& SkinWeightBuffer,
        uint32 VertexIndex,
        FVector3f& OutNormal) const;

    TArray<FVector3f> CachedSkinnedPositions;
    TArray<FVector3f> CachedSkinnedNormals;
    TArray<FMatrix44f> CachedRefToLocalMatrices;
};
