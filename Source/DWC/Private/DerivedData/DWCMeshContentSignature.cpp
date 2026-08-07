//Copyright 2026 Team Tofunut. All Rights Reserved.
#include "DerivedData/DWCMeshContentSignature.h"

#include "Engine/SkeletalMesh.h"
#include "Misc/Crc.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"

namespace DWCMeshContentSignaturePrivate
{
    uint32 BuildTopologyHash(const FSkeletalMeshLODRenderData& LODData)
    {
        TArray<uint32> IndexBuffer;
        LODData.MultiSizeIndexContainer.GetIndexBuffer(IndexBuffer);
        return IndexBuffer.IsEmpty()
            ? 0
            : FCrc::MemCrc32(IndexBuffer.GetData(), IndexBuffer.Num() * sizeof(uint32));
    }

    uint32 BuildPositionHash(const FSkeletalMeshLODRenderData& LODData)
    {
        uint32 Hash = 0;
        const int32 VertexCount = static_cast<int32>(LODData.GetNumVertices());
        for (int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
        {
            const FVector3f Position =
                LODData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(VertexIndex);
            Hash = FCrc::MemCrc32(&Position, sizeof(Position), Hash);
        }
        return Hash;
    }

    uint32 BuildSectionHash(const FSkeletalMeshLODRenderData& LODData)
    {
        uint32 Hash = 0;
        for (const FSkelMeshRenderSection& Section : LODData.RenderSections)
        {
            const int32 SectionData[] =
            {
                static_cast<int32>(Section.MaterialIndex),
                static_cast<int32>(Section.BaseIndex),
                static_cast<int32>(Section.NumTriangles),
                static_cast<int32>(Section.BaseVertexIndex),
                static_cast<int32>(Section.NumVertices)
            };
            Hash = FCrc::MemCrc32(SectionData, sizeof(SectionData), Hash);
        }
        return Hash;
    }

    uint32 BuildUVHash(
        const FSkeletalMeshLODRenderData& LODData,
        const int32 UVChannelIndex)
    {
        if (UVChannelIndex < 0 || UVChannelIndex >= static_cast<int32>(LODData.GetNumTexCoords()))
        {
            return 0;
        }

        uint32 Hash = 0;
        const int32 VertexCount = static_cast<int32>(LODData.GetNumVertices());
        for (int32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
        {
            const FVector2f UV =
                LODData.StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV(VertexIndex, UVChannelIndex);
            Hash = FCrc::MemCrc32(&UV, sizeof(UV), Hash);
        }
        return Hash;
    }
}

FString FDWCMeshContentSignature::BuildStructure(
    const USkeletalMesh* SkeletalMesh,
    const int32 LODIndex)
{
    const FSkeletalMeshRenderData* RenderData =
        SkeletalMesh != nullptr ? SkeletalMesh->GetResourceForRendering() : nullptr;
    if (RenderData == nullptr || !RenderData->LODRenderData.IsValidIndex(LODIndex))
    {
        return FString();
    }

    return BuildStructure(SkeletalMesh, RenderData->LODRenderData[LODIndex], LODIndex);
}

FString FDWCMeshContentSignature::BuildStructure(
    const USkeletalMesh* SkeletalMesh,
    const FSkeletalMeshLODRenderData& LODData,
    const int32 LODIndex)
{
    TArray<uint32> IndexBuffer;
    LODData.MultiSizeIndexContainer.GetIndexBuffer(IndexBuffer);

    return FString::Printf(
        TEXT("LOD=%d|Vertices=%d|Indices=%d|Materials=%d|Topology=%08X|Positions=%08X|Sections=%08X"),
        LODIndex,
        LODData.GetNumVertices(),
        IndexBuffer.Num(),
        SkeletalMesh != nullptr ? SkeletalMesh->GetMaterials().Num() : 0,
        DWCMeshContentSignaturePrivate::BuildTopologyHash(LODData),
        DWCMeshContentSignaturePrivate::BuildPositionHash(LODData),
        DWCMeshContentSignaturePrivate::BuildSectionHash(LODData));
}

FString FDWCMeshContentSignature::BuildUVContent(
    const USkeletalMesh* SkeletalMesh,
    const int32 LODIndex,
    const int32 UVChannelIndex)
{
    const FSkeletalMeshRenderData* RenderData =
        SkeletalMesh != nullptr ? SkeletalMesh->GetResourceForRendering() : nullptr;
    if (RenderData == nullptr || !RenderData->LODRenderData.IsValidIndex(LODIndex))
    {
        return FString();
    }

    const FSkeletalMeshLODRenderData& LODData = RenderData->LODRenderData[LODIndex];
    return FString::Printf(
        TEXT("%s|UV=%d|UVHash=%08X"),
        *BuildStructure(SkeletalMesh, LODData, LODIndex),
        UVChannelIndex,
        DWCMeshContentSignaturePrivate::BuildUVHash(LODData, UVChannelIndex));
}
