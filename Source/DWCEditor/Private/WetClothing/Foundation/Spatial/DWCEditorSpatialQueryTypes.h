// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "WetClothing/Foundation/Cache/DWCEditorCacheStore.h"

struct FDWCEditorSpatialTriangle
{
    int32     MaterialSlotIndex = INDEX_NONE;
    int32     TriangleID = INDEX_NONE;
    int32     UVIslandID = INDEX_NONE;
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
    FVector3f LocalNormal = FVector3f(0.0f, 0.0f, 1.0f);
    FVector3f LocalTangent = FVector3f(1.0f, 0.0f, 0.0f);
    FVector3f LocalBitangent = FVector3f(0.0f, 1.0f, 0.0f);
    FBox3f    LocalBounds = FBox3f(ForceInit);
    FBox2f    UVBounds = FBox2f(ForceInit);
};

struct FDWCEditorSpatialBVHNode
{
    FBox3f Bounds = FBox3f(ForceInit);
    int32  LeftChildIndex = INDEX_NONE;
    int32  RightChildIndex = INDEX_NONE;
    int32  FirstTriangleIndex = 0;
    int32  TriangleCount = 0;

    bool IsLeaf() const
    {
        return LeftChildIndex == INDEX_NONE && RightChildIndex == INDEX_NONE;
    }
};

struct FDWCEditorSpatialData final : IDWCEditorCacheValue
{
    static constexpr int32 UVGridResolution = 64;

    int32                             LODIndex = 0;
    int32                             UVChannelIndex = INDEX_NONE;
    int32                             MaterialSlotIndex = INDEX_NONE;
    TArray<FDWCEditorSpatialTriangle> Triangles;
    TMap<uint64, int32>               TriangleLookup;
    TArray<int32>                     BVHTriangleIndices;
    TArray<FDWCEditorSpatialBVHNode>  BVHNodes;
    TArray<TArray<int32>>             UVTriangleGrid;

    static FName   StaticCacheTypeName();
    virtual FName  GetCacheTypeName() const override { return StaticCacheTypeName(); }
    virtual uint64 GetAllocatedSizeBytes() const override;
};

struct FDWCEditorSurfaceHit
{
    bool      bHit = false;
    int32     MaterialSlotIndex = INDEX_NONE;
    int32     TriangleID = INDEX_NONE;
    int32     UVIslandID = INDEX_NONE;
    int32     UVChannelIndex = INDEX_NONE;
    FVector   WorldPosition = FVector::ZeroVector;
    FVector   WorldNormal = FVector::UpVector;
    FVector   WorldTangent = FVector::ForwardVector;
    FVector   WorldBitangent = FVector::RightVector;
    FVector   LocalPosition = FVector::ZeroVector;
    FVector   LocalNormal = FVector::UpVector;
    FVector   LocalTangent = FVector::ForwardVector;
    FVector   LocalBitangent = FVector::RightVector;
    FVector2D UV = FVector2D::ZeroVector;
    FVector   Barycentric = FVector::ZeroVector;
    double    DistanceSq = TNumericLimits<double>::Max();
};

struct FDWCEditorProjectedSurface
{
    int32   MaterialSlotIndex = INDEX_NONE;
    int32   TriangleID = INDEX_NONE;
    int32   UVIslandID = INDEX_NONE;
    FVector Barycentric = FVector(1.0, 0.0, 0.0);
    FVector WorldPosition = FVector::ZeroVector;
    FVector WorldNormal = FVector::UpVector;
    FVector WorldTangent = FVector::ForwardVector;
    FVector WorldBitangent = FVector::RightVector;
};

using FDWCEditorSpatialHandle = TSharedPtr<const FDWCEditorSpatialData, ESPMode::ThreadSafe>;
using FDWCEditorSpatialLease = FDWCEditorCacheLease;
