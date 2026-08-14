// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "DWCEditorUVTopologyBuilder.h"

#include "DataAssets/WetClothingAsset.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "WetClothing/Foundation/MeshAnalysis/WetClothingAssetMeshAnalyzer.h"
#include "WetClothing/Foundation/UV/DWCUVGeometry.h"

namespace DWCEditorUVTopologyBuilderPrivate
{
    void MoveBuiltIslands(
        TArray<FWetClothingAssetUVIsland>&& BuiltIslands,
        TArray<TSharedPtr<FWetClothingAssetUVIsland>>& OutIslands)
    {
        OutIslands.Reset(BuiltIslands.Num());
        for (FWetClothingAssetUVIsland& Island : BuiltIslands)
        {
            OutIslands.Add(MakeShared<FWetClothingAssetUVIsland>(MoveTemp(Island)));
        }
    }
}

bool FDWCEditorUVTopologyBuilder::BuildMaterialSlotUVIslands(
    const USkeletalMesh* SkeletalMesh,
    const int32 LODIndex,
    const int32 UVChannelIndex,
    const int32 MaterialSlotIndex,
    TArray<TSharedPtr<FWetClothingAssetUVIsland>>& OutIslands,
    FString* OutErrorMessage)
{
    OutIslands.Reset();
    if (SkeletalMesh == nullptr)
    {
        FWetClothingAssetMeshAnalyzer::SetError(
            OutErrorMessage,
            TEXT("Generate the DWC UV Channel to inspect its UV islands."));
        return false;
    }

    TArray<FWetClothingAssetUVIsland> BuiltIslands;
    if (!FWetClothingAssetMeshAnalyzer::BuildMaterialSlotUVIslands(
            SkeletalMesh,
            LODIndex,
            UVChannelIndex,
            MaterialSlotIndex,
            BuiltIslands,
            OutErrorMessage))
    {
        return false;
    }

    DWCEditorUVTopologyBuilderPrivate::MoveBuiltIslands(MoveTemp(BuiltIslands), OutIslands);
    return true;
}

bool FDWCEditorUVTopologyBuilder::BuildMaterialSlotUVIslands(
    const UWetClothingAsset* WetClothingAsset,
    const int32 LODIndex,
    const int32 UVChannelIndex,
    const int32 MaterialSlotIndex,
    TArray<TSharedPtr<FWetClothingAssetUVIsland>>& OutIslands,
    FString* OutErrorMessage)
{
    OutIslands.Reset();
    if (WetClothingAsset == nullptr)
    {
        FWetClothingAssetMeshAnalyzer::SetError(
            OutErrorMessage,
            TEXT("No Wet Clothing Asset is assigned."));
        return false;
    }

    const USkeletalMesh* AnalysisMesh = WetClothingAsset->GetRuntimeSkeletalMesh();
    if (AnalysisMesh == nullptr)
    {
        AnalysisMesh = WetClothingAsset->GetSourceSkeletalMesh();
    }
    if (AnalysisMesh == nullptr)
    {
        FWetClothingAssetMeshAnalyzer::SetError(
            OutErrorMessage,
            TEXT("No mesh is available for UV island analysis."));
        return false;
    }

    const int32 OriginalUVChannelIndex = WetClothingAsset->GetOriginalUVChannelIndex();
    const FDWCEditorUVTopologyHandle TopologyHandle =
        WetClothingAsset->AcquireOriginalUVTopologyForLOD(LODIndex);
    const FDWCEditorUVTopologyData* Topology = TopologyHandle.Get();
    const bool bUseStoredTopology =
        Topology != nullptr && Topology->bIsValid &&
        Topology->UVChannelIndex == OriginalUVChannelIndex &&
        UVChannelIndex == OriginalUVChannelIndex &&
        Topology->GeneratorVersion == DWCGeneratedDataVersion::OriginalUVTopology &&
        !Topology->BuildSignature.IsEmpty() && !Topology->Islands.IsEmpty();

    TArray<FWetClothingAssetUVIsland> BuiltIslands;
    bool bBuilt = false;
    if (bUseStoredTopology)
    {
        bBuilt = FWetClothingAssetMeshAnalyzer::BuildMaterialSlotUVIslandsFromTopology(
            AnalysisMesh,
            LODIndex,
            UVChannelIndex,
            MaterialSlotIndex,
            Topology->Islands,
            BuiltIslands,
            OutErrorMessage);
    }
    if (!bBuilt)
    {
        BuiltIslands.Reset();
        bBuilt = FWetClothingAssetMeshAnalyzer::BuildMaterialSlotUVIslands(
            AnalysisMesh,
            LODIndex,
            UVChannelIndex,
            MaterialSlotIndex,
            BuiltIslands,
            OutErrorMessage);
    }
    if (!bBuilt)
    {
        return false;
    }

    DWCEditorUVTopologyBuilderPrivate::MoveBuiltIslands(MoveTemp(BuiltIslands), OutIslands);
    return true;
}

