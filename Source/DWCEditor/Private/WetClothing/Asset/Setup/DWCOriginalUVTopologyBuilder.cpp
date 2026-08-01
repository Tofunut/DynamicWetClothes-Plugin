#include "DWCOriginalUVTopologyBuilder.h"

#include "DataAssets/WetClothingAsset.h"
#include "Engine/SkeletalMesh.h"
#include "WetClothing/Foundation/MeshAnalysis/WetClothingAssetMeshAnalyzer.h"

bool FDWCOriginalUVTopologyBuilder::BuildLOD(
    const UWetClothingAsset& Asset,
    USkeletalMesh* PreparedMesh,
    const int32 LODIndex,
    FDWCEditorUVTopologyData& OutTopology,
    FString* OutErrorMessage,
    const TSet<int32>* TargetMaterialSlotIndices)
{
    OutTopology = FDWCEditorUVTopologyData();
    if (PreparedMesh == nullptr)
    {
        if (OutErrorMessage) *OutErrorMessage = TEXT("No DWC Prepared Skeletal Mesh is available.");
        return false;
    }

    OutTopology.LODIndex = LODIndex;
    OutTopology.UVChannelIndex = Asset.GetOriginalUVChannelIndex();
    OutTopology.BuildSignature = UWetClothingAsset::BuildMeshContentSignature(
        PreparedMesh,
        OutTopology.LODIndex,
        OutTopology.UVChannelIndex);
    OutTopology.GeneratorVersion = DWCGeneratedDataVersion::OriginalUVTopology;
    if (OutTopology.BuildSignature.IsEmpty())
    {
        if (OutErrorMessage) *OutErrorMessage = TEXT("Failed to build the DWC Prepared Mesh Original-UV signature.");
        return false;
    }

    for (int32 MaterialSlotIndex = 0; MaterialSlotIndex < PreparedMesh->GetMaterials().Num(); ++MaterialSlotIndex)
    {
        if (TargetMaterialSlotIndices != nullptr && !TargetMaterialSlotIndices->Contains(MaterialSlotIndex))
        {
            continue;
        }

        TArray<FWetClothingAssetUVIsland> Islands;
        FString Error;
        if (!FWetClothingAssetMeshAnalyzer::BuildMaterialSlotUVIslands(
                PreparedMesh,
                OutTopology.LODIndex,
                OutTopology.UVChannelIndex,
                MaterialSlotIndex,
                Islands,
                &Error))
        {
            if (OutErrorMessage)
            {
                *OutErrorMessage = FString::Printf(
                    TEXT("Material Slot %d Original-UV analysis failed: %s"),
                    MaterialSlotIndex,
                    *Error);
            }
            return false;
        }

        for (const FWetClothingAssetUVIsland& Island : Islands)
        {
            FDWCOriginalUVIslandTopology& Record = OutTopology.Islands.AddDefaulted_GetRef();
            Record.MaterialSlotIndex = MaterialSlotIndex;
            Record.IslandID = Island.UVIslandID;
            Record.TriangleIndices = Island.TriangleIDs;
            Record.UVBounds = Island.UVBounds;
            Record.UVArea = Island.UVArea;
        }
    }

    OutTopology.bIsValid = !OutTopology.Islands.IsEmpty();
    if (!OutTopology.bIsValid)
    {
        if (OutErrorMessage) *OutErrorMessage = TEXT("The DWC Prepared Mesh contains no Original-UV island records.");
        return false;
    }

    if (OutErrorMessage) OutErrorMessage->Reset();
    return true;
}
