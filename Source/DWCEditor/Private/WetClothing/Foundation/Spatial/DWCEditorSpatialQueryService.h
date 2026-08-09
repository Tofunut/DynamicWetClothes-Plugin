//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSpatialQueryTypes.h"
#include "WetClothing/Foundation/Spatial/DWCEditorSurfaceOrientationPolicy.h"

class FDWCEditorCacheStore;
class USkeletalMesh;
class USkeletalMeshComponent;
class UWetClothingAsset;
struct FDWCEditorPreviewMemoryBucket;

/** Shared LOD0 authoring-surface topology and query service for WCA editor modes. */
class FDWCEditorSpatialQueryService final
{
  public:
    explicit FDWCEditorSpatialQueryService(
        TSharedRef<FDWCEditorCacheStore> InCacheStore,
        FDWCEditorSurfaceOrientationPolicy InOrientationPolicy = {});

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

    /** Resolves a legacy UV position only when it identifies exactly one surface triangle. */
    bool ResolveUniqueSurfaceAtUV(
        const FDWCEditorSpatialHandle& Handle,
        const USkeletalMeshComponent* MeshComponent,
        const FVector2D& UV,
        FDWCEditorProjectedSurface& OutSurface) const;

    bool GetTriangleEdgeTopology(
        const FDWCEditorSpatialHandle& Handle,
        int32 MaterialSlotIndex,
        int32 TriangleID,
        int32 EdgeIndex,
        FDWCEditorTriangleEdgeTopology& OutTopology) const;

    static bool NormalizeSurfaceAnchor(
        const FVector3f& Barycentric,
        FVector3f& OutNormalizedBarycentric);

    /** Builds an orthonormal local-space frame without consulting Data UV. */
    static bool BuildStableSurfaceFrame(
        const FVector3f& SurfaceNormal,
        const FVector3f& PreferredDirection,
        const FVector3f& PreferredBitangent,
        FVector3f& OutFrameU,
        FVector3f& OutFrameV);

    /** Builds deterministic physical-edge adjacency for a pre-populated spatial payload. */
    static void BuildTriangleTopology(FDWCEditorSpatialData& InOutData);

    void InvalidateMesh(const USkeletalMesh* Mesh);
    void Reset();
    void AppendDiagnosticMemoryBucket(TArray<FDWCEditorPreviewMemoryBucket>& OutBuckets) const;
    void ResetDiagnosticCounters();

  private:
    TOptional<FDWCEditorCacheKey> MakeCacheKey(
        const UWetClothingAsset* WetClothingAsset,
        const USkeletalMesh* Mesh,
        int32 LODIndex,
        int32 UVChannelIndex,
        int32 MaterialSlotIndex) const;
    bool BuildSpatialData(
        const UWetClothingAsset* WetClothingAsset,
        USkeletalMesh* Mesh,
        int32 LODIndex,
        int32 UVChannelIndex,
        int32 MaterialSlotIndex,
        FDWCEditorSpatialData& OutData,
        FString* OutError) const;

    TSharedRef<FDWCEditorCacheStore> CacheStore;
    FDWCEditorSurfaceOrientationPolicy OrientationPolicy;
};
