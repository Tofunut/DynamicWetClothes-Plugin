//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSpatialQueryTypes.h"

class FDWCEditorCacheStore;
class USkeletalMesh;
class USkeletalMeshComponent;
class UWetClothingAsset;
struct FDWCEditorPreviewMemoryBucket;

/** Shared LOD0 authoring-surface topology and query service for WCA editor modes. */
class FDWCEditorSpatialQueryService final
{
  public:
    explicit FDWCEditorSpatialQueryService(TSharedRef<FDWCEditorCacheStore> InCacheStore);

    FDWCEditorSpatialHandle Acquire(
        const UWetClothingAsset* WetClothingAsset,
        USkeletalMesh* Mesh,
        int32 UVChannelIndex,
        int32 MaterialSlotIndex,
        FString* OutError = nullptr);

    /** Returns an active lease that pins the immutable spatial payload. */
    FDWCEditorSpatialLease AcquireLease(
        const UWetClothingAsset* WetClothingAsset,
        USkeletalMesh* Mesh,
        int32 UVChannelIndex,
        int32 MaterialSlotIndex,
        FString* OutError = nullptr);

    bool TraceSurface(
        const FDWCEditorSpatialHandle& Handle,
        const USkeletalMeshComponent* MeshComponent,
        const FVector& RayOrigin,
        const FVector& RayDirection,
        FDWCEditorSurfaceHit& OutHit) const;

    void FindSurfacesAtUV(
        const FDWCEditorSpatialHandle& Handle,
        const USkeletalMeshComponent* MeshComponent,
        const FVector2D& UV,
        TArray<FDWCEditorProjectedSurface>& OutSurfaces) const;

    bool ResolveTriangleAnchor(
        const FDWCEditorSpatialHandle& Handle,
        const USkeletalMeshComponent* MeshComponent,
        int32 MaterialSlotIndex,
        int32 TriangleID,
        const FVector3f& Barycentric,
        FDWCEditorProjectedSurface& OutSurface) const;

    void InvalidateMesh(const USkeletalMesh* Mesh);
    void Reset();
    void AppendDiagnosticMemoryBucket(TArray<FDWCEditorPreviewMemoryBucket>& OutBuckets) const;
    void ResetDiagnosticCounters();

  private:
    static TOptional<FDWCEditorCacheKey> MakeCacheKey(
        const UWetClothingAsset* WetClothingAsset,
        const USkeletalMesh* Mesh,
        int32 LODIndex,
        int32 UVChannelIndex,
        int32 MaterialSlotIndex);
    static bool BuildSpatialData(
        const UWetClothingAsset* WetClothingAsset,
        USkeletalMesh* Mesh,
        int32 LODIndex,
        int32 UVChannelIndex,
        int32 MaterialSlotIndex,
        FDWCEditorSpatialData& OutData,
        FString* OutError);

    TSharedRef<FDWCEditorCacheStore> CacheStore;
};
