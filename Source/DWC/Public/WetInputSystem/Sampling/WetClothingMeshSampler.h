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
    void CommitSkinnedCacheFromTask(
        USkeletalMeshComponent*    TargetSkeletalMesh,
        int32                      LODIndex,
        uint64                     FrameNumber,
        TArray<FVector3f>&&        SkinnedPositions,
        TArray<FVector3f>&&        SkinnedNormals);
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

  private:
    bool IsSkinningMatrixCacheValid(const USkeletalMeshComponent* TargetSkeletalMesh, uint64 FrameNumber) const;
    bool IsSkinnedPositionCacheValid(const USkeletalMeshComponent* TargetSkeletalMesh, int32 LODIndex, uint64 FrameNumber) const;
    bool IsSkinnedNormalCacheValid(const USkeletalMeshComponent* TargetSkeletalMesh, int32 LODIndex, uint64 FrameNumber) const;
    void InvalidateSkinnedPositionCache();
    void InvalidateSkinnedNormalCache();

    TWeakObjectPtr<USkeletalMeshComponent> CachedSkinningMatrixMesh;
    TWeakObjectPtr<USkeletalMeshComponent> CachedSkinnedPositionMesh;
    TWeakObjectPtr<USkeletalMeshComponent> CachedSkinnedNormalMesh;

    uint64 CachedSkinningMatrixFrameNumber = 0;
    uint64 CachedSkinnedPositionFrameNumber = 0;
    uint64 CachedSkinnedNormalFrameNumber = 0;

    int32 CachedSkinnedPositionLODIndex = INDEX_NONE;
    int32 CachedSkinnedNormalLODIndex = INDEX_NONE;

    bool bCachedSkinningMatricesValid = false;
    bool bCachedSkinnedPositionsValid = false;
    bool bCachedSkinnedNormalsValid = false;
};
