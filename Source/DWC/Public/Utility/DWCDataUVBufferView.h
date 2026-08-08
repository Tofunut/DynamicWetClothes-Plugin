// Copyright 2026 Team Tofunut. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class USkeletalMesh;
class FSkeletalMeshLODRenderData;
class FStaticMeshVertexBuffer;

/**
 * Lightweight, non-owning view of one DWC Data UV channel in a Skeletal Mesh LOD.
 * The view never copies or owns UV coordinates. It is valid only while the mesh render resource remains unchanged.
 */
class DWC_API FDWCDataUVBufferView
{
  public:
    bool Initialize(
        const USkeletalMesh* SkeletalMesh,
        int32                LODIndex,
        int32                UVChannelIndex,
        FString*             OutErrorMessage = nullptr);

    void Reset();

    bool  IsValid() const { return LODData != nullptr && VertexBuffer != nullptr; }
    int32 NumVertices() const { return VertexCount; }
    int32 GetLODIndex() const { return ResolvedLODIndex; }
    int32 GetUVChannelIndex() const { return ResolvedUVChannelIndex; }

    bool      IsValidVertexIndex(int32 RenderVertexIndex) const;
    FVector2f GetUV(int32 RenderVertexIndex) const;

    const FSkeletalMeshLODRenderData* GetLODData() const { return LODData; }

  private:
    const FSkeletalMeshLODRenderData* LODData = nullptr;
    const FStaticMeshVertexBuffer*    VertexBuffer = nullptr;
    int32                             VertexCount = 0;
    int32                             ResolvedLODIndex = INDEX_NONE;
    int32                             ResolvedUVChannelIndex = INDEX_NONE;
};