bool FDWCEditorUVTopologyBuilder::BuildMaterialSlotGeometryPreviewTriangles(
    const USkeletalMesh* SkeletalMesh,
    const int32 LODIndex,
    const int32 PreferredUVChannelIndex,
    const int32 MaterialSlotIndex,
    TArray<FWetClothingAssetUVTriangle>& OutTriangles)
{
    OutTriangles.Reset();
    if (SkeletalMesh == nullptr || !SkeletalMesh->GetMaterials().IsValidIndex(MaterialSlotIndex))
    {
        return false;
    }

    const FSkeletalMeshRenderData* RenderData = SkeletalMesh->GetResourceForRendering();
    if (RenderData == nullptr || !RenderData->LODRenderData.IsValidIndex(LODIndex))
    {
        return false;
    }

    const FSkeletalMeshLODRenderData& LODData = RenderData->LODRenderData[LODIndex];
    const int32 VertexCount = static_cast<int32>(LODData.GetNumVertices());
    if (VertexCount <= 0)
    {
        return false;
    }

    TArray<uint32> IndexBuffer;
    LODData.MultiSizeIndexContainer.GetIndexBuffer(IndexBuffer);
    if (IndexBuffer.IsEmpty())
    {
        return false;
    }

    const int32 NumUVChannels = static_cast<int32>(LODData.GetNumTexCoords());
    const int32 PreviewUVChannelIndex = NumUVChannels > 0
        ? FMath::Clamp(PreferredUVChannelIndex, 0, NumUVChannels - 1)
        : INDEX_NONE;

    for (const FSkelMeshRenderSection& Section : LODData.RenderSections)
    {
        if (!Section.IsValid() || Section.MaterialIndex != MaterialSlotIndex)
        {
            continue;
        }

        const int32 FirstIndex = static_cast<int32>(Section.BaseIndex);
        const int32 LastIndex = FMath::Min(
            FirstIndex + static_cast<int32>(Section.NumTriangles * 3),
            IndexBuffer.Num());
        for (int32 TriangleIndex = FirstIndex; TriangleIndex + 2 < LastIndex; TriangleIndex += 3)
        {
            const uint32 Indices[3] = {
                IndexBuffer[TriangleIndex],
                IndexBuffer[TriangleIndex + 1],
                IndexBuffer[TriangleIndex + 2]
            };
            if (Indices[0] >= static_cast<uint32>(VertexCount) ||
                Indices[1] >= static_cast<uint32>(VertexCount) ||
                Indices[2] >= static_cast<uint32>(VertexCount))
            {
                continue;
            }

            FWetClothingAssetUVTriangle Triangle;
            Triangle.TriangleID = TriangleIndex / 3;
            Triangle.MaterialSlotIndex = MaterialSlotIndex;
            Triangle.UVIslandID = INDEX_NONE;
            for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
            {
                const uint32 VertexIndex = Indices[CornerIndex];
                Triangle.LocalPositions[CornerIndex] = FVector(
                    LODData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(VertexIndex));
                Triangle.RenderVertexIndices[CornerIndex] = static_cast<int32>(VertexIndex);
                Triangle.LocalNormals[CornerIndex] = FVector(
                    LODData.StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(VertexIndex)).GetSafeNormal();

                FVector2D PreviewUV = FVector2D::ZeroVector;
                if (PreviewUVChannelIndex != INDEX_NONE)
                {
                    PreviewUV = FVector2D(
                        LODData.StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV(
                            VertexIndex,
                            PreviewUVChannelIndex));
                    if (!FDWCUVGeometry::IsFiniteReasonableUV(PreviewUV))
                    {
                        PreviewUV = FVector2D::ZeroVector;
                    }
                }
                Triangle.UVs[CornerIndex] = PreviewUV;
            }

            if (FDWCUVGeometry::ComputeTriangleDoubleArea3D(
                    Triangle.LocalPositions[0],
                    Triangle.LocalPositions[1],
                    Triangle.LocalPositions[2]) <= 1.0e-10)
            {
                continue;
            }
            OutTriangles.Add(MoveTemp(Triangle));
        }
    }
    return !OutTriangles.IsEmpty();
}
