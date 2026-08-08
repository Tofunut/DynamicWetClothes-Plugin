// Copyright 2026 Team Tofunut. All Rights Reserved.

/*
 * Defines vertex, index, normal, UV, color, and tangent arrays used to build Wet Part and selection overlays.
 */

#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"

struct FDWCOverlayMeshData
{
    TArray<FVector>          Vertices;
    TArray<int32>            Indices;
    TArray<FVector>          Normals;
    TArray<FVector2D>        UVs;
    TArray<FLinearColor>     VertexColors;
    TArray<FProcMeshTangent> Tangents;

    void Reset()
    {
        Vertices.Reset();
        Indices.Reset();
        Normals.Reset();
        UVs.Reset();
        VertexColors.Reset();
        Tangents.Reset();
    }

    bool HasVertices() const
    {
        return Vertices.Num() > 0;
    }
};
