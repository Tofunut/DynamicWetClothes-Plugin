#include "WetClothing/WCAEditor/WCAGeneratedDataInvalidator.h"

#include "DataAssets/WetClothingAsset.h"
#include "WetClothing/WCAEditor/UI/UVView/WCAUVIslandViewCache.h"
#include "WetClothing/Foundation/TextureAccess/WetClothingTextureReadback.h"

void FWCAGeneratedDataInvalidator::InvalidateAll()
{
    FWCAUVIslandViewCache::InvalidateAll();
    FWetClothingTextureReadbackUtils::ClearCache();
    UWetClothingAsset::ClearMeshContentSignatureCache();
}

void FWCAGeneratedDataInvalidator::InvalidateAsset(UWetClothingAsset& Asset)
{
    Asset.BumpPreviewTopologyRevision();
    FWCAUVIslandViewCache::InvalidateAsset(&Asset);
    FWCAUVIslandViewCache::InvalidateMesh(Asset.GetRuntimeSkeletalMesh());
    // Texture readback currently has no asset-scoped API, so explicit rebuild/setup changes
    // conservatively invalidate it globally. It remains transient and fully regenerable.
    FWetClothingTextureReadbackUtils::ClearCache();
    UWetClothingAsset::ClearMeshContentSignatureCache();
}

void FWCAGeneratedDataInvalidator::InvalidateDataUVInitialization(
    UWetClothingAsset& Asset,
    const USkeletalMesh* TouchedMesh)
{
    InvalidateAsset(Asset);
    FWCAUVIslandViewCache::InvalidateMesh(TouchedMesh);
}
