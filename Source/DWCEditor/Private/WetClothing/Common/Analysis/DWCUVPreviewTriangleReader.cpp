#include "DWCUVPreviewTriangleReader.h"

#include "DataAssets/WetClothingAsset.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Utility/DWCDataUVBufferView.h"
#include "Utility/DWCError.h"

namespace DWCUVPreviewTriangleReaderPrivate
{
    bool ValidateMaterialSlot(
        const USkeletalMesh* Mesh,
        const int32 MaterialSlotIndex,
        FString* OutErrorMessage)
    {
        if (Mesh == nullptr || !Mesh->GetMaterials().IsValidIndex(MaterialSlotIndex))
        {
            DWC::Error::SetMessage(OutErrorMessage, TEXT("The selected material slot is no longer valid."));
            return false;
        }
        return true;
    }

    void AddSectionTriangles(
        const FSkeletalMeshLODRenderData& LODData,
        const TArray<uint32>& IndexBuffer,
        const int32 UVChannelIndex,
        const int32 MaterialSlotIndex,
        const FDWCDataUVBufferView* DataUVView,
        TArray<FDWCUVPreviewSourceTriangle>& OutTriangles)
    {
        const int32 VertexCount = static_cast<int32>(LODData.GetNumVertices());
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
                const uint32 Indices[3] =
                {
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

                FDWCUVPreviewSourceTriangle& Triangle = OutTriangles.AddDefaulted_GetRef();
                Triangle.TriangleID = TriangleIndex / 3;
                Triangle.MaterialSlotIndex = MaterialSlotIndex;
                for (int32 CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
                {
                    const uint32 VertexIndex = Indices[CornerIndex];
                    Triangle.UVs[CornerIndex] = DataUVView != nullptr
                        ? FVector2D(DataUVView->GetUV(VertexIndex))
                        : FVector2D(LODData.StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV(
                            VertexIndex,
                            UVChannelIndex));
                    Triangle.LocalPositions[CornerIndex] = FVector(
                        LODData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(VertexIndex));
                }
            }
        }
    }
}

bool FDWCUVPreviewTriangleReader::ReadFromSkeletalMesh(
    const USkeletalMesh* SkeletalMesh,
    const int32 LODIndex,
    const int32 UVChannelIndex,
    const int32 MaterialSlotIndex,
    TArray<FDWCUVPreviewSourceTriangle>& OutTriangles,
    FString* OutErrorMessage)
{
    using namespace DWCUVPreviewTriangleReaderPrivate;

    OutTriangles.Reset();
    if (SkeletalMesh == nullptr)
    {
        DWC::Error::SetMessage(OutErrorMessage, TEXT("No Skeletal Mesh is assigned."));
        return false;
    }

    const FSkeletalMeshRenderData* RenderData = SkeletalMesh->GetResourceForRendering();
    if (RenderData == nullptr || !RenderData->LODRenderData.IsValidIndex(LODIndex))
    {
        DWC::Error::SetMessage(OutErrorMessage, TEXT("Requested Skeletal Mesh LOD render data is unavailable."));
        return false;
    }
    if (!ValidateMaterialSlot(SkeletalMesh, MaterialSlotIndex, OutErrorMessage))
    {
        return false;
    }

    const FSkeletalMeshLODRenderData& LODData = RenderData->LODRenderData[LODIndex];
    const int32 NumUVChannels = static_cast<int32>(LODData.GetNumTexCoords());
    if (NumUVChannels <= 0 || UVChannelIndex < 0 || UVChannelIndex >= NumUVChannels)
    {
        DWC::Error::SetMessage(OutErrorMessage, TEXT("The selected UV channel is not available on this mesh."));
        return false;
    }

    TArray<uint32> IndexBuffer;
    LODData.MultiSizeIndexContainer.GetIndexBuffer(IndexBuffer);
    if (IndexBuffer.IsEmpty())
    {
        DWC::Error::SetMessage(OutErrorMessage, TEXT("The Skeletal Mesh index buffer is empty."));
        return false;
    }

    AddSectionTriangles(
        LODData,
        IndexBuffer,
        UVChannelIndex,
        MaterialSlotIndex,
        nullptr,
        OutTriangles);
    DWC::Error::SetMessage(OutErrorMessage, TEXT(""));
    return true;
}

