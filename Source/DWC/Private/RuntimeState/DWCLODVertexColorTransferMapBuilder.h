#pragma once

#include "Async/DWCLODVertexColorTypes.h"
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

bool BuildDWCLODVertexColorTransferMap(
    const FDWCLODVertexColorTransferGeometryView& SourceGeometry,
    const FDWCLODVertexColorTransferGeometryView& TargetGeometry,
    const FDWCLODVertexColorTransferSettings&     Settings,
    TArray<int32>&                                OutTargetToSourceVertex);

bool BuildDWCLODVertexColorTransferMap(
    const FDWCLODVertexStaticData&            SourceLODData,
    const FDWCLODVertexStaticData&            TargetLODData,
    const FDWCLODVertexColorTransferSettings& Settings,
    TArray<int32>&                            OutTargetToSourceVertex);
