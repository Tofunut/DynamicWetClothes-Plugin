#pragma once

#include "CoreMinimal.h"
#include "DataAssets/WetClothingAssetSetupData.h"

class USkeletalMesh;
class UWetClothingAsset;

/** Builds the small persistent metadata describing one generated DWC UV Channel LOD payload. */
class FDWCDataUVMetadataBuilder
{
public:
    static bool BuildLOD(
        const UWetClothingAsset& Asset,
        const USkeletalMesh* Mesh,
        int32 LODIndex,
        int32 DataUVChannelIndex,
        FDWCDataUVLODMetadata& OutMetadata,
        FString* OutErrorMessage = nullptr,
        const TSet<int32>* GeneratedMaterialSlotIndices = nullptr);
};
