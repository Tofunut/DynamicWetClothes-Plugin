#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetClothingAssetSetupData.h"

class USkeletalMesh;
class UWetClothingAsset;

/** Builds WCA-owned persistent Editor-only Original UV topology from the DWC Prepared Skeletal Mesh. */
class FDWCOriginalUVTopologyBuilder
{
public:
    static bool BuildLOD(
        const UWetClothingAsset& Asset,
        USkeletalMesh* PreparedMesh,
        int32 LODIndex,
        FDWCEditorUVTopologyData& OutTopology,
        FString* OutErrorMessage = nullptr);
};
