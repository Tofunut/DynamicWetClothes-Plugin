//Copyright 2026 Team Tofunut. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Cache/DWCEditorCacheStore.h"

enum class EDWCEditorSpatialEdgeType : uint8
{
    Boundary,
    Regular,
    UVSeam,
    Blocked
};

struct FDWCEditorSpatialTriangle
{
    int32 MaterialSlotIndex = INDEX_NONE;
    int32 TriangleID = INDEX_NONE;
    int32 UVIslandID = INDEX_NONE;
    FVector3f LocalPositions[3] = {
        FVector3f::ZeroVector,
        FVector3f::ZeroVector,
        FVector3f::ZeroVector
    };
    FVector2f UVs[3] = {
        FVector2f::ZeroVector,
        FVector2f::ZeroVector,
        FVector2f::ZeroVector
    };
    // Stable mesh-topology identities. Imported vertex IDs are preferred;
    // conservative welded IDs are used only when imported data is unavailable.
    int64 TopologyVertexIDs[3] = { INDEX_NONE, INDEX_NONE, INDEX_NONE };
    // Edge i connects corner i to corner (i + 1) % 3.
    int32 AdjacentTriangleIndices[3] = { INDEX_NONE, INDEX_NONE, INDEX_NONE };
    EDWCEditorSpatialEdgeType EdgeTypes[3] = {
        EDWCEditorSpatialEdgeType::Boundary,
        EDWCEditorSpatialEdgeType::Boundary,
        EDWCEditorSpatialEdgeType::Boundary
    };
    FVector3f LocalNormal = FVector3f(0.0f, 0.0f, 1.0f);
    FVector3f LocalNormals[3] = {
        FVector3f(0.0f, 0.0f, 1.0f),
        FVector3f(0.0f, 0.0f, 1.0f),
        FVector3f(0.0f, 0.0f, 1.0f)
    };
    // Render tangent basis at each triangle corner. Normal maps sampled with the
    // generated Data UV are still interpreted in this mesh tangent space.
    FVector3f LocalTangents[3] = {
        FVector3f(1.0f, 0.0f, 0.0f),
        FVector3f(1.0f, 0.0f, 0.0f),
        FVector3f(1.0f, 0.0f, 0.0f)
    };
    FVector3f LocalBitangents[3] = {
        FVector3f(0.0f, 1.0f, 0.0f),
        FVector3f(0.0f, 1.0f, 0.0f),
        FVector3f(0.0f, 1.0f, 0.0f)
    };
    // Representative basis retained for hit/debug consumers that do not need
    // corner interpolation.
    FVector3f LocalTangent = FVector3f(1.0f, 0.0f, 0.0f);
    FVector3f LocalBitangent = FVector3f(0.0f, 1.0f, 0.0f);
    FVector3f LocalSurfaceAxisU = FVector3f(1.0f, 0.0f, 0.0f);
    FVector3f LocalSurfaceAxisV = FVector3f(0.0f, 1.0f, 0.0f);
    FVector2f SurfaceUnitsPerUV = FVector2f::ZeroVector;
    FBox3f LocalBounds = FBox3f(ForceInit);
    FBox2f UVBounds = FBox2f(ForceInit);
};

struct FDWCEditorSpatialBVHNode
{
    FBox3f Bounds = FBox3f(ForceInit);
    int32 LeftChildIndex = INDEX_NONE;
    int32 RightChildIndex = INDEX_NONE;
    int32 FirstTriangleIndex = 0;
    int32 TriangleCount = 0;

    bool IsLeaf() const
    {
        return LeftChildIndex == INDEX_NONE && RightChildIndex == INDEX_NONE;
    }
};

struct FDWCEditorSpatialData final : IDWCEditorCacheValue
{
    static constexpr int32 UVGridResolution = 64;

    int32 LODIndex = 0;
    int32 UVChannelIndex = INDEX_NONE;
    int32 MaterialSlotIndex = INDEX_NONE;
    TArray<FDWCEditorSpatialTriangle> Triangles;
    TMap<uint64, int32> TriangleLookup;
    TArray<int32> BVHTriangleIndices;
    TArray<FDWCEditorSpatialBVHNode> BVHNodes;
    TArray<TArray<int32>> UVTriangleGrid;

    static FName StaticCacheTypeName();
    virtual FName GetCacheTypeName() const override { return StaticCacheTypeName(); }
    virtual uint64 GetAllocatedSizeBytes() const override;
};

struct FDWCEditorSurfaceHit
{
    bool bHit = false;
    int32 MaterialSlotIndex = INDEX_NONE;
    int32 TriangleID = INDEX_NONE;
    int32 UVIslandID = INDEX_NONE;
    int32 UVChannelIndex = INDEX_NONE;
    FVector WorldPosition = FVector::ZeroVector;
    FVector WorldNormal = FVector::UpVector;
    FVector WorldTangent = FVector::ForwardVector;
    FVector WorldBitangent = FVector::RightVector;
    FVector LocalPosition = FVector::ZeroVector;
    FVector LocalNormal = FVector::UpVector;
    FVector LocalTangent = FVector::ForwardVector;
    FVector LocalBitangent = FVector::RightVector;
    FVector LocalSurfaceAxisU = FVector::ForwardVector;
    FVector LocalSurfaceAxisV = FVector::RightVector;
    FVector LocalSurfaceFrameU = FVector::ForwardVector;
    FVector LocalSurfaceFrameV = FVector::RightVector;
    FVector2f SurfaceUnitsPerUV = FVector2f::ZeroVector;
    FVector2D UV = FVector2D::ZeroVector;
    FVector Barycentric = FVector::ZeroVector;
    double DistanceSq = TNumericLimits<double>::Max();
};

struct FDWCEditorProjectedSurface
{
    int32 MaterialSlotIndex = INDEX_NONE;
    int32 TriangleID = INDEX_NONE;
    int32 UVIslandID = INDEX_NONE;
    FVector Barycentric = FVector(1.0, 0.0, 0.0);
    FVector2D UV = FVector2D::ZeroVector;
    FVector LocalPosition = FVector::ZeroVector;
    FVector LocalNormal = FVector::UpVector;
    FVector LocalTangent = FVector::ForwardVector;
    FVector LocalBitangent = FVector::RightVector;
    FVector LocalSurfaceAxisU = FVector::ForwardVector;
    FVector LocalSurfaceAxisV = FVector::RightVector;
    FVector LocalSurfaceFrameU = FVector::ForwardVector;
    FVector LocalSurfaceFrameV = FVector::RightVector;
    FVector2f SurfaceUnitsPerUV = FVector2f::ZeroVector;
    FVector WorldPosition = FVector::ZeroVector;
    FVector WorldNormal = FVector::UpVector;
    FVector WorldTangent = FVector::ForwardVector;
    FVector WorldBitangent = FVector::RightVector;
};

struct FDWCEditorTriangleEdgeTopology
{
    int32 TriangleIndex = INDEX_NONE;
    int32 EdgeIndex = INDEX_NONE;
    int32 AdjacentTriangleIndex = INDEX_NONE;
    EDWCEditorSpatialEdgeType EdgeType = EDWCEditorSpatialEdgeType::Boundary;
};

using FDWCEditorSpatialHandle = TSharedPtr<const FDWCEditorSpatialData, ESPMode::ThreadSafe>;
using FDWCEditorSpatialLease = FDWCEditorCacheLease;
