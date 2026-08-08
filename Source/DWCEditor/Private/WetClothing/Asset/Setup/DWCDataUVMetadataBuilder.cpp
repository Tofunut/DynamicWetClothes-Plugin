// Copyright 2026 Team Tofunut. All Rights Reserved.

#include "DWCDataUVMetadataBuilder.h"

#include "DataAssets/WetClothingAsset.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "WetClothing/Foundation/MeshAnalysis/WetClothingAssetMeshAnalyzer.h"

bool FDWCDataUVMetadataBuilder::BuildLOD(
    const UWetClothingAsset& Asset,
    const USkeletalMesh*     Mesh,
    const int32              LODIndex,
    const int32              DataUVChannelIndex,
    FDWCDataUVLODMetadata&   OutMetadata,
    FString*                 OutErrorMessage,
    const TSet<int32>*       GeneratedMaterialSlotIndices)
{
    OutMetadata = FDWCDataUVLODMetadata();
    if (Mesh == nullptr)
    {
        if (OutErrorMessage)
            *OutErrorMessage = TEXT("No runtime mesh is available.");
        return false;
    }

    const FSkeletalMeshRenderData* RenderData = Mesh->GetResourceForRendering();
    if (RenderData == nullptr || !RenderData->LODRenderData.IsValidIndex(LODIndex))
    {
        if (OutErrorMessage)
            *OutErrorMessage = FString::Printf(TEXT("LOD%d render data is unavailable."), LODIndex);
        return false;
    }

    const FSkeletalMeshLODRenderData& LODData = RenderData->LODRenderData[LODIndex];
    const int32                       VertexCount = static_cast<int32>(LODData.GetNumVertices());
    if (VertexCount <= 0)
    {
        if (OutErrorMessage)
            *OutErrorMessage = FString::Printf(TEXT("LOD%d has no render vertices."), LODIndex);
        return false;
    }

    const int32 NumTexCoords = static_cast<int32>(LODData.StaticVertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords());
    if (DataUVChannelIndex < 0 || DataUVChannelIndex >= NumTexCoords)
    {
        if (OutErrorMessage)
        {
            *OutErrorMessage = FString::Printf(
                TEXT("LOD%d does not contain DWC UV Channel %d."),
                LODIndex,
                DataUVChannelIndex);
        }
        return false;
    }

    OutMetadata.bIsValid = true;
    OutMetadata.LODIndex = LODIndex;
    OutMetadata.RenderVertexCount = VertexCount;
    OutMetadata.MaterialSlotCount = Mesh->GetMaterials().Num();
    OutMetadata.UVChannelIndex = DataUVChannelIndex;
    OutMetadata.MeshInputSignature = UWetClothingAsset::BuildMeshContentSignature(
        Mesh,
        LODIndex,
        Asset.GetOriginalUVChannelIndex());
    OutMetadata.DataUVOutputSignature = UWetClothingAsset::BuildMeshContentSignature(
        Mesh,
        LODIndex,
        DataUVChannelIndex);
    OutMetadata.GeneratorVersion = DWCGeneratedDataVersion::DataUV;

    if (OutMetadata.MeshInputSignature.IsEmpty() || OutMetadata.DataUVOutputSignature.IsEmpty())
    {
        if (OutErrorMessage)
            *OutErrorMessage = FString::Printf(TEXT("LOD%d DWC UV Channel signature is empty."), LODIndex);
        return false;
    }

    if (LODIndex == UWetClothingAsset::RuntimeSimulationLODIndex)
    {
        int32                                     MaximumTriangleID = INDEX_NONE;
        TArray<TArray<FWetClothingAssetUVIsland>> IslandsByMaterialSlot;
        IslandsByMaterialSlot.SetNum(Mesh->GetMaterials().Num());
        for (int32 MaterialSlotIndex = 0; MaterialSlotIndex < Mesh->GetMaterials().Num(); ++MaterialSlotIndex)
        {
            if (GeneratedMaterialSlotIndices != nullptr)
            {
                if (!GeneratedMaterialSlotIndices->Contains(MaterialSlotIndex))
                {
                    continue;
                }
            }
            else if (!Asset.IsMaterialSlotWettable(MaterialSlotIndex))
            {
                continue;
            }

            TArray<FWetClothingAssetUVIsland>& Islands = IslandsByMaterialSlot[MaterialSlotIndex];
            FString                            TopologyError;
            if (!FWetClothingAssetMeshAnalyzer::BuildMaterialSlotUVIslands(
                    Mesh,
                    LODIndex,
                    DataUVChannelIndex,
                    MaterialSlotIndex,
                    Islands,
                    &TopologyError))
            {
                if (OutErrorMessage)
                {
                    *OutErrorMessage = FString::Printf(
                        TEXT("LOD%d DWC UV Channel topology failed for material slot %d: %s"),
                        LODIndex,
                        MaterialSlotIndex,
                        *TopologyError);
                }
                return false;
            }

            for (const FWetClothingAssetUVIsland& Island : Islands)
            {
                for (const int32 TriangleID : Island.TriangleIDs)
                {
                    MaximumTriangleID = FMath::Max(MaximumTriangleID, TriangleID);
                }
            }
        }

        if (MaximumTriangleID != INDEX_NONE)
        {
            OutMetadata.DataUVIslandIDByTriangleID.Init(INDEX_NONE, MaximumTriangleID + 1);
            for (const TArray<FWetClothingAssetUVIsland>& Islands : IslandsByMaterialSlot)
            {
                for (const FWetClothingAssetUVIsland& Island : Islands)
                {
                    for (const int32 TriangleID : Island.TriangleIDs)
                    {
                        if (OutMetadata.DataUVIslandIDByTriangleID.IsValidIndex(TriangleID))
                        {
                            OutMetadata.DataUVIslandIDByTriangleID[TriangleID] = Island.UVIslandID;
                        }
                    }
                }
            }
        }
    }

    if (OutErrorMessage)
        OutErrorMessage->Reset();
    return true;
}
