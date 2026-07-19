#include "WetClothing/Common/DerivedData/DWCEditorDerivedDataInvalidator.h"

#include "DataAssets/WetClothingAsset.h"
#include "WetClothing/Common/Analysis/DWCUVIslandViewCache.h"
#include "WetClothing/Common/Texture/WetClothingTextureReadback.h"

void FDWCEditorDerivedDataInvalidator::InvalidateAll()
{
    FDWCUVIslandViewCache::InvalidateAll();
    FWetClothingTextureReadbackUtils::ClearCache();
    UWetClothingAsset::ClearMeshContentSignatureCache();
}

void FDWCEditorDerivedDataInvalidator::InvalidateAsset(const UWetClothingAsset& Asset)
{
    FDWCUVIslandViewCache::InvalidateAsset(&Asset);
    FDWCUVIslandViewCache::InvalidateMesh(Asset.GetRuntimeSkeletalMesh());
    // Texture readback currently has no asset-scoped API, so explicit rebuild/setup changes
    // conservatively invalidate it globally. It remains transient and fully regenerable.
    FWetClothingTextureReadbackUtils::ClearCache();
    UWetClothingAsset::ClearMeshContentSignatureCache();
}

void FDWCEditorDerivedDataInvalidator::InvalidateDataUVRebuild(
    const UWetClothingAsset& Asset,
    const USkeletalMesh* TouchedMesh)
{
    InvalidateAsset(Asset);
    FDWCUVIslandViewCache::InvalidateMesh(TouchedMesh);
}