bool FDWCUVPreviewTriangleReader::ReadFromDataUV(
    const UWetClothingAsset& Asset,
    const int32 LODIndex,
    const int32 MaterialSlotIndex,
    TArray<FDWCUVPreviewSourceTriangle>& OutTriangles,
    FString* OutErrorMessage)
{
    using namespace DWCUVPreviewTriangleReaderPrivate;

    OutTriangles.Reset();
    const USkeletalMesh* RuntimeMesh = Asset.GetRuntimeSkeletalMesh();
    if (RuntimeMesh == nullptr)
    {
        DWC::Error::SetMessage(OutErrorMessage, TEXT("No DWC Prepared Skeletal Mesh is assigned."));
        return false;
    }
    if (!ValidateMaterialSlot(RuntimeMesh, MaterialSlotIndex, OutErrorMessage))
    {
        return false;
    }

    const FDWCDataUVLODMetadata* Metadata = Asset.FindDataUVMetadataForLOD(LODIndex);
    if (Metadata == nullptr || !Metadata->bIsValid)
    {
        DWC::Error::SetMessage(OutErrorMessage, TEXT("DWC Data UV metadata is missing for the requested LOD."));
        return false;
    }

    const FSkeletalMeshRenderData* RenderData = RuntimeMesh->GetResourceForRendering();
    if (RenderData == nullptr || !RenderData->LODRenderData.IsValidIndex(LODIndex))
    {
        DWC::Error::SetMessage(OutErrorMessage, TEXT("Requested DWC Prepared Mesh LOD render data is unavailable."));
        return false;
    }

    const FSkeletalMeshLODRenderData& LODData = RenderData->LODRenderData[LODIndex];
    const int32 VertexCount = static_cast<int32>(LODData.GetNumVertices());
    FDWCDataUVBufferView DataUVView;
    FString DataUVError;
    if (!DataUVView.Initialize(RuntimeMesh, LODIndex, Asset.GetDWCDataUVChannelIndex(), &DataUVError) ||
        VertexCount <= 0 ||
        Metadata->RenderVertexCount != VertexCount ||
        DataUVView.NumVertices() != VertexCount)
    {
        DWC::Error::SetMessage(
            OutErrorMessage,
            *FString::Printf(TEXT("DWC Data UV does not match render vertices. %s"), *DataUVError));
        return false;
    }

#if WITH_EDITOR
    const FString CurrentSignature = UWetClothingAsset::BuildMeshContentSignature(
        RuntimeMesh,
        LODIndex,
        Asset.GetDWCDataUVChannelIndex());
    if (CurrentSignature.IsEmpty() || Metadata->DataUVOutputSignature != CurrentSignature)
    {
        DWC::Error::SetMessage(OutErrorMessage, TEXT("DWC Data UV metadata is out of date."));
        return false;
    }
#endif

    TArray<uint32> IndexBuffer;
    LODData.MultiSizeIndexContainer.GetIndexBuffer(IndexBuffer);
    if (IndexBuffer.IsEmpty())
    {
        DWC::Error::SetMessage(OutErrorMessage, TEXT("The DWC Prepared Mesh index buffer is empty."));
        return false;
    }

    AddSectionTriangles(
        LODData,
        IndexBuffer,
        Asset.GetDWCDataUVChannelIndex(),
        MaterialSlotIndex,
        &DataUVView,
        OutTriangles);
    DWC::Error::SetMessage(OutErrorMessage, TEXT(""));
    return true;
}
