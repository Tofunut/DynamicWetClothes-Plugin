#pragma once

#include "CoreMinimal.h"

struct FDWCLODVertexStaticData;

struct FDWCLODVertexColorTransferGeometryView
{
    TConstArrayView<FVector3f> Positions;
    TConstArrayView<FVector3f> Normals;

    bool IsValid() const
    {
        return !Positions.IsEmpty();
    }
};

struct FDWCLODVertexColorTransferTargetGeometryView
{
    int32 LODIndex = INDEX_NONE;
    FDWCLODVertexColorTransferGeometryView Geometry;
};

struct FDWCLODVertexColorTransferMapBuildResult
{
    int32 LODIndex = INDEX_NONE;
    TArray<int32> TargetToSourceVertex;
};

bool BuildDWCLODVertexColorTransferMaps(
    const FDWCLODVertexColorTransferGeometryView& SourceGeometry,
    TConstArrayView<FDWCLODVertexColorTransferTargetGeometryView> TargetGeometries,
    TArray<FDWCLODVertexColorTransferMapBuildResult>& OutResults);
