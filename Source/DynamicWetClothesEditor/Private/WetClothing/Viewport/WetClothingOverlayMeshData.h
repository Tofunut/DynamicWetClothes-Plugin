/*
 *  Wet Part 및 선택 Overlay Mesh 생성을 위한 정점, 인덱스, 노멀, 색상 배열 데이터를 정의합니다.
 */

#pragma once

#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"

struct FWetClothingOverlayMeshData
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
