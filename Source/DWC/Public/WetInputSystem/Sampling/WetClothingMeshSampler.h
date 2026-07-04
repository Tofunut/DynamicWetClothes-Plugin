#pragma once

#include "CoreMinimal.h"

class USkeletalMeshComponent;
class FSkeletalMeshLODRenderData;
class FSkinWeightVertexBuffer;

class FWetClothingMeshSampler
{
  public:
    void ResetPositions();
    void ResetNormals();

    bool UpdateSkinningMatrices(USkeletalMeshComponent* TargetSkeletalMesh);
    bool UpdateSkinnedPositions(USkeletalMeshComponent* TargetSkeletalMesh, int32 LODIndex = 0);
    bool UpdateSkinnedPositionsDirect(USkeletalMeshComponent* TargetSkeletalMesh, int32 LODIndex = 0);
    bool UpdateSkinnedNormals(USkeletalMeshComponent* TargetSkeletalMesh, int32 LODIndex = 0);
    bool ComputeSkinnedPosition(
        const FSkeletalMeshLODRenderData& LODData,
        const FSkinWeightVertexBuffer&    SkinWeightBuffer,
        uint32                            VertexIndex,
        FVector3f&                        OutPosition) const;
    bool ComputeSkinnedNormal(
        const FSkeletalMeshLODRenderData& LODData,
        const FSkinWeightVertexBuffer&    SkinWeightBuffer,
        uint32                            VertexIndex,
        FVector3f&                        OutNormal) const;

    TArray<FVector3f>  CachedSkinnedPositions;
    TArray<FVector3f>  CachedSkinnedNormals;
    TArray<FMatrix44f> CachedRefToLocalMatrices;
};
