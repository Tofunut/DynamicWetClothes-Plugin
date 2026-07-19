#include "DWCDataUVMetadataBuilder.h"

#include "DataAssets/WetClothingAsset.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"

bool FDWCDataUVMetadataBuilder::BuildLOD(
    const UWetClothingAsset& Asset,
    const USkeletalMesh* Mesh,
    const int32 LODIndex,
    const int32 DataUVChannelIndex,
    FDWCDataUVLODMetadata& OutMetadata,
    FString* OutErrorMessage)
{
    OutMetadata = FDWCDataUVLODMetadata();
    if (Mesh == nullptr)
    {
        if (OutErrorMessage) *OutErrorMessage = TEXT("No runtime mesh is available.");
        return false;
    }

    const FSkeletalMeshRenderData* RenderData = Mesh->GetResourceForRendering();
    if (RenderData == nullptr || !RenderData->LODRenderData.IsValidIndex(LODIndex))
    {
        if (OutErrorMessage) *OutErrorMessage = FString::Printf(TEXT("LOD%d render data is unavailable."), LODIndex);
        return false;
    }

    const FSkeletalMeshLODRenderData& LODData = RenderData->LODRenderData[LODIndex];
    const int32 VertexCount = static_cast<int32>(LODData.GetNumVertices());
    if (VertexCount <= 0)
    {
        if (OutErrorMessage) *OutErrorMessage = FString::Printf(TEXT("LOD%d has no render vertices."), LODIndex);
        return false;
    }

    const int32 NumTexCoords = static_cast<int32>(LODData.StaticVertexBuffers.StaticMeshVertexBuffer.GetNumTexCoords());
    if (DataUVChannelIndex < 0 || DataUVChannelIndex >= NumTexCoords)
    {
        if (OutErrorMessage)
        {
            *OutErrorMessage = FString::Printf(
                TEXT("LOD%d does not contain DWC Data UV channel %d."),
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
        if (OutErrorMessage) *OutErrorMessage = FString::Printf(TEXT("LOD%d DWC Data UV signature is empty."), LODIndex);
        return false;
    }

    if (OutErrorMessage) OutErrorMessage->Reset();
    return true;
}
