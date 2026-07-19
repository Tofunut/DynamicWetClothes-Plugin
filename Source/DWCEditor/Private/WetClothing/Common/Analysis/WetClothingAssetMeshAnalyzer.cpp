#include "WetClothingAssetMeshAnalyzer.h"

#include "DWCUVPreviewDataBuilder.h"
#include "DWCUVPreviewTriangleReader.h"
#include "DataAssets/WetClothingAsset.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Utility/DWCError.h"

void FWetClothingAssetMeshAnalyzer::SetError(
    FString* OutErrorMessage,
    const TCHAR* InMessage)
{
    DWC::Error::SetMessage(OutErrorMessage, InMessage);
}

int32 FWetClothingAssetMeshAnalyzer::GetNumUVChannels(
    const USkeletalMesh* SkeletalMesh,
    const int32 LODIndex)
{
    if (SkeletalMesh == nullptr)
    {
        return 0;
    }

    const FSkeletalMeshRenderData* RenderData = SkeletalMesh->GetResourceForRendering();
    if (RenderData == nullptr || !RenderData->LODRenderData.IsValidIndex(LODIndex))
    {
        return 0;
    }

    return static_cast<int32>(RenderData->LODRenderData[LODIndex].GetNumTexCoords());
}

bool FWetClothingAssetMeshAnalyzer::BuildMaterialSlotUVIslands(
    const USkeletalMesh* SkeletalMesh,
    const int32 LODIndex,
    const int32 UVChannelIndex,
    const int32 MaterialSlotIndex,
    TArray<FWetClothingAssetUVIsland>& OutIslands,
    FString* OutErrorMessage)
{
    TArray<FDWCUVPreviewSourceTriangle> SourceTriangles;
    if (!FDWCUVPreviewTriangleReader::ReadFromSkeletalMesh(
            SkeletalMesh,
            LODIndex,
            UVChannelIndex,
            MaterialSlotIndex,
            SourceTriangles,
            OutErrorMessage))
    {
        OutIslands.Reset();
        return false;
    }

    FDWCUVPreviewDataBuilder::BuildFromConnectivity(SourceTriangles, OutIslands);
    SetError(OutErrorMessage, TEXT(""));
    return true;
}

bool FWetClothingAssetMeshAnalyzer::BuildMaterialSlotDataUVIslands(
    const UWetClothingAsset& WetClothingAsset,
    const int32 LODIndex,
    const int32 MaterialSlotIndex,
    TArray<FWetClothingAssetUVIsland>& OutIslands,
    FString* OutErrorMessage)
{
    TArray<FDWCUVPreviewSourceTriangle> SourceTriangles;
    if (!FDWCUVPreviewTriangleReader::ReadFromDataUV(
            WetClothingAsset,
            LODIndex,
            MaterialSlotIndex,
            SourceTriangles,
            OutErrorMessage))
    {
        OutIslands.Reset();
        return false;
    }

    FDWCUVPreviewDataBuilder::BuildFromConnectivity(SourceTriangles, OutIslands);
    SetError(OutErrorMessage, TEXT(""));
    return true;
}

bool FWetClothingAssetMeshAnalyzer::BuildMaterialSlotUVIslandsFromTopology(
    const USkeletalMesh* SkeletalMesh,
    const int32 LODIndex,
    const int32 UVChannelIndex,
    const int32 MaterialSlotIndex,
    const TArray<FDWCOriginalUVIslandTopology>& Topology,
    TArray<FWetClothingAssetUVIsland>& OutIslands,
    FString* OutErrorMessage)
{
    TArray<FDWCUVPreviewSourceTriangle> SourceTriangles;
    if (!FDWCUVPreviewTriangleReader::ReadFromSkeletalMesh(
            SkeletalMesh,
            LODIndex,
            UVChannelIndex,
            MaterialSlotIndex,
            SourceTriangles,
            OutErrorMessage))
    {
        OutIslands.Reset();
        return false;
    }

    return FDWCUVPreviewDataBuilder::BuildFromStoredTopology(
        SourceTriangles,
        MaterialSlotIndex,
        Topology,
        OutIslands,
        OutErrorMessage);
}
