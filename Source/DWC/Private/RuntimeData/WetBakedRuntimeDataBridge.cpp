#include "RuntimeData/WetBakedRuntimeDataBridge.h"

#include "DataAssets/WetClothingAsset.h"
#include "Engine/SkeletalMesh.h"
#include "Utility/DWCError.h"

bool FWetBakedRuntimeDataBridge::TryCopyBakedBoneOptimizationCache(
    const UWetClothingAsset*   WetClothingAsset,
    USkeletalMesh*             SkeletalMesh,
    const int32                LODIndex,
    FWetBoneOptimizationCache& OutRuntimeCache,
    FString*                   OutErrorMessage)
{
    OutRuntimeCache = FWetBoneOptimizationCache();

    if (WetClothingAsset == nullptr || SkeletalMesh == nullptr)
    {
        DWC::Error::SetMessage(OutErrorMessage, TEXT("No WetClothingAsset or SkeletalMesh is available."));
        return false;
    }

    if (!WetClothingAsset->IsBakedRuntimeDataValidForMesh(SkeletalMesh, LODIndex))
    {
        DWC::Error::SetMessage(OutErrorMessage, TEXT("Baked runtime data is stale or invalid for the target mesh."));
        return false;
    }

    const FWetClothingAssetBakedRuntimeData& BakedData = WetClothingAsset->GetBakedRuntimeData();
    if (!BakedData.BoneOptimizationCache.IsValidForMesh(
            SkeletalMesh,
            LODIndex,
            BakedData.VertexCount,
            BakedData.MeshBuildSignature))
    {
        DWC::Error::SetMessage(OutErrorMessage, TEXT("Baked bone optimization cache is stale or invalid for the target mesh."));
        return false;
    }

    if (!BakedData.BoneOptimizationCache.CopyToRuntimeCache(SkeletalMesh, OutRuntimeCache))
    {
        DWC::Error::SetMessage(OutErrorMessage, TEXT("Failed to copy baked bone optimization cache to runtime cache."));
        return false;
    }

    DWC::Error::SetMessage(OutErrorMessage, TEXT(""));
    return true;
}
